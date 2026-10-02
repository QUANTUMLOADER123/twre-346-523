// Procedural Web Audio API Sound Engine for CS:GO Weapons & HvH Hitsounds

class SoundEngine {
  constructor() {
    this.ctx = null;
    this.masterGain = null;
    this.enabled = true;
  }

  init() {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') {
        this.ctx.resume().catch(() => {});
      }
      return;
    }
    try {
      const AudioContext = window.AudioContext || window.webkitAudioContext;
      this.ctx = new AudioContext();
      this.masterGain = this.ctx.createGain();
      this.masterGain.gain.value = 0.35;
      this.masterGain.connect(this.ctx.destination);
    } catch (e) {
      this.enabled = false;
    }
  }

  createNoiseBuffer(duration = 0.25) {
    const sampleRate = this.ctx.sampleRate;
    const bufferSize = Math.floor(sampleRate * duration);
    const buffer = this.ctx.createBuffer(1, bufferSize, sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < bufferSize; i++) {
      data[i] = Math.random() * 2 - 1;
    }
    return buffer;
  }

  playWeaponFire(weaponId, isEnemy = false) {
    this.init();
    if (!this.ctx || !this.enabled) return;

    const now = this.ctx.currentTime;
    const volScale = isEnemy ? 0.35 : 0.85;

    if (weaponId === 'knife') {
      // Crisp metallic Karambit slash
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(1400, now);
      osc.frequency.exponentialRampToValueAtTime(320, now + 0.09);
      gain.gain.setValueAtTime(0.25 * volScale, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.09);
      osc.connect(gain);
      gain.connect(this.masterGain);
      osc.start(now);
      osc.stop(now + 0.09);
      return;
    }

    // Gun noise burst + low punch oscillator
    const duration = weaponId === 'awp' ? 0.38 : weaponId === 'ssg08' ? 0.24 : 0.16;
    const noise = this.ctx.createBufferSource();
    noise.buffer = this.createNoiseBuffer(duration);

    const filter = this.ctx.createBiquadFilter();
    filter.type = 'bandpass';
    if (weaponId === 'awp') {
      filter.frequency.setValueAtTime(750, now);
      filter.Q.value = 0.7;
    } else if (weaponId === 'ssg08') {
      filter.frequency.setValueAtTime(1650, now);
      filter.Q.value = 1.1;
    } else if (weaponId === 'scar20') {
      filter.frequency.setValueAtTime(1350, now);
      filter.Q.value = 0.9;
    } else if (weaponId === 'deagle') {
      filter.frequency.setValueAtTime(950, now);
      filter.Q.value = 0.8;
    } else {
      filter.frequency.setValueAtTime(1150, now);
      filter.Q.value = 1.0;
    }

    const noiseGain = this.ctx.createGain();
    noiseGain.gain.setValueAtTime(0.65 * volScale, now);
    noiseGain.gain.exponentialRampToValueAtTime(0.001, now + duration);

    noise.connect(filter);
    filter.connect(noiseGain);
    noiseGain.connect(this.masterGain);
    noise.start(now);
    noise.stop(now + duration);

    // Sub-bass punch
    const osc = this.ctx.createOscillator();
    const oscGain = this.ctx.createGain();
    osc.type = weaponId === 'awp' ? 'sawtooth' : 'triangle';
    const startFreq = weaponId === 'awp' ? 140 : weaponId === 'ssg08' ? 240 : 190;
    osc.frequency.setValueAtTime(startFreq, now);
    osc.frequency.exponentialRampToValueAtTime(38, now + duration * 0.75);
    oscGain.gain.setValueAtTime(0.55 * volScale, now);
    oscGain.gain.exponentialRampToValueAtTime(0.001, now + duration * 0.75);
    osc.connect(oscGain);
    oscGain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + duration * 0.75);
  }

  playHitSound(style = 'skeet', isHeadshot = false) {
    this.init();
    if (!this.ctx || !this.enabled || style === 'off') return;
    const now = this.ctx.currentTime;

    if (style === 'skeet') {
      // Classic Gamesense / Skeet metallic high-pitched tick + harmonic bell
      const osc1 = this.ctx.createOscillator();
      const osc2 = this.ctx.createOscillator();
      const gain = this.ctx.createGain();

      osc1.type = 'sine';
      osc2.type = 'triangle';
      const baseFreq = isHeadshot ? 2450 : 1950;
      osc1.frequency.setValueAtTime(baseFreq, now);
      osc1.frequency.exponentialRampToValueAtTime(baseFreq * 0.65, now + 0.085);

      osc2.frequency.setValueAtTime(baseFreq * 1.5, now);
      osc2.frequency.exponentialRampToValueAtTime(baseFreq * 1.1, now + 0.085);

      gain.gain.setValueAtTime(0.45, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.09);

      osc1.connect(gain);
      osc2.connect(gain);
      gain.connect(this.masterGain);

      osc1.start(now);
      osc2.start(now);
      osc1.stop(now + 0.09);
      osc2.stop(now + 0.09);
    } else if (style === 'neverlose') {
      // Crisp dual-tone synth pluck
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(isHeadshot ? 1560 : 1180, now);
      osc.frequency.setValueAtTime(isHeadshot ? 2120 : 1640, now + 0.035);
      gain.gain.setValueAtTime(0.42, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.12);
      osc.connect(gain);
      gain.connect(this.masterGain);
      osc.start(now);
      osc.stop(now + 0.12);
    } else if (style === 'bell') {
      // Arena bell dink
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(isHeadshot ? 2800 : 2200, now);
      gain.gain.setValueAtTime(0.45, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.18);
      osc.connect(gain);
      gain.connect(this.masterGain);
      osc.start(now);
      osc.stop(now + 0.18);
    } else {
      // COD classic thwack
      const noise = this.ctx.createBufferSource();
      noise.buffer = this.createNoiseBuffer(0.065);
      const filter = this.ctx.createBiquadFilter();
      filter.type = 'highpass';
      filter.frequency.value = 1800;
      const gain = this.ctx.createGain();
      gain.gain.setValueAtTime(0.5, now);
      gain.gain.exponentialRampToValueAtTime(0.01, now + 0.065);
      noise.connect(filter);
      filter.connect(gain);
      gain.connect(this.masterGain);
      noise.start(now);
      noise.stop(now + 0.065);
    }
  }

  playFootstep() {
    this.init();
    if (!this.ctx || !this.enabled) return;
    const now = this.ctx.currentTime;
    const noise = this.ctx.createBufferSource();
    noise.buffer = this.createNoiseBuffer(0.055);
    const filter = this.ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.value = 420 + Math.random() * 160;
    const gain = this.ctx.createGain();
    gain.gain.setValueAtTime(0.14, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.055);
    noise.connect(filter);
    filter.connect(gain);
    gain.connect(this.masterGain);
    noise.start(now);
    noise.stop(now + 0.055);
  }

  playJump() {
    this.init();
    if (!this.ctx || !this.enabled) return;
    const now = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(110, now);
    osc.frequency.exponentialRampToValueAtTime(175, now + 0.06);
    gain.gain.setValueAtTime(0.12, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.065);
    osc.connect(gain);
    gain.connect(this.masterGain);
    osc.start(now);
    osc.stop(now + 0.065);
  }
}

export const soundEngine = new SoundEngine();
