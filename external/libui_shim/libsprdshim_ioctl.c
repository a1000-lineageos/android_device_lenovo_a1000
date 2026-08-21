/*
 * libsprdshim_ioctl.c — SPRD display color/geometry fixups for the A1000 (otm8019a panel).
 *
 * The panel scans out RGBA with the channels CYCLICALLY rotated (R,G,B)->(B,R,G):
 * teal looks magenta. The correction is the inverse 3-cycle [R,G,B]->[G,B,R] applied
 * to the source pixels before the panel reads them. A 3-cycle CANNOT be expressed by
 * the DISPC OSD register bits (endian = byte permutations, rb_switch = R<->B swap only),
 * so it must be done on pixel data in software.
 *
 * TWO composition paths, TWO interception points (this one LD_PRELOAD'd ioctl):
 *
 *  (A9) SPRD HWC OverlayComposer used the GSP hardware compositor. We hooked the GSP
 *       ioctl (magic 'G', nr 0) and rotated each source layer. [kept below, harmless]
 *
 *  (8.1) OverlayComposer/GSP-GLES is dead (eglCreateWindowSurface EGL_BAD_NATIVE_WINDOW).
 *       SurfaceFlinger composes via GLES into a FramebufferSurface (ION overlay carveout),
 *       and the SPRD HWC presents it straight to the DISPC OSD layer via SPRD_FB_SET_OVERLAY
 *       (magic 'm', nr 1). We hook THAT ioctl and rotate the OSD buffer before scanout.
 *       The ioctl passes the buffer as a PHYSICAL address (DISPC OSD has no IOMMU), so we
 *       recover CPU access via the phys->(ion fd,len) table that Get_phy_addr_from_ion()
 *       (implemented in libsprdshim.c, called by the HWC to obtain that phys) records here.
 *       After rotating we ion_sync_fd() so the DISPC DMA sees the rotated bytes.
 *
 *  IDEMPOTENCY — buffers ping-pong through a small stable set; a re-presented UNCHANGED
 *  buffer must not be re-rotated. We key per fd: hash what we last LEFT there; equal hash
 *  on the next present => already rotated => skip; else a new frame => rotate once.
 *
 *  DIAGNOSTICS — if /data/gsp_shimlog_on exists, per-event lines go to /data/gsp_shim.log
 *  as a rolling window. No flag -> no log.
 *
 * Deploy: part of libui_shim.so, LD_PRELOAD'd into the composer@2.1 service so this ioctl()
 * interposes libc's; gralloc/hwcomposer are also --add-needed on it for the ABI symbols.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/mman.h>
#include <arm_neon.h>
#include <log/log.h>

#define IOC_NR(x)   ((unsigned)(x) & 0xff)
#define IOC_TYPE(x) (((unsigned)(x) >> 8) & 0xff)
#define GSP_IO_MAGIC 'G'
#define GSP_FMT_ARGB888 0
#define GSP_FMT_RGB888  1

/* ===== SPRD fb (dispc) overlay ioctl — the 8.1 scanout path ===== */
#define SPRD_FB_MAGIC           'm'   /* SPRD_FB_IOCTL_MAGIC */
#define SPRD_FB_SET_OVERLAY_NR  1     /* _IOW('m',1,uint) */
#define SPRD_LAYER_OSD          0x2   /* overlay_setting.layer_index for the OSD (UI) layer */
#define SPRD_DATA_FORMAT_RGB888 3     /* 32bpp RGB path (the UI/anim FramebufferSurface) */

typedef struct { uint16_t x, y, w, h; } OV_RECT_T;
typedef struct {                 /* mirrors kernel struct overlay_setting (sprd_fb.h) */
    int32_t   layer_index;       /* 0  */
    int32_t   data_type;         /* 4  */
    int32_t   y_endian;          /* 8  */
    int32_t   uv_endian;         /* 12 */
    uint8_t   rb_switch;         /* 16 (kernel bool) */
    OV_RECT_T rect;              /* 18: x,y,w,h */
    uint32_t  buffer;            /* 28: PHYSICAL address written to DISPC_OSD_BASE_ADDR */
} OV_SETTING_T;                  /* size 32 */
typedef char _assert_ov_layout[(offsetof(OV_SETTING_T, buffer) == 28
                                && sizeof(OV_SETTING_T) == 32) ? 1 : -1];

