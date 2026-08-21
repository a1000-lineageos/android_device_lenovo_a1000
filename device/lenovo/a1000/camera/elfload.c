/*
 * A1000: минимальный загрузчик ELF для стоковых библиотек камеры.
 *
 * Нужен потому, что bionic отказывается грузить не-PIC библиотеки (текстовые
 * релокации + запрет на сегмент W+X). Мы делаем то же, что делал бы линковщик,
 * но с правильным порядком защиты памяти: сначала RW для применения
 * релокаций, затем R+X для исполнения.
 *
 * Поддерживаем ровно то, что встречается в этих двух библиотеках.
 * Неизвестный тип релокации — это ошибка, а не повод молча продолжить.
 */
#define LOG_TAG "elfload"

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <log/log.h>

#include "elfload.h"

#ifndef R_ARM_ABS32
#define R_ARM_ABS32     2
#endif
#ifndef R_ARM_GLOB_DAT
#define R_ARM_GLOB_DAT  21
#endif
#ifndef R_ARM_JUMP_SLOT
#define R_ARM_JUMP_SLOT 22
#endif
#ifndef R_ARM_RELATIVE
#define R_ARM_RELATIVE  23
#endif

#define PAGE_DOWN(x) ((x) & ~(uintptr_t)(4096 - 1))
#define PAGE_UP(x)   (((x) + 4096 - 1) & ~(uintptr_t)(4096 - 1))

struct loaded {
    uint8_t *base;
    size_t span;
    Elf32_Sym *symtab;
    const char *strtab;
    uint32_t nsyms;
};

/*
 * Три символа старого ARM-ABI исключений C++ (Android 5), которых в 8.1 нет
 * ни в libc++, ни в libgcc. Нужны они только когда C++-исключение реально
 * разматывается; в библиотеках обработки изображений этого не происходит.
 * Даём заглушки, но не молча: если их всё-таки позовут, мы об этом узнаем.
 */
static int a1000_cxa_type_match(void *ucbp, const void *rttip,
                                int is_ref, void **matched)
{
    (void)ucbp; (void)rttip; (void)is_ref; (void)matched;
    ALOGE("вызван __cxa_type_match — в библиотеке РЕАЛЬНО летит исключение C++, "
          "заглушка не подходит");
    return 0;
}

static int a1000_cxa_begin_cleanup(void *ucbp)
{
    (void)ucbp;
    ALOGE("вызван __cxa_begin_cleanup — реальное исключение C++");
    return 0;
}

static void a1000_cxa_call_unexpected(void *ucbp)
{
    (void)ucbp;
    ALOGE("вызван __cxa_call_unexpected — реальное исключение C++, прерываю");
    abort();
}

static void *legacy_abi(const char *name)
{
    if (strcmp(name, "__cxa_type_match") == 0)
        return (void *)a1000_cxa_type_match;
    if (strcmp(name, "__cxa_begin_cleanup") == 0)
        return (void *)a1000_cxa_begin_cleanup;
    if (strcmp(name, "__cxa_call_unexpected") == 0)
        return (void *)a1000_cxa_call_unexpected;
    return NULL;
}

/* Разрешение символа: сначала внутри самой библиотеки (у обеих стоит
 * SYMBOLIC), потом в уже загруженных библиотеках процесса, и лишь затем —
 * наши заглушки для выпавшего из 8.1 старого C++ ABI. */
static void *resolve(const struct loaded *lo, const char *name)
{
    uint32_t i;
    void *p;

    for (i = 0; i < lo->nsyms; i++) {
        const Elf32_Sym *s = &lo->symtab[i];

        if (s->st_shndx != SHN_UNDEF && s->st_value &&
            strcmp(lo->strtab + s->st_name, name) == 0)
            return lo->base + s->st_value;
    }

    p = dlsym(RTLD_DEFAULT, name);
    if (p != NULL)
        return p;

    p = legacy_abi(name);
    if (p != NULL) {
        ALOGI("%s: подставлена заглушка старого C++ ABI", name);
        return p;
    }

    ALOGE("не найден внешний символ %s", name);
    return NULL;
}

