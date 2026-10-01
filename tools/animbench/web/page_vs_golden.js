// page_vs_golden.js: does the firmware draw the page's design?
//
// The goldens under tools/animbench/golden are rendered by the firmware code,
// so `make check` can only prove the firmware still draws what it drew
// yesterday. It can never notice that the port drifted away from the page
// entry in anim_bench.html, which is the design the owner approved. Four ports
// had drifted badly before anyone looked (gm-pciz): Nebula by 225,600 pixels a
// frame out of 230,400, Mandala by 174,000, Lava by up to 69,615, and Ember by
// every pixel of every frame at a mean of about 60 per channel, which on the
// device is a materially darker animation than the design.
//
// This is the check that catches it. It plays the page entry the same way
// bench.cpp plays the firmware, frame by frame from t = 1000 in 33 ms steps,
// captures frames 30, 120 and 210, quantises both sides to RGB565 because that
// is what the panel stores, and counts differing pixels.
//
// Playback matters. Several entries carry state across render calls, so
// rendering at 4960 ms without the 119 frames before it does not give the
// picture the firmware has at frame 120.
//
// The warm-up frame is part of the playback, because the goldens were made
// with it. bench.cpp renders one frame at t = 0 before its timed loop, to
// build lazy tables outside the timed region, so an animation that builds
// its picture on the first frame it is given builds it at t = 0 on the
// firmware side. The page is played the same way here. Without it the tool
// reported a harness artefact as a design gap: Steam, whose wisps are born
// at the time of its first frame, read 16,724 differing pixels at a mean of
// 2.19 per channel, and reads 0 at all three frames with the warm-up
// (gm-pciz, 2026-09-13). Adding it moved no other animation by a single
// pixel. The device has no warm-up frame, and neither does it need one:
// there the firmware and the page both start at the same clock.
//
// usage:
//   node page_vs_golden.js [--ids 1,2] [--json out.json] [--frames 30,120,210]
//
// Exit code is 1 when an animation differs by more than its allowance in
// page_exact.json, 0 otherwise.

const fs = require('fs');
const path = require('path');

const HERE = __dirname;
const HTML = path.join(HERE, 'anim_bench.html');
const GOLDEN = path.join(HERE, '..', 'golden');
const ALLOW = path.join(HERE, '..', 'page_exact.json');

const W = 480, H = 480;
const START_MS = 1000, FRAME_MS = 33;

function loadAnims(htmlPath) {
  const src = fs.readFileSync(htmlPath, 'utf8');
  const js = src.slice(src.indexOf('<script>') + 8, src.lastIndexOf('</script>'));
  const els = {};
  const mk = () => ({
    appendChild() {}, addEventListener() {}, setAttribute() {}, style: {},
    classList: { toggle() {} }, value: 0, checked: false, textContent: '', innerHTML: '',
  });
  global.document = { getElementById: id => els[id] || (els[id] = mk()), createElement: () => mk() };
  global.performance = { now: () => Date.now() };
  global.requestAnimationFrame = () => {};
  global.setInterval = () => {};
  els.screen = {
    getContext: () => ({
      createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }),
      putImageData() {},
    }),
  };
  const cut = js.indexOf('/* ================= bench harness');
  // eslint-disable-next-line no-eval
  eval(js.slice(0, cut) + ';global.__ANIMS=ANIMS;');
  return global.__ANIMS;
}

function readPpm(file) {
  const buf = fs.readFileSync(file);
  if (buf.slice(0, 2).toString() !== 'P6') throw new Error(file + ': not a P6 ppm');
  // header is "P6\n<w> <h>\n<max>\n"; walk three whitespace separated fields
  let i = 2, fields = [];
  while (fields.length < 3) {
    while (i < buf.length && /\s/.test(String.fromCharCode(buf[i]))) i++;
    let j = i;
    while (j < buf.length && !/\s/.test(String.fromCharCode(buf[j]))) j++;
    fields.push(Number(buf.slice(i, j).toString()));
    i = j;
  }
  i++; // the single whitespace byte after maxval
  const [w, h, max] = fields;
  if (w !== W || h !== H || max !== 255) throw new Error(file + ': unexpected ' + w + 'x' + h + ' max ' + max);
  return buf.slice(i, i + w * h * 3);
}

