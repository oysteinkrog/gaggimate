// Movement measurement for the GaggiMate background animations.
//
// Loads the animation registry out of tools/animbench/web/anim_bench.html the
// same way tools/animbench/web/page_render.js does, renders 480x480 frames
// under node, and scores how much each animation moves.
//
// Why it plays the animation back instead of sampling times directly.
// Several entries carry state across render calls. Ripples spawns a drop when
// tMs passes a stored deadline and drops ripples older than their lifetime, so
// a render at t = 41 s pushes the next drop past the end of time and every
// later render shows still water. Starfield keeps a shooting star on a
// deadline the same way. So the harness renders one continuous playback per
// animation, 66 ms per step, which is one frame at the 15 Hz the panel gives a
// pixel row, and takes all its measurements out of that single pass.
//
// Metric
//   D(a,b)  mean of |dR|+|dG|+|dB| per pixel inside the 480 px circle
//   anchors six frames spread across the playback
//   curve   D(anchor, anchor + k steps) on a ladder of k, averaged over anchors
//   short   the curve at one step, 66 ms
//   sat     the "fully changed" level: mean of the top third of every D at a
//           separation of 7.9 s or more. A single long separation is not safe,
//           because Ripples and Steam are near-periodic and land back on
//           themselves at some separations.
//   score   short / sat. 0 is a still picture. 1 means one 66 ms step changes
//           the picture as much as two unrelated moments differ.
//   thalfMs the separation at which the curve first reaches half of sat. The
//           same information as a time rather than a ratio, and the number to
//           equalise if Speed 50 is to mean the same movement everywhere.
//
// Brightness and contrast cancel in score and thalfMs, because both frames of
// every pair come from the same animation at the same parameters. Two numbers
// are reported alongside because the ratio alone can mislead:
//   chg66   fraction of circle pixels that changed by more than 12 at 66 ms
//   spread  mean deviation of a frame from its own mean colour, which is how
//           much picture there is to move in the first place
//
// usage: node motion.js <anim_bench.html> <out.json> [--ids 1,2] [--speeds 0,50]

const fs = require('fs');
const path = require('path');
const M = require(path.join(__dirname, 'motion_lib.js'));

const STEP_MS = 66;
const ANCHOR_MS = [1000, 3000, 6000, 10000, 15000, 21000];
// Separations, in steps of 66 ms, from each anchor. 512 steps is 33.8 s.
const LADDER = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512];
const SAT_FROM_STEPS = 120; // 7.9 s and up feeds the saturation estimate
const CHG_THR = 12;
const PLAY_MS = Math.max(...ANCHOR_MS) + Math.max(...LADDER) * STEP_MS + STEP_MS;
// The fleet's Speed curve, the same one the firmware and the page entries use.
const rateOf = sp => Math.pow(2, (sp - 50) / 18.2);

