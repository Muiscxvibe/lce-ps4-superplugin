#!/usr/bin/env python3
"""Keep integration/shipping-plugin.patch applicable to the CURRENT shipping files.

The shipping tree may be pristine or may already carry this patch (an engineer applied it to
build/test). Both are handled:
  - pristine reconstruction: a file whose hunks reverse-apply cleanly is taken as "applied" and
    reversed; build-crashlog.sh is "applied" when it contains the SDK block marker, which is
    then stripped (the block and the lcemodloader.o link argument);
  - the build-crashlog.sh hunk is regenerated from the live script: the block in
    integration/build-crashlog.sdk-block goes right before the main plugin link (the `ld.lld` line, not `-r`, writing super195.elf) and
    "$lcesdk/build/ps4/lcemodloader.o" follows `$crash/build/lce_*.o` on that line;
  - every other hunk is re-applied to the pristine copy (it must still apply).

usage:
  rebase-patch.py                 rewrite shipping-plugin.patch + patch-base.sha256 (pristine hashes)
  rebase-patch.py --check         report only
  rebase-patch.py --pristine DIR  write pristine copies of every patched file under DIR (verify-patch.sh)
  rebase-patch.py --upgrade OUT   write OUT = the diff from the LIVE (already patched) tree to the new
                                  patched state (apply with `patch -p0` in minecraft-lce-work)
Reads the shipping trees, never writes them.
"""
import hashlib, re, shutil, subprocess, sys, tempfile
from pathlib import Path

W = Path('/home/codey/minecraft-lce-work')
SDK = W / 'modloader-sdk'
PATCH = SDK / 'integration/shipping-plugin.patch'
PREV = SDK / 'integration/shipping-plugin.prev.patch'
OVERRIDE = SDK / 'integration/pristine'   # hand-reconstructed pristine copies, used only when detection fails   # the version an engineer may have applied before this one
BASE = SDK / 'integration/patch-base.sha256'
BLOCK = SDK / 'integration/build-crashlog.sdk-block'
BUILD = 'ps4-metadata-snapshot/crashlog-build/build-crashlog.sh'
MARK = '# LCE mod loader SDK'
OBJ = ' "$lcesdk/build/ps4/lcemodloader.o"'


def parts_of(text):
    out = {}
    for x in re.split(r'(?m)^(?=diff -ruN )', text):
        if x.strip():
            out[re.search(r'(?m)^\+\+\+ (\S+)', x).group(1)] = x
    return out


def patch(cwd, text, *args):
    return subprocess.run(['patch', '-p0', '--quiet', '--batch', '--no-backup-if-mismatch', '-r', '-', *args], input=text, text=True, cwd=cwd, capture_output=True)


def is_main_link(line):
    """The final plugin link: an ld.lld command (not `ld.lld -r`) that writes super195.elf.
    Other helpers add --wrap flags anywhere on it; the partial craft_provider link is `ld.lld -r`."""
    return line.startswith('ld.lld ') and not line.startswith('ld.lld -r ') and 'super195.elf' in line


def strip_build(text):
    lines = text.split('\n')
    if not any(l.startswith(MARK) for l in lines):
        return text, False
    i = next(k for k, l in enumerate(lines) if l.startswith(MARK))
    j = next(k for k in range(i, len(lines)) if is_main_link(lines[k]))
    lines[j] = lines[j].replace(OBJ, '')
    del lines[i:j]
    return '\n'.join(lines), True


def add_build(text):
    lines = text.split('\n')
    idx = [i for i, l in enumerate(lines) if is_main_link(l)]
    if len(idx) != 1 or ' $crash/build/lce_*.o --start-group' not in lines[idx[0]]:
        raise SystemExit('build-crashlog.sh: expected exactly one main `ld.lld ... -o ...super195.elf` line with $crash/build/lce_*.o --start-group')
    j = idx[0]
    lines[j] = lines[j].replace(' $crash/build/lce_*.o --start-group', ' $crash/build/lce_*.o' + OBJ + ' --start-group')
    lines[j:j] = BLOCK.read_text().rstrip('\n').split('\n')
    return '\n'.join(lines)


