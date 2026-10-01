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
for (const mode of ['Directional', 'Rage', 'Legit', 'Off']) {
  const cfg = loadCfg(); Object.assign(cfg.misc, { bhop: true, autostrafe: mode });
  const input = { keys: new Set(['KeyW', 'Space']), down(c) { return this.keys.has(c); }, pressed() { return false; }, consumeMouse2Press() { return false; }, wheelTime: -1e9 };
  const app = { cfg, audio: noop, input, viewmodel: null, pointerActive: true, menuOpen: false };
  const g = new Game(app);
  g.load('bhop_lava', { name: 'T' });
  const p = g.player; p.yaw = 180; p.origin.x = 0; p.prevOrigin.copy(p.origin);
  let maxS = 0, maxZ = -1e9, tp = 0;
  const okt = g.kzTeleport.bind(g); g.kzTeleport = (r) => { tp++; return okt(r); };
  for (let i = 0; i < 64 * 20; i++) {
    // legit mode: simulate mouse wiggle
    g.mouseDXTick = Math.sin(i / 6) * 5;
    if (mode === 'Legit') p.yaw = 180 + Math.sin(i / 6) * 10;
    g.tick(); if (i === 64 * 3 && mode === 'Directional') console.log('kz@3s', JSON.stringify(g.kz)); maxS = Math.max(maxS, p.speed2d()); maxZ = Math.max(maxZ, p.origin.z);
  }
  console.log(mode.padEnd(12), 'maxSpeed', maxS.toFixed(0), 'furthest row', Math.floor((maxZ + 52 * 64) / 64), 'teleports', tp, 'jumpstats', JSON.stringify(g.jump.last && { d: g.jump.last.dist.toFixed(1), type: g.jump.last.type }));
}