void *elfload_open(const char *path, const char *const *want,
                   void **out, int nwant)
{
    int fd = -1, i;
    struct stat st;
    Elf32_Ehdr eh;
    Elf32_Phdr *ph = NULL;
    struct loaded lo;
    uint8_t *base = MAP_FAILED;
    Elf32_Addr lo_v = (Elf32_Addr)-1, hi_v = 0;
    Elf32_Dyn *dyn = NULL;
    Elf32_Rel *rel = NULL, *jmprel = NULL;
    uint32_t relsz = 0, jmprelsz = 0;
    uint32_t *hash = NULL;
    Elf32_Addr init_array = 0;
    uint32_t init_arraysz = 0;

    memset(&lo, 0, sizeof(lo));

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { ALOGE("не открыть %s: %s", path, strerror(errno)); goto fail; }
    if (fstat(fd, &st) != 0) goto fail;
    if (read(fd, &eh, sizeof(eh)) != (ssize_t)sizeof(eh)) goto fail;
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS32) {
        ALOGE("%s: не 32-битный ELF", path); goto fail;
    }

    ph = malloc((size_t)eh.e_phnum * eh.e_phentsize);
    if (ph == NULL) goto fail;
    if (pread(fd, ph, (size_t)eh.e_phnum * eh.e_phentsize, eh.e_phoff) < 0) goto fail;

    for (i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_vaddr < lo_v) lo_v = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > hi_v)
            hi_v = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (lo_v == (Elf32_Addr)-1) { ALOGE("%s: нет PT_LOAD", path); goto fail; }

    lo.span = PAGE_UP(hi_v) - PAGE_DOWN(lo_v);

    /*
     * Всё пространство берём анонимным RW: так и .bss обнуляется даром, и
     * не приходится воевать с выравниванием файловых отображений.
     */
    base = mmap(NULL, lo.span, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { ALOGE("mmap %zu: %s", lo.span, strerror(errno)); goto fail; }
    lo.base = base - PAGE_DOWN(lo_v);

    for (i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_filesz == 0) continue;
        if (pread(fd, lo.base + ph[i].p_vaddr, ph[i].p_filesz,
                  ph[i].p_offset) != (ssize_t)ph[i].p_filesz) {
            ALOGE("не прочитать сегмент %d", i); goto fail;
        }
    }

    for (i = 0; i < eh.e_phnum; i++)
        if (ph[i].p_type == PT_DYNAMIC)
            dyn = (Elf32_Dyn *)(lo.base + ph[i].p_vaddr);
    if (dyn == NULL) { ALOGE("%s: нет PT_DYNAMIC", path); goto fail; }

    for (; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
        case DT_SYMTAB:   lo.symtab = (Elf32_Sym *)(lo.base + dyn->d_un.d_ptr); break;
        case DT_STRTAB:   lo.strtab = (const char *)(lo.base + dyn->d_un.d_ptr); break;
        case DT_HASH:     hash = (uint32_t *)(lo.base + dyn->d_un.d_ptr); break;
        case DT_REL:      rel = (Elf32_Rel *)(lo.base + dyn->d_un.d_ptr); break;
        case DT_RELSZ:    relsz = dyn->d_un.d_val; break;
        case DT_JMPREL:   jmprel = (Elf32_Rel *)(lo.base + dyn->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmprelsz = dyn->d_un.d_val; break;
        case DT_INIT_ARRAY:   init_array = dyn->d_un.d_ptr; break;
        case DT_INIT_ARRAYSZ: init_arraysz = dyn->d_un.d_val; break;
        default: break;
        }
    }
    if (lo.symtab == NULL || lo.strtab == NULL || hash == NULL) {
        ALOGE("%s: нет symtab/strtab/hash", path); goto fail;
    }
    lo.nsyms = hash[1];   /* nchain у таблицы SysV = число символов */

    /* --- релокации --- */
    {
        int pass;

        for (pass = 0; pass < 2; pass++) {
            Elf32_Rel *r = pass ? jmprel : rel;
            uint32_t n = (pass ? jmprelsz : relsz) / sizeof(Elf32_Rel);
            uint32_t k;

            for (k = 0; r && k < n; k++) {
                uint32_t type = ELF32_R_TYPE(r[k].r_info);
                uint32_t si = ELF32_R_SYM(r[k].r_info);
                Elf32_Addr *slot = (Elf32_Addr *)(lo.base + r[k].r_offset);
                void *val;

                switch (type) {
                case R_ARM_RELATIVE:
                    *slot += (Elf32_Addr)(uintptr_t)lo.base;
                    break;
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT:
                case R_ARM_ABS32:
                    val = resolve(&lo, lo.strtab + lo.symtab[si].st_name);
                    if (val == NULL) goto fail;
                    if (type == R_ARM_ABS32)
                        *slot += (Elf32_Addr)(uintptr_t)val;
                    else
                        *slot = (Elf32_Addr)(uintptr_t)val;
                    break;
                default:
                    ALOGE("%s: неизвестный тип релокации %u", path, type);
                    goto fail;
                }
            }
        }
    }

    /* --- защита памяти по-настоящему: код становится R+X, но уже НЕ RW --- */
    for (i = 0; i < eh.e_phnum; i++) {
        uintptr_t start, end;
        int prot = 0;

        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_flags & PF_R) prot |= PROT_READ;
        if (ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (ph[i].p_flags & PF_X) prot |= PROT_EXEC;

        start = (uintptr_t)lo.base + PAGE_DOWN(ph[i].p_vaddr);
        end = (uintptr_t)lo.base + PAGE_UP(ph[i].p_vaddr + ph[i].p_memsz);
        if (mprotect((void *)start, end - start, prot) != 0)
            ALOGE("mprotect: %s", strerror(errno));
    }

    /* --- инициализаторы --- */
    if (init_array && init_arraysz) {
        void (**fns)(void) = (void (**)(void))(lo.base + init_array);
        uint32_t k;

        for (k = 0; k < init_arraysz / sizeof(void *); k++)
            if (fns[k] && fns[k] != (void (*)(void))-1)
                fns[k]();
    }

    /* --- нужные символы --- */
    for (i = 0; i < nwant; i++) {
        out[i] = resolve(&lo, want[i]);
        if (out[i] == NULL) {
            ALOGE("%s: не найден %s", path, want[i]);
            goto fail;
        }
    }

    ALOGI("%s загружен на %p (%zu байт), символов найдено %d",
          path, lo.base, lo.span, nwant);
    free(ph);
    close(fd);
    return lo.base;

fail:
    if (base != MAP_FAILED) munmap(base, lo.span);
    free(ph);
    if (fd >= 0) close(fd);
    return NULL;
}
