#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
a1000_bootimg_tool — разбор и пересборка boot.img планшета A1000.

ЗАЧЕМ ОТДЕЛЬНЫЙ ИНСТРУМЕНТ. Рабочий boot.img собран деревом los16 (Android 9),
а не тем, из которого мы обычно собираем, поэтому пересобрать его целиком
нельзя — не совпадут ни init.rc, ни файлы контекстов. Приходится править
готовый образ точечно: подменять ядро или отдельные файлы внутри ramdisk.

  info    <boot.img>                       — что внутри
  kernel  <boot.img> <Image> <out.img>     — подменить ТОЛЬКО ядро
  replace <boot.img> <имя> <файл> <out.img> — подменить файл в ramdisk

ГЛАВНАЯ СТРАХОВКА: перед любой правкой ramdisk распаковывается и собирается
обратно БЕЗ изменений, и результат сверяется с оригиналом байт в байт. Если не
сошлось — работа прекращается. Один раз это уже спасло: в старом ramdisk было
два потока gzip, и наивная перепаковка теряла второй (SELinux nonplat_*),
получался чёрный экран. В нынешнем ramdisk поток ОДИН и 76 записей cpio.

Размер образа всегда добивается нулями ровно до размера раздела (15728640),
иначе dd оставит хвост от прежнего содержимого.
"""
import gzip, io, os, re, struct, sys, zlib


def parse_hdr(d):
    magic = d[:8]
    if magic != b'ANDROID!':
        sys.exit('это не boot.img (нет ANDROID!)')
    ks, ka, rs, ra, ss, sa, ta, ps, dts = struct.unpack('<9I', d[8:44])
    return dict(ks=ks, rs=rs, ss=ss, ps=ps, dts=dts)


def pad_to(n, ps):
    return (n + ps - 1) // ps * ps


def split(d):
    h = parse_hdr(d)
    ps = h['ps']
    off_k = ps
    off_r = off_k + pad_to(h['ks'], ps)
    off_s = off_r + pad_to(h['rs'], ps)
    off_d = off_s + pad_to(h['ss'], ps)
    return h, d[off_k:off_k + h['ks']], d[off_r:off_r + h['rs']], d[off_d:off_d + h['dts']]


def cpio_parse(buf):
    """newc: 110-байтовый заголовок, поля по 8 hex-символов."""
    items, q = [], 0
    while q + 110 <= len(buf):
        if buf[q:q + 6] != b'070701':
            break
        hdr = buf[q:q + 110]
        nsz = int(hdr[94:102], 16)
        fsz = int(hdr[54:62], 16)
        name = buf[q + 110:q + 110 + nsz - 1]
        p = (q + 110 + nsz + 3) // 4 * 4
        items.append([hdr, name, buf[p:p + fsz]])
        q = (p + fsz + 3) // 4 * 4
        if name == b'TRAILER!!!':
            break
    return items, buf[q:]


def cpio_build(items, tail):
    out = bytearray()
    for hdr, name, data in items:
        h = bytearray(hdr)
        h[54:62] = b'%08X' % len(data)
        h[94:102] = b'%08X' % (len(name) + 1)
        out += h + name + b'\x00'
        while len(out) % 4:
            out += b'\x00'
        out += data
        while len(out) % 4:
            out += b'\x00'
    return bytes(out) + tail


def ramdisk_open(rd):
    """Распаковать и убедиться, что сборка обратно воспроизводит оригинал."""
    cpio = zlib.decompressobj(16 + zlib.MAX_WBITS).decompress(rd)
    items, tail = cpio_parse(cpio)
    if cpio_build(items, tail) != cpio:
        sys.exit('перепаковка НЕ воспроизводит оригинал — работать нельзя')
    return items, tail


def assemble(orig, kernel, rd_gz, dt):
    """Собрать образ, добив нулями ровно до размера раздела."""
    h = parse_hdr(orig)
    ps = h['ps']
    hdr = bytearray(orig[:ps])
    hdr[8:12] = struct.pack('<I', len(kernel))
    hdr[16:20] = struct.pack('<I', len(rd_gz))
    out = bytearray(hdr)
    out += kernel + b'\x00' * (pad_to(len(kernel), ps) - len(kernel))
    out += rd_gz + b'\x00' * (pad_to(len(rd_gz), ps) - len(rd_gz))
    out += dt + b'\x00' * (pad_to(len(dt), ps) - len(dt))
    if len(out) > len(orig):
        sys.exit('не влезает в раздел: %d > %d' % (len(out), len(orig)))
    out += b'\x00' * (len(orig) - len(out))
    return bytes(out)


def cmd_info(path):
    d = open(path, 'rb').read()
    h, kernel, rd, dt = split(d)
    m = re.search(rb'Linux version [^\x00]{0,120}', kernel)
    print('образ   : %d байт' % len(d))
    print('ядро    : %d байт  %s' % (len(kernel), m.group(0).decode() if m else '?'))
    print('ramdisk : %d байт сжато' % len(rd))
    print('dt      : %d байт' % len(dt))
    items, _ = ramdisk_open(rd)
    print('в ramdisk %d записей, round-trip совпал' % len(items))
    for _, name, data in items:
        if name in (b'sepolicy', b'init.rc', b'fstab.sc8830'):
            print('   %-16s %d байт' % (name.decode(), len(data)))


def cmd_kernel(src, image, dst):
    d = open(src, 'rb').read()
    h, kernel, rd, dt = split(d)
    new = open(image, 'rb').read()
    print('ядро: %d -> %d байт' % (len(kernel), len(new)))
    out = assemble(d, new, rd, dt)
    open(dst, 'wb').write(out)
    m = re.search(rb'Linux version [^\x00]{0,80}', split(out)[1])
    print('готово: %s  %s' % (dst, m.group(0).decode() if m else ''))


def cmd_replace(src, name, path, dst):
    d = open(src, 'rb').read()
    h, kernel, rd, dt = split(d)
    items, tail = ramdisk_open(rd)
    new = open(path, 'rb').read()
    hit = False
    for it in items:
        if it[1] == name.encode():
            print('%s: %d -> %d байт' % (name, len(it[2]), len(new)))
            it[2] = new
            hit = True
    if not hit:
        sys.exit('в ramdisk нет файла %s' % name)
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode='wb', compresslevel=9, mtime=0) as g:
        g.write(cpio_build(items, tail))
    out = assemble(d, kernel, buf.getvalue(), dt)
    open(dst, 'wb').write(out)
    # самопроверка: перечитать собранное
    items2, _ = ramdisk_open(split(out)[2])
    got = [x[2] for x in items2 if x[1] == name.encode()][0]
    print('готово: %s  (проверка: файл внутри совпадает — %s)' % (dst, got == new))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    c = sys.argv[1]
    if c == 'info':
        cmd_info(sys.argv[2])
    elif c == 'kernel':
        cmd_kernel(sys.argv[2], sys.argv[3], sys.argv[4])
    elif c == 'replace':
        cmd_replace(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        print(__doc__)
        sys.exit(2)
