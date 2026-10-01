// Cheat menu (INSERT) — skeet-style window, generated from a declarative spec
import { getPath, setPath, saveCfg, resetCfg, listSlots, saveSlot, loadSlot, mergeCfg } from './config.js';
import { SKINS } from './models.js';

const CHAMS_MATS = ['Flat', 'Shaded', 'Metallic', 'Glow', 'Wireframe'];

function spec(app) {
  return [
    {
      id: 'rage', icon: '☠', label: 'RAGE', cols: [
        [{ g: 'Аимбот', items: [
          { t: 'check', k: 'rage.enabled', l: 'Включить рейдж-бот' },
          { t: 'check', k: 'rage.silent', l: 'Silent aim (камера не дёргается)' },
          { t: 'check', k: 'rage.autofire', l: 'Автовыстрел' },
          { t: 'check', k: 'rage.autoscope', l: 'Автоприцел (снайперки)' },
          { t: 'check', k: 'rage.autostop', l: 'Автостоп (quick stop)' },
          { t: 'check', k: 'rage.visibleOnly', l: 'Только видимые' },
          { t: 'combo', k: 'rage.target', l: 'Выбор цели', o: ['Crosshair', 'Distance', 'Health'] },
          { t: 'slider', k: 'rage.fov', l: 'FOV', min: 1, max: 180, step: 1, u: '°' },
        ] }],
        [{ g: 'Точность', items: [
          { t: 'multi', k: 'rage.hitboxes', l: 'Хитбоксы', o: { head: 'Голова', chest: 'Грудь', stomach: 'Живот', legs: 'Ноги' } },
          { t: 'slider', k: 'rage.hitchance', l: 'Хитшанс', min: 0, max: 100, step: 1, u: '%' },
          { t: 'slider', k: 'rage.mindmg', l: 'Мин. урон', min: 1, max: 100, step: 1, u: 'hp' },
          { t: 'check', k: 'rage.doubletap', l: 'Double tap (2 выстрела за тик)' },
          { t: 'check', k: 'misc.norecoil', l: 'No recoil' },
          { t: 'check', k: 'misc.nospread', l: 'No spread' },
        ] }],
      ],
    },
    {
      id: 'aa', icon: '↻', label: 'ANTI-AIM', cols: [
        [{ g: 'Анти-аим (крутилка)', items: [
          { t: 'check', k: 'aa.enabled', l: 'Включить анти-аим' },
          { t: 'combo', k: 'aa.pitch', l: 'Pitch', o: ['None', 'Down', 'Up', 'Zero', 'Jitter', 'Random'] },
          { t: 'combo', k: 'aa.yawBase', l: 'Yaw base', o: ['View', 'At targets'] },
          { t: 'combo', k: 'aa.yaw', l: 'Yaw', o: ['Backward', 'Forward', 'Spin', 'Jitter', 'Sideways', 'Random'] },
          { t: 'slider', k: 'aa.yawOffset', l: 'Yaw offset', min: -180, max: 180, step: 1, u: '°' },
          { t: 'slider', k: 'aa.jitter', l: 'Jitter range', min: 0, max: 90, step: 1, u: '°' },
          { t: 'slider', k: 'aa.spinSpeed', l: 'Скорость спина', min: 1, max: 100, step: 1, u: '' },
          { t: 'label', l: 'Стрелки ← → ↓ — ручной AA, ↑ — сброс' },
        ] }],
        [{ g: 'Десинк / Body yaw', items: [
          { t: 'combo', k: 'aa.desync', l: 'Body yaw', o: ['Off', 'Static', 'Jitter'] },
          { t: 'slider', k: 'aa.bodyYaw', l: 'Угол десинка', min: 0, max: 60, step: 1, u: '°' },
          { t: 'check', k: 'aa.invert', l: 'Инверт стороны (клавиша X)' },
        ] }, { g: 'Фейк-лаг', items: [
          { t: 'check', k: 'fakelag.enabled', l: 'Включить фейк-лаг' },
          { t: 'combo', k: 'fakelag.mode', l: 'Режим', o: ['Static', 'Adaptive', 'Random'] },
          { t: 'slider', k: 'fakelag.limit', l: 'Лимит (choke)', min: 1, max: 16, step: 1, u: ' тик' },
          { t: 'slider', k: 'fakelag.variance', l: 'Разброс (Random)', min: 0, max: 100, step: 1, u: '%' },
          { t: 'check', k: 'fakelag.unchokeOnShot', l: 'Отправлять при выстреле' },
          { t: 'label', l: 'Боты целятся в твою «сетевую» модель — фейк-лаг и десинк реально заставляют их мазать. Белый призрак в 3-м лице = то, что видят боты.' },
        ] }],
      ],
    },
    {
      id: 'legit', icon: '◎', label: 'LEGIT', cols: [
        [{ g: 'Легит аим', items: [
          { t: 'check', k: 'legit.aimbot', l: 'Аимбот' },
          { t: 'combo', k: 'legit.activation', l: 'Активация', o: ['On fire', 'Always'] },
          { t: 'slider', k: 'legit.fov', l: 'FOV', min: 0.5, max: 30, step: 0.5, u: '°' },
          { t: 'slider', k: 'legit.smooth', l: 'Сглаживание', min: 1, max: 30, step: 0.5, u: '' },
          { t: 'combo', k: 'legit.hitbox', l: 'Хитбокс', o: ['Head', 'Chest', 'Nearest'] },
          { t: 'check', k: 'legit.visibleOnly', l: 'Только видимые' },
        ] }],
        [{ g: 'RCS / Триггер', items: [
          { t: 'check', k: 'legit.rcs', l: 'Контроль отдачи (RCS)' },
          { t: 'slider', k: 'legit.rcsX', l: 'RCS X', min: 0, max: 100, step: 1, u: '%' },
          { t: 'slider', k: 'legit.rcsY', l: 'RCS Y', min: 0, max: 100, step: 1, u: '%' },
          { t: 'check', k: 'legit.trigger', l: 'Триггербот' },
          { t: 'slider', k: 'legit.triggerDelay', l: 'Задержка', min: 0, max: 400, step: 5, u: 'ms' },
          { t: 'check', k: 'legit.triggerHeadOnly', l: 'Только голова' },
        ] }],
      ],
    },
    {
      id: 'visuals', icon: '◉', label: 'VISUALS', cols: [
        [{ g: 'ESP', items: [
          { t: 'check', k: 'visuals.esp', l: 'Включить ESP' },
          { t: 'combo', k: 'visuals.box', l: 'Бокс', o: ['Off', '2D', 'Corner'] },
          { t: 'check', k: 'visuals.name', l: 'Ник' },
          { t: 'check', k: 'visuals.health', l: 'Полоса HP' },
          { t: 'check', k: 'visuals.healthText', l: 'HP числом' },
          { t: 'check', k: 'visuals.weapon', l: 'Оружие' },
          { t: 'check', k: 'visuals.distance', l: 'Дистанция' },
          { t: 'check', k: 'visuals.skeleton', l: 'Скелет' },
          { t: 'check', k: 'visuals.headDot', l: 'Точка на голове' },
          { t: 'check', k: 'visuals.snaplines', l: 'Снаплайны' },
          { t: 'check', k: 'visuals.offscreen', l: 'Стрелки за экраном' },
          { t: 'check', k: 'visuals.teammates', l: 'Показывать тиммейтов' },
          { t: 'color', k: 'visuals.espColor', l: 'Цвет (за стеной)' },
          { t: 'color', k: 'visuals.espVisColor', l: 'Цвет (видим)' },
          { t: 'check', k: 'visuals.radar', l: 'Радар-хак' },
        ] }, { g: 'Чамсы', items: [
          { t: 'check', k: 'visuals.chams', l: 'Чамсы (видимые)', color: 'visuals.chamsColor' },
          { t: 'combo', k: 'visuals.chamsMat', l: 'Материал', o: CHAMS_MATS },
          { t: 'check', k: 'visuals.xqz', l: 'Сквозь стены (XQZ)', color: 'visuals.xqzColor' },
          { t: 'check', k: 'visuals.localChams', l: 'Свои чамсы (3-е лицо)', color: 'visuals.localColor' },
          { t: 'combo', k: 'visuals.localMat', l: 'Материал своих', o: CHAMS_MATS },
          { t: 'check', k: 'visuals.fakeChams', l: 'Фейк-модель (десинк/лаг)', color: 'visuals.fakeColor' },
        ] }],
        [{ g: 'Мир и камера', items: [
          { t: 'check', k: 'visuals.thirdperson', l: 'Третье лицо (клавиша V)' },
          { t: 'slider', k: 'visuals.tpDist', l: 'Дистанция 3-го лица', min: 50, max: 300, step: 5, u: '' },
          { t: 'slider', k: 'visuals.fov', l: 'FOV', min: 60, max: 130, step: 1, u: '°' },
          { t: 'slider', k: 'visuals.vmFov', l: 'FOV рук', min: 40, max: 110, step: 1, u: '°' },
          { t: 'slider', k: 'visuals.vmX', l: 'Руки X', min: -6, max: 6, step: 0.5, u: '' },
          { t: 'slider', k: 'visuals.vmY', l: 'Руки Y', min: -6, max: 6, step: 0.5, u: '' },
          { t: 'slider', k: 'visuals.vmZ', l: 'Руки Z', min: -6, max: 6, step: 0.5, u: '' },
          { t: 'check', k: 'visuals.nightmode', l: 'Найтмод' },
          { t: 'slider', k: 'visuals.nightAmount', l: 'Сила найтмода', min: 0, max: 100, step: 1, u: '%' },
          { t: 'check', k: 'visuals.fullbright', l: 'Fullbright' },
          { t: 'slider', k: 'visuals.asus', l: 'Asus walls (прозрачность)', min: 10, max: 100, step: 1, u: '%' },
          { t: 'check', k: 'visuals.fog', l: 'Туман' },
          { t: 'check', k: 'visuals.noVisRecoil', l: 'Убрать визуальную отдачу' },
          { t: 'check', k: 'visuals.noScope', l: 'Убрать оверлей прицела' },
        ] }, { g: 'Эффекты и HUD', items: [
          { t: 'check', k: 'visuals.tracers', l: 'Трассеры пуль', color: 'visuals.tracerColor' },
          { t: 'check', k: 'visuals.impacts', l: 'Bullet impacts' },
          { t: 'check', k: 'visuals.hitmarker', l: 'Хитмаркер' },
          { t: 'check', k: 'visuals.damageIndicator', l: 'Индикатор урона' },
          { t: 'check', k: 'visuals.killEffect', l: 'Эффект убийства' },
          { t: 'check', k: 'visuals.spreadCircle', l: 'Круг разброса' },
          { t: 'check', k: 'visuals.recoilCrosshair', l: 'Recoil crosshair' },
          { t: 'check', k: 'visuals.sniperCrosshair', l: 'Прицел на снайперках' },
          { t: 'check', k: 'visuals.speedIndicator', l: 'Индикатор скорости' },
          { t: 'check', k: 'visuals.speedGraph', l: 'График скорости' },
          { t: 'check', k: 'visuals.keystrokes', l: 'Отображение клавиш' },
          { t: 'check', k: 'visuals.jumpstats', l: 'Джамп-статы' },
          { t: 'check', k: 'visuals.watermark', l: 'Вотермарк' },
          { t: 'color', k: 'visuals.crosshairColor', l: 'Цвет прицела' },
          { t: 'slider', k: 'visuals.crosshairGap', l: 'Зазор прицела', min: -2, max: 12, step: 1, u: '' },
          { t: 'slider', k: 'visuals.crosshairSize', l: 'Размер прицела', min: 1, max: 20, step: 1, u: '' },
          { t: 'check', k: 'visuals.crosshairDynamic', l: 'Динамический прицел' },
        ] }],
      ],
    },
    {
      id: 'misc', icon: '⚙', label: 'MISC', cols: [
        [{ g: 'Движение', items: [
          { t: 'check', k: 'misc.bhop', l: 'Бхоп (держи пробел)' },
          { t: 'combo', k: 'misc.autostrafe', l: 'Автострейф', o: ['Off', 'Legit', 'Directional', 'Rage'] },
          { t: 'slider', k: 'misc.strafeSmooth', l: 'Точность стрейфа', min: 0, max: 100, step: 1, u: '%' },
          { t: 'check', k: 'misc.edgejump', l: 'Edge jump' },
          { t: 'check', k: 'misc.quickstop', l: 'Quick stop' },
          { t: 'label', l: 'Без бхопа: прыгай колесом мыши или пробелом точно в момент приземления. Стрейфь A/D + мышь в воздухе.' },
        ] }, { g: 'Сервер (sv_)', items: [
          { t: 'check', k: 'misc.svBunnyhop', l: 'sv_enablebunnyhopping' },
          { t: 'slider', k: 'misc.airaccelerate', l: 'sv_airaccelerate', min: 1, max: 150, step: 1, u: '' },
          { t: 'button', l: 'Вернуть CS:GO значения', fn: () => { app.cfg.misc.airaccelerate = 12; app.cfg.misc.svBunnyhop = true; app.menu.refresh(); app.onCfg(); } },
        ] }],
        [{ g: 'Прочее', items: [
          { t: 'combo', k: 'misc.hitsound', l: 'Хитсаунд', o: ['Off', 'Skeet', 'Bell', 'Bubble', 'Click'] },
          { t: 'check', k: 'misc.infiniteAmmo', l: 'Бесконечные патроны' },
          { t: 'check', k: 'misc.godmode', l: 'Бессмертие' },
          { t: 'check', k: 'misc.killsay', l: 'Киллсей в чат' },
          { t: 'slider', k: 'game.sensitivity', l: 'Сенса (как в CS)', min: 0.1, max: 8, step: 0.05, u: '' },
          { t: 'slider', k: 'game.volume', l: 'Громкость', min: 0, max: 100, step: 1, u: '%' },
          { t: 'color', k: 'menu.accent', l: 'Цвет меню' },
        ] }],
      ],
    },
    {
      id: 'skins', icon: '✦', label: 'SKINS', cols: [
        [{ g: 'Скинченджер', items: [
          { t: 'combo', k: 'skins.ak47', l: 'AK-47', o: SKINS },
          { t: 'combo', k: 'skins.m4a4', l: 'M4A4', o: SKINS },
          { t: 'combo', k: 'skins.awp', l: 'AWP', o: SKINS },
          { t: 'combo', k: 'skins.ssg08', l: 'SSG 08', o: SKINS },
          { t: 'combo', k: 'skins.deagle', l: 'Desert Eagle', o: SKINS },
        ] }],
        [{ g: 'Нож', items: [
          { t: 'combo', k: 'skins.knifeModel', l: 'Модель ножа', o: ['default', 'karambit'] },
          { t: 'combo', k: 'skins.knife', l: 'Скин ножа', o: SKINS },
          { t: 'color', k: 'skins.color', l: 'Цвет «Crimson»' },
          { t: 'label', l: 'F — осмотр оружия' },
        ] }],
      ],
    },
    { id: 'config', icon: '☰', label: 'CONFIG', custom: true },
  ];
}

