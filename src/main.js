// App bootstrap: renderer, screens, input routing, main loop, frame rendering
import * as THREE from 'three';
import { Game, TICK } from './game.js';
import { HUD } from './hud.js';
import { Viewmodel } from './viewmodel.js';
import { Audio } from './audio.js';
import { Input } from './input.js';
import { CheatMenu } from './menu.js';
import { loadCfg, saveCfg } from './config.js';
import { MAP_INFO } from './maps.js';
import { WEAPONS } from './weapons.js';
import { angleVectors, clamp, wrap180, D2R } from './util.js';
import { HULL } from './movement.js';

const $ = (id) => document.getElementById(id);
const hfovToV = (h, aspect = 4 / 3) => 2 * Math.atan(Math.tan(h * D2R / 2) / aspect) / D2R;

class App {
  constructor() {
    this.cfg = loadCfg();
    this.input = new Input();
    this.audio = new Audio();
    this.audio.setVolume(this.cfg.game.volume / 100);
    this.canvas = $('gl');
    this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(1.5, window.devicePixelRatio || 1));
    this.renderer.setSize(innerWidth, innerHeight);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.autoClear = false;
    this.hud = new HUD($('hud'));
    this.viewmodel = new Viewmodel(this.cfg);
    this.menu = new CheatMenu(this);
    this.input.bindCanvas(this.canvas);
    this.game = null;
    this.menuOpen = false; this.buyOpen = false; this.chatOpen = false;
    this.pointerActive = false; this.noLock = false;
    this.selectedMap = localStorage.getItem('sz_map') || 'de_dust';
    this.acc = 0; this.last = performance.now();
    this.mouseDX = 0; this.mouseDY = 0;
    this.setupMainMenu();
    this.setupEvents();
    requestAnimationFrame((t) => this.loop(t));
  }

  // ------------------------------------------------------------ screens
  setupMainMenu() {
    const list = $('map-list');
    list.innerHTML = '';
    for (const m of MAP_INFO) {
      const d = document.createElement('div');
      d.className = 'map-card' + (m.id === this.selectedMap ? ' sel' : '');
      d.innerHTML = `<div class="stripe" style="background:${m.accent}"></div><div class="nm">${m.name}</div><div class="ds">${m.desc}</div><div class="md">${m.mode === 'kz' ? 'KZ / BHOP' : 'TDM'}</div>`;
      d.onclick = () => { this.selectedMap = m.id; localStorage.setItem('sz_map', m.id); this.setupMainMenu(); };
      d.ondblclick = () => this.startGame();
      list.appendChild(d);
    }
    const g = this.cfg.game;
    $('opt-bots').value = g.bots; $('opt-bots-v').textContent = g.bots;
    $('opt-team').value = g.teammates; $('opt-team-v').textContent = g.teammates;
    $('opt-diff').value = g.difficulty; $('opt-primary').value = g.primary; $('opt-sens').value = g.sensitivity;
    $('opt-name').value = localStorage.getItem('sz_name') || 'Player';
    $('opt-bots').oninput = (e) => { $('opt-bots-v').textContent = e.target.value; };
    $('opt-team').oninput = (e) => { $('opt-team-v').textContent = e.target.value; };
    $('btn-play').onclick = () => this.startGame();
  }

  startGame() {
    this.audio.init();
    const g = this.cfg.game;
    g.bots = Number($('opt-bots').value); g.teammates = Number($('opt-team').value);
    g.difficulty = $('opt-diff').value; g.primary = $('opt-primary').value; g.sensitivity = Number($('opt-sens').value) || 2;
    const name = $('opt-name').value.trim() || 'Player';
    localStorage.setItem('sz_name', name);
    saveCfg(this.cfg);
    $('loading').classList.add('show');
    setTimeout(() => {
      if (this.game) this.quitGame(true);
      this.game = new Game(this);
      this.game.load(this.selectedMap, { bots: g.bots, teammates: g.teammates, difficulty: g.difficulty, name });
      this.viewmodel.setWeapon(this.game.player.weaponId, true);
      this.game.applySkins();
      this.onCfg('all');
      $('main-menu').classList.remove('show');
      $('loading').classList.remove('show');
      this.acc = 0;
      this.lockPointer();
      this.game.say('strafe.zone', 'Нажми INSERT, чтобы открыть чит-меню. B — выбор оружия.', 'KZ');
    }, 30);
  }

  quitGame(silent) {
    if (!this.game) return;
    this.game.dispose();
    this.game = null;
    this.showMenu(false); this.showBuy(false);
    $('pause').classList.remove('show');
    if (!silent) { $('main-menu').classList.add('show'); document.exitPointerLock?.(); }
  }

  lockPointer() {
    if (this.noLock) { this.pointerActive = true; $('click-to-play').classList.remove('show'); return; }
    try {
      const p = this.canvas.requestPointerLock({ unadjustedMovement: true });
      if (p && p.catch) p.catch(() => {
        try { const p2 = this.canvas.requestPointerLock(); if (p2 && p2.catch) p2.catch(() => this.enableNoLock()); } catch { this.enableNoLock(); }
      });
    } catch { this.enableNoLock(); }
  }
  enableNoLock() {
    // if pointer lock worked before, this is a transient failure (e.g. Chrome's re-lock cooldown) — just ask for a click
    if (this.everLocked) { if (this.game && !this.menuOpen && !this.buyOpen) $('click-to-play').classList.add('show'); return; }
    this.noLock = true; this.pointerActive = true; this.input.allowWheel = true;
    $('pause').classList.remove('show');
    this.game?.say('СИСТЕМА', 'Захват мыши недоступен (iframe) — открой превью в отдельной вкладке для нормального обзора.', 'sys');
    $('lock-warn').textContent = 'Захват мыши недоступен в этом окне (iframe). Обзор работает при движении мыши, но для нормальной игры открой превью в отдельной вкладке.';
  }

  showMenu(v) {
    this.menuOpen = v;
    this.menu.show(v);
    this.audio.ui(v ? 'open' : 'close');
    if (v) { if (document.pointerLockElement) document.exitPointerLock(); }
    else if (this.game && !this.buyOpen) { $('pause').classList.remove('show'); this.lockPointer(); }
  }
  showBuy(v) {
    this.buyOpen = v;
    $('buy-menu').classList.toggle('show', v);
    if (v) { if (document.pointerLockElement) document.exitPointerLock(); }
    else if (this.game && !this.menuOpen) { $('pause').classList.remove('show'); this.lockPointer(); }
  }
  buy(id) {
    const g = this.game; if (!g || g.mode === 'kz') { this.showBuy(false); return; }
    const p = g.player;
    this.cfg.game.primary = id; saveCfg(this.cfg);
    p.inventory[0] = id;
    p.ammo[id] = { clip: WEAPONS[id].clip, reserve: WEAPONS[id].reserve };
    p.slot = -1; p.switchTo(0, g.time); p.prevSlot = 1;
    this.viewmodel.setWeapon(id);
    g.applySkins();
    g.netModel.setGun(id); g.netModel.setGhost(g.ghostMat);
    this.showBuy(false);
  }

  setCfg(c) {
    this.cfg = c;
    this.viewmodel.cfg = c;
    if (this.game) this.game.cfg = c;
    saveCfg(c);
    this.menu.build();
    this.onCfg('all');
  }

  onCfg(k) {
    const c = this.cfg;
    this.audio.setVolume(c.game.volume / 100);
    if (this.game && (k === 'all' || (k && k.startsWith('skins')))) {
      this.viewmodel.cfg = c;
      this.viewmodel.setWeapon(this.game.player.weaponId, true);
      this.game.applySkins();
    }
  }

  // ------------------------------------------------------------ events
  setupEvents() {
    window.addEventListener('resize', () => {
      this.renderer.setSize(innerWidth, innerHeight);
      this.hud.resize(); this.viewmodel.resize();
      if (this.game) { this.game.camera.aspect = innerWidth / innerHeight; this.game.camera.updateProjectionMatrix(); }
    });
    document.addEventListener('pointerlockchange', () => {
      const locked = document.pointerLockElement === this.canvas;
      this.pointerActive = locked || this.noLock;
      if (!locked && this.game && !this.menuOpen && !this.buyOpen && !this.noLock) $('pause').classList.add('show');
      if (locked) this.everLocked = true;
      if (locked) { $('pause').classList.remove('show'); $('click-to-play').classList.remove('show'); }
    });
    document.addEventListener('pointerlockerror', () => {
      if (this.game && !this.menuOpen) this.enableNoLock();
    });
    this.canvas.addEventListener('click', () => {
      this.audio.init();
      if (this.game && !this.pointerActive && !this.menuOpen) this.lockPointer();
    });
    document.addEventListener('mousemove', (e) => {
      if (!this.game || !this.pointerActive || this.menuOpen || this.buyOpen) return;
      if (this.noLock && e.target !== this.canvas && e.target !== document.body) return;
      this.mouseDX += e.movementX; this.mouseDY += e.movementY;
      const p = this.game.player;
      const sens = this.cfg.game.sensitivity * 0.022 * this.zoomSens;
      p.yaw = wrap180(p.yaw - e.movementX * sens);
      p.pitch = clamp(p.pitch + e.movementY * sens, -89, 89);
      this.game.mouseDXTick += e.movementX;
    });
    $('btn-resume').onclick = () => { $('pause').classList.remove('show'); this.lockPointer(); };
    $('btn-cheats').onclick = () => { $('pause').classList.remove('show'); this.showMenu(true); };
    $('btn-quit').onclick = () => this.quitGame();
    document.querySelectorAll('#buy-menu [data-w]').forEach(b => b.onclick = () => this.buy(b.dataset.w));
    this.zoomSens = 1;

    window.addEventListener('keydown', (e) => {
      if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return;
      if (!this.game) { if (e.code === 'Enter' && $('main-menu').classList.contains('show')) this.startGame(); return; }
      if (e.repeat) return;
      if (e.code === 'Insert' || e.code === 'Home' || e.code === 'NumpadDecimal' && e.getModifierState && !e.getModifierState('NumLock')) {
        e.preventDefault();
        if (this.buyOpen) this.showBuy(false);
        this.showMenu(!this.menuOpen);
        return;
      }
      if (this.menuOpen) { if (e.code === 'Escape') this.showMenu(false); return; }
      if (this.buyOpen) {
        if (e.code === 'KeyB' || e.code === 'Escape') this.showBuy(false);
        const map = { Digit1: 'ak47', Digit2: 'm4a4', Digit3: 'awp', Digit4: 'ssg08' };
        if (map[e.code]) this.buy(map[e.code]);
        return;
      }
      const c = this.cfg;
      if (e.code === 'KeyV') { c.visuals.thirdperson = !c.visuals.thirdperson; saveCfg(c); }
      if (e.code === 'KeyX') { c.aa.invert = !c.aa.invert; saveCfg(c); }
      if (e.code === 'KeyB' && this.game.mode === 'tdm') this.showBuy(true);
      const st = this.game.aaState || (this.game.aaState = { spin: 0, flip: false, manual: 0 });
      if (e.code === 'ArrowLeft') st.manual = st.manual === 1 ? 0 : 1;
      if (e.code === 'ArrowRight') st.manual = st.manual === 2 ? 0 : 2;
      if (e.code === 'ArrowDown') st.manual = st.manual === 3 ? 0 : 3;
      if (e.code === 'ArrowUp') st.manual = 0;
    });
  }

  // ------------------------------------------------------------ loop
  loop(t) {
    requestAnimationFrame((tt) => this.loop(tt));
    let dt = (t - this.last) / 1000; this.last = t;
    if (dt > 0.25) dt = 0.25;
    const g = this.game;
    if (!g) { this.renderer.clear(); this.hud.draw(null, this, dt); return; }
    this.acc += dt;
    let n = 0;
    while (this.acc >= TICK && n < 10) { g.tick(); this.input.pressedSet.clear(); this.acc -= TICK; n++; }
    if (n >= 10) this.acc = 0;
    this.render(this.acc / TICK, dt);
  }

  render(alpha, dt) {
    const g = this.game, p = g.player, cfg = this.cfg, V = cfg.visuals;
    const tp = V.thirdperson || !p.alive;
    // interpolate bodies
    for (const a of g.actors) a.model.root.position.lerpVectors(a.prevOrigin, a.origin, alpha);
    p.model.root.visible = tp && (p.alive || g.time - p.deathTime < 5);
    const showGhost = tp && p.alive && V.fakeChams && (cfg.aa.enabled || cfg.fakelag.enabled);
    g.netModel.root.visible = showGhost;
    if (showGhost) g.ghostMat.color.set(V.fakeColor);
    // chams
    for (const a of g.actors) {
      if (a === p) continue;
      const enemy = a.team !== p.team;
      const on = enemy || V.teammates;
      const vis = V.chams && on ? g.getChamsMat(V.chamsMat, enemy ? V.chamsColor : V.teamColor, false) : null;
      const hid = V.xqz && on ? g.getChamsMat(V.chamsMat === 'Glow' ? 'Glow' : 'Flat', enemy ? V.xqzColor : V.teamColor, true) : null;
      a.model.setChams(vis, hid);
    }
    p.model.setChams(V.localChams ? g.getChamsMat(V.localMat, V.localColor, false) : null, null);

    // world look: nightmode / fullbright / asus walls / fog
    const ms = g.mapScene;
    const night = V.nightmode ? V.nightAmount / 100 : 0;
    ms.hemi.intensity = V.fullbright ? 3 : ms.base.hemi * (1 - night * 0.8);
    ms.sun.intensity = ms.base.sun * (1 - night * 0.9);
    ms.amb.intensity = V.fullbright ? 2 : ms.base.amb * (1 - night * 0.5);
    ms.skyMat.uniforms.night.value = night;
    const alphaW = V.asus / 100;
    for (const m of ms.materials) {
      const tr = alphaW < 0.999;
      if (m.transparent !== tr) { m.transparent = tr; m.needsUpdate = true; }
      m.opacity = alphaW; m.depthWrite = !tr || alphaW > 0.9;
    }
    g.scene.fog = V.fog ? (g.scene.fog || new THREE.Fog(ms.base.fog, 1500, g.map.sky.fogFar)) : null;
    if (g.scene.fog) g.scene.fog.color.copy(ms.base.fog).multiplyScalar(1 - night * 0.85);
    for (const m of ms.root.children) if (m.userData.lava && m.material.map) { m.material.map.offset.x = g.time * 0.02; m.material.map.offset.y = Math.sin(g.time * 0.3) * 0.05; }

    // camera
    const targetOff = p.ducked ? 46 : 64;
    p.viewOffsetSmooth += (targetOff - p.viewOffsetSmooth) * Math.min(1, dt * 16);
    const eye = new THREE.Vector3().lerpVectors(p.prevOrigin, p.origin, alpha);
    eye.y += p.viewOffsetSmooth;
    const noVR = V.noVisRecoil;
    let camYaw = p.yaw + (noVR ? 0 : p.ws.punchY * 0.9);
    let camPitch = p.pitch - (noVR ? 0 : p.ws.punchP * 0.9);
    const cam = g.camera;
    if (tp) {
      if (!p.alive) { camPitch = 25; camYaw = p.yaw + (g.time - p.deathTime) * 15; eye.copy(p.origin); eye.y += 40; }
      const dir = angleVectors(camYaw, camPitch);
      const want = eye.clone().addScaledVector(dir, -V.tpDist);
      const tr = g.world.traceBox(eye, want, new THREE.Vector3(-5, -5, -5), new THREE.Vector3(5, 5, 5));
      cam.position.copy(tr.endpos);
    } else cam.position.copy(eye);
    cam.rotation.set(-camPitch * D2R, camYaw * D2R, 0);
    // fov & zoom
    const w = p.weapon;
    const scoped = p.alive && w.sniper && p.ws.scoped > 0;
    const baseV = hfovToV(V.fov);
    const wantV = scoped ? hfovToV(w.scope[p.ws.scoped - 1]) : baseV;
    const newFov = cam.fov + (wantV - cam.fov) * Math.min(1, dt * 25);
    if (Math.abs(newFov - cam.fov) > 0.01) { cam.fov = newFov; cam.updateProjectionMatrix(); }
    this.zoomSens = scoped ? Math.tan(wantV * D2R / 2) / Math.tan(baseV * D2R / 2) : 1;
    cam.updateMatrixWorld();

    // audio listener
    this.audio.listener.x = eye.x; this.audio.listener.y = eye.y; this.audio.listener.z = eye.z; this.audio.listener.yaw = p.yaw * D2R;

    g.effects.update(dt);
    this.renderer.clear();
    this.renderer.render(g.scene, cam);
    // viewmodel
    this.viewmodel.update(dt, {
      mouseDX: this.mouseDX, mouseDY: this.mouseDY, speed: p.speed2d(), onGround: p.onGround, ducked: p.ducked,
      visible: !tp && p.alive && !scoped, inspectT: p.ws.inspect, time: g.time,
    });
    this.mouseDX = 0; this.mouseDY = 0;
    this.viewmodel.render(this.renderer);
    this.hud.draw(g, this, dt);
    $('click-to-play').classList.toggle('show', !this.pointerActive && !this.menuOpen && !this.buyOpen && !$('pause').classList.contains('show'));
  }
}

try {
  window.app = new App();
  document.getElementById('boot-msg')?.remove();
} catch (err) {
  const noGL = /webgl|context/i.test(String(err && err.message));
  const msg = noGL
    ? 'Не удалось создать WebGL-контекст. Включи аппаратное ускорение в настройках браузера (chrome://settings/system) или попробуй другой браузер.\n' + err.message
    : 'Ошибка запуска: ' + (err && (err.stack || err.message) || err);
  (window.__bootShow || console.error)(msg);
  const bm = document.getElementById('boot-msg'); if (bm) bm.textContent = noGL ? 'WebGL недоступен' : 'Ошибка запуска (подробности внизу)';
}
