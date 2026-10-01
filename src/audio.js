// Procedural WebAudio sound effects (no audio files)
export class Audio {
  constructor() {
    this.ctx = null; this.master = null; this.volume = 0.7;
    this.listener = { x: 0, y: 0, z: 0, yaw: 0 };
  }
  init() {
    if (this.ctx) { if (this.ctx.state === 'suspended') this.ctx.resume(); return; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    this.ctx = new AC();
    this.master = this.ctx.createGain(); this.master.gain.value = this.volume;
    const comp = this.ctx.createDynamicsCompressor();
    this.master.connect(comp); comp.connect(this.ctx.destination);
    const len = this.ctx.sampleRate * 1.0;
    this.noise = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
    const d = this.noise.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
  }
  setVolume(v) { this.volume = v; if (this.master) this.master.gain.value = v; }

  // returns a destination node with distance attenuation/pan for a world position
  _dest(pos, vol = 1) {
    const ctx = this.ctx;
    const g = ctx.createGain();
    let gain = vol;
    if (pos) {
      const dx = pos.x - this.listener.x, dz = pos.z - this.listener.z, dy = pos.y - this.listener.y;
      const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
      gain *= 1 / (1 + dist / 400);
      if (ctx.createStereoPanner) {
        const p = ctx.createStereoPanner();
        const yaw = this.listener.yaw;
        const rx = Math.cos(yaw), rz = -Math.sin(yaw);
        p.pan.value = dist > 1 ? Math.max(-1, Math.min(1, (dx * rx + dz * rz) / dist)) : 0;
        g.connect(p); p.connect(this.master);
      } else g.connect(this.master);
    } else g.connect(this.master);
    g.gain.value = gain;
    return g;
  }
  _noise(dest, t0, dur, freq, q = 1, type = 'lowpass', peak = 1, attack = 0.002) {
    const ctx = this.ctx;
    const src = ctx.createBufferSource(); src.buffer = this.noise;
    src.playbackRate.value = 0.7 + Math.random() * 0.6;
    const f = ctx.createBiquadFilter(); f.type = type; f.frequency.value = freq; f.Q.value = q;
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t0); g.gain.exponentialRampToValueAtTime(peak, t0 + attack);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    src.connect(f); f.connect(g); g.connect(dest);
    src.start(t0, Math.random() * 0.5); src.stop(t0 + dur + 0.05);
  }
  _tone(dest, t0, dur, f0, f1, type = 'sine', peak = 0.5) {
    const ctx = this.ctx;
    const o = ctx.createOscillator(); o.type = type;
    o.frequency.setValueAtTime(f0, t0); o.frequency.exponentialRampToValueAtTime(Math.max(1, f1), t0 + dur);
    const g = ctx.createGain();
    g.gain.setValueAtTime(peak, t0); g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    o.connect(g); g.connect(dest); o.start(t0); o.stop(t0 + dur + 0.05);
  }