export class CheatMenu {
  constructor(app) {
    this.app = app;
    this.el = document.getElementById('cheat-menu');
    this.tab = 'rage';
    this.controls = [];
    this.build();
    this.makeDraggable();
  }
  get cfg() { return this.app.cfg; }

  build() {
    const S = spec(this.app);
    this.spec = S;
    this.el.innerHTML = `
      <div class="cm-bar"></div>
      <div class="cm-head"><span class="cm-logo">strafe<b>.zone</b></span><span class="cm-sub">INSERT — закрыть</span></div>
      <div class="cm-body"><div class="cm-tabs"></div><div class="cm-page"></div></div>`;
    const tabs = this.el.querySelector('.cm-tabs');
    for (const t of S) {
      const b = document.createElement('div');
      b.className = 'cm-tab' + (t.id === this.tab ? ' on' : '');
      b.innerHTML = `<span class="ic">${t.icon}</span><span>${t.label}</span>`;
      b.onclick = () => { this.tab = t.id; this.app.audio.ui('click'); this.build(); };
      tabs.appendChild(b);
    }
    const page = this.el.querySelector('.cm-page');
    const cur = S.find(t => t.id === this.tab);
    this.controls = [];
    if (cur.custom) this.buildConfig(page);
    else for (const col of cur.cols) {
      const c = document.createElement('div'); c.className = 'cm-col';
      for (const grp of col) {
        const g = document.createElement('div'); g.className = 'cm-group';
        g.innerHTML = `<div class="cm-gt">${grp.g}</div>`;
        for (const it of grp.items) g.appendChild(this.control(it));
        c.appendChild(g);
      }
      page.appendChild(c);
    }
    this.applyAccent();
  }

