// Background animation registry — MUST mirror the firmware registry in
// src/display/ui/default/bganim/BgAnimRegistry.cpp (same order: the array
// index is the persisted animation id; append only, never reorder).
//
// Params are up to 4 sliders, each 0-100, persisted per animation in the
// `bgAnimParams` setting as "p0,p1,p2,p3;p0,p1,p2,p3;..." indexed by id.
// By convention p0 is always Speed.

export const BG_ANIMATIONS = [
  {
    id: 'plasma',
    name: 'Plasma',
    description: 'The classic palette-cycling plasma.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Scale', def: 50 },
      { key: 'brightness', label: 'Brightness', def: 70 },
    ],
  },
  {
    id: 'lava',
    name: 'Lava',
    description: 'Slow lava-lamp metaballs merging and drifting.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Blob size', def: 50 },
      { key: 'glow', label: 'Glow', def: 60 },
    ],
  },
  {
    id: 'silk',
    name: 'Silk',
    description: 'Light sweeping across flowing fabric.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Fringe density', def: 45 },
      { key: 'glow', label: 'Sheen', def: 55 },
    ],
  },
  {
    id: 'starfield',
    name: 'Starfield',
    description: 'Deep sky with twinkling stars and rare shooting stars.',
    params: [
      { key: 'speed', label: 'Drift speed', def: 50 },
      { key: 'density', label: 'Stars', def: 45 },
      { key: 'twinkle', label: 'Twinkle', def: 50 },
      { key: 'shooting', label: 'Shooting stars', def: 30 },
    ],
  },
  {
    id: 'aurora',
    name: 'Aurora',
    description: 'Northern-light curtains waving over a dark sky.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'intensity', label: 'Intensity', def: 55 },
      { key: 'waviness', label: 'Waviness', def: 50 },
    ],
  },
  {
    id: 'ripples',
    name: 'Ripples',
    description: 'Rain drops on still, dark water.',
    params: [
      { key: 'speed', label: 'Ring speed', def: 50 },
      { key: 'rate', label: 'Drop rate', def: 40 },
      { key: 'decay', label: 'Fade', def: 50 },
      { key: 'glow', label: 'Glow', def: 50 },
    ],
  },
  {
    id: 'caustics',
    name: 'Caustics',
    description: 'Underwater light webs drifting on the deep.',
    params: [
      { key: 'speed', label: 'Drift speed', def: 50 },
      { key: 'scale', label: 'Cell scale', def: 45 },
      { key: 'contrast', label: 'Contrast', def: 55 },
    ],
  },
  {
    id: 'mandala',
    name: 'Mandala',
    description: 'Breathing kaleidoscopic symmetry, living jewelry.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'symmetry', label: 'Symmetry', def: 50 },
      { key: 'complexity', label: 'Complexity', def: 45 },
    ],
  },
  {
    id: 'orbits',
    name: 'Orbits',
    description: 'Glowing bodies on elliptical orbits, watch-face clockwork.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'orbitCount', label: 'Orbits', def: 55 },
      { key: 'eccentricity', label: 'Eccentricity', def: 55 },
      { key: 'trail', label: 'Trail', def: 50 },
    ],
  },
  {
    id: 'fireflies',
    name: 'Fireflies',
    description: 'Soft glowing motes drifting through dusk.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'count', label: 'Count', def: 60 },
      { key: 'glow', label: 'Glow', def: 55 },
      { key: 'shimmer', label: 'Shimmer', def: 40 },
    ],
  },
  {
    id: 'steam',
    name: 'Steam',
    description: 'Wisps of steam rising and dissolving.',
    params: [
      { key: 'speed', label: 'Rise speed', def: 50 },
      { key: 'count', label: 'Wisps', def: 55 },
      { key: 'swirl', label: 'Swirl', def: 45 },
      { key: 'density', label: 'Density', def: 50 },
    ],
  },
  {
    id: 'ember',
    name: 'Ember',
    description: 'A warm glow breathing from below, like coals in a hearth.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'glow', label: 'Glow size', def: 45 },
      { key: 'flicker', label: 'Flicker', def: 20 },
      { key: 'pulse', label: 'Pulse', def: 50 },
    ],
  },
  {
    id: 'nebula',
    name: 'Nebula',
    description: 'Deep-space clouds drifting in slow multi-layer turbulence.',
    params: [
      { key: 'speed', label: 'Drift speed', def: 50 },
      { key: 'density', label: 'Density', def: 50 },
      { key: 'turbulence', label: 'Turbulence', def: 40 },
    ],
  },
];