typedef struct { uint32_t addr_y, addr_uv, addr_v; } GSP_DATA_ADDR_T;
typedef struct { uint16_t st_x, st_y, rect_w, rect_h; } GSP_RECT_T;
typedef struct { uint16_t pos_pt_x, pos_pt_y; } GSP_POS_PT_T;
typedef struct { uint8_t b_val, g_val, r_val, a_val; } GSP_RGB_T;
typedef struct { int e0,e1,e2,e3,e4,e5,rgb_swap_mode,a_swap_mode; } GSP_ENDIAN_INFO_PARAM_T;
typedef struct { uint8_t is_pa; int32_t share_fd; uint32_t uv_offset, v_offset; } GSP_MEM_INFO;
typedef struct { uint8_t dithering_en, gsp_gap, gsp_clock, ahb_clock, split_pages, y2r_opt; } GSP_MISC_CONFIG_INFO_T;
typedef struct {
    GSP_DATA_ADDR_T src_addr; uint32_t pitch; GSP_RECT_T clip_rect; GSP_RECT_T des_rect;
    GSP_RGB_T grey; GSP_RGB_T colorkey; GSP_ENDIAN_INFO_PARAM_T endian_mode;
    int img_format; int rot_angle; GSP_MEM_INFO mem_info;
    uint8_t row_tap_mode, col_tap_mode, alpha, colorkey_en, pallet_en, scaling_en, layer_en, pmargb_en, pmargb_mod;
} GSP_LAYER0_CONFIG_INFO_T;
typedef struct {
    GSP_DATA_ADDR_T src_addr; uint32_t pitch; GSP_RECT_T clip_rect; GSP_POS_PT_T des_pos;
    GSP_RGB_T grey; GSP_RGB_T colorkey; GSP_ENDIAN_INFO_PARAM_T endian_mode;
    int img_format; int rot_angle; GSP_MEM_INFO mem_info;
    uint8_t row_tap_mode, col_tap_mode, alpha, colorkey_en, pallet_en, layer_en, pmargb_en, pmargb_mod;
} GSP_LAYER1_CONFIG_INFO_T;
typedef struct {
    GSP_DATA_ADDR_T src_addr; uint32_t pitch; GSP_ENDIAN_INFO_PARAM_T endian_mode;
    int img_format; GSP_MEM_INFO mem_info; uint8_t compress_r8_en;
} GSP_LAYER_DES_CONFIG_INFO_T;
typedef struct {
    GSP_MISC_CONFIG_INFO_T misc_info; GSP_LAYER0_CONFIG_INFO_T layer0_info;
    GSP_LAYER1_CONFIG_INFO_T layer1_info; GSP_LAYER_DES_CONFIG_INFO_T layer_des_info;
} GSP_CONFIG_INFO_T;

static int (*real_ioctl)(int, int, void*) = 0;

/* NEON pass. do_rotate=0: hash current; 1: rotate [R,G,B,A]->[G,B,R,A] in place AND hash. */
static uint32_t process(uint8_t *b, unsigned npix, int do_rotate)
{
    const uint32x4_t C  = vdupq_n_u32(2654435761u);
    const uint32x4_t K0 = vdupq_n_u32(0x9E3779B1u);
    const uint32x4_t K1 = vdupq_n_u32(0x85EBCA77u);
    const uint32x4_t K2 = vdupq_n_u32(0xC2B2AE3Du);
    const uint32x4_t K3 = vdupq_n_u32(0x27D4EB2Fu);
    uint32x4_t acc = vdupq_n_u32(0);
    unsigned i = 0;
    for (; i + 16 <= npix; i += 16, b += 64) {
        uint8x16x4_t px = vld4q_u8(b);     /* val0=R val1=G val2=B val3=A */
        uint8x16x4_t s = px;
        if (do_rotate) {
            uint8x16x4_t out;
            out.val[0] = px.val[1];        /* R' = G */
            out.val[1] = px.val[2];        /* G' = B */
            out.val[2] = px.val[0];        /* B' = R */
            out.val[3] = px.val[3];        /* A' = A */
            vst4q_u8(b, out);
            s = out;
        }
        uint32x4_t w0 = vreinterpretq_u32_u8(s.val[0]);
        uint32x4_t w1 = vreinterpretq_u32_u8(s.val[1]);
        uint32x4_t w2 = vreinterpretq_u32_u8(s.val[2]);
        uint32x4_t w3 = vreinterpretq_u32_u8(s.val[3]);
        acc = vaddq_u32(vmulq_u32(acc, C), vmulq_u32(w0, K0));
        acc = vaddq_u32(acc, vmulq_u32(w1, K1));
        acc = vaddq_u32(acc, vmulq_u32(w2, K2));
        acc = vaddq_u32(acc, vmulq_u32(w3, K3));
    }
    uint32_t h = vgetq_lane_u32(acc, 0) * 1u
               + vgetq_lane_u32(acc, 1) * 2246822519u
               + vgetq_lane_u32(acc, 2) * 3266489917u
               + vgetq_lane_u32(acc, 3) * 668265263u;
    for (; i < npix; i++, b += 4) {
        uint32_t *p = (uint32_t *)b;
        uint32_t v = *p;
        if (do_rotate) { v = (v & 0xFF000000u) | ((v & 0x00FFFF00u) >> 8) | ((v & 0x000000FFu) << 16); *p = v; }
        h = h * 2654435761u + v;
    }
    return h;
}

