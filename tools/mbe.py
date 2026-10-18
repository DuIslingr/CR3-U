#!/usr/bin/env python3
"""CR3's parameter tables (`data\\*.mbe`; MbParameterManager::structParam in the Android library).

  'EXPA', u32 tables, then each table:
    u32 name length, the name (NUL-padded to that length), u32 row size, u32 rows,
    the rows - each starting 8-byte aligned in the file (4 bytes skipped where it isn't)
  'CHNK', u32 count, then each: u32 file offset, u32 size, the bytes (a NUL-terminated string,
    padded): the 8-byte slot at that offset is a pointer to them (0xcccccccc placeholders until then)

  The columns' types are not in the file (the game parses each table with its own code, by name);
  a slot CHNK points into is a string, the rest ints / floats.

  mbe.py check <archive>                    every table parses, in every .mbe
  mbe.py tables <archive> [name]            each file's tables: name, row size, rows, string slots
  mbe.py dump <archive> <file> [table]      rows as JSON lines (strings by slot, the rest as u32)
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mvgl  # noqa: E402


class Mbe:
    def __init__(self, d, wide=None):
        """wide: the 64-bit builds' layout (None: whichever reads)"""
        if d[:4] != b'EXPA':
            raise ValueError('not an EXPA table file')
        if wide is None:
            try:
                self.__init__(d, True)
            except (ValueError, struct.error):
                self.__init__(d, False)
            return
        self.d, self.wide = d, wide
        n = struct.unpack_from('<I', d, 4)[0]
        p = 8
        self.tables = []
        for _ in range(n):
            ln = struct.unpack_from('<I', d, p)[0]
            name = d[p + 4:p + 4 + ln].split(b'\0')[0].decode('ascii')
            p += 4 + ln
            size, rows = struct.unpack_from('<II', d, p)
            if ln > 256 or size == 0 or rows > len(d) or p + size * rows > len(d):
                raise ValueError(f'table {len(self.tables)} past the file\'s end')
            p += 8
            if wide and p % 8:
                p += 4
            offsets = []
            for _ in range(rows):
                offsets.append(p)
                p += size
                if wide and p % 8:
                    p += 4
            self.tables.append({'name': name, 'size': size, 'rows': offsets})
        if d[p:p + 4] != b'CHNK':
            raise ValueError(f'no CHNK at {p:#x}')
        count = struct.unpack_from('<I', d, p + 4)[0]
        p += 8
        self.strings = {}  # file offset of the slot -> bytes
        for _ in range(count):
            at, size = struct.unpack_from('<II', d, p)
            self.strings[at] = d[p + 8:p + 8 + size].split(b'\0')[0]
            p += 8 + size
        self.end = p

    def slots(self, t):
        """the string slots' offsets within a row"""
        out = set()
        for r in t['rows']:
            for k in range(0, t['size'], 4):
                if r + k in self.strings:
                    out.add(k)
        return sorted(out)

    def row(self, t, i):
        r = t['rows'][i]
        strs = set(self.slots(t))
        out, k = {}, 0
        while k < t['size']:
            if k in strs:
                out[f'{k:02x}'] = self.strings.get(r + k, b'').decode('utf-8', 'replace')
                k += 8 if self.wide else 4
            else:
                out[f'{k:02x}'] = struct.unpack_from('<I', self.d, r + k)[0]
                k += 4
        return out


def files(arc):
    return [(n, o, s, st) for n, o, s, st in arc.files() if n.lower().endswith('.mbe')]


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, arc = argv[1], mvgl.Mvgl(argv[2])
    if cmd == 'check':
        ok = bad = tables = rows = strings = 0
        for n, o, s, st in files(arc):
            try:
                m = Mbe(arc.decoded(o, s, st))
                ok += 1
                tables += len(m.tables)
                rows += sum(len(t['rows']) for t in m.tables)
                strings += len(m.strings)
                if m.end != len(m.d):
                    print(f'{n}: {len(m.d) - m.end} bytes after CHNK')
            except Exception as e:  # noqa: BLE001
                bad += 1
                print(f'bad {n}: {e}')
        print(f'{ok} files parse, {bad} bad: {tables} tables, {rows} rows, {strings} strings')
        return 1 if bad else 0
    if cmd == 'tables':
        for n, o, s, st in files(arc):
            if len(argv) > 3 and argv[3] not in n:
                continue
            m = Mbe(arc.decoded(o, s, st))
            for t in m.tables:
                print(f'{n}\t{t["name"]}\trow {t["size"]:#x}\t{len(t["rows"])} rows\tstrings at {m.slots(t)}')
        return 0
    if cmd == 'dump' and len(argv) > 3:
        for n, o, s, st in files(arc):
            if n.lower() != argv[3].lower() and os.path.basename(n.replace('\\', '/')).lower() != argv[3].lower():
                continue
            m = Mbe(arc.decoded(o, s, st))
            for t in m.tables:
                if len(argv) > 4 and t['name'] != argv[4]:
                    continue
                for i in range(len(t['rows'])):
                    print(json.dumps({'table': t['name'], 'row': i, **m.row(t, i)}, ensure_ascii=False))
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
