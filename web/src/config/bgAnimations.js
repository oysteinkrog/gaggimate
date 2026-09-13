// Background animation registry — MUST mirror the firmware registry in
// src/display/ui/default/bganim/BgAnimRegistry.cpp (same order: the array
// index is the persisted animation id; append only, never reorder).
//
// Params are up to 8 sliders (BG_ANIM_PARAMS in BgAnim.h), each 0-100,
// persisted per animation in the `bgAnimParams` setting as
// "p0,p1,...;p0,p1,...;..." indexed by id; a shorter stored group keeps the
// defaults for the slots it does not name.
// By convention p0 is always Speed.

// The built-in gradients and their categories come from data/gradients.json
// through scripts/gen_gradients.py, which writes the firmware's table from the
// same source. Imported here and re-exported below so every importer of
// BG_THEMES is unaffected.
import { BG_THEMES, BG_THEME_CATEGORIES } from './bgThemes.js';

export const BG_ANIMATIONS = [
  {
    id: 'plasma',
    name: 'Plasma',
    description: 'The classic palette-cycling plasma.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Scale', def: 50 },
      { key: 'brightness', label: 'Brightness', def: 70 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'cycle', label: 'Colour cycle', def: 50 },
      { key: 'stretch', label: 'Stretch', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
      { key: 'shift', label: 'Palette shift', def: 50 },
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
      { key: 'count', label: 'Blob count', def: 67 },
      { key: 'core', label: 'Hot core', def: 50 },
      { key: 'falloff', label: 'Falloff', def: 40 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'wander', label: 'Wander', def: 50 },
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
      { key: 'spread', label: 'Wave spread', def: 50 },
      { key: 'twist', label: 'Twist', def: 50 },
      { key: 'wobble', label: 'Breathe', def: 50 },
      { key: 'vignette', label: 'Edge fade', def: 80 },
      { key: 'grain', label: 'Grain', def: 50 },
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
      { key: 'glow', label: 'Star glow', def: 50 },
      { key: 'skyglow', label: 'Sky glow', def: 50 },
      { key: 'falloff', label: 'Sky falloff', def: 50 },
      { key: 'tint', label: 'Star tint', def: 50 },
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
      { key: 'height', label: 'Height', def: 50 },
      { key: 'spread', label: 'Spread', def: 50 },
      { key: 'glow', label: 'Glow', def: 50 },
      { key: 'drift', label: 'Drift', def: 50 },
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
      { key: 'spread', label: 'Drop spread', def: 50 },
      { key: 'width', label: 'Ring width', def: 50 },
      { key: 'tone', label: 'Water tone', def: 50 },
      { key: 'trough', label: 'Trough dip', def: 50 },
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
      { key: 'glow', label: 'Glow', def: 50 },
      { key: 'spot', label: 'Spot size', def: 50 },
      { key: 'spread', label: 'Wave spread', def: 50 },
      { key: 'tilt', label: 'Pattern tilt', def: 50 },
      { key: 'turn', label: 'Turn rate', def: 50 },
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
      { key: 'drift', label: 'Ring drift', def: 50 },
      { key: 'vignette', label: 'Vignette', def: 50 },
      { key: 'breathe', label: 'Breathe', def: 50 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'rings', label: 'Ring pitch', def: 50 },
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
      { key: 'size', label: 'Orbit size', def: 50 },
      { key: 'path', label: 'Path glow', def: 50 },
      { key: 'glow', label: 'Body glow', def: 50 },
      { key: 'tilt', label: 'Tilt spread', def: 50 },
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
      { key: 'spread', label: 'Spread', def: 50 },
      { key: 'drift', label: 'Drift', def: 50 },
      { key: 'pulse', label: 'Pulse depth', def: 50 },
      { key: 'halo', label: 'Halo', def: 50 },
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
      { key: 'size', label: 'Puff size', def: 50 },
      { key: 'spread', label: 'Base spread', def: 50 },
      { key: 'tint', label: 'Steam tint', def: 50 },
      { key: 'taper', label: 'Top fade', def: 50 },
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
      { key: 'height', label: 'Height', def: 50 },
      { key: 'falloff', label: 'Falloff', def: 50 },
      { key: 'core', label: 'Core heat', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
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
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'drift', label: 'Drift angle', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
      { key: 'detail', label: 'Fine detail', def: 50 },
      { key: 'lspeed', label: 'Layer speed', def: 50 },
    ],
  },
  {
    id: 'silk2',
    name: 'Silk 2',
    description: 'Light sweeping across flowing fabric, redesigned for speed.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Fringe density', def: 45 },
      { key: 'glow', label: 'Contrast', def: 55 },
      { key: 'mix', label: 'Wave balance', def: 50 },
      { key: 'cross', label: 'Cross detail', def: 50 },
      { key: 'sheenw', label: 'Sheen width', def: 55 },
      { key: 'rim', label: 'Rim spread', def: 50 },
      { key: 'drift', label: 'Drift', def: 50 },
    ],
  },
  {
    id: 'brushed',
    name: 'Brushed',
    description: 'A dark brushed surface with a wide sheen sliding across it.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'grain', label: 'Grain', def: 35 },
      { key: 'reflection', label: 'Reflection', def: 45 },
      { key: 'contrast', label: 'Contrast', def: 30 },
    ],
  },
  {
    id: 'horizon',
    name: 'Horizon',
    description: 'A glowing horizon line between ground and sky, drifting slowly.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'height', label: 'Height', def: 45 },
      { key: 'curvature', label: 'Curvature', def: 35 },
      { key: 'softness', label: 'Softness', def: 60 },
    ],
  },
  {
    id: 'oculus',
    name: 'Oculus',
    description: 'A dark pupil inside a softly lit ring that breathes.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'diameter', label: 'Diameter', def: 65 },
      { key: 'breath', label: 'Breath', def: 20 },
      { key: 'edge', label: 'Edge softness', def: 55 },
    ],
  },
  {
    id: 'chevrons',
    name: 'Chevrons',
    description: 'Wide soft folds travelling down the face.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'spacing', label: 'Spacing', def: 65 },
      { key: 'angle', label: 'Angle', def: 50 },
      { key: 'contrast', label: 'Contrast', def: 35 },
    ],
  },
  {
    id: 'mosaic',
    name: 'Mosaic',
    description: 'Large soft tiles, each fading on its own clock.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'size', label: 'Tile size', def: 45 },
      { key: 'contrast', label: 'Contrast', def: 30 },
      { key: 'variation', label: 'Variation', def: 55 },
    ],
  },
  {
    id: 'saddle',
    name: 'Saddle',
    description: 'Tonal contours flowing along a drifting saddle surface.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'curvature', label: 'Curvature', def: 35 },
      { key: 'drift', label: 'Drift', def: 25 },
      { key: 'contrast', label: 'Contrast', def: 30 },
    ],
  },
  {
    id: 'refraction',
    name: 'Refraction',
    description: 'Broad tonal channels bending as if seen through uneven glass.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'bend', label: 'Bend', def: 35 },
      { key: 'width', label: 'Channel width', def: 65 },
      { key: 'contrast', label: 'Contrast', def: 30 },
    ],
  },
  {
    id: 'sundial',
    name: 'Sundial',
    description: 'A soft wedge of light turning slowly around the centre.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'width', label: 'Wedge width', def: 40 },
      { key: 'contrast', label: 'Contrast', def: 25 },
      { key: 'shading', label: 'Surface shading', def: 30 },
    ],
  },
  {
    id: 'crescent',
    name: 'Crescent',
    description: 'A pale crescent turning, swelling and thinning.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'size', label: 'Size', def: 70 },
      { key: 'phase', label: 'Phase range', def: 40 },
      { key: 'contrast', label: 'Contrast', def: 40 },
    ],
  },
  {
    id: 'glint',
    name: 'Glint',
    description: 'A soft curved highlight sweeping across a dark face.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'length', label: 'Length', def: 35 },
      { key: 'width', label: 'Width', def: 45 },
      { key: 'brightness', label: 'Brightness', def: 55 },
    ],
  },
  {
    id: 'tunnel',
    name: 'Tunnel',
    description: 'A soft walled tunnel with bands gliding toward the viewer.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'pitch', label: 'Band pitch', def: 50 },
      { key: 'brightness', label: 'Brightness', def: 74 },
      { key: 'mix', label: 'Band share', def: 50 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'curve', label: 'Depth curve', def: 50 },
      { key: 'spiral', label: 'Spiral', def: 50 },
      { key: 'turn', label: 'Turn rate', def: 50 },
    ],
  },
  {
    id: 'kaleido',
    name: 'Kaleido',
    description: 'A six fold flower of soft blotches that keeps reforming.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Blotch scale', def: 50 },
      { key: 'brightness', label: 'Brightness', def: 62 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'rays', label: 'Rays', def: 50 },
      { key: 'vignette', label: 'Vignette', def: 50 },
      { key: 'sweep', label: 'Sweep', def: 50 },
    ],
  },
  {
    id: 'shafts',
    name: 'Shafts',
    description: 'Soft shafts of light fanning from above and swaying.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'density', label: 'Shaft count', def: 50 },
      { key: 'brightness', label: 'Brightness', def: 66 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'falloff', label: 'Falloff', def: 50 },
      { key: 'reach', label: 'Reach', def: 50 },
      { key: 'breath', label: 'Breath', def: 50 },
      { key: 'sway', label: 'Sway', def: 50 },
    ],
  },
  {
    id: 'weave',
    name: 'Weave',
    description: 'A large soft honeycomb cloth turning and breathing.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Weave scale', def: 50 },
      { key: 'brightness', label: 'Brightness', def: 62 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'cross', label: 'Cross weave', def: 50 },
      { key: 'turn', label: 'Turn rate', def: 50 },
      { key: 'drift', label: 'Drift', def: 50 },
      { key: 'breath', label: 'Breath rate', def: 50 },
    ],
  },
  {
    id: 'lens',
    name: 'Lens',
    description: 'A magnifying lens wandering over a dim mottled ground.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'size', label: 'Lens size', def: 55 },
      { key: 'brightness', label: 'Brightness', def: 62 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'edge', label: 'Edge width', def: 50 },
      { key: 'rim', label: 'Rim darkness', def: 50 },
      { key: 'travel', label: 'Lens travel', def: 50 },
      { key: 'drift', label: 'Ground drift', def: 50 },
    ],
  },
  {
    id: 'tide',
    name: 'Tide',
    description: 'Broad soft bars gliding through each other and brightening where they cross.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'width', label: 'Band width', def: 50 },
      { key: 'glow', label: 'Glow', def: 55 },
      { key: 'bands', label: 'Band count', def: 50 },
      { key: 'sway', label: 'Sway', def: 50 },
      { key: 'edge', label: 'Edge shape', def: 50 },
      { key: 'floor', label: 'Floor', def: 30 },
      { key: 'grain', label: 'Grain', def: 50 },
    ],
  },
  {
    id: 'truchet',
    name: 'Truchet',
    description: 'Soft quarter circle arcs linking into meandering loops.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'arc', label: 'Arc width', def: 50 },
      { key: 'glow', label: 'Glow', def: 55 },
      { key: 'drift', label: 'Drift angle', def: 50 },
      { key: 'bias', label: 'Tile bias', def: 50 },
      { key: 'sharp', label: 'Sharpness', def: 50 },
      { key: 'contrast', label: 'Contrast', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
    ],
  },
  {
    id: 'quilt',
    name: 'Quilt',
    description: 'A grid of soft pillows with the light walking around them.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'pitch', label: 'Pillow size', def: 75 },
      { key: 'relief', label: 'Relief', def: 55 },
      { key: 'turn', label: 'Light turn', def: 50 },
      { key: 'drift', label: 'Drift', def: 50 },
      { key: 'dome', label: 'Puffiness', def: 50 },
      { key: 'stretch', label: 'Stretch', def: 50 },
      { key: 'bright', label: 'Brightness', def: 50 },
    ],
  },
  {
    id: 'rain',
    name: 'Rain',
    description: 'Sparse soft streaks falling down a dark face.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'tail', label: 'Tail length', def: 50 },
      { key: 'glow', label: 'Head glow', def: 55 },
      { key: 'width', label: 'Drop width', def: 50 },
      { key: 'fade', label: 'Tail fade', def: 50 },
      { key: 'spread', label: 'Speed spread', def: 50 },
      { key: 'base', label: 'Base light', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
    ],
  },
  {
    id: 'stripes',
    name: 'Stripes',
    description: 'Broad soft stripes sliding and turning while two gratings beat.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'pitch', label: 'Stripe pitch', def: 50 },
      { key: 'depth', label: 'Depth', def: 55 },
      { key: 'beat', label: 'Beat depth', def: 75 },
      { key: 'beats', label: 'Beat count', def: 20 },
      { key: 'turn', label: 'Turn rate', def: 50 },
      { key: 'floor', label: 'Black level', def: 39 },
      { key: 'grain', label: 'Grain', def: 50 },
    ],
  },
  {
    id: 'ribbon',
    name: 'Ribbon',
    description: 'One wide ribbon twisting about its axis down the face.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'width', label: 'Ribbon width', def: 50 },
      { key: 'twist', label: 'Twist', def: 50 },
      { key: 'bright', label: 'Brightness', def: 62 },
      { key: 'waist', label: 'Waist', def: 50 },
      { key: 'glow', label: 'Edge glow', def: 50 },
      { key: 'shade', label: 'Face shading', def: 50 },
      { key: 'wash', label: 'Backdrop', def: 50 },
    ],
  },
  {
    id: 'harmonograph',
    name: 'Harmonograph',
    description: 'A luminous Lissajous thread drawing slow loops over a glow.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'size', label: 'Figure size', def: 68 },
      { key: 'glow', label: 'Thread glow', def: 60 },
      { key: 'bright', label: 'Brightness', def: 60 },
      { key: 'lobes', label: 'Lobe count', def: 50 },
      { key: 'turn', label: 'Turn rate', def: 50 },
      { key: 'trail', label: 'Trail length', def: 50 },
      { key: 'vign', label: 'Vignette', def: 50 },
    ],
  },
  {
    id: 'floor',
    name: 'Floor',
    description: 'A soft plaid floor running back to a glowing horizon.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'yaw', label: 'Yaw sway', def: 50 },
      { key: 'scale', label: 'Plaid scale', def: 50 },
      { key: 'bright', label: 'Brightness', def: 60 },
      { key: 'glide', label: 'Glide rate', def: 50 },
      { key: 'haze', label: 'Haze depth', def: 50 },
      { key: 'glow', label: 'Horizon glow', def: 50 },
      { key: 'tile', label: 'Tile size', def: 50 },
    ],
  },
  {
    id: 'hills',
    name: 'Hills',
    description: 'Three hill layers scrolling at different speeds under drifting stars.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'relief', label: 'Ridge relief', def: 50 },
      { key: 'depth', label: 'Layer contrast', def: 55 },
      { key: 'bright', label: 'Brightness', def: 60 },
      { key: 'spread', label: 'Ridge spacing', def: 50 },
      { key: 'haze', label: 'Ridge haze', def: 50 },
      { key: 'sky', label: 'Sky tone', def: 50 },
      { key: 'stars', label: 'Star density', def: 50 },
    ],
  },
  {
    id: 'gyroid',
    name: 'Gyroid',
    description: 'Broad luminous passages through a gyroid section, opening and reconnecting.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'scale', label: 'Passage size', def: 50 },
      { key: 'glow', label: 'Passage width', def: 55 },
      { key: 'bright', label: 'Brightness', def: 60 },
      { key: 'aspect', label: 'Aspect', def: 50 },
      { key: 'morph', label: 'Morph rate', def: 50 },
      { key: 'floor', label: 'Ground level', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
    ],
  },
  {
    id: 'barrel',
    name: 'Barrel',
    description: 'Satin bands climbing a shaded cylinder.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'bands', label: 'Bands', def: 14 },
      { key: 'shade', label: 'Cylinder shade', def: 62 },
      { key: 'tilt', label: 'Band tilt', def: 50 },
      { key: 'width', label: 'Barrel width', def: 50 },
      { key: 'edge', label: 'Edge fade', def: 50 },
      { key: 'light', label: 'Light angle', def: 50 },
      { key: 'depth', label: 'Band depth', def: 50 },
    ],
  },
  {
    id: 'grid',
    name: 'Grid',
    description: 'A soft wire floor receding into the distance, crossings sliding forward.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'density', label: 'Grid density', def: 50 },
      { key: 'lines', label: 'Line strength', def: 58 },
      { key: 'width', label: 'Line width', def: 50 },
      { key: 'cross', label: 'Cross lines', def: 50 },
      { key: 'reach', label: 'Grid reach', def: 50 },
      { key: 'shade', label: 'Floor shade', def: 50 },
      { key: 'drift', label: 'Side drift', def: 50 },
    ],
  },
  {
    id: 'cells',
    name: 'Cells',
    description: 'Glowing channels dividing quiet dark cells, drifting slowly.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'width', label: 'Channel width', def: 55 },
      { key: 'depth', label: 'Contrast', def: 60 },
      { key: 'count', label: 'Cell count', def: 42 },
      { key: 'halo', label: 'Halo width', def: 40 },
      { key: 'tilt', label: 'Drift tilt', def: 50 },
      { key: 'grain', label: 'Grain', def: 50 },
      { key: 'glow', label: 'Glow', def: 50 },
    ],
  },
  {
    id: 'dimples',
    name: 'Dimples',
    description: 'A hammered relief with the highlight sweeping around it.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'relief', label: 'Relief', def: 58 },
      { key: 'bright', label: 'Brightness', def: 62 },
    ],
  },
  {
    id: 'cube',
    name: 'Cube',
    description: 'A translucent cube with feathered edges turning in a graded field.',
    params: [
      { key: 'speed', label: 'Speed', def: 50 },
      { key: 'size', label: 'Cube size', def: 50 },
      { key: 'glow', label: 'Face glow', def: 55 },
    ],
  },
];