/* per-fd "last hash we left" map (buffer fds are few and stable) */
#define FDMAP 24
static struct { int fd; uint32_t hash; } g_fd[FDMAP];
static int g_fdrr = 0;
static uint32_t *fd_slot(int fd)
{
    int i, freei = -1;
    for (i = 0; i < FDMAP; i++) {
        if (g_fd[i].fd == fd) return &g_fd[i].hash;
        if (freei < 0 && g_fd[i].fd == 0) freei = i;
    }
    if (freei < 0) { freei = g_fdrr; g_fdrr = (g_fdrr + 1) % FDMAP; }  /* evict round-robin */
    g_fd[freei].fd = fd; g_fd[freei].hash = 0;
    return &g_fd[freei].hash;
}

/* ===== phys -> (ion fd, len) table, filled by Get_phy_addr_from_ion (libsprdshim.c) ===== */
#define PHYSMAP 16
static struct { uint32_t phys; int fd; uint32_t len; } g_phys[PHYSMAP];
static int g_physrr = 0;
/* called from libsprdshim.c right after it resolves an ion fd -> physical address */
/* We keep our OWN dup of the ion fd rather than the caller's number. The caller closes its
 * fd as soon as it has the physical address, after which that number gets recycled for an
 * unrelated file: mmap()ing it later would then hand us somebody else's memory to rotate,
 * and ion_sync_fd() on it fails EBADF. A dup pins the buffer for as long as we reference it.
 * Bounded by PHYSMAP entries; the previous dup is closed whenever a slot is reused. */
void _sprdcolor_record(unsigned long phys, int fd, unsigned int len)
{
    int i, freei = -1, dupfd;
    if (!phys || fd < 0) return;
    for (i = 0; i < PHYSMAP; i++) {
        if (g_phys[i].phys == (uint32_t)phys) {
            /* > 0, never >= 0: the table starts zeroed, and closing fd 0 would take
             * stdin away from the whole process. Leaking one dup beats that. */
            if (g_phys[i].fd > 0) close(g_phys[i].fd);
            dupfd = dup(fd);
            g_phys[i].fd = dupfd; g_phys[i].len = len;
            return;
        }
        if (freei < 0 && g_phys[i].phys == 0) freei = i;
    }
    if (freei < 0) {
        freei = g_physrr; g_physrr = (g_physrr + 1) % PHYSMAP;
        if (g_phys[freei].phys && g_phys[freei].fd > 0) close(g_phys[freei].fd);
    }
    dupfd = dup(fd);
    g_phys[freei].phys = (uint32_t)phys; g_phys[freei].fd = dupfd; g_phys[freei].len = len;
}

/* ion cache flush so the DISPC DMA sees our CPU writes (ion buffers are cacheable) */
extern int ion_sync_fd(int fd, int handle_fd);
static int g_ionfd = -2;
static int g_cpucolor = -1;   /* -1 unknown, 0 GPU does the colour, 1 rotate on the CPU */
static int g_sweep = -1;      /* -1 unknown, 1 = force a fixed DISPC permutation */
static int g_sweep_last = -1;
static int g_hw_endian = 0, g_hw_rb = 0;

/* Per-present frame tracer, gated by /data/gsp_shimlog_on.
 *
 * This ioctl hook is the exact choke point where pixels meet the display controller, and
 * it now runs in BOTH presenting processes, so it is the only place that can produce a
 * ground-truth, frame-by-frame record. One file per pid: surfaceflinger and
 * composer@2.1-service would otherwise overwrite each other's log.
 *
 * The field that matters most is hleft: the hash we left in the buffer after rotating it
 * last time. On the next present we re-hash the same buffer; if hnow != hleft the content
 * changed since we wrote it. That is either a legitimate new frame (expected) or somebody
 * writing while we were mid-rotation (the tearing/noise we are hunting). Correlate with
 * dur to see how long we held the buffer. */
