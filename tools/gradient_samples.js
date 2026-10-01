// gradient_samples.js: the bridge the Python gradient tools sample through.
//
// tools/gradient_check.py and tools/gradient_sheet.py must not carry their own
// copy of the tone curve. A third implementation would drift from the firmware
// the way the editor's helpers already had: the editor interpolates every
// gradient positionally while the panel keeps separate uniform arithmetic, and
// it rounds the two percentage conversions the firmware truncates.
//
// So the Python side asks this, this asks web/src/config/gradientRamp.js, and
// tools/animbench/web/ramp_parity.js proves that module equal to the real C++
// entry by entry. One definition, checked.
//
// usage:  node gradient_samples.js < request.json > response.json
//
// request:  {"brightnessPct": 100, "kneePct": 100, "gain256": 256,
//            "mode": "ramp" | "reversed" | "wheel",
//            "gradients": ["rrggbb,rrggbb,...", ...]}
// response: {"brightnessPct": ..., "kneePct": ..., "gain256": ..., "mode": ...,
//            "ramps": [[256 RGB565 values], ...]}
//
// A gradient the firmware's parser would reject comes back as null, so the
// caller can report it rather than silently sampling something else.

import { readFileSync } from 'node:fs';

import { parseGradientWire, publishStops, toneFromPercent, buildThemeRamp, buildThemeWheel } from '../web/src/config/gradientRamp.js';

const req = JSON.parse(readFileSync(0, 'utf8'));
const brightnessPct = req.brightnessPct ?? 100;
const kneePct = req.kneePct ?? 100;
const gain256 = req.gain256 ?? 256;
const mode = req.mode ?? 'ramp';
const tone = toneFromPercent(brightnessPct, kneePct);

const ramps = (req.gradients ?? []).map(wire => {
  const theme = parseGradientWire(wire);
  if (theme === null) return null;
  const published = publishStops(theme, tone);
  const ramp =
    mode === 'wheel' ? buildThemeWheel(published, { gain256 }) : buildThemeRamp(published, { gain256, reversed: mode === 'reversed' });
  return Array.from(ramp);
});

process.stdout.write(JSON.stringify({ brightnessPct, kneePct, gain256, mode, ramps }));
