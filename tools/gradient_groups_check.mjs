// Checks that GradientEditor's gradient picker groups the built-ins by their
// declared category, in the declared order, in both of its select branches,
// and that grouping changed no option value.
//
// It bundles the real component with esbuild (from web/node_modules) and
// renders it with preact-render-to-string, then reads the HTML. It is not a
// browser: no layout, no click, no save. What it proves is the markup both
// branches emit, which is where the grouping lives. Whether a chosen ref
// reaches the device still needs the Playwright rig in C:\work\camshots.
//
// Usage: node tools/gradient_groups_check.mjs
//
// Exits 0 on PASS, 1 on any failed check.

import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const web = join(here, '..', 'web');
const components = join(web, 'src', 'components');

const esbuild = await import(pathToFileURL(join(web, 'node_modules', 'esbuild', 'lib', 'main.js')));

// The check itself, bundled with the component so it can import it directly.
const ENTRY = `
import { h } from 'preact';
import render from 'preact-render-to-string';
import { GradientEditor } from './GradientEditor.jsx';
import { BG_THEMES, BG_THEME_CATEGORIES } from '../config/bgAnimations.js';

// Two saved gradients, so "My gradients" has something to hold.
const LIBRARY = '1|Mine|ff0000,0000ff;2|Also mine|00ff00,ffffff';

const base = {
  bgAnimGradients: '',
  bgAnimThemeMap: '',
  bgAnimGradientRef: '0',
  bgAnimTheme: '0',
  bgAnimBrightness: '100',
  bgAnimHighlightKnee: '100',
};

function html(scope, formData) {
  return render(h(GradientEditor, { scope, formData, setField: () => {} }));
}

// The picker is the first select in the markup in both branches.
function picker(markup) {
  const start = markup.indexOf('<select');
  const end = markup.indexOf('</select>', start);
  if (start < 0 || end < 0) throw new Error('no select rendered');
  return markup.slice(start, end);
}

// [{label, values, names}, ...] in render order. Options before the first
// optgroup (the "Global (...)" entry) come back under the label null.
function groups(sel) {
  const out = [];
  let cur = { label: null, values: [], names: [] };
  const token = /<optgroup[^>]*label="([^"]*)"|<option([^>]*)>([^<]*)</g;
  let m;
  while ((m = token.exec(sel)) !== null) {
    if (m[1] !== undefined) {
      if (cur.values.length > 0 || cur.label !== null) out.push(cur);
      cur = { label: m[1], values: [], names: [] };
    } else {
      // preact-render-to-string writes an empty value as a bare attribute
      // (<option selected value>), which is how the Global entry comes out.
      const withValue = /\\svalue="([^"]*)"/.exec(m[2]);
      cur.values.push(withValue ? withValue[1] : '');
      cur.names.push(m[3]);
    }
  }
  out.push(cur);
  return out;
}

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

// What the built-ins should look like grouped: every declared category that
// has gradients, in declared order, each holding its gradients in table order
// with the table index as the option value.
const wantBuiltin = BG_THEME_CATEGORIES.map(c => ({
  label: c,
  values: BG_THEMES.map((t, i) => [t, i])
    .filter(([t]) => t.category === c)
    .map(([, i]) => String(i)),
  names: BG_THEMES.filter(t => t.category === c).map(t => t.name),
})).filter(g => g.values.length > 0);

const mine = { label: 'My gradients', values: ['c1', 'c2'], names: ['Mine', 'Also mine'] };
const globalEntry = {
  label: null,
  values: [''],
  names: ['Global (' + BG_THEMES[0].name + ')'],
};

console.log('global scope, empty library (full editor branch)');
let g = groups(picker(html({ kind: 'global' }, { ...base })));
check('no My gradients group', g.map(x => x.label), wantBuiltin.map(x => x.label));
check('declared groups, names and option values', g, wantBuiltin);

console.log('global scope, two saved gradients (full editor branch)');
g = groups(picker(html({ kind: 'global' }, { ...base, bgAnimGradients: LIBRARY })));
check('My gradients first, then declared order', g, [mine, ...wantBuiltin]);

console.log('animation scope, no override (collapsed branch)');
g = groups(picker(html({ kind: 'anim', animIdx: 0 }, { ...base, bgAnimGradients: LIBRARY })));
check('Global entry, My gradients, declared order', g, [globalEntry, mine, ...wantBuiltin]);

console.log('animation scope, override set (full editor branch)');
g = groups(
  picker(
    html({ kind: 'anim', animIdx: 0 }, { ...base, bgAnimGradients: LIBRARY, bgAnimThemeMap: '2' }),
  ),
);
check('Global entry, My gradients, declared order', g, [globalEntry, mine, ...wantBuiltin]);

console.log('animation scope, empty library (collapsed branch)');
g = groups(picker(html({ kind: 'anim', animIdx: 0 }, { ...base })));
check('no My gradients group', g.map(x => x.label), [null, ...wantBuiltin.map(x => x.label)]);

// Every built-in is still reachable, at the value it had before grouping.
const flat = groups(picker(html({ kind: 'global' }, { ...base })))
  .flatMap(x => x.values)
  .sort((a, b) => Number(a) - Number(b));
check('every built-in reachable at its table index', flat, BG_THEMES.map((_, i) => String(i)));

console.log(failures === 0 ? 'PASS' : 'FAIL (' + failures + ')');
process.exit(failures === 0 ? 0 : 1);
`;

const out = mkdtempSync(join(tmpdir(), 'gradgroups-'));
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
