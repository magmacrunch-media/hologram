#!/usr/bin/env python3
"""Serve the repository for the editor, without caching anything.

    python editor/serve.py                 # http://127.0.0.1:8731/editor/
    python editor/serve.py 9000            # somewhere else

    python editor/serve.py --mount cmm=../../games/crystal-mirror-maze/build
    ... then  /editor/?dir=/mount/cmm&s=cmm_hall

`python -m http.server` works too, and is what tools/gldiff has always told
people to run. The trouble is that it lets the browser cache, and everything
this page does is edit a file and reload: the tracer, the editor's own
scripts, a scene you just re-dumped. A stale script that looks current is a
bad afternoon -- you read the code, the code is right, and the page is
running the version from ten minutes ago.

So: no-store on everything, and bound to the loopback address, because this
serves the whole repository and has no business being reachable from
anywhere else.

WHY --mount EXISTS

The editor reads a scene from build/<name>_scene.json, and this server's root
is the engine. But the scenes worth editing belong to games, which are
separate repositories: crystal-mirror-maze dumps its First Hall into its own
build/, and the engine cannot see it.

Copying the files across works once and is a bad habit -- the copy goes stale
the next time the game is rebuilt and nothing says so, and editing a stale
room is exactly the failure the editor is otherwise careful to prevent. So a
game's build directory can be mounted read-only instead, and the editor
points at it with ?dir=. Nothing is copied and there is only ever one of
each file.

Mounts are explicit, one per invocation. The engine has no business holding a
list of the games that consume it.
"""

import http.server
import os
import posixpath
import socketserver
import sys
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOUNT_PREFIX = "/mount/"


def parse_args(argv):
    port = 8731
    mounts = {}
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--mount":
            i += 1
            if i >= len(argv) or "=" not in argv[i]:
                raise SystemExit("serve.py: --mount wants NAME=PATH")
            name, _, path = argv[i].partition("=")
            if not name or "/" in name or "\\" in name:
                raise SystemExit("serve.py: mount name must be a bare word")
            full = os.path.realpath(os.path.join(ROOT, path))
            if not os.path.isdir(full):
                raise SystemExit("serve.py: no such directory: %s" % full)
            mounts[name] = full
        elif arg.isdigit():
            port = int(arg)
        else:
            raise SystemExit("serve.py: usage: serve.py [PORT] "
                             "[--mount NAME=PATH ...]")
        i += 1
    return port, mounts


class NoCache(http.server.SimpleHTTPRequestHandler):
    mounts = {}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def translate_path(self, path):
        """/mount/<name>/... comes from somewhere else on disk.

        The containment check is the part that matters: a mounted path is
        joined and then required to still be inside its mount, so a request
        full of .. segments reaches nothing it was not offered. The base
        class does the same for the document root, and this is the same
        rule applied to a second root."""
        clean = urllib.parse.urlparse(path).path
        if not clean.startswith(MOUNT_PREFIX):
            return super().translate_path(path)

        rest = clean[len(MOUNT_PREFIX):]
        name, _, tail = rest.partition("/")
        base = self.mounts.get(name)
        if base is None:
            return super().translate_path(path)

        tail = urllib.parse.unquote(tail)
        target = os.path.realpath(os.path.join(base, *[
            part for part in posixpath.normpath(tail).split("/")
            if part not in ("", ".", "..")
        ]))
        if target != base and not target.startswith(base + os.sep):
            return base           # refuse to leave the mount
        return target

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def log_message(self, fmt, *args):
        """Quiet by default: a page load is thirty requests and none of them
        are interesting unless they fail."""
        status = args[1] if len(args) > 1 else ""
        if not str(status).startswith("2"):
            super().log_message(fmt, *args)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    PORT, MOUNTS = parse_args(sys.argv[1:])
    NoCache.mounts = MOUNTS
    with Server(("127.0.0.1", PORT), NoCache) as httpd:
        print("serving %s at http://127.0.0.1:%d/editor/" % (ROOT, PORT))
        for name, path in sorted(MOUNTS.items()):
            print("  %s%s/  ->  %s" % (MOUNT_PREFIX, name, path))
            print("      /editor/?dir=%s%s&s=<name>" % (MOUNT_PREFIX, name))
        print("(no-store: edit a file, reload, and you get what you edited)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
