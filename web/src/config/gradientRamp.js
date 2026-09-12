// A gradient sampler that reproduces the firmware's palette arithmetic
// exactly, so a preview, a contact sheet or a set-quality check can say what
// the panel will store rather than what CSS happens to draw.
//
// Why this exists next to the helpers in bgAnimations.js: those are for the
// editor, and they are not an oracle. sampleGradient() always walks the
// positional path, but the firmware keeps separate uniform arithmetic for a
// gradient whose stops carry no explicit positions, and the two disagree.
// Espresso sampled at position 255 is 0xF738 (63288) through the uniform path
// the panel uses and 0xFF39 (65337) through the positional one, because the
// uniform path's last segment reaches only 251/256 of the way to the final
// stop. toneColor() also rounds the percentage conversions that DefaultUI
// truncates. Both differences are small and both change the answer.
//
// The arithmetic below is transcribed from, in order:
//   src/display/ui/default/bganim/BgAnimThemes.cpp   parseGradient
//   src/display/ui/default/DefaultUI.cpp             setThemeTone call site
//   src/display/ui/default/bganim/BgAnimCommon.cpp   publishStops, themeRGB,
//                                                    buildThemeRamp,
//                                                    buildThemeWheel
//
// tools/animbench/web/ramp_parity.js proves the transcription against the
// real C++ (tools/animbench/ramp_dump.cpp links BgAnimCommon.cpp itself), and
// `make check` in tools/animbench runs it. Do not "fix" this module to make a
// preview look better: the firmware is the definition, and a change here that
// is not matched in the firmware will fail the parity test.

export const BG_THEME_MAX_STOPS = 16;

// Positions a uniform gradient would carry: p_i = i * 255 / (n - 1),
// truncating, so six stops sit at 0, 51, 102, 153, 204, 255.
export function uniformRampPositions(n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push(Math.floor((i * 255) / (n - 1)));
  return out;
}

