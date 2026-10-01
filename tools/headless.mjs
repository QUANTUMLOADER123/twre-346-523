// Headless smoke test: runs the real game simulation with DOM stubs
const ctxProxy = new Proxy({}, { get(t, k) {
  if (k === 'createLinearGradient' || k === 'createRadialGradient') return () => ({ addColorStop() {} });
  if (k === 'getImageData') return (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4) });
  if (k === 'measureText') return () => ({ width: 10 });
  if (typeof k === 'string' && /^[a-z]/.test(k)) return () => {};
  return undefined;
}, set() { return true; } });
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctxProxy, style: {} }) };
globalThis.window = globalThis; globalThis.innerWidth = 1280; globalThis.innerHeight = 720;
globalThis.localStorage = { _d: {}, getItem(k) { return this._d[k] ?? null; }, setItem(k, v) { this._d[k] = String(v); } };

const { Game } = await import('../src/game.js');
const { loadCfg } = await import('../src/config.js');
const noop = new Proxy({}, { get: () => () => {} });

function run(mapId, cheatsOn, seconds, diff = 'Normal') {
  const cfg = loadCfg();
  if (cheatsOn) {
    Object.assign(cfg.rage, { enabled: true, doubletap: true });
    Object.assign(cfg.aa, { enabled: true, yaw: 'Spin', desync: 'Jitter' });
    Object.assign(cfg.fakelag, { enabled: true, mode: 'Adaptive' });
    Object.assign(cfg.misc, { bhop: true, autostrafe: 'Rage', edgejump: true });
    Object.assign(cfg.legit, { trigger: true, rcs: true });
  }
  const input = { keys: new Set(cheatsOn ? ['KeyW', 'Space'] : []), down(c) { return this.keys.has(c); }, pressed() { return false; }, consumeMouse2Press() { return false; }, wheelTime: -1e9, mouse1: false, mouse2: false };
  const app = { cfg, audio: noop, input, viewmodel: null, pointerActive: true, menuOpen: false, chatOpen: false, buyOpen: false };
  const g = new Game(app);
  g.load(mapId, { bots: 6, teammates: 2, difficulty: diff, name: 'Tester' });
  let maxSpeed = 0, shotsByBots = 0;
  const t0 = Date.now();
  for (let i = 0; i < seconds * 64; i++) {
    // player wanders: turn slowly
    g.player.yaw += 0.6;
    g.tick();
    maxSpeed = Math.max(maxSpeed, g.player.speed2d());
  }
  const ms = Date.now() - t0;
  const bots = g.actors.filter(a => a.isBot);
  const moved = bots.map(b => b.kills + '/' + b.deaths).join(' ');
  console.log(`${mapId} cheats=${cheatsOn} ${seconds}s sim in ${ms}ms (${(ms / (seconds * 64)).toFixed(2)}ms/tick) | player K/D ${g.player.kills}/${g.player.deaths} | score CT ${g.score.CT} T ${g.score.T} | bots K/D ${moved} | maxSpeed ${maxSpeed.toFixed(0)} | killfeed total ${g.killfeed.length}`);
  return g;
}
run('de_dust', false, 120);
run('de_dust', true, 120);
run('de_warehouse', true, 90, 'Expert');
run('aim_redline', false, 60, 'Hard');
const k = run('bhop_lava', true, 30);
console.log('kz', JSON.stringify(k.kz), 'pos', k.player.origin.toArray().map(v => v.toFixed(0)));
