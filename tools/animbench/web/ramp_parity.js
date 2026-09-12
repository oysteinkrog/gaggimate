// ramp_parity.js: is the JS gradient sampler the same function as the firmware?
//
// web/src/config/gradientRamp.js is the sampler the gradient tools sample
// through, and a hand transcription of firmware arithmetic is worth nothing
// until something checks it. This runs build/ramp_dump, which links the real
// BgAnimThemes.cpp and BgAnimCommon.cpp, and compares all 256 RGB565 entries
// of every ramp, entry by entry.
//
// It is not a spot check. The cases cover every branch the two
// implementations could disagree on:
//
//   - every built-in in BG_THEMES, through the uniform path
//   - 2 stops and 16 stops, the ends of the stop count range
//   - explicit positions, including positions that happen to be even
//     (the wire format's uniform flag, not the numbers, picks the path)
//   - repeated positions, so a zero-width segment
//   - flat runs before the first stop and after the last
//   - tone 100/100 (the default, identity) and 60/65 (the bead's case),
//     plus percentages where the conversion truncates and rounding would
//     give a different answer
//   - extra palette gain above and below 256, and the clamp at the top
//   - the reversed ramp and the cyclic wheel
//
// usage:  node ramp_parity.js [--verbose]
// Exit code is 1 on any differing entry.

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import { BG_THEMES } from '../../../web/src/config/bgThemes.js';
import { parseGradientWire, publishStops, toneFromPercent, buildThemeRamp, buildThemeWheel } from '../../../web/src/config/gradientRamp.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const DUMP = resolve(HERE, '..', 'build', 'ramp_dump');
const verbose = process.argv.includes('--verbose');

// brightness %, highlight rolloff %. 7 and 33 are there because 7 * 256 / 100
// is 17.92 and 7 * 255 / 100 is 17.85: the firmware truncates both to 17 and
// rounding would say 18, which is the bug this test exists to keep out.
const TONES = [
  [100, 100],
  [60, 65],
  [7, 7],
  [33, 71],
  [99, 1],
  [0, 100],
  [100, 0],
  [50, 50],
];

const GAINS = [256, 128, 400];

function hexN(n, seed) {
  // Deterministic, spread over the cube so a channel-order slip shows up.
  const out = [];
  for (let i = 0; i < n; i++) {
    const r = (seed * 37 + i * 61) & 255;
    const g = (seed * 91 + i * 23) & 255;
    const b = (seed * 13 + i * 149) & 255;
    out.push([r, g, b].map(v => v.toString(16).padStart(2, '0')).join(''));
  }
  return out;
}

const cases = [];
for (const theme of BG_THEMES) {
  cases.push({ name: `builtin ${theme.name}`, wire: theme.stops.map(s => s.replace('#', '')).join(',') });
}
cases.push({ name: 'uniform 2 stops', wire: '000000,ffffff' });
cases.push({ name: 'uniform 3 stops', wire: hexN(3, 5).join(',') });
cases.push({ name: 'uniform 16 stops', wire: hexN(16, 9).join(',') });
cases.push({
  name: 'explicit positions, evenly spaced',
  // The same numbers a uniform six-stop gradient gets, but written out. The
  // firmware then uses the positional path, and the two paths differ.
  wire: BG_THEMES[0].stops.map((s, i) => `${s.replace('#', '')}@${Math.floor((i * 255) / 5)}`).join(','),
});
cases.push({ name: 'explicit positions, uneven', wire: '020408@0,10203c@12,2c4a74@30,5486b4@190,9cc8e4@250,eafaff@255' });
cases.push({ name: 'explicit positions, one stop only', wire: '020408,10203c@120,2c4a74,5486b4,9cc8e4,eafaff' });
cases.push({ name: 'repeated positions', wire: '020408@40,10203c@40,2c4a74@40,5486b4@200,9cc8e4@200,eafaff@255' });
cases.push({ name: 'flat run before and after', wire: '080402@30,2a1206@60,6b3413@90,b8703a@120,e8b268@150,f8e6c8@180' });
cases.push({ name: 'flat run, both ends far in', wire: '000000@100,ffffff@110' });
cases.push({ name: 'unordered positions, repaired', wire: '080402@200,2a1206@10,6b3413@90,b8703a@20,e8b268@150,f8e6c8@255' });
cases.push({ name: 'two stops at the same position', wire: '000000@128,ffffff@128' });
cases.push({ name: '16 stops with positions', wire: hexN(16, 3).map((h, i) => `${h}@${Math.min(255, i * 17)}`).join(',') });

