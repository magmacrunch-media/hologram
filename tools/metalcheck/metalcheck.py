#!/usr/bin/env python3
"""metalcheck: type-check shaders/trace.metal without a Metal toolchain.

The Metal tracer is the one twin that cannot be checked where it is usually
written. There is no Metal compiler off macOS, and unlike the GLSL tracer --
which tools/gldiff runs against the CPU oracle in a browser -- there is no
way to execute it either. So this does the next best thing: MSL is a C++14
dialect, so an ordinary host C++ compiler can be made to parse and type-check
the file.

That catches undeclared names, misspelled calls, wrong argument counts and
type mismatches. In particular it catches a `params` argument dropped from
one of the six functions that need one, which is the mistake this port is
most likely to make. It says NOTHING about the Metal attributes, the address
spaces, whether the two stages link, or whether the shader renders. Only a
Mac can answer those:

    xcrun -sdk macosx metal -c shaders/trace.metal -o /dev/null
    ./build.sh && ./build/m7_room --diff

Usage, from the repository root:

    python tools/metalcheck/metalcheck.py [shader.metal] [--cc COMPILER]

Exits 0 when the shader type-checks, 1 when it does not, 2 when no usable
compiler was found. Writes its generated C++ into build/, which is ignored.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def translate(src):
    """MSL to something a host C++ compiler will accept."""
    # the real header becomes our stand-in
    src = src.replace('#include <metal_stdlib>', '#include "metalshim.h"')
    src = src.replace('using namespace metal;', '')

    # [[position]], [[stage_in]], [[buffer(0)]], [[user(locn0)]], [[vertex_id]]
    src = re.sub(r'\s*\[\[[^\]]*\]\]', '', src)

    # address spaces and stage qualifiers
    src = re.sub(r'\bconstant\b', 'const', src)
    src = re.sub(r'\bthread\b', '', src)
    src = re.sub(r'\bfragment\b(?=\s+\w)', '', src)
    src = re.sub(r'\bvertex\b(?=\s+\w)', '', src)

    # C++ has no member aliasing, so swizzle reads become calls. Longest
    # alternative first, and the lookahead stops .xy matching inside .xyz.
    src = re.sub(r'\.(xyz|yzw|xy)(?![A-Za-z0-9_])', r'.\1()', src)
    return src


def find_compiler(explicit):
    if explicit:
        return explicit if shutil.which(explicit) else None
    for name in ('cl', 'clang++', 'g++', 'c++'):
        if shutil.which(name):
            return name
    return None


def command_for(cc, cpp_path, objdir):
    """MSL literals are float; in C++ they are double, so the narrowing
    warnings that fall out of that are noise and are turned off. Everything
    else stays on."""
    if os.path.basename(cc).lower().startswith('cl'):
        return [cc, '/nologo', '/std:c++14', '/W3', '/wd4244', '/wd4305',
                '/c', '/I' + HERE, '/Fo' + objdir + os.sep, cpp_path]
    return [cc, '-std=c++14', '-Wall', '-Wextra', '-Wno-unused-parameter',
            '-fsyntax-only', '-I' + HERE, cpp_path]


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument('shader', nargs='?', default=os.path.join('shaders', 'trace.metal'))
    ap.add_argument('--cc', default=None,
                    help='C++ compiler to use (default: cl, clang++, g++, c++)')
    args = ap.parse_args()

    if not os.path.isfile(args.shader):
        print('metalcheck: no such file: %s' % args.shader)
        print('            run this from the repository root')
        return 2

    cc = find_compiler(args.cc)
    if not cc:
        print('metalcheck: no C++ compiler found on PATH.')
        print('            clang++, g++ or c++ will do; on Windows, run this')
        print('            from a Developer Command Prompt so cl is present.')
        return 2

    with open(args.shader, 'r', encoding='utf-8') as f:
        src = f.read()

    outdir = 'build'
    if not os.path.isdir(outdir):
        os.makedirs(outdir)
    cpp_path = os.path.join(outdir, 'metalcheck.cpp')
    with open(cpp_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(translate(src))

    proc = subprocess.run(command_for(cc, cpp_path, outdir),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout.decode('utf-8', 'replace')

    # cl echoes the source filename on success; that is not a diagnostic
    noise = os.path.basename(cpp_path)
    lines = [l for l in out.splitlines() if l.strip() and l.strip() != noise]

    if proc.returncode == 0 and not lines:
        n = len(src.splitlines())
        print('metalcheck: %s type-checks clean (%d lines, %s)'
              % (args.shader, n, cc))
        print('            attributes, linkage and rendering still need a Mac.')
        return 0

    print('metalcheck: %s FAILED to type-check (%s)' % (args.shader, cc))
    print('            line numbers refer to %s' % cpp_path)
    for l in lines[:40]:
        print('  ' + l)
    return 1


if __name__ == '__main__':
    sys.exit(main())