// Shared color themes — MUST mirror BgAnimThemes.cpp (append only, the theme
// index is persisted in the bgAnimTheme setting). Stops run dark -> bright.
export const BG_THEMES = [
  { name: 'Espresso', stops: ['#080402', '#2a1206', '#6b3413', '#b8703a', '#e8b268', '#f8e6c8'] },
  { name: 'Ocean', stops: ['#02060c', '#06284a', '#0a5276', '#2596be', '#66d3e8', '#d8f6ff'] },
  {
    name: 'Violet Dusk',
    stops: ['#0a0512', '#2a1050', '#5c2a94', '#9a5ad4', '#d09af0', '#f4e2ff'],
  },
  { name: 'Forest', stops: ['#020803', '#0c2c12', '#1e5c28', '#46963c', '#8cd464', '#e6ffc8'] },
  { name: 'Sunset', stops: ['#0c0410', '#4a1030', '#952038', '#d4542c', '#f89c3c', '#ffe8a0'] },
  { name: 'Fire', stops: ['#0a0200', '#401004', '#8c2808', '#d85c10', '#f8a428', '#ffe8b0'] },
  { name: 'Ice', stops: ['#020408', '#10203c', '#2c4a74', '#5486b4', '#9cc8e4', '#eafaff'] },
  { name: 'Mono', stops: ['#000000', '#202020', '#484848', '#808080', '#c0c0c0', '#ffffff'] },
  { name: 'Rose', stops: ['#0e0407', '#3c1020', '#7a2440', '#c04868', '#ee8ca4', '#ffdce6'] },
  { name: 'Gold', stops: ['#060402', '#2e2008', '#6e5014', '#b48c24', '#e8c453', '#fff0b8'] },
  { name: 'Aurora', stops: ['#010806', '#063020', '#0c6444', '#14a878', '#48e0b0', '#c8ffec'] },
  { name: 'Cyber', stops: ['#050008', '#240448', '#501090', '#9018d8', '#e030f8', '#ff9cf0'] },
  { name: 'Ember Coal', stops: ['#0a0604', '#2b0a06', '#6b1a08', '#b8420f', '#e2751f', '#f4a94a'] },
  { name: 'Deep Space', stops: ['#05050f', '#150a28', '#341840', '#6b2f5e', '#b3477d', '#e6b3d6'] },
  { name: 'Teal Reef', stops: ['#050a0f', '#0a1c28', '#123a44', '#1f6b6e', '#3fb3a8', '#bdeee0'] },
  { name: 'Sakura', stops: ['#0c060a', '#341828', '#6e3050', '#b45c80', '#e896b0', '#ffe0ec'] },
  { name: 'Lime', stops: ['#040802', '#16300a', '#326016', '#5ea024', '#9ee44c', '#eaffc0'] },
  {
    name: 'Arctic Night',
    stops: ['#020206', '#0a1424', '#1a3048', '#34587c', '#6c94bc', '#c4e4f8'],
  },
];

// bgAnimTheme == BG_THEMES.length selected the pre-library custom theme
// (bgAnimCustomTheme); the firmware moves that into the gradient library on
// boot, so the editor only ever deals in built-ins and library entries.
export const BG_THEME_CUSTOM = BG_THEMES.length;
export const BG_THEME_MAX_STOPS = 16;
export const BG_GRADIENT_LIB_MAX = 12;
export const BG_GRADIENT_NAME_MAX = 24;

// ---- gradients ---------------------------------------------------------
// A gradient is { stops: [{ color: '#rrggbb', pos: 0..255 }] } with stops in
// ascending position. Its wire form mirrors BgAnimThemes.cpp:
// "rrggbb,rrggbb,..." when the stops are spaced evenly (the firmware then
// uses its original palette arithmetic), else "rrggbb@pos,...". Like a CSS
// gradient, the colour holds flat before the first stop and after the last.

export function uniformPositions(n) {
  return Array.from({ length: n }, (_, i) => Math.floor((i * 255) / (n - 1)));
}

