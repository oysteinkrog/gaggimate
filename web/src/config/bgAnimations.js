// Background animation registry — MUST mirror the firmware registry in
// src/display/ui/default/bganim/BgAnimRegistry.cpp (same order: the array
// index is the persisted animation id; append only, never reorder).
//
// Params are up to 4 sliders, each 0-100, persisted per animation in the
// `bgAnimParams` setting as "p0,p1,p2,p3;p0,p1,p2,p3;..." indexed by id.

export const BG_ANIMATIONS = [
  {
    id: 'plasma',
    name: 'Plasma',
    description: 'The classic espresso-toned plasma.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Scale', def: 50 },
      { key: 'palette', label: 'Palette', def: 0, options: ['Espresso', 'Ocean', 'Violet', 'Mono'] },
      { key: 'brightness', label: 'Brightness', def: 70 },
    ],
  },
  {
    id: 'lava',
    name: 'Lava',
    description: 'Slow lava-lamp metaballs merging and drifting.',
    params: [
      { key: 'speed', label: 'Speed', def: 35 },
      { key: 'scale', label: 'Blob size', def: 50 },
      { key: 'hue', label: 'Palette', def: 0 },
      { key: 'glow', label: 'Glow', def: 60 },
    ],
  },
  {
    id: 'silk',
    name: 'Silk',
    description: 'Light sweeping across flowing fabric.',
    params: [
      { key: 'speed', label: 'Speed', def: 30 },
      { key: 'scale', label: 'Fringe density', def: 45 },
      { key: 'hue', label: 'Palette', def: 20 },
      { key: 'glow', label: 'Sheen', def: 55 },
    ],
  },
  {
    id: 'starfield',
    name: 'Starfield',
    description: 'Deep sky with twinkling stars and rare shooting stars.',
    params: [
      { key: 'density', label: 'Stars', def: 45 },
      { key: 'twinkle', label: 'Twinkle', def: 50 },
      { key: 'shooting', label: 'Shooting stars', def: 30 },
      { key: 'drift', label: 'Drift', def: 20 },
    ],
  },
  {
    id: 'aurora',
    name: 'Aurora',
    description: 'Northern-light curtains waving over a dark sky.',
    params: [
      { key: 'intensity', label: 'Intensity', def: 55 },
      { key: 'speed', label: 'Speed', def: 40 },
      { key: 'hue', label: 'Hue shift', def: 35 },
      { key: 'waviness', label: 'Waviness', def: 50 },
    ],
  },
  {
    id: 'ripples',
    name: 'Ripples',
    description: 'Rain drops on still, dark water.',
    params: [
      { key: 'rate', label: 'Drop rate', def: 30 },
      { key: 'speed', label: 'Ring speed', def: 40 },
      { key: 'decay', label: 'Fade', def: 50 },
      { key: 'glow', label: 'Glow', def: 50 },
    ],
  },
  {
    id: 'caustics',
    name: 'Caustics',
    description: 'Underwater light webs drifting on deep blue.',
    params: [
      { key: 'speed', label: 'Drift speed', def: 35 },
      { key: 'scale', label: 'Cell scale', def: 45 },
      { key: 'contrast', label: 'Contrast', def: 55 },
      { key: 'hue', label: 'Hue', def: 50 },
    ],
  },
  {
    id: 'mandala',
    name: 'Mandala',
    description: 'Breathing kaleidoscopic symmetry, living jewelry.',
    params: [
      { key: 'symmetry', label: 'Symmetry', def: 50 },
      { key: 'complexity', label: 'Complexity', def: 45 },
      { key: 'speed', label: 'Speed', def: 35 },
      { key: 'warmth', label: 'Warmth', def: 50 },
    ],
  },
  {
    id: 'orbits',
    name: 'Orbits',
    description: 'Glowing bodies on elliptical orbits, watch-face clockwork.',
    params: [
      { key: 'orbitCount', label: 'Orbits', def: 55 },
      { key: 'speed', label: 'Speed', def: 40 },
      { key: 'eccentricity', label: 'Eccentricity', def: 55 },
      { key: 'trail', label: 'Trail', def: 50 },
    ],
  },
  {
    id: 'fireflies',
    name: 'Fireflies',
    description: 'Soft glowing motes drifting through dusk.',
    params: [
      { key: 'count', label: 'Count', def: 60 },
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'glow', label: 'Glow', def: 55 },
      { key: 'shimmer', label: 'Shimmer', def: 40 },
    ],
  },
  {
    id: 'steam',
    name: 'Steam',
    description: 'Wisps of steam rising and dissolving.',
    params: [
      { key: 'count', label: 'Wisps', def: 55 },
      { key: 'riseSpeed', label: 'Rise speed', def: 50 },
      { key: 'swirl', label: 'Swirl', def: 45 },
      { key: 'density', label: 'Density', def: 50 },
    ],
  },
];

// Parses the packed setting into a per-animation array of 4-value arrays,
// filling missing/short groups with each animation's defaults.
export function parseBgAnimParams(packed) {
  const groups = String(packed ?? '').split(';');
  return BG_ANIMATIONS.map((anim, i) => {
    const defs = anim.params.map(p => p.def ?? 0);
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
