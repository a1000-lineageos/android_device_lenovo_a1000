/*
 * A1000: прямой опрос стокового gralloc.sc8830.so — какие сочетания
 * формата и usage он готов выделить.
 */
#define LOG_TAG "gralloc_probe"

#include <stdio.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

struct fmt { int v; const char *name; };
struct usg { int v; const char *name; };

static struct fmt formats[] = {
    { HAL_PIXEL_FORMAT_RGBA_8888,      "RGBA_8888" },
    { HAL_PIXEL_FORMAT_RGBX_8888,      "RGBX_8888" },
    { HAL_PIXEL_FORMAT_RGB_565,        "RGB_565" },
    { HAL_PIXEL_FORMAT_YCrCb_420_SP,   "YCrCb_420_SP (NV21, 17)" },
    { HAL_PIXEL_FORMAT_YCbCr_420_888,  "YCbCr_420_888 (35)" },
    { HAL_PIXEL_FORMAT_YV12,           "YV12" },
    { HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, "IMPLEMENTATION_DEFINED" },
    { HAL_PIXEL_FORMAT_BLOB,           "BLOB" },
    { 0, NULL }
};

static struct usg usages[] = {
    { GRALLOC_USAGE_HW_TEXTURE,                              "HW_TEXTURE (0x100)" },
    { GRALLOC_USAGE_HW_RENDER,                               "HW_RENDER (0x200)" },
    { GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER,    "HW_TEXTURE|HW_RENDER" },
    { GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN
                               | GRALLOC_USAGE_SW_WRITE_OFTEN, "HW_TEXTURE|SW_RW" },
    { GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_TEXTURE,  "HW_COMPOSER|HW_TEXTURE" },
    { GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_HW_TEXTURE, "HW_CAMERA_WRITE|HW_TEXTURE" },
    { 0, NULL }
};

int main(void)
{
    const hw_module_t *mod = NULL;
    alloc_device_t *dev = NULL;
    int i, j, err;

    err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &mod);
    if (err) { printf("hw_get_module(gralloc) = %d\n", err); return 1; }
    printf("модуль: %s (api 0x%x)\n", mod->name ? mod->name : "?",
           mod->module_api_version);

    err = gralloc_open(mod, &dev);
    if (err || dev == NULL) { printf("gralloc_open = %d\n", err); return 1; }

    printf("\n%-28s", "формат \\ usage");
    for (j = 0; usages[j].name; j++) printf(" %-28s", usages[j].name);
    printf("\n");

    for (i = 0; formats[i].name; i++) {
        printf("%-28s", formats[i].name);
        for (j = 0; usages[j].name; j++) {
            buffer_handle_t h = NULL;
            int stride = 0;
            char res[32];
            err = dev->alloc(dev, 640, 480, formats[i].v, usages[j].v, &h, &stride);
            if (err == 0) {
                snprintf(res, sizeof(res), "ок (stride %d)", stride);
                dev->free(dev, h);
            } else {
                snprintf(res, sizeof(res), "ОТКАЗ %d", err);
            }
            printf(" %-28s", res);
        }
        printf("\n");
    }

    gralloc_close(dev);
    return 0;
}
