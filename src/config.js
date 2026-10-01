// Cheat / game configuration with persistence
const KEY = 'strafezone_cfg_v1';

export const DEFAULT_CFG = {
  rage: {
    enabled: false, silent: true, autofire: true, autoscope: true, autostop: true,
    hitboxes: { head: true, chest: true, stomach: true, legs: false },
    hitchance: 55, mindmg: 20, fov: 180, target: 'Crosshair', doubletap: false, visibleOnly: true,
  },
  aa: {
    enabled: false, pitch: 'Down', yawBase: 'At targets', yaw: 'Backward', yawOffset: 0, jitter: 35, spinSpeed: 25,
    desync: 'Static', bodyYaw: 58, invert: false, lbyBreak: true,
  },
  fakelag: { enabled: false, mode: 'Adaptive', limit: 14, variance: 30, unchokeOnShot: true },
  legit: {
    aimbot: false, activation: 'On fire', fov: 6, smooth: 7, hitbox: 'Head', visibleOnly: true,
    rcs: false, rcsX: 100, rcsY: 100, trigger: false, triggerDelay: 60, triggerHeadOnly: false,
  },
  visuals: {
    esp: false, box: 'Corner', name: true, health: true, healthText: false, weapon: true, distance: false, skeleton: false,
    snaplines: false, headDot: false, offscreen: true, teammates: false,
    espColor: '#ff4040', espVisColor: '#ffd23f', teamColor: '#4ca3ff',
    chams: false, chamsMat: 'Flat', chamsColor: '#ff3fa4', xqz: false, xqzColor: '#7a5cff',
    localChams: false, localMat: 'Glow', localColor: '#00e1ff', fakeChams: true, fakeColor: '#ffffff',
    radar: false, glowColor: '#ff3fa4',
    nightmode: false, nightAmount: 60, asus: 100, fullbright: false, fog: true,
    fov: 90, vmFov: 68, vmX: 0, vmY: 0, vmZ: 0, thirdperson: false, tpDist: 130,
    noVisRecoil: false, noScope: false, tracers: false, tracerColor: '#7cff6b', impacts: false,
    hitmarker: true, damageIndicator: false, killEffect: true, spreadCircle: false, recoilCrosshair: false, sniperCrosshair: false,
    speedIndicator: true, speedGraph: false, keystrokes: false, jumpstats: true, watermark: true,
    crosshairColor: '#4cff4c', crosshairGap: 3, crosshairSize: 6, crosshairDynamic: true,
  },
  misc: {
    bhop: false, autostrafe: 'Off', strafeSmooth: 100, edgejump: false, quickstop: false, norecoil: false, nospread: false,
    svBunnyhop: true, airaccelerate: 12, hitsound: 'Skeet', infiniteAmmo: false, godmode: false, killsay: false,
  },
  skins: { ak47: 'Default', m4a4: 'Default', awp: 'Default', ssg08: 'Default', deagle: 'Default', knife: 'Default', knifeModel: 'default', color: '#b0102a' },
  menu: { accent: '#9ee35d' },
  game: { sensitivity: 2.0, volume: 70, bots: 6, teammates: 2, difficulty: 'Normal', primary: 'ak47', showFps: true },
};

function deepMerge(base, over) {
  const out = Array.isArray(base) ? [...base] : { ...base };
  if (!over || typeof over !== 'object') return out;
  for (const k of Object.keys(base)) {
    if (base[k] && typeof base[k] === 'object' && !Array.isArray(base[k])) out[k] = deepMerge(base[k], over[k]);
    else if (over[k] !== undefined && typeof over[k] === typeof base[k]) out[k] = over[k];
  }
  return out;
}

export function loadCfg() {
  try { return deepMerge(DEFAULT_CFG, JSON.parse(localStorage.getItem(KEY) || '{}')); }
  catch { return deepMerge(DEFAULT_CFG, {}); }
}
export function saveCfg(cfg) { try { localStorage.setItem(KEY, JSON.stringify(cfg)); } catch { /* ignore */ } }
export function resetCfg() { return deepMerge(DEFAULT_CFG, {}); }

export function getPath(obj, path) { return path.split('.').reduce((o, k) => o?.[k], obj); }
export function setPath(obj, path, v) {
  const ks = path.split('.'); const last = ks.pop();
  const o = ks.reduce((o, k) => o[k], obj); o[last] = v;
}

// named config slots
export function listSlots() { try { return JSON.parse(localStorage.getItem(KEY + '_slots') || '{}'); } catch { return {}; } }
export function saveSlot(name, cfg) { const s = listSlots(); s[name] = cfg; localStorage.setItem(KEY + '_slots', JSON.stringify(s)); }
export function loadSlot(name) { const s = listSlots(); return s[name] ? deepMerge(DEFAULT_CFG, s[name]) : null; }
export function mergeCfg(o) { return deepMerge(DEFAULT_CFG, o); }
