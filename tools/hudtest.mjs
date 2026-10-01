const ctxProxy = new Proxy({}, { get(t, k) {
  if (k === 'createLinearGradient' || k === 'createRadialGradient') return () => ({ addColorStop() {} });
  if (k === 'getImageData') return (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4) });
  if (k === 'measureText') return () => ({ width: 10 });
  if (typeof k === 'string' && /^[a-z]/.test(k)) return () => {};
}, set() { return true; } });
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctxProxy, style: {} }) };
globalThis.window = globalThis; globalThis.innerWidth = 1280; globalThis.innerHeight = 720; globalThis.devicePixelRatio = 1;
globalThis.localStorage = { _d: {}, getItem(k) { return this._d[k] ?? null; }, setItem(k, v) { this._d[k] = String(v); } };
const THREE = await import('three');
const { Game } = await import('../src/game.js');
const { HUD } = await import('../src/hud.js');
const { Viewmodel } = await import('../src/viewmodel.js');
const { loadCfg } = await import('../src/config.js');
const noop = new Proxy({}, { get: () => () => {} });
const cfg = loadCfg();
for (const k of Object.keys(cfg.visuals)) if (typeof cfg.visuals[k] === 'boolean') cfg.visuals[k] = true;
Object.assign(cfg.rage, { enabled: true, doubletap: true }); Object.assign(cfg.aa, { enabled: true }); cfg.fakelag.enabled = true; cfg.legit.aimbot = true;
const input = { keys: new Set(['Tab', 'KeyW']), down(c) { return this.keys.has(c); }, pressed() { return false; }, consumeMouse2Press() { return false; }, wheelTime: -1e9 };
const vm = new Viewmodel(cfg);
const app = { cfg, audio: noop, input, viewmodel: vm, pointerActive: true, menuOpen: false };
const hud = new HUD({ getContext: () => ctxProxy, style: {} });
for (const map of ['de_dust', 'bhop_lava']) {
  const g = new Game(app); app.game = g;
  g.load(map, { bots: 6, teammates: 2, difficulty: 'Normal', name: 'T' });
  vm.setWeapon(g.player.weaponId, true); g.applySkins();
  for (let i = 0; i < 64 * 30; i++) {
    g.tick();
    if (i % 4 === 0) {
      g.camera.position.copy(g.player.eye(new THREE.Vector3())); g.camera.rotation.set(-g.player.pitch * Math.PI / 180, g.player.yaw * Math.PI / 180, 0);
      g.camera.updateMatrixWorld();
      hud.draw(g, app, 1 / 60);
      vm.update(1 / 60, { mouseDX: 3, mouseDY: 1, speed: g.player.speed2d(), onGround: g.player.onGround, ducked: false, visible: true, inspectT: 1, time: g.time });
    }
    if (i === 300) { g.player.ws.scoped = 1; }
    if (i === 600) g.playerSwitch(2);
    if (i === 900) g.playerSwitch(1);
  }
  console.log(map, 'ok. kills', g.player.kills, 'deaths', g.player.deaths);
}