export function isUniformStops(stops) {
  const uni = uniformPositions(stops.length);
  return stops.every((s, i) => s.pos === uni[i]);
}

// Built-in theme as an editable gradient (evenly spaced stops).
export function builtinGradient(themeId) {
  const theme = BG_THEMES[themeId] ?? BG_THEMES[0];
  const pos = uniformPositions(theme.stops.length);
  return { stops: theme.stops.map((color, i) => ({ color, pos: pos[i] })) };
}

// Parses a gradient string into stops (null if malformed), with the same
// tolerance and repairs as the firmware parser.
export function parseGradient(str) {
  const parts = String(str ?? '')
    .split(/[\s,]+/)
    .filter(Boolean);
  if (parts.length < 2 || parts.length > BG_THEME_MAX_STOPS) return null;
  const stops = [];
  for (const part of parts) {
    const m = /^#?([0-9a-fA-F]{6})(?:@(\d{1,3}))?$/.exec(part);
    if (!m) return null;
    const pos = m[2] === undefined ? null : parseInt(m[2], 10);
    if (pos !== null && pos > 255) return null;
    stops.push({ color: `#${m[1].toLowerCase()}`, pos });
  }
  const uni = uniformPositions(stops.length);
  stops.forEach((s, i) => {
    if (s.pos === null) s.pos = uni[i];
  });
  for (let i = 1; i < stops.length; i++) {
    if (stops[i].pos < stops[i - 1].pos) stops[i].pos = stops[i - 1].pos;
  }
  return { stops };
}

