/* A1000: снять содержимое ВСЕХ буферов framebuffer через mmap.
 *
 * Зачем: read() на /dev/graphics/fb0 у sprdfb не реализован (dd отдаёт 0 байт),
 * а нам нужно понять, расходится ли то, что композирует SurfaceFlinger
 * (screencap показывает полный экран блокировки), с тем, что реально лежит в
 * памяти, которую сканирует DISPC (на панели — одни обои).
 *
 * virtual_size = 480,2400 при видимых 480x800, то есть буферов три. Печатаем
 * на каждой итерации yoffset (какой буфер сейчас показывается) и контрольную
 * сумму каждого буфера — сразу видно, какие из них вообще обновляются.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static unsigned int crc32_of(const unsigned char *p, size_t n)
{
    static unsigned int table[256];
    static int ready = 0;
    unsigned int c;
    size_t i;
    if (!ready) {
        int k;
        for (i = 0; i < 256; i++) {
            c = (unsigned int) i;
            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        ready = 1;
    }
    c = 0xFFFFFFFFu;
    for (i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int main(int argc, char **argv)
{
    int iterations = (argc > 1) ? atoi(argv[1]) : 10;
    int delay_ms   = (argc > 2) ? atoi(argv[2]) : 200;
    const char *prefix = (argc > 3) ? argv[3] : "/data/local/tmp/fb";

    int fd = open("/dev/graphics/fb0", O_RDONLY);
    if (fd < 0) { perror("open fb0"); return 1; }

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) { perror("VSCREENINFO"); return 1; }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) { perror("FSCREENINFO"); return 1; }

    size_t frame = (size_t) fi.line_length * vi.yres;
    int buffers = vi.yres ? (int) (vi.yres_virtual / vi.yres) : 1;

    printf("xres=%u yres=%u virtual=%ux%u bpp=%u line=%u smem_len=%u smem_start=0x%lx\n",
           vi.xres, vi.yres, vi.xres_virtual, vi.yres_virtual, vi.bits_per_pixel,
           fi.line_length, fi.smem_len, (unsigned long) fi.smem_start);
    printf("кадр = %zu байт, буферов = %d\n", frame, buffers);

    unsigned char *base = mmap(NULL, fi.smem_len, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }

    int i, b;
    for (i = 0; i < iterations; i++) {
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) == 0)
            printf("[%02d] yoffset=%-5u", i, vi.yoffset);
        else
            printf("[%02d] yoffset=?    ", i);
        for (b = 0; b < buffers; b++)
            printf("  buf%d=%08x", b, crc32_of(base + (size_t) b * frame, frame));
        printf("\n");
        fflush(stdout);
        usleep(delay_ms * 1000);
    }

    for (b = 0; b < buffers; b++) {
        char path[256];
        snprintf(path, sizeof(path), "%s_buf%d.raw", prefix, b);
        int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0) {
            ssize_t w = write(out, base + (size_t) b * frame, frame);
            close(out);
            printf("сохранён %s (%zd байт)\n", path, w);
        } else {
            perror(path);
        }
    }

    munmap(base, fi.smem_len);
    close(fd);
    return 0;
}
