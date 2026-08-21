/*
 * A1000: memtrack HAL поверх Mali-400 (drivers/gpu/arm/mali400).
 *
 * Данные берём из /sys/kernel/debug/mali0/gpu_memory — единственного места,
 * где ядро раскладывает память GPU по процессам:
 *
 *   Name (:bytes)   pid    mali_mem   max_mali_mem   external_mem   ump_mem   dma_mem
 *   ==================================================================================
 *   RenderThread    1608   5074944    5074944        0              0         6144000
 *   surfaceflinger  289    786432     786432         0              0         14086144
 *   Mali mem usage: 8220672        <- строки-итоги, их пропускаем
 *
 * У одного процесса может быть несколько строк (разные потоки), поэтому
 * суммируем все совпадения по pid.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/memtrack.h>

#define LOG_TAG "memtrack_sc8830"
#include <log/log.h>

#define MALI_GPU_MEMORY "/sys/kernel/debug/mali0/gpu_memory"

/*
 * На каждый тип отдаём ровно одну запись. Количество записей не должно
 * меняться между вызовами — этого требует контракт getMemory().
 */
#define RECORDS_PER_TYPE 1

static int memtrack_sc8830_init(const struct memtrack_module *module)
{
    (void)module;
    return 0;
}

/*
 * Суммирует по процессу нужную колонку gpu_memory.
 * column: 0 = mali_mem, 4 = dma_mem (нумерация после pid).
 * Возвращает 0 и кладёт сумму в *out; при отсутствии файла — -errno.
 */
static int mali_sum_for_pid(pid_t pid, int column, unsigned long long *out)
{
    FILE *fp;
    char line[512];
    unsigned long long total = 0;

    fp = fopen(MALI_GPU_MEMORY, "r");
    if (fp == NULL) {
        /*
         * Обычно это значит, что debugfs не смонтирован или у нас нет прав на
         * файл (см. a1000_memtrack.rc). Ругаемся один раз на вызов, но не
         * притворяемся, что памяти ноль — отдаём ошибку наверх.
         */
        ALOGE("не открыть %s: %s", MALI_GPU_MEMORY, strerror(errno));
        return -errno;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char name[128];
        int row_pid;
        unsigned long long vals[5]; /* mali_mem, max_mali_mem, external, ump, dma */

        if (sscanf(line, "%127s %d %llu %llu %llu %llu %llu",
                   name, &row_pid, &vals[0], &vals[1], &vals[2], &vals[3],
                   &vals[4]) != 7) {
            continue; /* шапка, разделитель, строки-итоги */
        }
        if (row_pid == (int)pid) {
            total += vals[column];
        }
    }
    fclose(fp);

    *out = total;
    return 0;
}

static int memtrack_sc8830_get_memory(const struct memtrack_module *module,
                                      pid_t pid, int type,
                                      struct memtrack_record *records,
                                      size_t *num_records)
{
    unsigned long long size = 0;
    int column;
    int ret;

    (void)module;

    switch (type) {
    case MEMTRACK_TYPE_GL:
        column = 0; /* mali_mem — собственные аллокации драйвера Mali */
        break;
    case MEMTRACK_TYPE_GRAPHICS:
        column = 4; /* dma_mem — импортированные dma-buf, это буферы кадров */
        break;
    default:
        /*
         * Неподдерживаемый тип. Вернуть -ENODEV, как предлагает memtrack.h,
         * НЕЛЬЗЯ: libmemtrack перебирает все типы подряд и обрывает обход на
         * первой же ошибке, а OTHER идёт первым — в результате запрос по
         * процессу целиком возвращал -1 и до GL/GRAPHICS дело не доходило.
         * Отдаём успех и ноль записей.
         */
        *num_records = 0;
        return 0;
    }

    /*
     * Быстрый путь: вызывающий сначала спрашивает, сколько записей нужно.
     * Здесь по контракту нельзя лезть в файлы — отвечаем константой.
     */
    if (*num_records == 0) {
        *num_records = RECORDS_PER_TYPE;
        return 0;
    }

    ret = mali_sum_for_pid(pid, column, &size);
    if (ret != 0) {
        return ret;
    }

    records[0].size_in_bytes = size;
    /*
     * Память Mali и dma-buf'ы не отражаются в smaps процесса, поэтому
     * UNACCOUNTED — иначе meminfo не прибавит их к итогу.
     * Если когда-нибудь окажется, что суммы в dumpsys meminfo задвоились,
     * смотреть надо именно сюда: значит, драйвер стал мапить эту память
     * в адресное пространство процесса и её следует считать ACCOUNTED.
     */
    records[0].flags = MEMTRACK_FLAG_SMAPS_UNACCOUNTED |
                       MEMTRACK_FLAG_PRIVATE |
                       MEMTRACK_FLAG_NONSECURE;
    *num_records = RECORDS_PER_TYPE;

    return 0;
}

static struct hw_module_methods_t memtrack_module_methods = {
    .open = NULL,
};

struct memtrack_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = MEMTRACK_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = MEMTRACK_HARDWARE_MODULE_ID,
        .name = "A1000 (sc8830) memtrack HAL",
        .author = "A1000 port",
        .methods = &memtrack_module_methods,
    },
    .init = memtrack_sc8830_init,
    .getMemory = memtrack_sc8830_get_memory,
};
