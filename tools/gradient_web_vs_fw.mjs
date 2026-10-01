// gradient_web_vs_fw.mjs: how far is the web's gradient from the panel's?
//
// The web draws every gradient through gradientCss() (web/src/config/
// bgAnimations.js), which hands the stops to the browser as a CSS
// linear-gradient. The panel builds a 256-entry RGB565 ramp with integer
// arithmetic, which web/src/config/gradientRamp.js reproduces exactly
// (tools/animbench/web/ramp_parity.js proves it against the C++). This script
// puts the two side by side for every built-in gradient and prints the size
// of the difference (gm-ryny).
//
// The CSS side is modelled, not rendered: it parses the string gradientCss()
// returns (so its one-decimal percentages are included) and interpolates
// linearly between stops in non-premultiplied sRGB. That is what CSS does by
// default for a linear-gradient whose colours are hex (CSS Images 3; CSS
// Color 4 keeps sRGB interpolation for legacy colour syntax). Entry i is the
// colour at i/255 of the bar's width, rounded to 8 bits. Browsers may dither,
// which this does not model.
//
// Two comparisons per entry, in 8-bit units per channel:
//   total  CSS colour against the panel's RGB565 entry expanded to 8 bits.
//          This is what a user comparing the web with the machine sees, and
//          most of it is the panel's own RGB565 step: the ramp truncates each
//          channel to 5 or 6 bits, so red and blue lose up to 7.
//   curve  CSS colour against the firmware's 8-bit colour before RGB565
//          (themeRGB), which is the arithmetic difference alone and the part
//          that adopting gradientRamp.js in the web would remove on top of
//          the RGB565 step.
// Plus dE, the CIE76 distance in Lab (about 2.3 is a just-noticeable
// difference) for each, and the widest run of entries whose total dE is over
// 2.3.
//
// usage:  node tools/gradient_web_vs_fw.mjs [--all-tones] [--verbose]

import { BG_THEMES } from "../web/src/config/bgThemes.js";
import {
  gradientCss,
  builtinGradient,
} from "../web/src/config/bgAnimations.js";
import {
  builtinTheme,
  themeRamp,
  themeRGB,
  publishStops,
  toneFromPercent,
  expandRgb565,
  hexToRgb,
} from "../web/src/config/gradientRamp.js";

// Brightness %, highlight rolloff %. 100/100 is what the editor bar and the
// browse swatches draw (no tone); the rest are ramp_parity.js's cases, which
// the editor's "On the panel" bar draws through gradientCss(stops, tone).
const TONES = process.argv.includes("--all-tones")
  ? [
      [100, 100],
      [60, 65],
      [7, 7],
      [33, 71],
      [99, 1],
      [0, 100],
      [100, 0],
      [50, 50],
    ]
  : [
      [100, 100],
      [60, 65],
      [50, 50],
      [33, 71],
    ];
const JND = 2.3;

function parseCss(css) {
  const body = css
    .replace(/^linear-gradient\(to right,\s*/, "")
    .replace(/\)$/, "");
  return body.split(/,\s*/).map((p) => {
    const [hex, pct] = p.trim().split(/\s+/);
    return { rgb: hexToRgb(hex), x: parseFloat(pct) / 100 };
  });
}

function cssAt(stops, x) {
  if (x <= stops[0].x) return stops[0].rgb;
  const last = stops[stops.length - 1];
  if (x >= last.x) return last.rgb;
  let k = 0;
  while (k < stops.length - 2 && x >= stops[k + 1].x) k++;
  const a = stops[k];
  const b = stops[k + 1];
  const f = b.x > a.x ? (x - a.x) / (b.x - a.x) : 0;
  return a.rgb.map((v, c) => Math.round(v + (b.rgb[c] - v) * f));
}

function lab([r, g, b]) {
  const lin = (v) => {
    v /= 255;
    return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  };
  const [R, G, B] = [lin(r), lin(g), lin(b)];
  const X = (0.4124 * R + 0.3576 * G + 0.1805 * B) / 0.95047;
  const Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
  const Z = (0.0193 * R + 0.1192 * G + 0.9505 * B) / 1.08883;
  const f = (t) =>
    t > 216 / 24389 ? Math.cbrt(t) : ((24389 / 27) * t + 16) / 116;
  return [116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z))];
}
const dE = (p, q) => Math.hypot(...lab(p).map((v, i) => v - lab(q)[i]));