  shot(kind, pos, local) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    const d = this._dest(local ? null : pos, local ? 0.55 : 0.9);
    switch (kind) {
      case 'rifle':
        this._noise(d, t, 0.25, 2600, 0.7, 'lowpass', 1.0);
        this._noise(d, t, 0.09, 6000, 0.5, 'highpass', 0.35);
        this._tone(d, t, 0.12, 160, 45, 'triangle', 0.9);
        break;
      case 'rifle2':
        this._noise(d, t, 0.2, 3200, 0.7, 'lowpass', 0.9);
        this._noise(d, t, 0.07, 7000, 0.5, 'highpass', 0.3);
        this._tone(d, t, 0.1, 190, 55, 'triangle', 0.7);
        break;
      case 'awp':
        this._noise(d, t, 0.7, 1800, 0.6, 'lowpass', 1.2);
        this._noise(d, t, 0.15, 5000, 0.4, 'highpass', 0.5);
        this._tone(d, t, 0.35, 110, 30, 'sawtooth', 0.6);
        this._noise(d, t + 0.45, 0.12, 3000, 3, 'bandpass', 0.25); // bolt
        this._noise(d, t + 0.75, 0.1, 2500, 3, 'bandpass', 0.25);
        break;
      case 'scout':
        this._noise(d, t, 0.45, 2400, 0.7, 'lowpass', 1.0);
        this._tone(d, t, 0.2, 140, 40, 'triangle', 0.6);
        this._noise(d, t + 0.4, 0.1, 3000, 3, 'bandpass', 0.2);
        break;
      case 'deagle':
        this._noise(d, t, 0.35, 2200, 0.7, 'lowpass', 1.1);
        this._tone(d, t, 0.18, 130, 35, 'triangle', 1.0);
        break;
      case 'knife':
        this._noise(d, t, 0.15, 1800, 2, 'bandpass', 0.3, 0.04);
        break;
    }
  }
  knifeHit(pos) { if (!this.ctx) return; const t = this.ctx.currentTime; const d = this._dest(pos, 0.8); this._noise(d, t, 0.12, 900, 2, 'bandpass', 0.8); }
  footstep(pos, local, loud = 1) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    const d = this._dest(local ? null : pos, (local ? 0.12 : 0.35) * loud);
    this._noise(d, t, 0.08, 500 + Math.random() * 300, 1.5, 'bandpass', 0.8);
    this._noise(d, t + 0.01, 0.05, 2500, 1, 'highpass', 0.2);
  }
  land(local, pos) { if (!this.ctx) return; const t = this.ctx.currentTime; const d = this._dest(local ? null : pos, local ? 0.25 : 0.4); this._noise(d, t, 0.14, 350, 1, 'lowpass', 0.9); }
  jump() { }
  hitsound(type) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime; const d = this._dest(null, 0.6);
    if (type === 'Skeet') { this._tone(d, t, 0.09, 1900, 1500, 'sine', 0.5); this._tone(d, t, 0.06, 3800, 3000, 'sine', 0.15); }
    else if (type === 'Bell') { this._tone(d, t, 0.4, 2600, 2550, 'sine', 0.4); this._tone(d, t, 0.3, 5200, 5100, 'sine', 0.1); }
    else if (type === 'Bubble') { this._tone(d, t, 0.08, 400, 1600, 'sine', 0.5); }
    else if (type === 'Click') { this._noise(d, t, 0.03, 4000, 3, 'bandpass', 0.8); }
  }
  headshot(local) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime; const d = this._dest(null, local ? 0.45 : 0.2);
    this._tone(d, t, 0.25, 3400, 3200, 'square', 0.07); this._tone(d, t, 0.3, 2100, 2000, 'sine', 0.35);
    this._noise(d, t, 0.05, 6000, 2, 'bandpass', 0.4);
  }
  hurt() { if (!this.ctx) return; const t = this.ctx.currentTime; const d = this._dest(null, 0.4); this._noise(d, t, 0.12, 700, 1, 'lowpass', 0.7); this._tone(d, t, 0.12, 140, 70, 'sine', 0.4); }
  reload(pos, local) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime; const d = this._dest(local ? null : pos, local ? 0.3 : 0.3);
    this._noise(d, t + 0.25, 0.06, 2200, 4, 'bandpass', 0.6);
    this._noise(d, t + 0.9, 0.08, 1600, 4, 'bandpass', 0.8);
    this._noise(d, t + 1.6, 0.07, 2800, 5, 'bandpass', 0.7);
  }
  empty() { if (!this.ctx) return; const t = this.ctx.currentTime; const d = this._dest(null, 0.3); this._noise(d, t, 0.04, 3000, 6, 'bandpass', 0.8); }
  ui(kind = 'click') {
    if (!this.ctx) return;
    const t = this.ctx.currentTime; const d = this._dest(null, 0.25);
    if (kind === 'click') this._tone(d, t, 0.05, 1200, 900, 'sine', 0.3);
    else if (kind === 'open') { this._tone(d, t, 0.08, 600, 1200, 'sine', 0.3); }
    else if (kind === 'close') { this._tone(d, t, 0.08, 1200, 600, 'sine', 0.3); }
    else if (kind === 'finish') { [0, 0.1, 0.2].forEach((o, i) => this._tone(d, t + o, 0.25, 600 + i * 300, 600 + i * 300, 'triangle', 0.3)); }
    else if (kind === 'cp') { this._tone(d, t, 0.15, 900, 1300, 'triangle', 0.3); }
  }
}
