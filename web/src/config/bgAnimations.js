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
  { name: 'Violet Dusk', stops: ['#0a0512', '#2a1050', '#5c2a94', '#9a5ad4', '#d09af0', '#f4e2ff'] },
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
  { name: 'Arctic Night', stops: ['#020206', '#0a1424', '#1a3048', '#34587c', '#6c94bc', '#c4e4f8'] },
];

// bgAnimTheme == BG_THEMES.length selects the custom theme (bgAnimCustomTheme
// holds comma-separated hex stops, 2-8 of them, dark -> bright).
export const BG_THEME_CUSTOM = BG_THEMES.length;
export const BG_THEME_MAX_STOPS = 8;

// Parses a custom-theme string into an array of '#rrggbb' stops (empty array
// if malformed), mirroring the firmware parser's tolerance.
export function parseCustomTheme(str) {
  const parts = String(str ?? '')
    .split(/[\s,]+/)
    .filter(Boolean)
    .map(p => p.replace(/^#/, '').toLowerCase());
  if (parts.length < 2 || parts.length > BG_THEME_MAX_STOPS) return [];
  if (!parts.every(p => /^[0-9a-f]{6}$/.test(p))) return [];
  return parts.map(p => '#' + p);
}

export function serializeCustomTheme(stops) {
  return stops.map(s => s.replace(/^#/, '').toLowerCase()).join(',');
}

// Resolved stop list for any theme id (falls back like the firmware).
export function themeStopsFor(themeId, customStr) {
  if (themeId === BG_THEME_CUSTOM) {
    const custom = parseCustomTheme(customStr);
    if (custom.length >= 2) return custom;
    return BG_THEMES[0].stops;
  }
  return (BG_THEMES[themeId] ?? BG_THEMES[0]).stops;
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
