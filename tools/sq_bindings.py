#!/usr/bin/env python3
"""The native functions the game gives its Squirrel scripts, read out of the Android library's
decompilation (re/android/armv7/decomp.c, tools/re_setup.sh): every Cr3Bind* function builds
tables of Sqrat closures -

    local_54 = Cr3UtilFlagSet;                    the native function
    FUN_003a237c(&t, "Set", &local_54, Sqrat::SqGlobal<void>::Func1<int,2>);   (or inline:
    sq_pushstring(vm, "Clear", -1); ... sq_newclosure(vm, Sqrat::SqGlobal<void>::Func1<int,2>, 1);)
    ...
    sq_pushstring(vm, "Cr3Flag", -1); ... sq_newslot   the table's name, into the root table

  sq_bindings.py <decomp.c> <the library> [--tsv] [--base 0x100000]

prints, per table, script name -> native (return type, argument types). Names Ghidra left as
DAT_xxxxxxxx are read from the library (Ghidra's addresses are the file's + --base: CR2's armv7
library 0x10000, the default; CR3's arm64 libcrx.so 0x100000).
"""
import re
import sys


BASE = 0x10000


def lib_string(lib, addr):
    o = addr - BASE
    if not 0 <= o < len(lib):
        return None
    end = lib.find(b'\0', o)
    s = lib[o:end]
    return s.decode('ascii') if s and all(32 <= c < 127 for c in s) else None


SIG = re.compile(r'Sqrat::Sq(Global|Member)<([^>]*(?:<[^>]*>)?[^>]*)>::Func(\d)(?:<(.*)>)?')


def signature(closure):
    m = SIG.search(closure)
    if not m:
        return closure.strip()
    ret, n, args = m.group(2), int(m.group(3)), m.group(4) or ''
    parts = [a.strip() for a in re.split(r',(?![^<]*>)', args)] if args else []
    if parts and parts[-1].isdigit():
        parts = parts[:-1]
    kind = 'method ' if m.group(1) == 'Member' else ''
    return f'{kind}{ret} ({", ".join(parts[-n:] if n else [])})'


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    text = open(argv[1], encoding='utf-8', errors='replace').read()
    lib = open(argv[2], 'rb').read()
    tsv = '--tsv' in argv
    global BASE
    if '--base' in argv:
        BASE = int(argv[argv.index('--base') + 1], 0)
    funcs = re.split(r'^// ---- ', text, flags=re.M)
    out = []
    for body in funcs:
        head = body.split(' @ ', 1)[0]
        if not head.startswith('Cr3Bind'):
            continue
        native = None
        pending = []      # (script name, native, signature) waiting for their table's name
        name = None
        lines, acc = [], ''
        for raw in body.splitlines():   # Ghidra wraps long calls: join until the brackets close
            acc = (acc + ' ' + raw.strip()).strip() if acc else raw.strip()
            if acc.count('(') <= acc.count(')'):
                # (arm64: a call's name alone on its line, its arguments on the next)
                if lines and acc.startswith('(') and not lines[-1].endswith((';', '{', '}')):
                    lines[-1] += acc
                else:
                    lines.append(acc)
                acc = ''
        for line in lines:
            if line.startswith(('sq_newclosure', 'FUN_', 'Sqrat::Object::BindFunc')):
                line = re.sub(r'\s*(,|::)\s*', r'\1', line)
            # (arm64: "local_a8 = (undefined **)Cr3UtilFlagSet;")
            m = re.match(r'local_\w+ = (?:\([\w ]+\*+\))?&?([A-Za-z_][\w:~<>,]*);$', line)
            if m and not m.group(1).startswith(('local_', 'PTR_', 'puVar', 'pvVar', 'pSVar', 'DAT_')):
                native = m.group(1)
                continue
            # (Ghidra leaves the template argument out where it repeats the previous call's)
            m = re.match(r'FUN_[0-9a-f]+\(&\w+,(?:"([^"]*)"|&DAT_([0-9a-f]+)),&\w+(?:,(.*))?\);?$', line)
            if m and native:
                nm = m.group(1) or lib_string(lib, int(m.group(2), 16)) or 'DAT_' + m.group(2)
                pending.append((nm, native, signature(m.group(3)) if m.group(3) else '(as the previous)'))
                native = None
                continue
            # CR3's arm64: Sqrat::Object::BindFunc((Object *)&t,"Set",&fn,8,<closure>,false)
            m = re.match(r'Sqrat::Object::BindFunc\s*\(\(Object \*\)&\w+,(?:"([^"]*)"|&DAT_([0-9a-f]+)),&\w+,\d+,(.*),(?:false|true)\s*\);?$', line)
            if m and native:
                nm = m.group(1) or lib_string(lib, int(m.group(2), 16)) or 'DAT_' + m.group(2)
                pending.append((nm, native, signature(m.group(3))))
                native = None
                continue
            m = re.match(r'sq_pushstring\(\w+,(?:"([^"]*)"|&DAT_([0-9a-f]+)),0xffffffff(?:ffffffff)?\);?$', line)
            if m:
                name = m.group(1) or lib_string(lib, int(m.group(2), 16)) or 'DAT_' + m.group(2)
                continue
            m = re.match(r'sq_newclosure\(\w+,(.*),1\);?$', line)
            if m and name and native:
                pending.append((name, native, signature(m.group(1))))
                native = name = None
                continue
            if line.startswith('sq_newslot(') and name and pending:
                out.append((head, name, pending))
                pending, name = [], None
        if pending:
            out.append((head, '(unnamed)', pending))
    total = 0
    for bind, table, entries in out:
        total += len(entries)
        if tsv:
            for nm, nat, sig in entries:
                print(f'{bind}\t{table}\t{nm}\t{nat}\t{sig}')
        else:
            print(f'{table}  ({bind}, {len(entries)})')
            for nm, nat, sig in entries:
                print(f'    {nm:28s} {nat:44s} {sig}')
    print(f'{len(out)} tables, {total} functions', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
