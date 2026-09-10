// Load anim_bench.html's script plus extra JS fragment files (each appends
// entries to ANIMS), then render a contact sheet and motion numbers.
// usage: node proto_harness.js <html> <out.ppm> <ids or 'new'> <times ms comma> <scale> frag1.js [frag2.js ...]
const fs = require('fs');
const [html, out, idsArg, timesArg, scaleArg, ...frags] = process.argv.slice(2);
const src = fs.readFileSync(html, 'utf8');
let js = src.slice(src.indexOf('<script>') + 8, src.lastIndexOf('</script>'));
const els = {};
const mk = () => ({ appendChild() {}, addEventListener() {}, setAttribute() {}, style: {}, classList: { toggle() {} }, value: 0, checked: false, textContent: '', innerHTML: '' });
global.document = { getElementById: id => els[id] || (els[id] = mk()), createElement: () => mk() };
global.performance = { now: () => Date.now() }; global.requestAnimationFrame = () => {}; global.setInterval = () => {};
els.screen = { getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData() {} }) };
const cut = js.indexOf('/* ================= bench harness');
const head = js.slice(0, cut);
let fragJs = '';
for (const f of frags) fragJs += '\n' + fs.readFileSync(f, 'utf8') + '\n';
const baseCount = (head.match(/ANIMS\.push\(/g) || []).length;
eval(head + fragJs + ';global.__ANIMS=ANIMS;');
const ANIMS = global.__ANIMS;
const ids = idsArg === 'new' ? ANIMS.map((_, i) => i).filter(i => i >= baseCount) : idsArg === 'all' ? ANIMS.map((_, i) => i) : idsArg.split(',').map(Number);
const times = timesArg.split(',').map(Number);
const sc = Number(scaleArg || 2);
const W = 480, H = 480, tw = W / sc, th = H / sc;
const cols = times.length, rows = ids.length;
const sheet = Buffer.alloc(tw * cols * th * rows * 3);
const report = [];
ids.forEach((id, r) => {
  const a = ANIMS[id]; const st = {}; a.init && a.init(W, H, st);
  const p = a.params.map(x => x.def);
  let prev = null; const diffs = []; let nan = 0; let ms = 0;
  times.forEach((t, c) => {
    const d = new Uint8ClampedArray(W * H * 4);
    const t0 = Date.now(); a.render(d, W, H, t, p, st); ms += Date.now() - t0;
    for (let k = 0; k < d.length; k += 4 * 97) if (Number.isNaN(d[k])) nan++;
    if (prev) { let s = 0; for (let k = 0; k < d.length; k += 4) s += Math.abs(d[k] - prev[k]) + Math.abs(d[k + 1] - prev[k + 1]) + Math.abs(d[k + 2] - prev[k + 2]); diffs.push(+(s / (W * H)).toFixed(2)); }
    prev = d;
    for (let y = 0; y < th; y++) for (let x = 0; x < tw; x++) {
      const si = ((y * sc) * W + x * sc) * 4, oi = ((r * th + y) * tw * cols + c * tw + x) * 3;
      sheet[oi] = d[si]; sheet[oi + 1] = d[si + 1]; sheet[oi + 2] = d[si + 2];
    }
  });
  // brightness: mean luma inside the circle
  let lum = 0, n = 0; for (let y = 0; y < H; y += 4) for (let x = 0; x < W; x += 4) { const dx = x - 240, dy = y - 240; if (dx * dx + dy * dy > 240 * 240) continue; const k = (y * W + x) * 4; lum += 0.3 * prev[k] + 0.59 * prev[k + 1] + 0.11 * prev[k + 2]; n++; }
  report.push({ id, name: a.name, meanChange: diffs, nan, msPerFrame: +(ms / times.length).toFixed(2), meanLuma: +(lum / n).toFixed(1) });
});
fs.writeFileSync(out, Buffer.concat([Buffer.from(`P6\n${tw * cols} ${th * rows}\n255\n`), sheet]));
console.log('base entries', baseCount, 'total', ANIMS.length);
console.log(JSON.stringify(report, null, 1));