export function serializeGradient(gradient) {
  const stops = gradient.stops;
  const uniform = isUniformStops(stops);
  return stops
    .map(s => s.color.replace(/^#/, '').toLowerCase() + (uniform ? '' : `@${s.pos}`))
    .join(',');
}

export function hexToRgb(hex) {
  const v = parseInt(String(hex).replace(/^#/, ''), 16) || 0;
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

export function rgbToHex(rgb) {
  return `#${rgb
    .map(v =>
      Math.min(255, Math.max(0, Math.round(v)))
        .toString(16)
        .padStart(2, '0'),
    )
    .join('')}`;
}

// Colour at t (0..255) along the ramp, as [r, g, b], using the firmware's
// integer blend so the on-page preview matches the panel.
export function sampleGradient(stops, t) {
  t = Math.min(255, Math.max(0, Math.round(t)));
  if (t <= stops[0].pos) return hexToRgb(stops[0].color);
  if (t >= stops[stops.length - 1].pos) return hexToRgb(stops[stops.length - 1].color);
  let seg = 0;
  while (seg < stops.length - 2 && t >= stops[seg + 1].pos) seg++;
  const a = hexToRgb(stops[seg].color);
  const b = hexToRgb(stops[seg + 1].color);
  const width = stops[seg + 1].pos - stops[seg].pos;
  const f = width > 0 ? Math.floor(((t - stops[seg].pos) * 256) / width) : 0;
  return a.map((v, c) => v + Math.floor(((b[c] - v) * f) / 256));
}

// The panel's tone controls (BgAnimCommon.cpp publishStops): highlight knee
// as a shoulder first, then brightness. Applied to stop colours for previews.
export function toneColor(hex, brightnessPct, kneePct) {
  const knee = Math.round((kneePct * 255) / 100);
  const bright = Math.round((brightnessPct * 256) / 100);
  return rgbToHex(
    hexToRgb(hex).map(v => {
      if (v > knee) v = knee + Math.floor((v - knee) / 4);
      return Math.floor((v * bright) / 256);
    }),
  );
}

// CSS gradient for a stop list (optionally toned), for preview bars.
export function gradientCss(stops, tone) {
  const parts = stops.map(s => {
    const color = tone ? toneColor(s.color, tone.brightness, tone.knee) : s.color;
    return `${color} ${((s.pos * 100) / 255).toFixed(1)}%`;
  });
  return `linear-gradient(to right, ${parts.join(', ')})`;
}

// Plasma wraps the ramp into a wheel (BgAnimCommon.cpp buildThemeWheel): the
// ramp over the first 256 - 256/n entries, then a blend back to the start.
export function wheelCss(stops, tone) {
  const n = stops.length;
  const rampEnd = ((256 - Math.floor(256 / n)) * 100) / 256;
  const color = s => (tone ? toneColor(s.color, tone.brightness, tone.knee) : s.color);
  const parts = stops.map(s => `${color(s)} ${((s.pos * rampEnd) / 255).toFixed(1)}%`);
  parts.push(`${color(stops[0])} 100%`);
  return `linear-gradient(to right, ${parts.join(', ')})`;
}

// ---- library "id|name|gradient;..." ------------------------------------

export function parseGradientLibrary(str) {
  const out = [];
  for (const entry of String(str ?? '').split(';')) {
    if (!entry) continue;
    const [idStr, name, gradientStr] = entry.split('|');
    const id = parseInt(idStr, 10);
    const gradient = parseGradient(gradientStr);
    if (!(id > 0) || !name || !gradient) continue;
    out.push({ id, name, stops: gradient.stops });
  }
  return out;
}

export function serializeGradientLibrary(library) {
  return library
    .map(g => `${g.id}|${sanitizeGradientName(g.name)}|${serializeGradient(g)}`)
    .join(';');
}

// Names travel inside the packed string, so its separators cannot appear.
export function sanitizeGradientName(name) {
  const clean = String(name ?? '')
    .replace(/[|;,]/g, ' ')
    .trim()
    .slice(0, BG_GRADIENT_NAME_MAX);
  return clean || 'Gradient';
}

export function nextGradientId(library) {
  return library.reduce((m, g) => Math.max(m, g.id), 0) + 1;
}

// ---- per-animation map "ref;ref;..." -----------------------------------
// ref: '' (use the global bgAnimTheme), a built-in index, or 'c<id>'.

export function parseThemeMap(str) {
  const parts = String(str ?? '').split(';');
  return BG_ANIMATIONS.map((_, i) => {
    const ref = parts[i] ?? '';
    return /^(\d+|c\d+)$/.test(ref) ? ref : '';
  });
}

export function serializeThemeMap(refs) {
  // Trailing empties are dropped; the firmware treats a short map the same.
  const out = refs.slice();
  while (out.length && !out[out.length - 1]) out.pop();
  return out.join(';');
}

// The ref an animation effectively draws with, after the firmware's
// fallbacks: its map entry when it resolves, else the global theme.
export function effectiveRef(refs, animIdx, library, globalThemeId) {
  const ref = refs[animIdx] ?? '';
  if (ref.startsWith('c')) {
    if (library.some(g => g.id === parseInt(ref.slice(1), 10))) return ref;
  } else if (ref !== '') {
    const idx = parseInt(ref, 10);
    if (idx >= 0 && idx < BG_THEMES.length) return ref;
  }
  const g = parseInt(globalThemeId, 10);
  return String(g >= 0 && g < BG_THEMES.length ? g : 0);
}

export function gradientForRef(ref, library) {
  if (ref.startsWith('c')) {
    const entry = library.find(g => g.id === parseInt(ref.slice(1), 10));
    if (entry) return { name: entry.name, stops: entry.stops, editable: true, id: entry.id };
  }
  const idx = parseInt(ref, 10);
  const themeId = idx >= 0 && idx < BG_THEMES.length ? idx : 0;
  return { name: BG_THEMES[themeId].name, ...builtinGradient(themeId), editable: false };
}

// Parses the packed setting into a per-animation array of 4-value arrays,
// filling missing/short groups with each animation's defaults.
export function parseBgAnimParams(packed) {
  const groups = String(packed ?? '').split(';');
  return BG_ANIMATIONS.map((anim, i) => {
    const defs = anim.params.map(p => p.def ?? 0);
    while (defs.length < 4) defs.push(0);
    const parts = (groups[i] ?? '').split(',');
    return defs.map((def, j) => {
      const v = parseInt(parts[j], 10);
      return Number.isFinite(v) ? Math.min(100, Math.max(0, v)) : def;
    });
  });
}

// Returns the packed string with one param of one animation replaced.
export function setBgAnimParam(packed, animIdx, paramIdx, value) {
  const all = parseBgAnimParams(packed);
  if (all[animIdx]) {
    all[animIdx][paramIdx] = Math.min(100, Math.max(0, Math.round(Number(value) || 0)));
  }
  return all.map(g => g.join(',')).join(';');
}