  applyAccent() { document.documentElement.style.setProperty('--accent', this.cfg.menu.accent); }

  changed(k) { saveCfg(this.cfg); this.app.onCfg(k); if (k === 'menu.accent') this.applyAccent(); }

  control(it) {
    const row = document.createElement('div'); row.className = 'cm-row';
    const cfg = this.cfg;
    if (it.t === 'check') {
      row.classList.add('check');
      const box = document.createElement('div'); box.className = 'cm-check';
      const lab = document.createElement('label'); lab.textContent = it.l;
      const upd = () => box.classList.toggle('on', !!getPath(cfg, it.k));
      row.onclick = (e) => { if (e.target.tagName === 'INPUT') return; setPath(cfg, it.k, !getPath(cfg, it.k)); upd(); this.app.audio.ui('click'); this.changed(it.k); };
      row.append(box, lab); upd();
      if (it.color) row.appendChild(this.colorInput(it.color));
      this.controls.push(upd);
    } else if (it.t === 'slider') {
      row.innerHTML = `<div class="cm-lbl"><span>${it.l}</span><span class="v"></span></div><div class="cm-slider"><div class="fill"></div></div>`;
      const sl = row.querySelector('.cm-slider'), fill = row.querySelector('.fill'), val = row.querySelector('.v');
      const upd = () => {
        const v = getPath(cfg, it.k);
        fill.style.width = ((v - it.min) / (it.max - it.min) * 100) + '%';
        val.textContent = (it.step < 1 ? Number(v).toFixed(it.step < 0.1 ? 2 : 1) : v) + it.u;
      };
      const set = (e) => {
        const r = sl.getBoundingClientRect();
        let t = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
        let v = it.min + t * (it.max - it.min);
        v = Math.round(v / it.step) * it.step;
        v = Number(v.toFixed(3));
        setPath(cfg, it.k, v); upd(); this.changed(it.k);
      };
      sl.onpointerdown = (e) => { sl.setPointerCapture(e.pointerId); set(e); sl.onpointermove = set; };
      sl.onpointerup = () => { sl.onpointermove = null; };
      upd(); this.controls.push(upd);
    } else if (it.t === 'combo') {
      row.innerHTML = `<div class="cm-lbl"><span>${it.l}</span></div>`;
      const sel = document.createElement('select'); sel.className = 'cm-select';
      for (const o of it.o) { const op = document.createElement('option'); op.value = o; op.textContent = o; sel.appendChild(op); }
      const upd = () => { sel.value = getPath(cfg, it.k); };
      sel.onchange = () => { setPath(cfg, it.k, sel.value); this.app.audio.ui('click'); this.changed(it.k); };
      row.appendChild(sel); upd(); this.controls.push(upd);
    } else if (it.t === 'multi') {
      row.innerHTML = `<div class="cm-lbl"><span>${it.l}</span></div>`;
      const wrap = document.createElement('div'); wrap.className = 'cm-multi';
      for (const [key, lbl] of Object.entries(it.o)) {
        const chip = document.createElement('div'); chip.className = 'chip'; chip.textContent = lbl;
        const upd = () => chip.classList.toggle('on', !!getPath(cfg, it.k + '.' + key));
        chip.onclick = () => { setPath(cfg, it.k + '.' + key, !getPath(cfg, it.k + '.' + key)); upd(); this.changed(it.k); };
        upd(); this.controls.push(upd); wrap.appendChild(chip);
      }
      row.appendChild(wrap);
    } else if (it.t === 'color') {
      row.classList.add('check');
      const lab = document.createElement('label'); lab.textContent = it.l; lab.style.marginLeft = '22px';
      row.append(lab, this.colorInput(it.k));
    } else if (it.t === 'button') {
      const b = document.createElement('button'); b.className = 'cm-btn'; b.textContent = it.l; b.onclick = () => { it.fn(); this.app.audio.ui('click'); };
      row.appendChild(b);
    } else if (it.t === 'label') {
      row.innerHTML = `<div class="cm-note">${it.l}</div>`;
    }
    return row;
  }

