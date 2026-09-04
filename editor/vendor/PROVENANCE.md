# vendor/

Copied verbatim from elsewhere. Do not edit these in place — fix them at the
source and copy again, or the copy silently becomes a fork.

| file | from | version | sha256 (LF-normalised, first 16) |
|---|---|---|---|
| `history.js` | `engines/magma-kit/js/history.js` | magma-kit 0.2.1 | `3611e7842ac8ad45` |

## Why copied and not synced

magma-kit has a real vendoring contract — `scripts/sync.mjs` byte-copies a
manifest into a consumer's `app/kit/`, writes a `KIT.md` of per-file hashes,
and `npm run check` verifies both directions. hologram is not a consumer: it
has no `package.json`, no `npm`, and no `node_modules`, and the editor exists
partly to demonstrate that it does not need them. Taking the contract for one
file would mean taking the toolchain.

So: one file, copied, hashed here. If the editor ever grows a desktop wrapper
it will have a `package.json` anyway, and that is the moment to join
`magma-kit/consumers.json` properly and delete this directory.

To check the copy is still faithful:

```
python -c "import hashlib,pathlib; print(hashlib.sha256(pathlib.Path('editor/vendor/history.js').read_bytes().replace(b'\r\n',b'\n')).hexdigest()[:16])"
```

## history.js

A snapshot undo/redo stack. Used here for exactly the case its header
describes: `beginStroke()` on `pointerdown` and `commitStroke()` on
`change`, so dragging a slider through two hundred `input` events is one
undo entry rather than two hundred.

It attaches to `window.MagmaKit`, not `window.Hologram`. That is deliberate —
the namespace is where it came from.
