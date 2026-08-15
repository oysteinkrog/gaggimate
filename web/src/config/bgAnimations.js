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
