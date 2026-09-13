// Checks the gradient browse dialog: what it lists, how it draws a swatch,
// and what choosing one writes.
//
// It bundles the real components with esbuild (from web/node_modules) and
// renders them with preact-render-to-string. Unlike tools/gradient_groups_check.mjs,
// which only reads markup, this one drives the real handlers: it captures the
// vnodes preact builds during the render (options.vnode), pulls the real
// onChange off the <select> and the real onClick off a swatch, calls them with
// a recording setField, and compares the two field writes byte for byte. That
// is the criterion "choosing a swatch writes the same ref the select writes",
// checked rather than asserted.
//
// It is still not a browser: no layout, no focus, no key events, no save. The
// dialog's Escape handling, focus return and 390 px layout need the Playwright
// rig in C:\work\camshots against a device.
//
// Usage: node tools/gradient_browse_check.mjs
//        node tools/gradient_browse_check.mjs --emit-page <file.html>
//
// --emit-page writes a standalone page that mounts the real editor with the
// application's own stylesheet, for the browser half of the check
// (tools/gradient_browse_browser.py drives it). It needs web/dist from a
// recent `npm run build`, because that is where the stylesheet comes from.
//
// Exits 0 on PASS, 1 on any failed check.

import { mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, basename } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const web = join(here, '..', 'web');
const components = join(web, 'src', 'components');

const esbuild = await import(pathToFileURL(join(web, 'node_modules', 'esbuild', 'lib', 'main.js')));

