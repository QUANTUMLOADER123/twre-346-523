// Runs the full App (main.js) in node with a fake DOM + fake WebGL renderer to catch runtime errors.
import fs from 'fs';
const ctxProxy = new Proxy({}, { get(t, k) {
  if (k === 'createLinearGradient' || k === 'createRadialGradient' || k === 'createPattern') return () => ({ addColorStop() {} });
  if (k === 'getImageData') return (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4) });
  if (k === 'measureText') return () => ({ width: 10 });
  if (typeof k === 'string' && /^[a-z]/.test(k)) return () => {};
}, set() { return true; } });
const els = {};
function fakeEl(tag = 'div', id) {
  const e = {
    tagName: tag.toUpperCase(), id, children: [], style: { setProperty() {} }, dataset: {}, _cls: new Set(), value: '', textContent: '', innerHTML: '', width: 300, height: 150,
    classList: { add: (...c) => c.forEach(x => e._cls.add(x)), remove: (...c) => c.forEach(x => e._cls.delete(x)), toggle: (c, v) => { v = v ?? !e._cls.has(c); v ? e._cls.add(c) : e._cls.delete(c); return v; }, contains: c => e._cls.has(c) },
    appendChild(c) { this.children.push(c); c.parentNode = this; return c; }, append(...c) { c.forEach(x => this.appendChild(typeof x === 'string' ? fakeEl('span') : x)); },
    removeChild(c) { this.children = this.children.filter(x => x !== c); }, remove() {}, prepend(c) { this.children.unshift(c); },
    addEventListener(t, f) { (this._l ||= {})[t] = f; }, removeEventListener() {}, setAttribute(k, v) { this[k] = v; }, getAttribute(k) { return this[k]; },
    querySelector: () => fakeEl(), querySelectorAll: () => [], getContext: () => ctxProxy, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 20 }),
    requestPointerLock() { return Promise.reject(new Error('no')); }, focus() {}, blur() {}, click() { this.onclick?.({ stopPropagation() {}, target: this }); },
    insertBefore(c) { this.children.push(c); }, contains: () => false, closest: () => null,
  };
  if (id) els[id] = e;
  return e;
}
globalThis.window = globalThis; globalThis.innerWidth = 1280; globalThis.innerHeight = 720; globalThis.devicePixelRatio = 1;
const winL = {}; globalThis.addEventListener = (t, f) => { (winL[t] ||= []).push(f); }; globalThis.removeEventListener = () => {};
const docL = {};
globalThis.document = {
  documentElement: { style: { setProperty() {} } }, createElement: (t) => fakeEl(t), getElementById: (id) => els[id] || fakeEl('div', id), body: fakeEl('body'),
  addEventListener: (t, f) => { (docL[t] ||= []).push(f); }, removeEventListener() {}, querySelectorAll: () => [], querySelector: () => fakeEl(),
  exitPointerLock() {}, pointerLockElement: null, hidden: false,
};
globalThis.localStorage = { _d: {}, getItem(k) { return this._d[k] ?? null; }, setItem(k, v) { this._d[k] = String(v); }, removeItem(k) { delete this._d[k]; } };
let rafCb = null; globalThis.requestAnimationFrame = (f) => { rafCb = f; };
globalThis.performance = performance;
Object.defineProperty(globalThis, 'navigator', { value: { clipboard: { writeText: async () => {}, readText: async () => '{}' } }, configurable: true });
globalThis.FakeRenderer = class { constructor() { this.shadowMap = {}; this.domElement = fakeEl('canvas'); } setPixelRatio() {} setSize() {} clear() {} clearDepth() {} render(scene, cam) { scene.updateMatrixWorld?.(); } setClearColor() {} getPixelRatio() { return 1; } setScissorTest() {} setViewport() {} };
let src = fs.readFileSync(new URL('../src/main.js', import.meta.url), 'utf8');
src = src.replace('new THREE.WebGLRenderer(', 'new globalThis.FakeRenderer(').replace('window.app = new App();', 'export { App };');
fs.writeFileSync(new URL('../src/_apptest_main.mjs', import.meta.url), src);
try {
  const { App } = await import('../src/_apptest_main.mjs');
  const app = new App();
  let t = 0; const frame = (n = 1) => { for (let i = 0; i < n; i++) { t += 16.7; rafCb(t); } };
  frame(5);
  for (const map of ['de_dust', 'de_warehouse', 'aim_redline', 'bhop_lava']) {
    app.selectedMap = map;
    els['opt-bots'].value = 4; els['opt-team'].value = 2; els['opt-diff'].value = 'Normal'; els['opt-primary'].value = 'ak47'; els['opt-sens'].value = 2; els['opt-name'].value = 'Tester';
    app.startGame();
    await new Promise(r => setTimeout(r, 60));
    if (!app.game) throw new Error('game not started');
    frame(120);
    // menu open & enable every boolean in every section, render frames
    app.showMenu(true); frame(5);
    for (const sec of Object.keys(app.cfg)) for (const k of Object.keys(app.cfg[sec])) if (typeof app.cfg[sec][k] === 'boolean') app.cfg[sec][k] = true;
    app.cfg.misc.godmode = false; app.cfg.visuals.asus = 50; app.cfg.visuals.nightmode = true;
    app.onCfg('all'); app.menu.build?.();
    app.showMenu(false); frame(200);
    app.cfg.visuals.thirdperson = false; frame(60);
    app.showBuy(true); app.buy('awp'); frame(30);
    app.game.player.ws.scoped = 1; frame(30);
    // simulate keydowns
    for (const code of ['KeyV', 'KeyX', 'ArrowLeft', 'ArrowRight', 'KeyB', 'Digit2']) winL.keydown.forEach(f => f({ code, target: {}, preventDefault() {}, repeat: false }));
    frame(60);
    console.log(map, 'OK  player K/D', app.game.player.kills, app.game.player.deaths, 'fov', app.game.camera.fov.toFixed(1));
    app.quitGame();
    // reset cfg
    localStorage._d = {};
    app.cfg = (await import('../src/config.js')).loadCfg();
    frame(3);
  }
} finally {
  fs.unlinkSync(new URL('../src/_apptest_main.mjs', import.meta.url));
}
