/*
 * a1000_sepolicy_inject — дописать правила allow в ГОТОВУЮ двоичную политику.
 *
 * ЗАЧЕМ. Рабочий boot.img этого планшета собран деревом los16 (Android 9), а
 * не los15.1, из которого мы обычно собираем. Политика, собранная в los15.1,
 * не подходит: из 646 типов, на которые ссылаются файлы контекстов в ramdisk,
 * 81 в ней отсутствует (типы девятки — bpfloader_exec, hal_wifi_hostapd_*,
 * ctl_start_prop и другие). Пересобирать los16 с нуля — часы, а его out/
 * удалён. Поэтому правим то, что есть: читаем /sepolicy, дописываем нужные
 * разрешения и пишем обратно.
 *
 * Правила читаются из файла, по одному в строке:
 *     исходный_домен  целевой_тип  класс  право[,право...]
 * Пустые строки и строки с '#' пропускаются.
 *
 * Сборка (в chroot дерева, libsepol берётся исходниками):
 *   gcc -O2 -I external/selinux/libsepol/include -I external/selinux/libsepol/src \
 *       a1000_sepolicy_inject.c external/selinux/libsepol/src/*.c -o inject
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sepol/policydb/policydb.h>
#include <sepol/policydb/expand.h>
#include <sepol/policydb/hashtab.h>

static policydb_t pdb;

/* Право может быть объявлено в самом классе или в общем предке (common). */
static int perm_bit(class_datum_t *cls, const char *name, uint32_t *bit)
{
    perm_datum_t *p = hashtab_search(cls->permissions.table, (char *)name);
    if (!p && cls->comdatum)
        p = hashtab_search(cls->comdatum->permissions.table, (char *)name);
    if (!p)
        return -1;
    *bit = 1U << (p->s.value - 1);
    return 0;
}

static int add_allow(const char *s, const char *t, const char *c, const char *perms)
{
    type_datum_t *src = hashtab_search(pdb.p_types.table, (char *)s);
    type_datum_t *tgt = hashtab_search(pdb.p_types.table, (char *)t);
    class_datum_t *cls = hashtab_search(pdb.p_classes.table, (char *)c);
    avtab_key_t key;
    avtab_datum_t *av, dat;
    uint32_t bits = 0, bit;
    char buf[512], *tok, *save;

    if (!src) { fprintf(stderr, "  НЕТ типа %s\n", s); return -1; }
    if (!tgt) { fprintf(stderr, "  НЕТ типа %s\n", t); return -1; }
    if (!cls) { fprintf(stderr, "  НЕТ класса %s\n", c); return -1; }

    snprintf(buf, sizeof buf, "%s", perms);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        if (perm_bit(cls, tok, &bit)) {
            fprintf(stderr, "  НЕТ права %s у класса %s\n", tok, c);
            return -1;
        }
        bits |= bit;
    }

    key.source_type  = src->s.value;
    key.target_type  = tgt->s.value;
    key.target_class = cls->s.value;
    key.specified    = AVTAB_ALLOWED;

    av = avtab_search(&pdb.te_avtab, &key);
    if (av) {
        if ((av->data & bits) == bits) {
            printf("  уже было: allow %s %s:%s\n", s, t, c);
            return 0;
        }
        av->data |= bits;
    } else {
        memset(&dat, 0, sizeof dat);
        dat.data = bits;
        if (avtab_insert(&pdb.te_avtab, &key, &dat)) {
            fprintf(stderr, "  не вставилось: allow %s %s:%s\n", s, t, c);
            return -1;
        }
    }
    printf("  + allow %s %s:%s { %s }\n", s, t, c, perms);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *fp;
    struct policy_file pf;
    char line[512];
    int bad = 0, added = 0;

    if (argc != 4) {
        fprintf(stderr, "запуск: %s <политика-на-входе> <файл-правил> <политика-на-выходе>\n", argv[0]);
        return 2;
    }

    fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    policy_file_init(&pf);
    pf.type = PF_USE_STDIO;
    pf.fp = fp;
    if (policydb_init(&pdb)) { fprintf(stderr, "policydb_init\n"); return 1; }
    if (policydb_read(&pdb, &pf, 0)) { fprintf(stderr, "не прочиталась политика\n"); return 1; }
    fclose(fp);
    printf("политика прочитана: версия %d, типов %u, классов %u\n",
           pdb.policyvers, pdb.p_types.nprim, pdb.p_classes.nprim);

    fp = fopen(argv[2], "r");
    if (!fp) { perror(argv[2]); return 1; }
    while (fgets(line, sizeof line, fp)) {
        char s[128], t[128], c[64], p[256];
        char *h = strchr(line, '#');
        if (h) *h = 0;
        if (sscanf(line, "%127s %127s %63s %255s", s, t, c, p) != 4)
            continue;
        if (add_allow(s, t, c, p)) bad++;
        else added++;
    }
    fclose(fp);

    fp = fopen(argv[3], "wb");
    if (!fp) { perror(argv[3]); return 1; }
    policy_file_init(&pf);
    pf.type = PF_USE_STDIO;
    pf.fp = fp;
    if (policydb_write(&pdb, &pf)) { fprintf(stderr, "не записалась политика\n"); return 1; }
    fclose(fp);

    printf("готово: применено %d, не вышло %d -> %s\n", added, bad, argv[3]);
    return bad ? 1 : 0;
}