const ENTRY = `
import { h, options } from 'preact';
import render from 'preact-render-to-string';
import { GradientEditor } from './GradientEditor.jsx';
import { GradientBrowser } from './GradientBrowser.jsx';
import {
  BG_THEMES,
  BG_THEME_CATEGORIES,
  gradientCss,
  parseGradientLibrary,
} from '../config/bgAnimations.js';

// Two saved gradients. The second has three stops at positions nobody would
// pick evenly, which is what proves a library entry draws by its own stops.
const LIBRARY = '1|Mine|ff0000,0000ff;2|Odd stops|ff0000@0,00ff00@20,0000ff@250';

const base = {
  bgAnimGradients: LIBRARY,
  bgAnimThemeMap: '',
  bgAnimGradientRef: '0',
  bgAnimTheme: '0',
  bgAnimCustomTheme: '',
  bgAnimBrightness: '100',
  bgAnimHighlightKnee: '100',
};

let failures = 0;
function check(what, got, want) {
  const a = JSON.stringify(got);
  const b = JSON.stringify(want);
  if (a === b) {
    console.log('  ok   ' + what);
  } else {
    failures++;
    console.log('  FAIL ' + what + '\\n       got  ' + a + '\\n       want ' + b);
  }
}

function ok(what, cond) {
  check(what, !!cond, true);
}

// Renders and keeps every vnode preact built on the way, so the check can call
// the handlers the components actually installed.
function renderCapturing(vnode) {
  const seen = [];
  const prev = options.vnode;
  options.vnode = v => {
    seen.push(v);
    if (prev) prev(v);
  };
  let html;
  try {
    html = render(vnode);
  } finally {
    options.vnode = prev;
  }
  return { html, seen };
}

function find(seen, pred) {
  const v = seen.find(pred);
  if (!v) throw new Error('vnode not found');
  return v;
}

// A recording setField, and the editor rendered with it.
function editor(scope, formData) {
  const writes = [];
  const out = renderCapturing(
    h(GradientEditor, { scope, formData, setField: (k, v) => writes.push([k, v]) }),
  );
  return {
    ...out,
    writes,
    select: find(out.seen, v => v.type === 'select'),
    browseButton: find(out.seen, v => v.type === 'button' && v.props['aria-haspopup'] === 'dialog'),
    browser: find(out.seen, v => v.type === GradientBrowser),
  };
}

// The dialog's own markup, opened with the exact props the editor gave it.
function openBrowser(ed) {
  return renderCapturing(h(GradientBrowser, { ...ed.browser.props, isOpen: true }));
}

// [{label, refs, names, current}] in render order, read back out of the HTML.
function dialogGroups(html) {
  const out = [];
  const groupRe = /<div role="group" aria-label="([^"]*)"([\\s\\S]*?)(?=<div role="group"|$)/g;
  let g;
  while ((g = groupRe.exec(html)) !== null) {
    const entry = { label: g[1], refs: [], names: [], current: [] };
    // An empty ref (the Global entry) comes out as a bare attribute.
    const btnRe = /<button type="button" data-ref(?:="([^"]*)")?([^>]*)>([\\s\\S]*?)<\\/button>/g;
    let b;
    while ((b = btnRe.exec(g[2])) !== null) {
      entry.refs.push(b[1] ?? '');
      entry.names.push(
        b[3]
          .replace(/<[^>]*>/g, '')
          .replace(/ \\(current\\)$/, '')
          .trim(),
      );
      if (/aria-current="true"/.test(b[2])) entry.current.push(b[1] ?? '');
    }
    out.push(entry);
  }
  return out;
}

// The background of one swatch, as the style attribute carries it.
function swatchCss(html, ref) {
  const attr = ref === '' ? 'data-ref' : 'data-ref="' + ref + '"';
  const btn = new RegExp(
    '<button type="button" ' + attr + '[^>]*>([\\\\s\\\\S]*?)</button>',
  ).exec(html);
  if (!btn) return null;
  const style = /style="background:([^"]*)"/.exec(btn[1]);
  return style ? style[1].trim().replace(/;$/, '') : null;
}

// The select's own options, group by group, so the two controls can be
// compared directly. Options before the first optgroup (the Global entry, and
// the disabled legacy stand-in) come back under the label null.
function selectGroups(html) {
  const start = html.indexOf('<select');
  const sel = html.slice(start, html.indexOf('</select>', start));
  const out = [];
  let cur = { label: null, refs: [], names: [] };
  const token = /<optgroup[^>]*label="([^"]*)"|<option([^>]*)>([^<]*)</g;
  let m;
  while ((m = token.exec(sel)) !== null) {
    if (m[1] !== undefined) {
      if (cur.refs.length > 0 || cur.label !== null) out.push(cur);
      cur = { label: m[1], refs: [], names: [] };
    } else {
      const withValue = /\\svalue="([^"]*)"/.exec(m[2]);
      cur.refs.push(withValue ? withValue[1] : '');
      cur.names.push(m[3]);
    }
  }
  out.push(cur);
  return out;
}

const wantBuiltin = BG_THEME_CATEGORIES.map(c => ({
  label: c,
  refs: BG_THEMES.map((t, i) => [t, i])
    .filter(([t]) => t.category === c)
    .map(([, i]) => String(i)),
  names: BG_THEMES.filter(t => t.category === c).map(t => t.name),
})).filter(g => g.refs.length > 0);

const mine = { label: 'My gradients', refs: ['c1', 'c2'], names: ['Mine', 'Odd stops'] };

// ---- global scope --------------------------------------------------------
console.log('global scope, two saved gradients');
let ed = editor({ kind: 'global' }, { ...base });
let dlg = openBrowser(ed);
let groups = dialogGroups(dlg.html);

check(
  'my gradients first, then the declared category order',
  groups.map(g => ({ label: g.label, refs: g.refs, names: g.names })),
  [mine, ...wantBuiltin],
);
check(
  'the same refs the select offers, group for group',
  groups.map(g => ({ label: g.label, refs: g.refs })),
  selectGroups(ed.html).map(g => ({ label: g.label, refs: g.refs })),
);
check(
  'the current gradient is the one marked, and only it',
  groups.flatMap(g => g.current),
  ['0'],
);

// A library entry with three stops at 0, 20 and 250 draws by its own
// positions, not evenly spaced, and by the same call the editor's bar uses.
const odd = parseGradientLibrary(LIBRARY).find(g => g.id === 2);
check('a library entry draws by its own stops', swatchCss(dlg.html, 'c2'), gradientCss(odd.stops));
ok(
  'and its stops land where its positions say',
  swatchCss(dlg.html, 'c2') === 'linear-gradient(to right, #ff0000 0.0%, #00ff00 7.8%, #0000ff 98.0%)',
);
check(
  'a built-in draws the same way the editor bar draws it',
  swatchCss(dlg.html, '3'),
  gradientCss(
    BG_THEMES[3].stops.map((color, i, a) => ({
      color,
      pos: Math.floor((i * 255) / (a.length - 1)),
    })),
  ),
);

// The two controls, driven with the same gradient.
function writesFromSelect(ed, ref) {
  ed.writes.length = 0;
  ed.select.props.onChange({ target: { value: ref } });
  return ed.writes.slice();
}
function writesFromSwatch(ed, dlg, ref) {
  const swatch = find(dlg.seen, v => v.type === 'button' && v.props['data-ref'] === ref);
  ed.writes.length = 0;
  swatch.props.onClick();
  return ed.writes.slice();
}

for (const ref of ['c1', '4', String(BG_THEMES.length - 1)]) {
  const fromSelect = writesFromSelect(ed, ref);
  ok('the select writes something for ' + ref, fromSelect.length > 0);
  check('choosing ' + ref + ' writes what the select writes', writesFromSwatch(ed, dlg, ref), fromSelect);
}

// The select owns the value, so feeding a swatch's writes back into the form
// has to leave the select showing the gradient that was chosen.
function selectedRef(html) {
  const start = html.indexOf('<select');
  const sel = html.slice(start, html.indexOf('</select>', start));
  const opts = [...sel.matchAll(/<option([^>]*)>/g)].filter(m => /\\sselected/.test(m[1]));
  return opts.map(m => {
    const v = /\\svalue="([^"]*)"/.exec(m[1]);
    return v ? v[1] : '';
  });
}
{
  const after = { ...base };
  for (const [k, v] of writesFromSwatch(ed, dlg, '4')) after[k] = v;
  check('the select then shows the chosen gradient', selectedRef(editor({ kind: 'global' }, after).html), ['4']);
}

// Closing without choosing.
ed.writes.length = 0;
ed.browser.props.onClose();
check('closing without choosing writes nothing', ed.writes, []);

// The dialog is mounted closed and renders nothing until it is opened.
check('closed dialog renders nothing', render(h(GradientBrowser, ed.browser.props)), '');
ok('the dialog is labelled by its own title', /aria-labelledby="([^"]+)"/.test(dlg.html));
ok('it is a modal dialog', /role="dialog" aria-modal="true"/.test(dlg.html));

// ---- animation scope, no override (collapsed editor) ---------------------
console.log('animation scope, no override');
ed = editor({ kind: 'anim', animIdx: 0 }, { ...base });
dlg = openBrowser(ed);
groups = dialogGroups(dlg.html);
check(
  'Global first, then my gradients, then the categories',
  groups.map(g => g.label),
  ['Global', 'My gradients', ...wantBuiltin.map(g => g.label)],
);
check('the global entry carries the global gradient name', groups[0].names, [
  'Global (' + BG_THEMES[0].name + ')',
]);
check('the global entry is the one marked', groups.flatMap(g => g.current), ['']);
check(
  'the same refs the collapsed select offers',
  groups.map(g => g.refs),
  selectGroups(ed.html).map(g => g.refs),
);
check(
  'choosing a built-in writes the animation override',
  writesFromSwatch(ed, dlg, '4'),
  writesFromSelect(ed, '4'),
);

// ---- animation scope, override set (full editor) -------------------------
console.log('animation scope, override set');
ed = editor({ kind: 'anim', animIdx: 0 }, { ...base, bgAnimThemeMap: 'c1' });
dlg = openBrowser(ed);
groups = dialogGroups(dlg.html);
check('the override is the one marked', groups.flatMap(g => g.current), ['c1']);
check(
  'going back to the global writes what the select writes',
  writesFromSwatch(ed, dlg, ''),
  writesFromSelect(ed, ''),
);

// ---- the legacy stand-in is not a choice ---------------------------------
console.log('global scope, pre-library custom gradient in use');
ed = editor(
  { kind: 'global' },
  { ...base, bgAnimGradientRef: '', bgAnimTheme: '18', bgAnimCustomTheme: '101010,f0f0f0' },
);
dlg = openBrowser(ed);
groups = dialogGroups(dlg.html);
ok('the legacy gradient is not offered', !groups.some(g => g.refs.includes('legacy')));
check('nothing is marked current', groups.flatMap(g => g.current), []);
ok('and the dialog says why', /nothing below is marked as current/.test(dlg.html));
ed.writes.length = 0;
ed.browser.props.onClose();
check('closing still writes nothing', ed.writes, []);

console.log(failures === 0 ? 'PASS' : 'FAIL (' + failures + ')');
process.exit(failures === 0 ? 0 : 1);
`;