  colorInput(k) {
    const inp = document.createElement('input'); inp.type = 'color'; inp.className = 'cm-color';
    const upd = () => { inp.value = getPath(this.cfg, k); };
    inp.oninput = () => { setPath(this.cfg, k, inp.value); this.changed(k); };
    inp.onclick = (e) => e.stopPropagation();
    upd(); this.controls.push(upd);
    return inp;
  }

  buildConfig(page) {
    const c1 = document.createElement('div'); c1.className = 'cm-col';
    const c2 = document.createElement('div'); c2.className = 'cm-col';
    const slots = listSlots();
    c1.innerHTML = `<div class="cm-group"><div class="cm-gt">Конфиги</div>
      <div class="cm-row"><input class="cm-input" id="cfg-name" placeholder="имя конфига" value="rage"></div>
      <div class="cm-row"><button class="cm-btn" id="cfg-save">Сохранить</button></div>
      <div class="cm-slots">${Object.keys(slots).map(n => `<div class="cm-slot"><span>${n}</span><button class="cm-btn sm" data-load="${n}">Загрузить</button></div>`).join('') || '<div class="cm-note">Сохранённых конфигов нет</div>'}</div>
      <div class="cm-row"><button class="cm-btn" id="cfg-preset-rage">Пресет: HvH рейдж</button></div>
      <div class="cm-row"><button class="cm-btn" id="cfg-preset-legit">Пресет: Легит</button></div>
      <div class="cm-row"><button class="cm-btn" id="cfg-preset-move">Пресет: Мувмент</button></div>
      <div class="cm-row"><button class="cm-btn danger" id="cfg-reset">Сбросить всё</button></div></div>`;
    c2.innerHTML = `<div class="cm-group"><div class="cm-gt">Экспорт / импорт</div>
      <textarea class="cm-text" id="cfg-json" spellcheck="false"></textarea>
      <div class="cm-row"><button class="cm-btn" id="cfg-export">Экспорт</button> <button class="cm-btn" id="cfg-import">Импорт</button></div></div>
      <div class="cm-group"><div class="cm-gt">Бинды</div><div class="cm-note binds">
      <b>INSERT / HOME</b> — меню<br><b>V</b> — третье лицо<br><b>X</b> — инверт десинка<br><b>← → ↓ ↑</b> — ручной AA<br>
      <b>B</b> — выбор оружия<br><b>Колесо</b> — прыжок (бхоп как в CS)<br><b>F</b> — осмотр<br><b>Tab</b> — счёт<br><b>T</b> — рестарт (KZ)</div></div>`;
    page.append(c1, c2);
    const app = this.app;
    c1.querySelector('#cfg-save').onclick = () => { const n = c1.querySelector('#cfg-name').value.trim() || 'cfg'; saveSlot(n, app.cfg); this.build(); };
    c1.querySelectorAll('[data-load]').forEach(b => b.onclick = () => { const c = loadSlot(b.dataset.load); if (c) { app.setCfg(c); } });
    c1.querySelector('#cfg-reset').onclick = () => app.setCfg(resetCfg());
    const preset = (fn) => { const c = JSON.parse(JSON.stringify(app.cfg)); fn(c); app.setCfg(c); };
    c1.querySelector('#cfg-preset-rage').onclick = () => preset(c => {
      Object.assign(c.rage, { enabled: true, silent: true, autofire: true, autoscope: true, autostop: true, hitchance: 50, mindmg: 30, doubletap: true });
      Object.assign(c.aa, { enabled: true, pitch: 'Down', yaw: 'Spin', spinSpeed: 40, desync: 'Jitter', bodyYaw: 58, yawBase: 'At targets' });
      Object.assign(c.fakelag, { enabled: true, mode: 'Adaptive', limit: 14 });
      Object.assign(c.visuals, { esp: true, chams: true, xqz: true, chamsMat: 'Glow', thirdperson: true, fakeChams: true, localChams: true, tracers: true, damageIndicator: true, radar: true });
      Object.assign(c.misc, { bhop: true, autostrafe: 'Rage', norecoil: true });
    });
    c1.querySelector('#cfg-preset-legit').onclick = () => preset(c => {
      c.rage.enabled = false; c.aa.enabled = false; c.fakelag.enabled = false;
      Object.assign(c.legit, { aimbot: true, fov: 4, smooth: 8, rcs: true, rcsX: 90, rcsY: 90, trigger: false });
      Object.assign(c.visuals, { esp: true, box: 'Corner', chams: false, xqz: false, radar: true, thirdperson: false });
    });
    c1.querySelector('#cfg-preset-move').onclick = () => preset(c => {
      Object.assign(c.misc, { bhop: true, autostrafe: 'Directional', strafeSmooth: 100, edgejump: true });
      Object.assign(c.visuals, { speedIndicator: true, speedGraph: true, keystrokes: true, jumpstats: true });
    });
    const ta = c2.querySelector('#cfg-json');
    c2.querySelector('#cfg-export').onclick = () => { ta.value = JSON.stringify(app.cfg); ta.select(); try { navigator.clipboard.writeText(ta.value); } catch { /* */ } };
    c2.querySelector('#cfg-import').onclick = () => { try { app.setCfg(mergeCfg(JSON.parse(ta.value))); } catch { ta.value = 'Ошибка: неверный JSON'; } };
  }

  refresh() { for (const u of this.controls) u(); }

  makeDraggable() {
    let drag = null;
    this.el.addEventListener('pointerdown', (e) => {
      if (!e.target.closest('.cm-head')) return;
      const r = this.el.getBoundingClientRect();
      drag = { dx: e.clientX - r.left, dy: e.clientY - r.top };
      this.el.setPointerCapture(e.pointerId);
    });
    this.el.addEventListener('pointermove', (e) => {
      if (!drag) return;
      this.el.style.left = (e.clientX - drag.dx) + 'px'; this.el.style.top = (e.clientY - drag.dy) + 'px';
      this.el.style.transform = 'none';
    });
    this.el.addEventListener('pointerup', () => { drag = null; });
  }

  show(v) { this.el.classList.toggle('show', v); if (v) { this.build(); } }
}