const queries = [];
for (const c of cases) {
  for (const [brightPct, kneePct] of TONES) {
    for (const gain of GAINS) {
      for (const mode of ['ramp', 'reversed', 'wheel']) {
        queries.push({ ...c, brightPct, kneePct, gain, mode });
      }
    }
  }
}

// Espresso at position 255, named in the bead because it is where the uniform
// and positional paths part company: 63288 against 65337.
const espresso = parseGradientWire(BG_THEMES[0].stops.map(s => s.replace('#', '')).join(','));
const espressoRamp = buildThemeRamp(publishStops(espresso, toneFromPercent(100, 100)), {});
if (espressoRamp[255] !== 63288) {
  console.error(`espresso at 255: expected 63288 from the uniform path, got ${espressoRamp[255]}`);
  process.exit(1);
}

function jsRamp(q) {
  const theme = parseGradientWire(q.wire);
  if (theme === null) return null;
  const published = publishStops(theme, toneFromPercent(q.brightPct, q.kneePct));
  return q.mode === 'wheel'
    ? buildThemeWheel(published, { gain256: q.gain })
    : buildThemeRamp(published, { gain256: q.gain, reversed: q.mode === 'reversed' });
}

if (!existsSync(DUMP)) {
  console.error(`missing ${DUMP}. Build it with: make build/ramp_dump`);
  process.exit(1);
}

const input = queries.map(q => `${q.wire} ${q.brightPct} ${q.kneePct} ${q.gain} ${q.mode}\n`).join('');
const child = spawn(DUMP, [], { stdio: ['pipe', 'pipe', 'inherit'] });
let out = '';
child.stdout.setEncoding('utf8');
child.stdout.on('data', d => (out += d));
child.stdin.end(input);

child.on('close', code => {
  if (code !== 0) {
    console.error(`ramp_dump exited ${code}`);
    process.exit(1);
  }
  const lines = out.split('\n').filter(Boolean);
  if (lines.length !== queries.length) {
    console.error(`ramp_dump returned ${lines.length} lines for ${queries.length} queries`);
    process.exit(1);
  }
  let bad = 0;
  let entries = 0;
  for (let i = 0; i < queries.length; i++) {
    const q = queries[i];
    if (lines[i] === 'ERR') {
      console.error(`case "${q.name}": the firmware parser rejected ${q.wire}`);
      bad++;
      continue;
    }
    const cpp = lines[i].split(' ').map(Number);
    const js = jsRamp(q);
    if (js === null) {
      console.error(`case "${q.name}": the JS parser rejected a string the firmware accepted`);
      bad++;
      continue;
    }
    for (let k = 0; k < 256; k++) {
      entries++;
      if (cpp[k] !== js[k]) {
        console.error(
          `case "${q.name}" tone ${q.brightPct}/${q.kneePct} gain ${q.gain} ${q.mode}: entry ${k} is ${js[k]} in JS and ${cpp[k]} in the firmware`,
        );
        bad++;
        break;
      }
    }
  }
  if (bad > 0) {
    console.error(`ramp parity FAIL: ${bad} of ${queries.length} cases differ`);
    process.exit(1);
  }
  console.log(`ramp parity OK: ${queries.length} cases, ${entries} ramp entries, JS and firmware agree`);
  if (verbose) {
    for (const c of cases) console.log(`  ${c.name}: ${c.wire}`);
  }
});