// The page --emit-page writes: the real editor, its state kept by a wrapper
// that prints every field it is asked to write, so a browser can read back
// what a chosen swatch produced.
const PAGE_ENTRY = `
import { h, render } from 'preact';
import { useState } from 'preact/hooks';
import { GradientEditor } from './GradientEditor.jsx';

const INITIAL = {
  bgAnimGradients: '1|Mine|ff0000,0000ff;2|Odd stops|ff0000@0,00ff00@20,0000ff@250',
  bgAnimThemeMap: '',
  bgAnimGradientRef: '0',
  bgAnimTheme: '0',
  bgAnimCustomTheme: '',
  bgAnimBrightness: '100',
  bgAnimHighlightKnee: '100',
};

function Harness() {
  const [formData, setFormData] = useState(INITIAL);
  const setField = (key, value) => setFormData(f => ({ ...f, [key]: value }));
  return h(
    'div',
    { className: 'p-4' },
    h(GradientEditor, { scope: { kind: 'global' }, formData, setField }),
    // Wrapped, so the harness itself cannot be what makes the page scroll
    // sideways when the dialog is measured. Inline, not utility classes: the
    // stylesheet this page borrows holds only the classes the application
    // itself uses, so a class nothing else uses would not be in it.
    h(
      'pre',
      {
        id: 'fields',
        className: 'mt-4 text-xs',
        style: { whiteSpace: 'pre-wrap', wordBreak: 'break-all' },
      },
      JSON.stringify(formData),
    ),
  );
}

render(h(Harness), document.getElementById('root'));
`;