static int g_logfd = -1;     /* -1 unknown, 0 off, >0 fd */
static int g_logn = 0;
static int g_pid = 0;
#define LOG_CAP 20000
static uint64_t now_us(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
}
static void log_init(void){
    char path[64];
    if (g_logfd != -1) return;
    if (access("/data/gsp_shimlog_on", F_OK) != 0) { g_logfd = 0; return; }
    g_pid = (int)getpid();
    snprintf(path, sizeof path, "/data/gsp_shim_%d.log", g_pid);
    g_logfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
}
static void log_line(const char *buf, int n){
    if (g_logfd <= 0 || n <= 0) return;
    if (g_logn >= LOG_CAP) { lseek(g_logfd, 0, SEEK_SET); ftruncate(g_logfd, 0); g_logn = 0; }
    write(g_logfd, buf, (size_t)n); g_logn++;
}
static void log_layer(const char *tag, const GSP_MEM_INFO *mi, uint32_t pitch,
                      GSP_RECT_T clip, int fmt, uint32_t h, int rot){
    char buf[200];
    int n = snprintf(buf, sizeof buf,
        "%s fd=%d pa=%d uvo=%x p=%u clip=%u,%u,%u,%u fmt=%d h=%08x %s\n",
        tag, mi->share_fd, mi->is_pa, mi->uv_offset, pitch,
        clip.st_x, clip.st_y, clip.rect_w, clip.rect_h, fmt, h, rot ? "ROT" : "skip");
    log_line(buf, n);
}
static void log_osd_full(const OV_SETTING_T *o, int bfd, uint32_t hnow, uint32_t hleft,
                         char st, int rot, uint64_t t0, uint64_t t1, const uint8_t *px){
    char buf[320];
    int n = snprintf(buf, sizeof buf,
        "t=%llu pid=%d OSD phys=%08x bfd=%d li=%d dt=%d rb=%d ye=%d ue=%d "
        "rect=%u,%u,%u,%u hnow=%08x hleft=%08x %c %s dur=%lluus px0=%02x%02x%02x%02x\n",
        (unsigned long long)t0, g_pid, o->buffer, bfd, o->layer_index, o->data_type,
        o->rb_switch, o->y_endian, o->uv_endian,
        o->rect.x, o->rect.y, o->rect.w, o->rect.h, hnow, hleft, st,
        rot ? "ROT" : "skip", (unsigned long long)(t1 - t0),
        px ? px[0] : 0, px ? px[1] : 0, px ? px[2] : 0, px ? px[3] : 0);
    log_line(buf, n);
}

/* rotate one GSP source layer once per new frame (idempotent, keyed per fd) */
static void handle_layer(const char *tag, GSP_MEM_INFO *mi, uint32_t pitch,
                         GSP_RECT_T clip, int fmt, uint8_t layer_en)
{
    if (!layer_en || mi->share_fd <= 0 || !pitch || !clip.rect_h) return;
    unsigned npix = (unsigned)pitch * clip.rect_h;
    size_t len = (size_t)npix * 4;
    void *m = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, mi->share_fd, 0);
    if (m == MAP_FAILED) return;
    uint32_t *slot = fd_slot(mi->share_fd);
    uint32_t h = process((uint8_t *)m, npix, 0);   /* hash current content */
    int rot = 0;
    if (*slot != h) {                              /* changed since we left it -> rotate once */
        *slot = process((uint8_t *)m, npix, 1);    /* store hash of rotated output */
        rot = 1;
    }
    munmap(m, len);
    log_layer(tag, mi, pitch, clip, fmt, h, rot);
}

