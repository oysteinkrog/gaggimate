// Fine to coarse change ratio for every animation on the page.
//
// The question it answers: is the fine detail of this animation changing
// faster than its picture is? Split each frame into its 8x8 box average (the
// picture) and what is left (dither grain, band edges, single pixel detail),
// and measure how much each part changes per 66 ms, which is what one pixel
// row of the panel actually gets.
//
// Read the delta across a change, never the level. That correction came from
// the lane that built this (gm-33fm lane 2, 2026-09-12) after the level misled
// it: 30 of the 44 animations sit at or above 1 today, and the top two are
// Starfield at 7.66 and Orbits at 3.03, neither of which is broken. Both are
// sparse point fields, so the 8x8 average legitimately barely moves while the
// points do, and the ratio measures sparseness as much as it measures fizz.
//
// What the delta catches is a defect the half change time in motion.js is
// blind to. Across the speed calibration the ratio moved Silk -0.831,
// Harmonograph -0.439, Shafts -0.363, Mosaic -0.312, Barrel -0.259,
// Fireflies +0.069 and Truchet +0.111, and nothing outside those seven moved
// at all, which is itself a check that the instrument is stable. Truchet's
// rise is the whole pixel stutter the slowdown introduced, where about 28
// percent of 66 ms refreshes repeat the previous picture exactly. The half
// change metric put Truchet comfortably on target through all of that.
//
// So: run it before and after, per animation, and treat a rise above about
// 0.15 on an animation whose rate went up as something to look at.
//
// This is a relative measure, so brightness and contrast cancel. It says
// nothing about whether an animation moves the right amount; motion.js does
// that. It only says whether what moves is the picture or the speckle.
//
// usage: node grain_ratio.js <anim_bench.html> [ids]

const path = require('path');
const M = require(path.join(__dirname, 'motion_lib.js'));

const LP = 8, LPW = M.W / LP;
// Cell index per masked pixel, built once: this is what makes the sweep cheap.
const CELL = new Int32Array(M.MASK.length);
for (let i = 0; i < M.MASK.length; i++) {
  const px = (M.MASK[i] >> 2) % M.W, py = (M.MASK[i] >> 2) / M.W | 0;
  CELL[i] = (((py / LP) | 0) * LPW + ((px / LP) | 0)) * 3;
}

function fineDiff(a, la, b, lb) {
  let s = 0;
  for (let i = 0; i < M.MASK.length; i++) {
    const k = M.MASK[i], c = CELL[i];
    s += Math.abs((a[k] - la[c]) - (b[k] - lb[c])) +
         Math.abs((a[k + 1] - la[c + 1]) - (b[k + 1] - lb[c + 1])) +
         Math.abs((a[k + 2] - la[c + 2]) - (b[k + 2] - lb[c + 2]));
  }
  return s / M.MASK.length;
}

const html = process.argv[2];
const ANIMS = M.loadAnims(html);
const ids = process.argv[3] ? process.argv[3].split(',').map(Number) : ANIMS.map((_, i) => i);

const bufs = [new Uint8ClampedArray(M.W * M.H * 4), new Uint8ClampedArray(M.W * M.H * 4)];
const lps = [new Float32Array(LPW * (M.H / LP) * 3), new Float32Array(LPW * (M.H / LP) * 3)];
const rows = [];

for (const id of ids) {
  const anim = ANIMS[id];
  const p = anim.params.map(x => x.def);
  const st = {};
  try {
    if (anim.init) anim.init(M.W, M.H, st);
    let coarse = 0, fine = 0, n = 0;
    for (let s = 0; s <= 45; s++) {
      const cur = s & 1, prv = cur ^ 1;
      anim.render(bufs[cur], M.W, M.H, s * 66, p, st);
      M.lowpass(bufs[cur], lps[cur]);
      if (s > 0) {
        coarse += M.lpDiff(lps[prv], lps[cur]);
        fine += fineDiff(bufs[prv], lps[prv], bufs[cur], lps[cur]);
        n++;
      }
    }
    rows.push({ id, name: anim.name, coarse: coarse / n, fine: fine / n, ratio: (fine / n) / (coarse / n) });
  } catch (e) {
    rows.push({ id, name: anim.name, err: String(e).slice(0, 60) });
  }
}

rows.sort((a, b) => (b.ratio || 0) - (a.ratio || 0));
console.log('ratio   picture   fine    id  name           (sorted worst first)');
for (const r of rows) {
  if (r.err) { console.log(`  ERR                     ${String(r.id).padStart(2)}  ${r.name}  ${r.err}`); continue; }
  const flag = r.ratio >= 1 ? '  <-- detail leads' : '';
  console.log(`${r.ratio.toFixed(3).padStart(5)}  ${r.coarse.toFixed(3).padStart(7)}  ${r.fine.toFixed(3).padStart(6)}   ` +
    `${String(r.id).padStart(2)}  ${String(r.name).padEnd(14)}${flag}`);
}
