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
const cfg = loadCfg(); cfg.rage.enabled = true;
const input = { keys: new Set(), down(c) { return this.keys.has(c); }, pressed() { return false; }, consumeMouse2Press() { return false; }, wheelTime: -1e9 };
const app = { cfg, audio: noop, input, viewmodel: null, pointerActive: true, menuOpen: false };
const g = new Game(app);
g.load('aim_redline', { bots: 5, teammates: 0, difficulty: 'Hard', name: 'T' });
let tgt = 0, hcok = 0, shots = 0, hits = 0, hsum = 0, alive = 0;
const of = g.fireWeapon.bind(g); g.fireWeapon = (a, y, p) => { if (a === g.player) shots++; return of(a, y, p); };
const od = g.applyDamage.bind(g); g.applyDamage = (v, a, ...r) => { if (a === g.player) hits++; return od(v, a, ...r); };
for (let i = 0; i < 64 * 60; i++) { g.tick(); if (g.player.alive) alive++; if (g.rageTarget) { tgt++; hsum += g.rageTarget.hitchance; if (g.rageTarget.hitchance >= cfg.rage.hitchance) hcok++; } }
console.log({ alive, tgt, hcok, avgHc: (hsum / tgt).toFixed(1), shots, hits, kills: g.player.kills, deaths: g.player.deaths });