/* rotate the OSD FramebufferSurface once per new frame (8.1 DISPC scanout path) */
static void handle_osd(OV_SETTING_T *o)
{
    int i, idx = -1, bfd;
    unsigned npix;
    size_t len;
    void *m;
    uint32_t *slot, h, hleft = 0;
    int rot = 0;
    uint64_t t0;
    uint8_t px[4] = {0, 0, 0, 0};

    if (o->layer_index != SPRD_LAYER_OSD || o->data_type != SPRD_DATA_FORMAT_RGB888) return;
    if (!o->buffer || !o->rect.w || !o->rect.h) return;

    /* Pin the presented address to the first OSD buffer this process ever posted.
     *
     * The blob allocates two plane buffers and SprdPrimaryPlane::display() alternates the
     * address it hands to DISPC, but GLES only ever renders into the first one. Dumping
     * three consecutive frames of physical memory in one read (the buffers are contiguous:
     * 0x9ffcc000 + 1536000 == 0xa0143000) showed 180/180 sampled pixels populated in the
     * first and 0/180 in the second, in two boots running. So every other frame the panel
     * is handed a buffer nobody drew into: that is the black screen when the image is
     * static, and the juddering when it is not.
     *
     * Presenting the rendered buffer twice instead of alternating costs the tearing that
     * double buffering was there to avoid, and nothing else. Set /data/gsp_nopin to fall
     * back to the stock alternating behaviour. */
    {
        static uint32_t pin;
        static int pin_off = -1;

        if (pin_off < 0)
            pin_off = (access("/data/gsp_nopin", F_OK) == 0) ? 1 : 0;
        if (!pin_off) {
            if (!pin) {
                pin = o->buffer;
                ALOGI("sprdshim: pinning OSD scanout to %08x", pin);
            }
            o->buffer = pin;
        }
    }

    /* The CPU rotation is ruinously expensive here: the frame tracer measured 27 ms just to
     * hash the buffer and 66 ms to hash+rotate it, because this ION mapping is uncached
     * (1.5 MB at ~55 MB/s). That is 2-4 whole frame budgets spent inside the ioctl that
     * programs the display controller, which is both the lag and the window in which the
     * DISPC can latch a half-rotated buffer -> the intermittent noise.
     *
     * When /data/gsp_nocpucolor exists we do none of it: SurfaceFlinger's RenderEngine does
     * the same permutation in the fragment shader instead (property debug.sf.swz=gbr), on
     * the GPU that is compositing the frame anyway, for free. Keep the trace line so the
     * two modes stay comparable. */
    /* Hardware-permutation sweep. The DISPC can permute bytes for free via the OSD_CTRL
     * endian field and the rb_switch bit, both of which arrive in THIS ioctl struct — no
     * kernel rebuild, no CPU cost. Reasoning on paper said none of the 8 combinations can
     * express the 3-cycle we need, but that rested on an assumption about how the DISPC
     * assigns the four bytes to R/G/B/A which I cannot verify from the docs we have.
     * So measure instead: hold each of the 8 combinations for 6 seconds and log it, and
     * let the panel answer. Enabled by /data/gsp_hwsweep. */
    if (g_sweep == -1) {
        int fd = open("/data/gsp_hwcolor", O_RDONLY);
        g_sweep = 0;
        if (fd >= 0) {
            char b[32];
            int n = (int)read(fd, b, sizeof b - 1);
            close(fd);
            if (n > 0) {
                b[n] = 0;
                if (sscanf(b, "%d %d", &g_hw_endian, &g_hw_rb) == 2) g_sweep = 1;
            }
        }
    }
    if (g_sweep) {
        /* Fixed hardware permutation, no CPU pixel work at all.
         *
         * The sweep answered the open question: with y_endian != 0 the alpha byte lands in
         * a colour channel, so an opaque black background scans out as a full red screen —
         * those modes are unusable, exactly as the paper analysis predicted. That leaves
         * y_endian=0 and the rb_switch bit. Working backwards from the configuration that
         * WAS correct (CPU gbr-rotation + rb_switch=1) the panel itself swaps G and B, so
         * the compensation the hardware would need is the gbr 3-cycle — which the DISPC
         * cannot express. rb_switch=0 leaves only G and B transposed, which is invisible on
         * greys, whites and the teal accents this UI is built from, and costs nothing.
         * The exact fix is the RenderEngine shader; that needs layers to reach it. */
        o->y_endian  = g_hw_endian;
        o->rb_switch = (uint8_t)g_hw_rb;
        if (g_sweep_last == -1) {
            char b[128];
            int n = snprintf(b, sizeof b, "HWCOLOR fixed y_endian=%d rb_switch=%d\n",
                             g_hw_endian, g_hw_rb);
            log_line(b, n);
            g_sweep_last = 0;
        }
        return;                            /* never touch pixels */
    }

    if (g_cpucolor == -1)
        g_cpucolor = (access("/data/gsp_nocpucolor", F_OK) == 0) ? 0 : 1;
    if (!g_cpucolor) {
        uint64_t t = now_us();
        log_osd_full(o, -1, 0, 0, 'G', 0, t, t, 0);   /* G: colour handed to the GPU */
        return;
    }

    for (i = 0; i < PHYSMAP; i++) if (g_phys[i].phys == o->buffer) { idx = i; break; }
    npix = (unsigned)o->rect.w * o->rect.h;
    len  = (idx >= 0 && g_phys[idx].len) ? g_phys[idx].len : (size_t)npix * 4;
    bfd  = (idx >= 0) ? g_phys[idx].fd : -1;

    t0 = now_us();
    if (bfd < 0) { log_osd_full(o, bfd, 0, 0, 'N', 0, t0, t0, 0); return; }  /* N: phys not in table */
    m = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, bfd, 0);
    if (m == MAP_FAILED) { log_osd_full(o, bfd, 0, 0, 'M', 0, t0, now_us(), 0); return; }

    slot = fd_slot(bfd);
    hleft = *slot;                                 /* what we left here last time */
    h = process((uint8_t *)m, npix, 0);            /* hash current content */
    if (hleft != h) {                              /* new frame -> rotate once */
        *slot = process((uint8_t *)m, npix, 1);
        if (g_ionfd == -2) g_ionfd = open("/dev/ion", O_RDWR);
        if (g_ionfd >= 0) ion_sync_fd(g_ionfd, bfd);   /* flush CPU writes to RAM */
        rot = 1;
    }
    memcpy(px, m, 4);                              /* first pixel, post-rotation */
    munmap(m, len);
    log_osd_full(o, bfd, h, hleft, 'K', rot, t0, now_us(), px);
}

