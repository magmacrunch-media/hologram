/* The panels, generated from core/schema.js.
 *
 * Nothing here knows what a sphere is. It knows 'vec3', 'unit', 'angle',
 * 'color', 'enum', 'vec4' and 'bool', and it walks a table. A new material
 * field on HoloRect is a line in schema.js and appears here for free -- the
 * alternative, hand-written panels, is how an editor falls a version behind
 * its engine and starts lying about it.
 *
 * Every control drives the same three-step edit: begin a stroke, mutate,
 * ask for a redraw. The stroke is what makes a slider drag one undo entry
 * instead of two hundred (see vendor/history.js).
 */
(function (root) {
    'use strict';

    var DEG = 180 / Math.PI;

    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) { e.className = cls; }
        if (text !== undefined) { e.textContent = text; }
        return e;
    }

    /* Colours are three floats, not a hex triple: albedo is a reflectance
       and the engine is happy to be handed 1.2. The picker is a convenience
       bolted beside the numbers, and it is the numbers that are the truth --
       so the picker clamps for display and never writes back a value the
       numbers did not already have. */
    function toHex(v) {
        var c = v.map(function (x) {
            var n = Math.round(Math.max(0, Math.min(1, x || 0)) * 255);
            return (n < 16 ? '0' : '') + n.toString(16);
        });
        return '#' + c.join('');
    }

    function fromHex(hex) {
        return [parseInt(hex.substr(1, 2), 16) / 255,
                parseInt(hex.substr(3, 2), 16) / 255,
                parseInt(hex.substr(5, 2), 16) / 255];
    }

    /* create({onEdit, onCommit}) -- onEdit(fn) runs fn inside a stroke and
       redraws; onCommit closes the stroke. */
    function create(opts) {
        var host = opts.host;
        var beginEdit = opts.beginEdit;    /* open a stroke */
        var changed = opts.changed;        /* mutated: repack and redraw */
        var commit = opts.commit;          /* close the stroke */

        /* A number input plus, where a range makes sense, a slider. Both
           drive the same setter; the number is authoritative because the
           slider cannot express a value outside its range and some of these
           fields legitimately go there. */
        function numberRow(label, get, set, f, help) {
            var row = el('div', 'field');
            var lab = el('label', 'flabel', label);
            if (help) { lab.title = help; }
            row.appendChild(lab);

            var wrap = el('div', 'fctl');
            var num = el('input');
            num.type = 'number';
            num.className = 'fnum';
            if (f.step !== undefined) { num.step = f.step; }
            num.value = get();

            var range = null;
            if (f.min !== undefined && f.max !== undefined) {
                range = el('input');
                range.type = 'range';
                range.className = 'frange';
                range.min = f.min;
                range.max = f.max;
                range.step = f.step || 0.001;
                range.value = Math.max(f.min, Math.min(f.max, get()));
                wrap.appendChild(range);
            }
            wrap.appendChild(num);
            if (f.unit) { wrap.appendChild(el('span', 'funit', f.unit)); }
            row.appendChild(wrap);

            function apply(v, from) {
                if (isNaN(v)) { return; }
                beginEdit();
                set(v);
                if (from !== 'num') { num.value = v; }
                if (range && from !== 'range') { range.value = v; }
                changed();
            }
            num.addEventListener('input', function () {
                apply(parseFloat(num.value), 'num');
            });
            num.addEventListener('change', commit);
            if (range) {
                range.addEventListener('input', function () {
                    apply(parseFloat(range.value), 'range');
                });
                range.addEventListener('change', commit);
            }
            row.refresh = function () {
                num.value = get();
                if (range) {
                    range.value = Math.max(f.min, Math.min(f.max, get()));
                }
            };
            return row;
        }

        function vecRow(obj, f, len, labels) {
            var row = el('div', 'field');
            var lab = el('label', 'flabel', f.label || f.key);
            if (f.help) { lab.title = f.help; }
            row.appendChild(lab);
            var wrap = el('div', 'fctl fvec');
            var inputs = [];
            for (var i = 0; i < len; i++) {
                (function (idx) {
                    var box = el('div', 'fvecbox');
                    if (labels) {
                        box.appendChild(el('span', 'fsub', labels[idx]));
                    }
                    var n = el('input');
                    n.type = 'number';
                    n.className = 'fnum';
                    n.step = f.step || 0.01;
                    n.value = (obj[f.key] || [])[idx];
                    n.addEventListener('input', function () {
                        var v = parseFloat(n.value);
                        if (isNaN(v)) { return; }
                        beginEdit();
                        obj[f.key][idx] = v;
                        changed();
                    });
                    n.addEventListener('change', commit);
                    inputs.push(n);
                    box.appendChild(n);
                    wrap.appendChild(box);
                }(i));
            }
            row.appendChild(wrap);
            if (f.normalize) {
                var btn = el('button', 'flink', 'normalize');
                btn.title = 'The engine wants this unit length.';
                btn.addEventListener('click', function () {
                    var v = root.linalg.norm(root.linalg.fromArray(obj[f.key]));
                    beginEdit();
                    obj[f.key] = [v.x, v.y, v.z];
                    row.refresh();
                    changed();
                    commit();
                });
                row.appendChild(btn);
            }
            row.refresh = function () {
                for (var i = 0; i < len; i++) {
                    inputs[i].value = (obj[f.key] || [])[i];
                }
            };
            return row;
        }

        function colorRow(obj, f) {
            var row = vecRow(obj, f, 3);
            var swatch = el('input');
            swatch.type = 'color';
            swatch.className = 'fswatch';
            swatch.value = toHex(obj[f.key] || [0, 0, 0]);
            swatch.addEventListener('input', function () {
                beginEdit();
                obj[f.key] = fromHex(swatch.value);
                row.refresh();
                changed();
            });
            swatch.addEventListener('change', commit);
            row.insertBefore(swatch, row.childNodes[1]);
            var baseRefresh = row.refresh;
            row.refresh = function () {
                baseRefresh();
                swatch.value = toHex(obj[f.key] || [0, 0, 0]);
            };
            return row;
        }

        /* Angles are stored in radians, because that is what the engine
           takes, and shown in degrees, because that is what a person tuning
           a polarizer is thinking in. */
        function angleRow(obj, f) {
            return numberRow((f.label || f.key) + ' (deg)',
                function () { return +((obj[f.key] || 0) * DEG).toFixed(3); },
                function (v) { obj[f.key] = v / DEG; },
                { min: f.min === undefined ? -180 : f.min,
                  max: f.max === undefined ? 180 : f.max,
                  step: 0.5, unit: 'deg' },
                f.help);
        }

        function enumRow(obj, f) {
            var row = el('div', 'field');
            var lab = el('label', 'flabel', f.label || f.key);
            if (f.help) { lab.title = f.help; }
            row.appendChild(lab);
            var sel = el('select', 'fsel');
            f.options.forEach(function (o) {
                var opt = el('option', null, o.label);
                opt.value = o.value;
                sel.appendChild(opt);
            });
            sel.value = obj[f.key] || 0;
            sel.addEventListener('change', function () {
                beginEdit();
                obj[f.key] = parseInt(sel.value, 10);
                changed();
                commit();
            });
            row.appendChild(sel);
            row.refresh = function () { sel.value = obj[f.key] || 0; };
            return row;
        }

        function boolRow(obj, f) {
            var row = el('div', 'field');
            var lab = el('label', 'flabel', f.label || f.key);
            if (f.help) { lab.title = f.help; }
            row.appendChild(lab);
            var box = el('input');
            box.type = 'checkbox';
            box.checked = !!obj[f.key];
            box.addEventListener('change', function () {
                beginEdit();
                obj[f.key] = box.checked ? 1 : 0;
                changed();
                commit();
            });
            row.appendChild(box);
            row.refresh = function () { box.checked = !!obj[f.key]; };
            return row;
        }

        function fieldRow(obj, f) {
            var row;
            if (f.kind === 'vec3') { row = vecRow(obj, f, 3); }
            else if (f.kind === 'vec4') { row = vecRow(obj, f, 4, f.labels); }
            else if (f.kind === 'color') { row = colorRow(obj, f); }
            else if (f.kind === 'angle') { row = angleRow(obj, f); }
            else if (f.kind === 'enum') { row = enumRow(obj, f); }
            else if (f.kind === 'bool') { row = boolRow(obj, f); }
            else {
                var spec = f.kind === 'unit'
                    ? { min: 0, max: 1, step: 0.01, unit: f.unit }
                    : f;
                row = numberRow(f.label || f.key,
                    function () { return obj[f.key] || 0; },
                    function (v) { obj[f.key] = v; },
                    spec, f.help);
            }

            /* Inert fields stay visible and say why they are ignored -- see
               the note in schema.js. Whether a field is inert depends on its
               NEIGHBOURS (ior on transmit, retard on filter), so this cannot
               be decided once at build time: raising transmit off zero has
               to un-grey ior in the same breath, or the panel goes on
               claiming the value is ignored when it has just started
               mattering. updateInert() is called after every edit. */
            var note = null;
            row.updateInert = function () {
                if (!f.inert) { return; }
                var why = f.inert(obj);
                row.classList.toggle('inert', !!why);
                if (why && !note) {
                    note = el('span', 'finert', why);
                    row.appendChild(note);
                } else if (why) {
                    note.textContent = why;
                    note.hidden = false;
                } else if (note) {
                    note.hidden = true;
                }
            };
            row.updateInert();
            return row;
        }

        /* Render a table of fields for one object into `into`. */
        function fields(into, obj, table) {
            into.innerHTML = '';
            into._rows = table.map(function (f) {
                var row = fieldRow(obj, f);
                into.appendChild(row);
                return row;
            });
        }

        /* Re-evaluate the inert decorations without rebuilding the inputs.
           Rebuilding would take the caret out of whatever is being typed
           into, which for a panel that updates on every keystroke means you
           could never type "1.62" -- the first "1" would replace the box. */
        function refreshInert(into) {
            (into._rows || []).forEach(function (row) {
                if (row.updateInert) { row.updateInert(); }
            });
        }

        return { fields: fields, refreshInert: refreshInert, host: host };
    }

    root.inspector = { create: create };
}(window.Hologram = window.Hologram || {}));