export { BG_THEMES, BG_THEME_CATEGORIES };

// The built-ins grouped for a picker: every declared category that holds
// gradients, in the declared order, each holding its gradients in table order
// with the table index, which is the stored id and the option value.
//
// A category with nothing in it is dropped rather than shown empty. A gradient
// whose category is not in the declared list would otherwise vanish, so it
// falls into a last group instead: the generator rejects that case, and this is
// the belt for a hand-edited file.
//
// One function, because the select and the browse dialog must group the same
// way. tools/gradient_groups_check.mjs reads the select's markup and
// tools/gradient_browse_check.mjs the dialog's, both against this order.
export function builtinThemeGroups() {
  const byCategory = new Map(BG_THEME_CATEGORIES.map(c => [c, []]));
  const strays = [];
  BG_THEMES.forEach((t, index) => {
    const bucket = byCategory.get(t.category);
    (bucket ?? strays).push({ name: t.name, index });
  });
  const groups = BG_THEME_CATEGORIES.filter(c => byCategory.get(c).length > 0).map(c => ({
    category: c,
    items: byCategory.get(c),
  }));
  if (strays.length > 0) groups.push({ category: 'Other', items: strays });
  return groups;
}

// The legacy bgAnimTheme namespace, frozen at 18 to match BG_THEME_LEGACY_CUSTOM
// in src/display/ui/default/bganim/BgAnim.h. Before the gradient library
// existed, bgAnimTheme was an index into an 18-entry built-in table, and 18
// meant the single custom gradient in bgAnimCustomTheme. It is NOT
// BG_THEMES.length: deriving it from the table would hand a device that stored
// 18 whatever built-in is appended at index 18. Legacy integers outside 0..18
// read as built-in 0.
export const BG_THEME_CUSTOM = 18;
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