/* Ноль копирования (libsprdshim_fbpost.cpp) цепляется отсюда, а не из
 * SurfaceFlinger: libui_shim подтягивается как зависимость gralloc/hwcomposer, в
 * ЛОКАЛЬНУЮ группу, поэтому её символы для libsurfaceflinger не видны совсем —
 * ни слабой ссылкой, ни dlsym(RTLD_DEFAULT) (проверено: возвращает NULL). Зато
 * ioctl gralloc'а приходит сюда, и первый же вызов даёт нам нужный момент:
 * ДО того, как вендорный HWC откроет своё fb-устройство. */
extern void a1000_zc_hook_fb(void *dev);
/* Ядро считает fb_var.pixclock из запрошенных 60 fps, а панель идёт 62.49 Гц —
 * подробности и лечение в libsprdshim_fbpost.cpp. */
extern void a1000_fix_vsync_period(int fd, int request, void *arg);

int ioctl(int fd, int request, ...)
{
    va_list ap; va_start(ap, request);
    void *arg = va_arg(ap, void*);
    va_end(ap);
    if (!real_ioctl)
        real_ioctl = (int (*)(int, int, void*))dlsym(RTLD_NEXT, "ioctl");
    a1000_zc_hook_fb(0);          /* внутри дешёвый early-out после первого раза */

    /* 8.1: SPRD_FB_SET_OVERLAY -> rotate OSD FramebufferSurface before DISPC scanout */
    if (arg && IOC_TYPE(request) == SPRD_FB_MAGIC && IOC_NR(request) == SPRD_FB_SET_OVERLAY_NR) {
        log_init();
        handle_osd((OV_SETTING_T *)arg);
    }
    /* A9: GSP compositor path (dead on 8.1, harmless) */
    else if (arg && IOC_TYPE(request) == GSP_IO_MAGIC && IOC_NR(request) == 0) {
        GSP_CONFIG_INFO_T *c = (GSP_CONFIG_INFO_T *)arg;
        GSP_LAYER0_CONFIG_INFO_T *l0 = &c->layer0_info;
        GSP_LAYER1_CONFIG_INFO_T *l1 = &c->layer1_info;
        log_init();
        if (l0->img_format == GSP_FMT_RGB888) l0->img_format = GSP_FMT_ARGB888;
        if (l1->img_format == GSP_FMT_RGB888) l1->img_format = GSP_FMT_ARGB888;
        if (c->layer_des_info.img_format == GSP_FMT_RGB888) c->layer_des_info.img_format = GSP_FMT_ARGB888;
        handle_layer("L0", &l0->mem_info, l0->pitch, l0->clip_rect, l0->img_format, l0->layer_en);
        handle_layer("L1", &l1->mem_info, l1->pitch, l1->clip_rect, l1->img_format, l1->layer_en);
    }
    {
        int rc = real_ioctl ? real_ioctl(fd, request, arg) : -1;
        if (rc == 0)
            a1000_fix_vsync_period(fd, request, arg);
        return rc;
    }
}
