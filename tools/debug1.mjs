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
const cfg = loadCfg();
const input = { keys: new Set(), down(c) { return this.keys.has(c); }, pressed() { return false; }, consumeMouse2Press() { return false; }, wheelTime: -1e9 };
const app = { cfg, audio: noop, input, viewmodel: null, pointerActive: true, menuOpen: false };
const g = new Game(app);
g.load('aim_redline', { bots: 3, teammates: 3, difficulty: 'Hard', name: 'T' });
let shots = 0, hits = 0;
const of = g.fireWeapon.bind(g); g.fireWeapon = (a, y, p) => { shots++; return of(a, y, p); };
const od = g.applyDamage.bind(g); g.applyDamage = (...args) => { hits++; return od(...args); };
for (let i = 0; i < 64 * 10; i++) {
  g.tick();
  if (i % 64 === 0) {
    const b = g.actors[1];
    console.log(i/64, 'bot1', b.team, b.origin.toArray().map(v=>v.toFixed(0)).join(','), 'target', b.ai.target?.name, 'yaw', b.yaw.toFixed(0), 'pitch', b.pitch.toFixed(1), 'path', b.ai.path.length, b.ai.pathIdx, 'shots', shots, 'hits', hits);
  }
}
const b = g.actors.find(a=>a.isBot && a.ai.target);
if (b) {
  const T = await import('three');
  const eye = b.eye(new T.Vector3()); const t = b.ai.target;
  console.log('vis head', g.world.visible(eye, t.model.hitboxes[0].wa), 'eye', eye.toArray(), 'thead', t.model.hitboxes[0].wa.toArray(), 'torigin', t.origin.toArray());
}