// Whether a ref names something that exists right now: a built-in index in
// this build's table, or a library entry that has not been deleted.
export function refResolves(ref, library) {
  const r = String(ref ?? '');
  if (r.startsWith('c')) return library.some(g => g.id === parseInt(r.slice(1), 10));
  if (r === '') return false;
  const idx = parseInt(r, 10);
  return idx >= 0 && idx < BG_THEMES.length;
}

// A ref the picker shows but never stores: the pre-library custom gradient is
// still what the global fallback draws, because the firmware's one-time
// migration had to defer. It is read only, and no stored field ever holds it.
export const BG_LEGACY_CUSTOM_REF = 'legacy';
export const BG_LEGACY_CUSTOM_NAME = 'Custom (legacy)';

// What the legacy pair (bgAnimTheme, bgAnimCustomTheme) draws: the built-in
// index, or -1 when the custom string is what draws. Mirrors bg_legacy_builtin
// in BgAnim.h, including the frozen sentinel, so the form never labels a
// stored 18 as the built-in that lands at index 18.
export function legacyBuiltin(globalThemeId, customTheme) {
  const g = parseInt(globalThemeId, 10);
  if (g === BG_THEME_CUSTOM) return parseGradient(customTheme) ? -1 : 0;
  return g >= 0 && g < BG_THEME_CUSTOM ? g : 0;
}

