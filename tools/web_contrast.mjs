#!/usr/bin/env node
// WCAG contrast of the web UI's secondary text, per theme (gm-owlz).
//
// Secondary text in the web UI is not a named token: it is the theme's
// --color-base-content at a Tailwind opacity modifier (text-base-content/60
// and so on). Tailwind v4 emits that as color-mix(in oklab, <colour> N%,
// transparent), which is the base-content colour at alpha N/100, and the
// browser then composites it over the background in gamma-encoded sRGB.
// This script does the same arithmetic from the oklch values in
// web/src/style.css, for every opacity the source uses, over the two
// backgrounds text sits on: base-100 (cards, the page body) and base-200
// (inset panels). Against the real-Chrome canvas readback gm-nov3.20 took
// on base-100 it reads within 0.07 (light /60 3.05 both, light /80 4.97
// both, coffee /60 3.28 here against 3.25, dark /80 7.42 against 7.35).
// The "uses" column counts text-base-content/N in web/src; /100 is the
// unmodified body text, for reference.
//
// Usage: node tools/web_contrast.mjs [--json]

import { readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const css = readFileSync(join(root, "web/src/style.css"), "utf8");

// --- theme parsing -------------------------------------------------------

function parseThemes(text) {
  const themes = [];
  const re = /@plugin\s+"daisyui\/theme"\s*\{([^}]*)\}/g;
  let m;
  while ((m = re.exec(text))) {
    const body = m[1];
    const name = /name:\s*'([^']+)'/.exec(body)?.[1];
    const tok = (k) => {
      const v = new RegExp(`--color-${k}:\\s*oklch\\(([^)]+)\\)`).exec(body);
      if (!v) throw new Error(`theme ${name}: no --color-${k}`);
      const [l, c, h] = v[1].trim().split(/\s+/);
      return [
        parseFloat(l) / (l.endsWith("%") ? 100 : 1),
        parseFloat(c),
        parseFloat(h),
      ];
    };
    themes.push({
      name,
      content: tok("base-content"),
      bg: { "base-100": tok("base-100"), "base-200": tok("base-200") },
    });
  }
  return themes;
}

// --- colour arithmetic ---------------------------------------------------

function oklchToSrgb([L, C, h]) {
  const a = C * Math.cos((h * Math.PI) / 180);
  const b = C * Math.sin((h * Math.PI) / 180);
  const l_ = L + 0.3963377774 * a + 0.2158037573 * b;
  const m_ = L - 0.1055613458 * a - 0.0638541728 * b;
  const s_ = L - 0.0894841775 * a - 1.291485548 * b;
  const l = l_ ** 3,
    m = m_ ** 3,
    s = s_ ** 3;
  const lin = [
    4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s,
  ];
  const enc = (x) => {
    x = Math.min(1, Math.max(0, x));
    return x <= 0.0031308 ? 12.92 * x : 1.055 * x ** (1 / 2.4) - 0.055;
  };
  // Quantise to 8 bits, as the browser's paint does.
  return lin.map((x) => Math.round(enc(x) * 255) / 255);
}

function luminance(rgb) {
  const dec = (x) => (x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4);
  const [r, g, b] = rgb.map(dec);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

function contrast(fg, bg) {
  const a = luminance(fg),
    b = luminance(bg);
  return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
}

function over(fg, bg, alpha) {
  return fg.map(
    (x, i) => Math.round((x * alpha + bg[i] * (1 - alpha)) * 255) / 255,
  );
}

// --- which opacities the source uses --------------------------------------

function walk(dir, out = []) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (/\.(jsx?|tsx?)$/.test(e)) out.push(p);
  }
  return out;
}

const uses = new Map();
for (const f of walk(join(root, "web/src"))) {
  for (const m of readFileSync(f, "utf8").matchAll(
    /text-base-content\/(\d+)/g,
  )) {
    uses.set(+m[1], (uses.get(+m[1]) || 0) + 1);
  }
}
const levels = [...uses.keys(), 100].sort((a, b) => a - b);

// --- table ---------------------------------------------------------------

const themes = parseThemes(css);
const rows = [];
for (const lv of levels) {
  const row = { opacity: lv, uses: uses.get(lv) || 0 };
  for (const t of themes) {
    const fg = oklchToSrgb(t.content);
    for (const [bgName, bgTok] of Object.entries(t.bg)) {
      const bg = oklchToSrgb(bgTok);
      row[`${t.name}/${bgName}`] = contrast(over(fg, bg, lv / 100), bg);
    }
  }
  rows.push(row);
}

if (process.argv.includes("--json")) {
  console.log(JSON.stringify(rows, null, 2));
} else {
  const cols = themes.flatMap((t) =>
    Object.keys(t.bg).map((b) => `${t.name}/${b}`),
  );
  const w = 16;
  console.log(
    ["opacity", "uses", ...cols]
      .map((c, i) => c.padStart(i < 2 ? 7 : w))
      .join(" "),
  );
  for (const r of rows) {
    const cells = cols.map((c) => {
      const v = r[c];
      return `${v.toFixed(2)}${v < 4.5 ? "*" : " "}`.padStart(w);
    });
    console.log(
      [`/${r.opacity}`.padStart(7), String(r.uses).padStart(7), ...cells].join(
        " ",
      ),
    );
  }
  console.log("\n* below 4.5:1, the WCAG AA floor for small text");
}