function compare(themeId, brightness, knee) {
  const theme = BG_THEMES[themeId];
  const tone =
    brightness === 100 && knee === 100 ? undefined : { brightness, knee };
  const css = parseCss(gradientCss(builtinGradient(themeId).stops, tone));
  const src = builtinTheme(theme.stops.map((s) => s.replace("#", "")));
  const ramp = themeRamp(src, { brightnessPct: brightness, kneePct: knee });
  const published = publishStops(src, toneFromPercent(brightness, knee));
  let total = { d: -1 };
  let curve = { d: -1 };
  let worstDe = { d: -1 };
  let curveDe = 0;
  let run = 0;
  let longest = 0;
  for (let i = 0; i < 256; i++) {
    const web = cssAt(css, i / 255);
    const panel = expandRgb565(ramp[i]);
    const fw8 = themeRGB(published, i);
    const d = Math.max(...web.map((v, c) => Math.abs(v - panel[c])));
    const dc = Math.max(...web.map((v, c) => Math.abs(v - fw8[c])));
    const e = dE(web, panel);
    if (d > total.d) total = { d, i, web, panel };
    if (dc > curve.d) curve = { d: dc, i, web, fw8 };
    curveDe = Math.max(curveDe, dE(web, fw8));
    if (e > worstDe.d) worstDe = { d: e, i };
    run = e > JND ? run + 1 : 0;
    if (run > longest) longest = run;
  }
  return {
    name: theme.name,
    tone: `${brightness}/${knee}`,
    total,
    curve,
    worstDe,
    curveDe,
    longest,
  };
}

const hex = (rgb) =>
  `#${rgb.map((v) => v.toString(16).padStart(2, "0")).join("")}`;
const rows = [];
for (const [b, k] of TONES)
  for (let id = 0; id < BG_THEMES.length; id++) rows.push(compare(id, b, k));

const verbose = process.argv.includes("--verbose");
if (verbose) {
  console.log(
    "gradient                tone     total at   curve at   web      fw 8-bit  dE total  dE curve  run",
  );
  for (const r of rows) {
    console.log(
      `${r.name.padEnd(22)}  ${r.tone.padEnd(7)}  ${String(r.total.d).padStart(4)} ${String(r.total.i).padStart(3)}  ` +
        `${String(r.curve.d).padStart(4)} ${String(r.curve.i).padStart(3)}  ${hex(r.curve.web)}  ${hex(r.curve.fw8)}  ` +
        `${r.worstDe.d.toFixed(2).padStart(8)}  ${r.curveDe.toFixed(2).padStart(8)}  ${String(r.longest).padStart(4)}`,
    );
  }
  console.log("");
}
const worst = (set, f) => set.reduce((a, r) => (f(r) > f(a) ? r : a));
console.log(
  "tone     total max (where)               curve max (where)               dE total  dE curve  run>2.3",
);
for (const t of TONES.map(([b, k]) => `${b}/${k}`)) {
  const set = rows.filter((r) => r.tone === t);
  const wT = worst(set, (r) => r.total.d);
  const wC = worst(set, (r) => r.curve.d);
  const wE = worst(set, (r) => r.worstDe.d);
  const wCe = worst(set, (r) => r.curveDe);
  const wR = worst(set, (r) => r.longest);
  console.log(
    `${t.padEnd(7)}  ${String(wT.total.d).padStart(2)} (${wT.name}, entry ${wT.total.i})`.padEnd(
      42,
    ) +
      `${String(wC.curve.d).padStart(2)} (${wC.name}, entry ${wC.curve.i})`.padEnd(
        32,
      ) +
      `${wE.worstDe.d.toFixed(2).padStart(8)}  ${wCe.curveDe.toFixed(2).padStart(8)}  ${String(wR.longest).padStart(4)} (${wR.name})`,
  );
}
console.log(
  `\n${BG_THEMES.length} built-ins, 256 entries each. --verbose prints every gradient.`,
);