// What the panel actually stores. Compare there, not in 8 bit, so a one LSB
// float difference that RGB565 throws away does not read as a divergence.
const q565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

function compare(rgba, ppm) {
  let diff = 0, sumAbs = 0, maxAbs = 0;
  for (let p = 0; p < W * H; p++) {
    const a = rgba[p * 4], b = rgba[p * 4 + 1], c = rgba[p * 4 + 2];
    const d = ppm[p * 3], e = ppm[p * 3 + 1], f = ppm[p * 3 + 2];
    if (q565(a, b, c) !== q565(d, e, f)) diff++;
    for (const [x, y] of [[a, d], [b, e], [c, f]]) {
      const k = Math.abs(x - y);
      sumAbs += k;
      if (k > maxAbs) maxAbs = k;
    }
  }
  return { diff, mean: sumAbs / (W * H * 3), max: maxAbs };
}

function main() {
  const argv = process.argv.slice(2);
  const flag = n => { const i = argv.indexOf(n); return i < 0 ? null : argv[i + 1]; };
  const frames = (flag('--frames') || '30,120,210').split(',').map(Number);
  const anims = loadAnims(HTML);
  const idsArg = flag('--ids');
  const ids = idsArg ? idsArg.split(',').map(Number) : anims.map((_, i) => i);
  const allow = fs.existsSync(ALLOW) ? JSON.parse(fs.readFileSync(ALLOW, 'utf8')) : { default: 0, animations: {} };
  const defAllow = allow.default == null ? 0 : allow.default;

  const last = Math.max(...frames);
  const rows = [];
  let failed = 0;

  for (const id of ids) {
    const a = anims[id];
    if (!a) { console.log('id ' + id + ': no such animation'); failed++; continue; }
    const state = {};
    if (a.init) a.init(W, H, state);
    const p = a.params.map(x => x.def);
    const buf = new Uint8ClampedArray(W * H * 4);
    const got = new Map();
    a.render(buf, W, H, 0, p, state); // bench.cpp's warm-up frame, see the header
    for (let i = 0; i <= last; i++) {
      a.render(buf, W, H, START_MS + i * FRAME_MS, p, state);
      if (frames.includes(i)) got.set(i, buf.slice());
    }
    const cap = allow.animations && allow.animations[a.id] != null
      ? allow.animations[a.id] : defAllow;
    const per = [];
    let worst = 0, missing = false;
    for (const f of frames) {
      const file = path.join(GOLDEN, a.id + '-' + String(f).padStart(3, '0') + '.ppm');
      if (!fs.existsSync(file)) { missing = true; per.push({ frame: f, missing: true }); continue; }
      const r = compare(got.get(f), readPpm(file));
      per.push({ frame: f, diff: r.diff, mean: +r.mean.toFixed(3), max: r.max });
      if (r.diff > worst) worst = r.diff;
    }
    const ok = !missing && worst <= cap;
    if (!ok) failed++;
    rows.push({ id, key: a.id, name: a.name, allowance: cap, worstDiff: worst, missing, frames: per, ok });
    const shown = per.map(x => x.missing ? 'f' + x.frame + ' NO GOLDEN'
      : 'f' + x.frame + ' ' + x.diff + 'px mean ' + x.mean.toFixed(2)).join('  ');
    console.log(
      String(id).padStart(3) + ' ' + String(a.name).padEnd(14) +
      (ok ? 'OK   ' : 'DIFF ') + shown +
      (cap > 0 ? '   (allowed ' + cap + ')' : ''));
  }

  const exact = rows.filter(r => !r.missing && r.worstDiff === 0).length;
  const debt = rows.length - exact - failed;
  console.log('');
  console.log(exact + ' of ' + rows.length + ' exact, ' + debt +
    ' within a recorded allowance, ' + failed + ' over');
  console.log(failed === 0 ? 'ALL OK' : 'FAILED');

  const outJson = flag('--json');
  if (outJson) {
    fs.writeFileSync(outJson, JSON.stringify({
      generated: new Date().toISOString(), W, H, startMs: START_MS, frameMs: FRAME_MS,
      frames, quantised: 'rgb565', rows,
    }, null, 1));
  }
  process.exit(failed === 0 ? 0 : 1);
}

main();
