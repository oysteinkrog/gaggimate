// Shared loader and pixel metrics for the motion measurement scripts.
const fs = require('fs');

const W = 480, H = 480, R2 = 240 * 240;

function loadAnims(htmlPath) {
  const src = fs.readFileSync(htmlPath, 'utf8');
  const js = src.slice(src.indexOf('<script>') + 8, src.lastIndexOf('</script>'));
  const els = {};
  const mk = () => ({ appendChild() {}, addEventListener() {}, setAttribute() {}, style: {}, classList: { toggle() {} }, value: 0, checked: false, textContent: '', innerHTML: '' });
  global.document = { getElementById: id => els[id] || (els[id] = mk()), createElement: () => mk() };
  global.performance = { now: () => Date.now() };
  global.requestAnimationFrame = () => {};
  global.setInterval = () => {};
  els.screen = { getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData() {} }) };
  const head = js.slice(0, js.indexOf('/* ================= bench harness'));
  eval(head + ';global.__ANIMS=ANIMS;');
  return global.__ANIMS;
}

const MASK = (() => {
  const idx = [];
  for (let y = 0; y < H; y++) {
    const dy = y - 239.5;
    for (let x = 0; x < W; x++) {
      const dx = x - 239.5;
      if (dx * dx + dy * dy <= R2) idx.push((y * W + x) * 4);
    }
  }
  return Int32Array.from(idx);
})();

function meanAbsDiff(a, b) {
  let s = 0;
  for (let i = 0; i < MASK.length; i++) {
    const k = MASK[i];
    s += Math.abs(a[k] - b[k]) + Math.abs(a[k + 1] - b[k + 1]) + Math.abs(a[k + 2] - b[k + 2]);
  }
  return s / MASK.length;
}

// Fraction of pixels whose summed RGB change exceeds thr.
function changedFrac(a, b, thr) {
  let n = 0;
  for (let i = 0; i < MASK.length; i++) {
    const k = MASK[i];
    const d = Math.abs(a[k] - b[k]) + Math.abs(a[k + 1] - b[k + 1]) + Math.abs(a[k + 2] - b[k + 2]);
    if (d > thr) n++;
  }
  return n / MASK.length;
}

function meanLuma(a) {
  let s = 0;
  for (let i = 0; i < MASK.length; i++) {
    const k = MASK[i];
    s += 0.3 * a[k] + 0.59 * a[k + 1] + 0.11 * a[k + 2];
  }
  return s / MASK.length;
}

// Mean absolute deviation of a frame from its own mean colour, inside the
// circle. Used as a fallback scale when two unrelated frames look alike.
function selfSpread(a) {
  let mr = 0, mg = 0, mb = 0;
  for (let i = 0; i < MASK.length; i++) {
    const k = MASK[i];
    mr += a[k]; mg += a[k + 1]; mb += a[k + 2];
  }
  mr /= MASK.length; mg /= MASK.length; mb /= MASK.length;
  let s = 0;
  for (let i = 0; i < MASK.length; i++) {
    const k = MASK[i];
    s += Math.abs(a[k] - mr) + Math.abs(a[k + 1] - mg) + Math.abs(a[k + 2] - mb);
  }
  return s / MASK.length;
}

// An 8x8 box average of the frame, 60x60 cells, circle cells only. Dither and
// fine grain average away, so a diff of two of these measures the picture's
// structure moving rather than per-pixel noise churning.
const LP = 8, LPW = W / LP, LPH = H / LP;
const LP_CELLS = (() => {
  const cells = [];
  for (let cy = 0; cy < LPH; cy++) {
    for (let cx = 0; cx < LPW; cx++) {
      const dx = cx * LP + LP / 2 - 240, dy = cy * LP + LP / 2 - 240;
      if (dx * dx + dy * dy <= (240 - LP) * (240 - LP)) cells.push(cy * LPW + cx);
    }
  }
  return Int32Array.from(cells);
})();

function lowpass(a, out) {
  const dst = out || new Float32Array(LPW * LPH * 3);
  dst.fill(0);
  for (let y = 0; y < H; y++) {
    const cy = (y / LP) | 0;
    for (let x = 0; x < W; x++) {
      const k = (y * W + x) * 4, c = (cy * LPW + ((x / LP) | 0)) * 3;
      dst[c] += a[k]; dst[c + 1] += a[k + 1]; dst[c + 2] += a[k + 2];
    }
  }
  const n = LP * LP;
  for (let i = 0; i < dst.length; i++) dst[i] /= n;
  return dst;
}

function lpDiff(a, b) {
  let s = 0;
  for (let i = 0; i < LP_CELLS.length; i++) {
    const c = LP_CELLS[i] * 3;
    s += Math.abs(a[c] - b[c]) + Math.abs(a[c + 1] - b[c + 1]) + Math.abs(a[c + 2] - b[c + 2]);
  }
  return s / LP_CELLS.length;
}

const PRIME = 66;
function render(anim, state, p, t, buf) { anim.render(buf, W, H, t, p, state); }
function primedFrame(anim, state, p, t, buf, scratch) {
  if (t >= PRIME && scratch) render(anim, state, p, t - PRIME, scratch);
  render(anim, state, p, t, buf);
}

module.exports = {
  W, H, MASK, LPW, LPH, loadAnims, meanAbsDiff, changedFrac, meanLuma, selfSpread,
  lowpass, lpDiff, render, primedFrame, PRIME,
};
