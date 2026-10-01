const ctxProxy = new Proxy({}, { get(t, k) {
  if (k === 'createLinearGradient' || k === 'createRadialGradient') return () => ({ addColorStop() {} });
  if (k === 'getImageData') return (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4) });
  if (k === 'measureText') return () => ({ width: 10 });
  if (typeof k === 'string' && /^[a-z]/.test(k)) return () => {};
}, set() { return true; } });
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctxProxy, style: {} }) };
globalThis.window = globalThis; globalThis.innerWidth = 1280; globalThis.innerHeight = 720;
globalThis.localStorage = { _d: {}, getItem(k) { return this._d[k] ?? null; }, setItem(k, v) { this._d[k] = String(v); } };
const { Game } = await import('../src/game.js');
const { loadCfg } = await import('../src/config.js');
const noop = new Proxy({}, { get: () => () => {} });
function run(label, mod, keys = [], secs = 120, map = 'aim_redline') {
  let tot = { k: 0, d: 0, hsTaken: 0, hitsTaken: 0 };
  for (let rep = 0; rep < 3; rep++) {
    const cfg = loadCfg(); mod(cfg);
    cfg.misc.godmode = false;
    const input = { keys: new Set(keys), down(c) { return this.keys.has(c); }, pressed() { return false; }, consumeMouse2Press() { return false; }, wheelTime: -1e9 };
    const app = { cfg, audio: noop, input, viewmodel: null, pointerActive: true, menuOpen: false };
    const g = new Game(app);
    g.load(map, { bots: 5, teammates: 0, difficulty: 'Hard', name: 'T' });
    const od = g.applyDamage.bind(g);
    g.applyDamage = (v, a, d, grp, ...r) => { if (v === g.player) { tot.hitsTaken++; if (grp === 'head') tot.hsTaken++; } return od(v, a, d, grp, ...r); };
    for (let i = 0; i < secs * 64; i++) g.tick();
    tot.k += g.player.kills; tot.d += g.player.deaths;
  }
  console.log(label.padEnd(34), `K ${tot.k}  D ${tot.d}  hits taken ${tot.hitsTaken} (head ${tot.hsTaken}, ${(tot.hsTaken / Math.max(1, tot.hitsTaken) * 100).toFixed(0)}%)`);
}
run('no cheats (afk)', c => {});
run('rage only', c => { c.rage.enabled = true; });
run('rage + DT + norecoil', c => { c.rage.enabled = true; c.rage.doubletap = true; c.misc.norecoil = true; });
run('AA backward+desync (afk)', c => { Object.assign(c.aa, { enabled: true }); });
run('AA spin + jitter desync (afk)', c => { Object.assign(c.aa, { enabled: true, yaw: 'Spin', desync: 'Jitter' }); });
run('rage + AA + fakelag', c => { c.rage.enabled = true; Object.assign(c.aa, { enabled: true, yaw: 'Spin', desync: 'Jitter' }); c.fakelag.enabled = true; });
run('legit aim+trigger', c => { Object.assign(c.legit, { aimbot: true, activation: 'Always', fov: 30, smooth: 4, trigger: true, rcs: true }); });