// --matched: does the slider scale the animation?
//
// The default window is fixed in wall clock, so at Speed 100 it sees 6.7 times
// more animation time than at Speed 50 and at Speed 0 it sees 6.7 times less.
// The saturation estimate, which reads separations of 7.9 s and up, therefore
// changes meaning with the setting, and comparing a half change time against a
// fixed target across the slider compares two different measurements. Seven
// animations read outside the fleet tolerance for that reason alone and none
// of them had anything wrong with its clock.
//
// With --matched every time in the playback (the step, the anchors and so the
// ladder) is divided by the setting's rate, so each Speed sees the same window
// of animation time, and the reported thalfAdjMs multiplies the result back by
// the rate. An animation whose Speed is one multiplier on one clock is then
// flat across the slider by construction, and one that leaves a term unscaled
// is not. That makes it the check for the law, and the wall-clock numbers stay
// the check for how much a setting moves in front of a person.
function measure(anim, p, scale) {
  const state = {};
  if (anim.init) anim.init(M.W, M.H, state);

  const stepMs = STEP_MS * scale;
  const anchorMs = ANCHOR_MS.map(ms => ms * scale);
  const playMs = PLAY_MS * scale;
  const nSteps = Math.ceil(playMs / stepMs);
  const anchorStep = anchorMs.map(ms => Math.round(ms / stepMs));
  const anchorBuf = anchorMs.map(() => new Uint8ClampedArray(M.W * M.H * 4));
  const cur = new Uint8ClampedArray(M.W * M.H * 4);
  const wanted = new Map(); // step index -> [{anchor, k}]
  anchorStep.forEach((s0, ai) => {
    for (const k of LADDER) {
      const s = s0 + k;
      if (!wanted.has(s)) wanted.set(s, []);
      wanted.get(s).push({ ai, k });
    }
  });

  const anchorLp = anchorMs.map(() => new Float32Array(M.LPW * M.LPH * 3));
  const curLp = new Float32Array(M.LPW * M.LPH * 3);
  const prevLp = new Float32Array(M.LPW * M.LPH * 3);
  let prev = new Uint8ClampedArray(M.W * M.H * 4);
  let cur2 = cur;

  const dByK = new Map(LADDER.map(k => [k, []]));
  const lpByK = new Map(LADDER.map(k => [k, []]));
  const chg = [];
  const spreads = [];
  const lumas = [];
  // One 66 ms step, measured on every consecutive pair of the playback rather
  // than on the six anchors. Kaleido scrolls by whole pixels about three times
  // a second, so it stands still on most steps and jumps on the rest, and six
  // anchor samples can land entirely between the jumps and report exactly zero.
  let stepSum = 0, stepLpSum = 0, stepMoved = 0, stepMax = 0, stepN = 0;

  for (let s = 0; s <= nSteps; s++) {
    // Whole milliseconds, because that is all a frame() ever gets: tMs is a
    // uint32 of milliseconds on the device and on the page. It matters only
    // under --matched, where a step can be a fraction of a millisecond: an
    // animation that truncates its own frame delta (Starfield's drift
    // accumulator) would then lose that fraction on every step and read as
    // though its clock did not scale. Rounding jitters a separation by half a
    // millisecond and biases nothing. Without --matched every step and anchor
    // is already whole, so this changes no existing number.
    const t = Math.round(s * stepMs);
    anim.render(cur2, M.W, M.H, t, p, state);
    M.lowpass(cur2, curLp);
    const ai = anchorStep.indexOf(s);
    if (ai >= 0) {
      anchorBuf[ai].set(cur2);
      anchorLp[ai].set(curLp);
      spreads.push(M.selfSpread(cur2));
      lumas.push(M.meanLuma(cur2));
    }
    if (s > 0) {
      const d = M.meanAbsDiff(prev, cur2);
      stepSum += d; stepLpSum += M.lpDiff(prevLp, curLp); stepN++;
      if (d > 1e-9) stepMoved++;
      if (d > stepMax) stepMax = d;
      chg.push(M.changedFrac(prev, cur2, CHG_THR));
    }
    const jobs = wanted.get(s);
    if (jobs) for (const j of jobs) {
      dByK.get(j.k).push(M.meanAbsDiff(anchorBuf[j.ai], cur2));
      lpByK.get(j.k).push(M.lpDiff(anchorLp[j.ai], curLp));
    }
    const tb = prev; prev = cur2; cur2 = tb;
    prevLp.set(curLp);
  }

  const mean = v => v.reduce((x, y) => x + y, 0) / v.length;

  // score, saturation and half-change time out of one set of per-separation
  // diffs. Run once on the raw frames and once on the 8x8 low-pass frames.
  const reduce = (byK, oneStep) => {
    const curve = new Map([...byK].map(([k, v]) => [k, mean(v)]));
    curve.set(1, oneStep);
    const pool = [];
    for (const [k, v] of byK) if (k >= SAT_FROM_STEPS) pool.push(...v);
    pool.sort((a, b) => b - a);
    const sat = mean(pool.slice(0, Math.max(1, Math.round(pool.length / 3))));
    const short = curve.get(1);
    let thalf = null;
    const half = sat / 2;
    let prevK = 0, prevD = 0;
    for (const k of LADDER) {
      const d = curve.get(k);
      if (d >= half) {
        thalf = prevK === 0
          ? k * stepMs * (half / d)
          : Math.exp(Math.log(prevK) + ((half - prevD) / (d - prevD)) * (Math.log(k) - Math.log(prevK))) * stepMs;
        break;
      }
      prevK = k; prevD = d;
    }
    return { curve, sat, short, thalf };
  };

  const raw = reduce(dByK, stepSum / stepN);
  const lp = reduce(lpByK, stepLpSum / stepN);

  return {
    score: raw.sat > 0 ? raw.short / raw.sat : 0,
    thalfMs: raw.thalf == null ? null : +raw.thalf.toFixed(0),
    scoreLp: lp.sat > 0 ? lp.short / lp.sat : 0,
    thalfLpMs: lp.thalf == null ? null : +lp.thalf.toFixed(0),
    // The half change time in animation time rather than wall clock. Equal to
    // thalfMs when scale is 1, which is every run without --matched and Speed
    // 50 with it.
    thalfAdjMs: raw.thalf == null ? null : +(raw.thalf / scale).toFixed(0),
    thalfLpAdjMs: lp.thalf == null ? null : +(lp.thalf / scale).toFixed(0),
    scale: +scale.toFixed(5),
    stepMs: +stepMs.toFixed(3),
    short: +raw.short.toFixed(4),
    sat: +raw.sat.toFixed(4),
    shortLp: +lp.short.toFixed(4),
    satLp: +lp.sat.toFixed(4),
    chg66: +mean(chg).toFixed(5),
    stepsMoved: +(stepMoved / stepN).toFixed(4),
    stepMax: +stepMax.toFixed(4),
    spread: +mean(spreads).toFixed(2),
    luma: +mean(lumas).toFixed(1),
    curve: Object.fromEntries([...raw.curve].map(([k, v]) => [+(k * stepMs).toFixed(1), +v.toFixed(4)])),
    curveLp: Object.fromEntries([...lp.curve].map(([k, v]) => [+(k * stepMs).toFixed(1), +v.toFixed(4)])),
  };
}

