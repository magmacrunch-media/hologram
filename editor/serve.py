#!/usr/bin/env python3
"""Serve the repository for the editor, without caching anything.

    python editor/serve.py            # http://127.0.0.1:8731/editor/
    python editor/serve.py 9000       # somewhere else

`python -m http.server` works too, and is what tools/gldiff has always told
people to run. The trouble is that it lets the browser cache, and everything
this page does is edit a file and reload: the tracer, the editor's own
scripts, a scene you just re-dumped. A stale script that looks current is a
bad afternoon -- you read the code, the code is right, and the page is
running the version from ten minutes ago.

So: no-store on everything, and bound to the loopback address, because this
serves the whole repository and has no business being reachable from
anywhere else.
"""

import http.server
import os
import socketserver
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8731


class NoCache(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

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
    with Server(("127.0.0.1", PORT), NoCache) as httpd:
        print("serving %s at http://127.0.0.1:%d/editor/" % (ROOT, PORT))
        print("(no-store: edit a file, reload, and you get what you edited)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