export function hexToRgb(hex) {
  const v = parseInt(String(hex).replace(/^#/, ''), 16) || 0;
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

export function rgbToHex(rgb) {
  return `#${rgb.map(v => (v & 255).toString(16).padStart(2, '0')).join('')}`;
}

export function rgb565(r, g, b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

// RGB565 back to 8 bits per channel by bit replication, which is what the
// panel shows and therefore the only honest way to compare two stored
// colours. 5-bit 31 becomes 255, not 248.
export function expandRgb565(v) {
  const r = (v >> 11) & 0x1f;
  const g = (v >> 5) & 0x3f;
  const b = v & 0x1f;
  return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
}

// Parses the wire format "rrggbb[@pos][,rrggbb[@pos]...]" the way the
// firmware's parseGradient does, including its repairs. Returns null for
// anything that parser rejects.
//
// The uniform flag is a property of the string, not of the numbers: one
// explicit @pos anywhere makes the whole gradient positional, even when every
// position it ends up with is evenly spaced. The firmware then samples it
// through different arithmetic, so evenly spaced explicit positions are not
// the same gradient as no positions at all.
export function parseGradientWire(str) {
  const s = String(str ?? '');
  const stops = [];
  const pos = [];
  const hasPos = [];
  let uniform = true;
  let i = 0;
  while (i < s.length && s[i] !== ';') {
    while (i < s.length && (s[i] === ' ' || s[i] === ',' || s[i] === '#')) i++;
    if (i >= s.length || s[i] === ';') break;
    if (stops.length >= BG_THEME_MAX_STOPS) return null;
    const hex = s.slice(i, i + 6);
    if (!/^[0-9a-fA-F]{6}$/.test(hex)) return null;
    stops.push(hexToRgb(hex));
    pos.push(0);
    hasPos.push(false);
    i += 6;
    if (s[i] === '@') {
      i++;
      let v = 0;
      let digits = 0;
      while (i < s.length && s[i] >= '0' && s[i] <= '9' && digits < 4) {
        v = v * 10 + (s.charCodeAt(i) - 48);
        i++;
        digits++;
      }
      if (digits === 0 || v > 255) return null;
      pos[pos.length - 1] = v;
      hasPos[hasPos.length - 1] = true;
      uniform = false;
    }
    if (i < s.length && s[i] !== ',' && s[i] !== ' ' && s[i] !== ';') return null;
  }
  const n = stops.length;
  if (n < 2) return null;
  const uni = uniformRampPositions(n);
  for (let k = 0; k < n; k++) if (!hasPos[k]) pos[k] = uni[k];
  for (let k = 1; k < n; k++) if (pos[k] < pos[k - 1]) pos[k] = pos[k - 1];
  return { stops, pos, uniform };
}

// A built-in from data/gradients.json as the firmware loads it: six stops, no
// positions on the wire, so the uniform path.
export function builtinTheme(hexStops) {
  const stops = hexStops.map(hexToRgb);
  return { stops, pos: uniformRampPositions(stops.length), uniform: true };
}

// The two settings percentages as DefaultUI converts them. Integer division
// truncates in C++; Math.floor is the same for non-negative values, and the
// settings clamp both to 0..100 before they get here.
export function toneFromPercent(brightnessPct, kneePct) {
  let bright256 = Math.floor((brightnessPct * 256) / 100);
  let knee = Math.floor((kneePct * 255) / 100);
  // setThemeTone's own clamps.
  if (bright256 < 0) bright256 = 0;
  else if (bright256 > 256) bright256 = 256;
  if (knee < 0) knee = 0;
  else if (knee > 255) knee = 255;
  return { bright256, knee };
}

// publishStops(): the shoulder acts on the theme's own values, then
// brightness scales whatever shape came out. The other order would move the
// knee wherever brightness happened to be set.
export function publishStops(theme, tone) {
  const { bright256, knee } = tone;
  const stops = theme.stops.map(rgb =>
    rgb.map(raw => {
      let v = raw;
      if (v > knee) v = knee + ((v - knee) >> 2);
      v = (v * bright256) >> 8;
      if (v < 0) v = 0;
      else if (v > 255) v = 255;
      return v;
    }),
  );
  return { stops, pos: theme.pos.slice(), uniform: theme.uniform };
}

// themeRGB(): the published theme sampled at 0..255.
export function themeRGB(published, pos) {
  const st = published.stops;
  const n = st.length;
  let t = pos;
  if (t < 0) t = 0;
  else if (t > 255) t = 255;
  if (published.uniform) {
    const scaled = t * (n - 1);
    const seg = scaled >> 8;
    const f = scaled & 255;
    return [0, 1, 2].map(c => (st[seg][c] + (((st[seg + 1][c] - st[seg][c]) * f) >> 8)) & 255);
  }
  const p = published.pos;
  if (t <= p[0]) return st[0].slice();
  if (t >= p[n - 1]) return st[n - 1].slice();
  let seg = 0;
  while (seg < n - 2 && t >= p[seg + 1]) seg++;
  const width = p[seg + 1] - p[seg];
  const f = width > 0 ? Math.floor(((t - p[seg]) * 256) / width) : 0;
  return [0, 1, 2].map(c => (st[seg][c] + (((st[seg + 1][c] - st[seg][c]) * f) >> 8)) & 255);
}

// buildThemeRamp(): the 256-entry RGB565 palette an animation indexes by
// intensity. gain256 is the animation's own extra palette gain, which is
// separate from the theme brightness above; 256 leaves the ramp alone.
export function buildThemeRamp(published, { gain256 = 256, reversed = false } = {}) {
  const out = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    const c = themeRGB(published, reversed ? 255 - i : i);
    const v = c.map(ch => {
      const x = (ch * gain256) >> 8;
      return x > 255 ? 255 : x;
    });
    out[i] = rgb565(v[0], v[1], v[2]);
  }
  return out;
}

// buildThemeWheel(): the cyclic palette Plasma uses. A uniform theme wraps
// over n segments; a positional one keeps its shape over the first 256 - W
// entries and spends W blending the last stop back into the first.
export function buildThemeWheel(published, { gain256 = 256 } = {}) {
  const st = published.stops;
  const n = st.length;
  const out = new Uint16Array(256);
  const clampGain = ch => {
    const x = (ch * gain256) >> 8;
    return x > 255 ? 255 : x;
  };
  if (!published.uniform) {
    const wrap = Math.floor(256 / n);
    const rampLen = 256 - wrap;
    for (let i = 0; i < 256; i++) {
      let c;
      if (i < rampLen) {
        c = themeRGB(published, Math.floor((i * 255) / (rampLen - 1)));
      } else {
        const f = Math.floor(((i - rampLen) * 256) / wrap);
        c = [0, 1, 2].map(ch => (st[n - 1][ch] + (((st[0][ch] - st[n - 1][ch]) * f) >> 8)) & 255);
      }
      const v = c.map(clampGain);
      out[i] = rgb565(v[0], v[1], v[2]);
    }
    return out;
  }
  for (let i = 0; i < 256; i++) {
    const scaled = i * n;
    const seg = scaled >> 8;
    const f = scaled & 255;
    const nextSeg = (seg + 1) % n;
    const v = [0, 1, 2].map(ch => {
      const raw = (st[seg][ch] + (((st[nextSeg][ch] - st[seg][ch]) * f) >> 8)) & 255;
      return clampGain(raw);
    });
    out[i] = rgb565(v[0], v[1], v[2]);
  }
  return out;
}

// One call from a source gradient to the palette the panel stores.
export function themeRamp(
  theme,
  { brightnessPct = 100, kneePct = 100, gain256 = 256, reversed = false, wheel = false } = {},
) {
  const published = publishStops(theme, toneFromPercent(brightnessPct, kneePct));
  return wheel
    ? buildThemeWheel(published, { gain256 })
    : buildThemeRamp(published, { gain256, reversed });
}