// The rollback mirror: the bgAnimTheme value that goes with a selected ref, or
// null to leave bgAnimTheme alone. Mirrors bg_legacy_mirror_for_ref in
// BgAnim.h. A built-in 0 to 17 mirrors unchanged, an appended built-in mirrors
// as 0 (never as its own index, which would mean the custom gradient or a
// gradient nobody chose to an older build), a library ref and a ref that does
// not resolve leave the legacy fallback where it is.
export function legacyThemeMirror(ref, themeCount = BG_THEMES.length) {
  const r = String(ref ?? '');
  if (r === '' || r.startsWith('c') || !/^\d+$/.test(r)) return null;
  const idx = parseInt(r, 10);
  if (idx < 0 || idx >= themeCount) return null;
  return idx < BG_THEME_CUSTOM ? idx : 0;
}

// The form fields a global gradient selection writes, and nothing else. The
// editor applies exactly this map, so the mirror policy can be checked without
// rendering anything (tools/gradient_mirror_check.mjs). The legacy stand-in is
// read only and writes nothing at all.
export function globalAssignFields(nextRef, themeCount = BG_THEMES.length) {
  if (nextRef === BG_LEGACY_CUSTOM_REF) return {};
  const fields = { bgAnimGradientRef: nextRef };
  const mirror = legacyThemeMirror(nextRef, themeCount);
  if (mirror !== null) fields.bgAnimTheme = String(mirror);
  return fields;
}

