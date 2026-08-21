#ifndef A1000_ELFLOAD_H
#define A1000_ELFLOAD_H

/*
 * Загружает библиотеку, которую отказывается грузить bionic (не-PIC, текстовые
 * релокации), и возвращает адреса запрошенных символов в out[].
 * Возвращает базу отображения или NULL.
 */
void *elfload_open(const char *path, const char *const *want,
                   void **out, int nwant);

#endif