def pristine(t):
    """Copies of every patched file, pristine, under t/a; returns (files, parts, state per file)."""
    parts = parts_of(PATCH.read_text())
    prev = parts_of(PREV.read_text()) if PREV.exists() else {}
    for f in prev:   # a file the previous version patched but this one no longer does: still reconstruct it
        parts.setdefault(f, '')
    state = {}
    for f, hunk in parts.items():
        dst = t / 'a' / f
        dst.parent.mkdir(parents=True, exist_ok=True)
        live = (W / f).read_text()
        if f == BUILD:
            txt, applied = strip_build(live)
            dst.write_text(txt)
            state[f] = 'applied' if applied else 'pristine'
            continue
        dst.write_text(live)
        state[f] = 'pristine'
        for name, h in (('applied', hunk), ('applied-prev', prev.get(f))):
            if h and patch(t / 'a', h, '--dry-run', '-R').returncode == 0 and patch(t / 'a', h, '--dry-run', '-N').returncode != 0:
                patch(t / 'a', h, '-R')
                state[f] = name
                break
        # Neither hunk reverses and the current one does not forward-apply either: the live file mixes this
        # patch with other edits. Use a hand-reconstructed pristine copy (integration/pristine/<path>) if one exists.
        if state[f] == 'pristine' and hunk and patch(t / 'a', hunk, '--dry-run', '-N').returncode != 0 and (OVERRIDE / f).exists():
            dst.write_text((OVERRIDE / f).read_text())
            state[f] = 'override'
    return parts, state


def main():
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory() as td:
        t = Path(td)
        parts, state = pristine(t)
        if args[:1] == ['--pristine']:
            out = Path(args[1])
            for f in parts:
                (out / f).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(t / 'a' / f, out / f)
            print(' '.join(f'{Path(f).name}={s}' for f, s in state.items()))
            return 0
        shutil.copytree(t / 'a', t / 'b')
        for f, hunk in parts.items():
            if f == BUILD:
                (t / 'b' / f).write_text(add_build((t / 'a' / f).read_text()))
                continue
            if not hunk:
                continue
            r = patch(t / 'b', hunk, '-N')
            if r.returncode:
                print(f'{f}: hunk no longer applies to the pristine file; port it by hand\n{r.stdout}{r.stderr}')
                return 1
        if args[:1] == ['--upgrade']:
            d = ''
            for f in parts:
                live, new = (W / f).read_text(), (t / 'b' / f).read_text()
                if live != new:
                    (t / 'l' / f).parent.mkdir(parents=True, exist_ok=True)
                    (t / 'l' / f).write_text(live)
                    (t / 'n' / f).parent.mkdir(parents=True, exist_ok=True)
                    (t / 'n' / f).write_text(new)
            if (t / 'l').exists():
                d = subprocess.run(['diff', '-ruN', 'l', 'n'], cwd=t, capture_output=True, text=True).stdout
                d = re.sub(r'(?m)^(---|\+\+\+) (l|n)/', r'\1 ', d)
            Path(args[1]).write_text(d)
            print(f'wrote {args[1]} ({len(d.splitlines())} lines): live (patched) tree -> current SDK patch state')
            return 0
        d = subprocess.run(['diff', '-ruN', 'a', 'b'], cwd=t, capture_output=True, text=True).stdout
        d = re.sub(r'(?m)^(---|\+\+\+) (a|b)/', r'\1 ', d)
        sums = ''.join(f'{hashlib.sha256((t / "a" / f).read_bytes()).hexdigest()}  {W / f}\n' for f in parts)
        strip = lambda s: re.sub(r'(?m)^((?:---|\+\+\+) \S+).*$', r'\1', s)
        changed = strip(d) != strip(PATCH.read_text())
        print(f'{len(parts)} files ({", ".join(f"{Path(f).name}={s}" for f, s in state.items())}); patch {"changed" if changed else "unchanged"}')
        if '--check' not in args:
            PATCH.write_text(d)
            BASE.write_text(sums)
            print(f'wrote {PATCH} and {BASE} (pristine hashes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