// The ref the global default resolves to: bgAnimGradientRef when it names
// something that exists, else the built-in the legacy pair resolves to, or
// BG_LEGACY_CUSTOM_REF when that pair is still the custom gradient. These are
// steps two and three of the firmware's bg_resolve_anim_theme, and the result
// is always a concrete ref, never ''.
export function globalGradientRef(globalRef, library, globalThemeId, customTheme) {
  if (refResolves(globalRef, library)) return String(globalRef);
  const b = legacyBuiltin(globalThemeId, customTheme);
  return b < 0 ? BG_LEGACY_CUSTOM_REF : String(b);
}

// The ref an animation effectively draws with, after the firmware's
// fallbacks: its own map entry when it resolves, else the global default.
export function effectiveRef(refs, animIdx, library, globalThemeId, globalRef, customTheme) {
  const ref = refs[animIdx] ?? '';
  if (refResolves(ref, library)) return ref;
  return globalGradientRef(globalRef, library, globalThemeId, customTheme);
}

// The retained pre-library custom gradient as the panel actually draws it.
// bgAnimCustomTheme may carry explicit positions, and the firmware's last
// fallback throws them away: bg_resolve_anim_theme's step three parses the
// colours and then spaces them evenly on the uniform path, which is different
// arithmetic from the positional one (config/gradientRamp.js says how much).
// So the positions are replaced here rather than passed through, or the page
// would preview a gradient the panel will not draw, and the editor's live
// preview would serialize those positions straight back to the device.
//
// Null when the string is not one the firmware's parser accepts; the fallback
// is then built-in 0, which is what legacyBuiltin already reports.
export function legacyCustomGradient(customTheme) {
  const gradient = parseGradient(customTheme);
  if (!gradient) return null;
  const uni = uniformPositions(gradient.stops.length);
  return { stops: gradient.stops.map((s, i) => ({ color: s.color, pos: uni[i] })) };
}

export function gradientForRef(ref, library, customTheme) {
  if (ref === BG_LEGACY_CUSTOM_REF) {
    const gradient = legacyCustomGradient(customTheme);
    // Read only: it is not in the library, so there is nothing to edit or
    // rename, and the editor must not write it anywhere.
    if (gradient) return { name: BG_LEGACY_CUSTOM_NAME, stops: gradient.stops, editable: false };
  }
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
    while (defs.length < 8) defs.push(0);
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