function main() {
  const argv = process.argv.slice(2);
  const html = argv[0], out = argv[1];
  const flag = n => { const i = argv.indexOf(n); return i < 0 ? null : argv[i + 1]; };
  const ANIMS = M.loadAnims(html);
  const idsArg = flag('--ids');
  const ids = idsArg ? idsArg.split(',').map(Number) : ANIMS.map((_, i) => i);
  const speeds = (flag('--speeds') || '50').split(',').map(Number);
  const matched = argv.includes('--matched');

  const rows = [];
  for (const id of ids) {
    const a = ANIMS[id];
    const row = {
      id, key: a.id, name: a.name, proto: !!a.proto, webOnly: !!a.webOnly,
      speedParam: a.params[0] && a.params[0].label,
      speedDefault: a.params[0] && a.params[0].def,
      speeds: {},
    };
    for (const sp of speeds) {
      const p = a.params.map(x => x.def);
      p[0] = sp;
      const t0 = Date.now();
      const scale = matched ? 1 / rateOf(sp) : 1;
      let r;
      try { r = measure(a, p, scale); } catch (e) { r = { error: String((e && e.stack) || e) }; }
      r.ms = Date.now() - t0;
      row.speeds[sp] = r;
      process.stderr.write(
        `${String(id).padStart(2)} ${String(a.name).padEnd(14)} sp=${String(sp).padStart(3)}` +
        ` score=${r.score != null ? r.score.toFixed(4) : 'ERR'}` +
        ` thalf=${r.thalfMs == null ? '  none' : String(r.thalfMs).padStart(6)}` +
        (matched ? ` adj=${r.thalfAdjMs == null ? '  none' : String(r.thalfAdjMs).padStart(6)}` +
                   ` adjLp=${r.thalfLpAdjMs == null ? '  none' : String(r.thalfLpAdjMs).padStart(6)}` : '') +
        ` scoreLp=${r.scoreLp != null ? r.scoreLp.toFixed(4) : '-'}` +
        ` thalfLp=${r.thalfLpMs == null ? '  none' : String(r.thalfLpMs).padStart(6)}` +
        ` chg66=${r.chg66 != null ? (100 * r.chg66).toFixed(2) + '%' : '-'}` +
        ` moved=${r.stepsMoved != null ? (100 * r.stepsMoved).toFixed(0) + '%' : '-'}` +
        ` spread=${r.spread != null ? String(r.spread).padStart(6) : '-'}` +
        ` ${(r.ms / 1000).toFixed(1)}s${r.error ? ' ' + r.error : ''}\n`);
    }
    rows.push(row);
  }
  fs.writeFileSync(out, JSON.stringify({
    generated: new Date().toISOString(),
    W: M.W, H: M.H, stepMs: STEP_MS, playMs: PLAY_MS, anchorMs: ANCHOR_MS, matched,
    ladderSteps: LADDER, satFromMs: SAT_FROM_STEPS * STEP_MS, changedThreshold: CHG_THR,
    rows,
  }, null, 1));
}

main();