const emitAt = process.argv.indexOf('--emit-page');
if (emitAt >= 0) {
  const htmlPath = process.argv[emitAt + 1];
  if (!htmlPath) throw new Error('--emit-page needs a file path');
  const assets = join(web, 'dist', 'assets');
  const cssName = readdirSync(assets).find(f => f.endsWith('.css'));
  if (!cssName) throw new Error('no stylesheet in web/dist/assets; run npm run build in web/');
  const jsPath = join(dirname(htmlPath), 'gradient_browse_page.js');
  await esbuild.build({
    stdin: { contents: PAGE_ENTRY, resolveDir: components, loader: 'jsx', sourcefile: 'page.jsx' },
    bundle: true,
    format: 'iife',
    platform: 'browser',
    logLevel: 'warning',
    jsxFactory: 'h',
    jsxFragment: 'Fragment',
    absWorkingDir: web,
    alias: {
      react: 'preact/compat',
      'react-dom': 'preact/compat',
      'react/jsx-runtime': 'preact/compat/jsx-runtime',
    },
    loader: { '.png': 'dataurl', '.svg': 'dataurl' },
    outfile: jsPath,
  });
  // esbuild collects the components' own CSS imports into a sibling file.
  const bundledCss = jsPath.replace(/\.js$/, '.css');
  let componentCss = '';
  try {
    componentCss = readFileSync(bundledCss, 'utf8');
  } catch {
    // Nothing imported a stylesheet; the application sheet is all there is.
  }
  writeFileSync(
    htmlPath,
    `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gradient browse harness</title>
<style>
${readFileSync(join(assets, cssName), 'utf8')}
${componentCss}
</style>
</head>
<body>
<div id="root"></div>
<script src="${basename(jsPath)}"></script>
</body>
</html>
`,
  );
  console.log('wrote ' + htmlPath);
  process.exit(0);
}

const out = mkdtempSync(join(tmpdir(), 'gradbrowse-'));
try {
  await esbuild.build({
    stdin: { contents: ENTRY, resolveDir: components, loader: 'jsx', sourcefile: 'check.jsx' },
    bundle: true,
    format: 'esm',
    platform: 'node',
    logLevel: 'warning',
    jsxFactory: 'h',
    jsxFragment: 'Fragment',
    // The web build aliases these through @preact/preset-vite; the picker
    // libraries import them by their React names.
    // Aliases resolve from the working directory, so it has to be web/.
    absWorkingDir: web,
    alias: {
      react: 'preact/compat',
      'react-dom': 'preact/compat',
      'react/jsx-runtime': 'preact/compat/jsx-runtime',
    },
    loader: { '.css': 'empty', '.png': 'dataurl', '.svg': 'dataurl' },
    outfile: join(out, 'bundle.mjs'),
  });
  await import(pathToFileURL(join(out, 'bundle.mjs')));
} finally {
  rmSync(out, { recursive: true, force: true });
}
