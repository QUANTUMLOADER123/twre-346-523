export class Input {
  constructor() {
    this.keys = new Set();
    this.pressedSet = new Set();
    this.mouse1 = false; this.mouse2 = false; this.mouse2Press = false;
    this.wheelTime = -1e9; this.wheelSwitch = 0;
    this.wheelMode = 'jump';
    window.addEventListener('keydown', (e) => {
      if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT')) return;
      if (!e.repeat) this.pressedSet.add(e.code);
      this.keys.add(e.code);
      if (['Space', 'Tab', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Insert', 'Home'].includes(e.code) || (e.ctrlKey && e.code !== 'KeyR')) e.preventDefault();
    });
    window.addEventListener('keyup', (e) => { this.keys.delete(e.code); });
    window.addEventListener('blur', () => { this.keys.clear(); this.mouse1 = this.mouse2 = false; });
  }
  bindCanvas(el) {
    el.addEventListener('mousedown', (e) => {
      if (e.button === 0) this.mouse1 = true;
      if (e.button === 2) { this.mouse2 = true; this.mouse2Press = true; }
    });
    window.addEventListener('mouseup', (e) => {
      if (e.button === 0) this.mouse1 = false;
      if (e.button === 2) this.mouse2 = false;
    });
    el.addEventListener('contextmenu', (e) => e.preventDefault());
    window.addEventListener('wheel', (e) => {
      if (!document.pointerLockElement && !this.allowWheel) return;
      this.wheelTime = performance.now();
    }, { passive: true });
  }
  down(code) { return this.keys.has(code); }
  pressed(code) { if (this.pressedSet.has(code)) { this.pressedSet.delete(code); return true; } return false; }
  consumeMouse2Press() { const v = this.mouse2Press; this.mouse2Press = false; return v; }
  endFrame() { /* pressed keys survive until consumed by a tick, but drop stale ones */ if (this.pressedSet.size > 16) this.pressedSet.clear(); }
}
