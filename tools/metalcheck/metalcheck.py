#!/usr/bin/env python3
"""metalcheck: verify the Metal side without a Mac.

Two things in this engine are written for macOS and cannot be compiled
anywhere else: the MSL tracer, and the Metal frame readback in display.c.
Both are dialects of languages an ordinary host compiler already knows --
MSL is C++14, and Objective-C is Objective-C -- so with a stand-in for the
headers, a host compiler can be made to parse and type-check them.

    python tools/metalcheck/metalcheck.py

It checks whichever of the two it can:

  shaders/trace.metal    as C++14 against metalshim.h, with any C++ compiler
  the readback           as Objective-C with ARC against metalobjc_shim.h,
                         which needs clang (gcc's Objective-C has no ARC)

What this buys: undeclared names, misspelled selectors and properties, wrong
arities, type mismatches, bad bridge casts. What it does not: whether the
selectors match Apple's real ones, whether the Metal attributes are right,
whether the two shader stages link, or whether any of it renders. Only a Mac
answers those, and there the real toolchain is the better check:

    xcrun -sdk macosx metal -c shaders/trace.metal -o /dev/null
    ./build.sh && ./build/m7_room --diff

Exits 0 when every check it could run passed, 1 when one failed, 2 when it
could not run any. A check it cannot run (no clang, say) is reported as
skipped rather than counted as a pass.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = 'build'


# ---------------------------------------------------------------- the shader

def translate_msl(src):
    """MSL into something a host C++ compiler will accept."""
    src = src.replace('#include <metal_stdlib>', '#include "metalshim.h"')
    src = src.replace('using namespace metal;', '')
    # [[position]], [[stage_in]], [[buffer(0)]], [[user(locn0)]], [[vertex_id]]
    src = re.sub(r'\s*\[\[[^\]]*\]\]', '', src)
    src = re.sub(r'\bconstant\b', 'const', src)
    src = re.sub(r'\bthread\b', '', src)
    src = re.sub(r'\bfragment\b(?=\s+\w)', '', src)
    src = re.sub(r'\bvertex\b(?=\s+\w)', '', src)
    # C++ has no member aliasing, so swizzle reads become calls. Longest
    # alternative first, and the lookahead stops .xy matching inside .xyz.
    src = re.sub(r'\.(xyz|yzw|xy)(?![A-Za-z0-9_])', r'.\1()', src)
    return src


def find_cxx(explicit):
    if explicit:
        return explicit if shutil.which(explicit) else None
    for name in ('cl', 'clang++', 'g++', 'c++'):
        if shutil.which(name):
            return name
    return None


def cxx_command(cc, path):
    """MSL literals are float; in C++ they are double, so the narrowing
    warnings that fall out of that are noise and are turned off."""
    # Exactly cl, not anything starting with those two letters: clang++
    # begins with "cl" and would be handed MSVC flags.
    if os.path.splitext(os.path.basename(cc).lower())[0] == 'cl':
        return [cc, '/nologo', '/std:c++14', '/W3', '/wd4244', '/wd4305',
                '/c', '/I' + HERE, '/Fo' + OUTDIR + os.sep, path]
    return [cc, '-std=c++14', '-Wall', '-Wextra', '-Wno-unused-parameter',
            '-fsyntax-only', '-I' + HERE, path]


def check_shader(shader, cc):
    with open(shader, 'r', encoding='utf-8') as f:
        src = f.read()
    out_path = os.path.join(OUTDIR, 'metalcheck.cpp')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(translate_msl(src))
    return run(cxx_command(cc, out_path), out_path,
               '%s (%d lines, %s)' % (shader, len(src.splitlines()), cc))


# ---------------------------------------------------------- the readback

DISPLAY = os.path.join('source', 'display.c')


def extract_readback(text):
    """The Metal arm of holo_display_read_frame, minus the imports the shim
    stands in for. Returns None if display.c no longer looks like that."""
    try:
        i = text.index('#elif defined(SOKOL_METAL)',
                       text.index('holo_display_read_frame'))
        j = text.index('#else', i)
    except ValueError:
        return None
    body = text[i:j].split('\n', 1)[1]
    return '\n'.join(l for l in body.split('\n')
                     if not l.lstrip().startswith('#import'))


def check_objc(cc):
    with open(DISPLAY, 'r', encoding='utf-8') as f:
        body = extract_readback(f.read())
    if body is None:
        print('metalcheck: could not find the Metal readback in %s --'
              % DISPLAY)
        print('            the extraction anchors in this script are stale.')
        return False
    out_path = os.path.join(OUTDIR, 'metalcheck_objc.m')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('#include "metalobjc_shim.h"\n')
        f.write(body)
    cmd = [cc, '-fsyntax-only', '-fobjc-arc', '-fobjc-runtime=macosx-10.15',
           '-Wall', '-Wextra', '-x', 'objective-c', '-I' + HERE, out_path]
    return run(cmd, out_path,
               'the Metal readback in %s (%d lines, %s)'
               % (DISPLAY, len(body.splitlines()), cc))


# ------------------------------------------------------------------- running

def run(cmd, generated, label):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout.decode('utf-8', 'replace')
    noise = os.path.basename(generated)
    lines = [l for l in out.splitlines() if l.strip() and l.strip() != noise]
    if proc.returncode == 0 and not lines:
        print('  ok      %s' % label)
        return True
    print('  FAILED  %s' % label)
    print('          line numbers refer to %s' % generated)
    for l in lines[:40]:
        print('    ' + l)
    return False


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument('shader', nargs='?',
                    default=os.path.join('shaders', 'trace.metal'))
    ap.add_argument('--cc', default=None,
                    help='C++ compiler for the shader check')
    args = ap.parse_args()

    if not os.path.isfile(args.shader) or not os.path.isfile(DISPLAY):
        print('metalcheck: run this from the repository root')
        return 2
    if not os.path.isdir(OUTDIR):
        os.makedirs(OUTDIR)

    ran = 0
    failed = 0

    cxx = find_cxx(args.cc)
    if cxx:
        ran += 1
        if not check_shader(args.shader, cxx):
            failed += 1
    else:
        print('  skipped the shader: no C++ compiler on PATH (clang++, g++,')
        print('          c++, or cl from a Developer Command Prompt)')

    # gcc's Objective-C has no ARC, so this one wants clang specifically.
    clang = shutil.which('clang')
    if clang:
        ran += 1
        if not check_objc(clang):
            failed += 1
    else:
        print('  skipped the readback: needs clang, which has the Objective-C')
        print('          ARC support gcc does not')

    if ran == 0:
        print('metalcheck: no compiler available for either check')
        return 2
    if failed:
        return 1
    print('metalcheck: %d of 2 checks ran, all clean.' % ran)
    print('            Attributes, linkage and rendering still need a Mac.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
