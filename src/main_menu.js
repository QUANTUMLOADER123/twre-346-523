import { matchConfig, cfg, WEAPONS, WEAPON_ORDER, SKIN_PALETTES } from './config.js';
import { MAP_LIST } from './maps.js';

export class MainMenuLobby {
  constructor({
    onStartMatch,
    onResumeMatch,
    onOpenCheatMenu,
    onWeaponSelect,
    onSkinUpdate
  }) {
    this.onStartMatch = onStartMatch;
    this.onResumeMatch = onResumeMatch;
    this.onOpenCheatMenu = onOpenCheatMenu;
    this.onWeaponSelect = onWeaponSelect;
    this.onSkinUpdate = onSkinUpdate;

    this.isVisible = true;
    this.hasStartedMatchOnce = false;
    this.activeSection = 'play'; // 'home' | 'play' | 'loadout' | 'settings'
    this.container = document.getElementById('main-menu-screen');
    this.render();
  }

  show(hasActiveMatch = false) {
    this.isVisible = true;
    this.hasStartedMatchOnce = hasActiveMatch || this.hasStartedMatchOnce;
    this.container.classList.remove('hidden');
    if (document.pointerLockElement) {
      document.exitPointerLock();
    }
    this.render();
  }

  hide() {
    this.isVisible = false;
    this.container.classList.add('hidden');
  }

  render() {
    if (!this.container) return;

    this.container.innerHTML = `
      <div class="csgo-panorama">
        <!-- Far-Left CS:GO Panorama Vertical Icon Rail -->
        <aside class="csgo-left-rail">
          <div class="csgo-rail-logo" title="Counter-Strike: Global Offensive">
            <svg viewBox="0 0 64 64" width="36" height="36" fill="#f59e0b">
              <circle cx="34" cy="14" r="6"/>
              <path d="M22 24l12-2 12 5 8 2v4l-9-1-6 9 5 18h-7l-4-14-5 14h-7l4-18-5-7z"/>
            </svg>
          </div>

          <button class="csgo-rail-btn ${this.activeSection === 'home' ? 'active' : ''}" data-sec="home" title="Главный экран (Осмотр агента)">
            <svg viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
              <path d="M12 3l9 8h-3v9h-5v-6h-2v6H6v-9H3l9-8z"/>
            </svg>
          </button>

          <button class="csgo-rail-btn ${this.activeSection === 'play' ? 'active' : ''}" data-sec="play" title="Играть (Тренировка с ботами)">
            <svg viewBox="0 0 24 24" width="26" height="26" fill="currentColor">
              <circle cx="12" cy="12" r="10" fill="none" stroke="currentColor" stroke-width="2"/>
              <polygon points="10,8 17,12 10,16"/>
            </svg>
          </button>

          <button class="csgo-rail-btn ${this.activeSection === 'loadout' ? 'active' : ''}" data-sec="loadout" title="Инвентарь и Скины">
            <svg viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
              <path d="M4 7h16v13H4V7zm5-3h6v3H9V4z"/>
            </svg>
          </button>

          <button class="csgo-rail-btn ${this.activeSection === 'settings' ? 'active' : ''}" data-sec="settings" title="Настройки игры и физики">
            <svg viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
              <path d="M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8zm9 4l-2.1-.8c-.2-.6-.4-1.2-.8-1.7l1-2-1.4-1.4-2 1c-.5-.4-1.1-.6-1.7-.8L13.2 4h-2.4l-.8 2.1c-.6.2-1.2.4-1.7.8l-2-1L4.9 7.3l1 2c-.4.5-.6 1.1-.8 1.7L3 11.8v2.4l2.1.8c.2.6.4 1.2.8 1.7l-1 2 1.4 1.4 2-1c.5.4 1.1.6 1.7.8l.8 2.1h2.4l.8-2.1c.6-.2 1.2-.4 1.7-.8l2 1 1.4-1.4-1-2c.4-.5.6-1.1.8-1.7L21 14.2v-2.4z"/>
            </svg>
          </button>

          <div class="csgo-rail-spacer"></div>

          <button class="csgo-rail-btn csgo-rail-skeet" id="mm-open-skeet" title="Открыть меню чита Skeet.cc [INSERT]">
            <span style="font-weight:900;font-size:11px;color:#a8cf0a;">INS</span>
          </button>
        </aside>

        <!-- Center Panorama Workspace -->
        <div class="csgo-center-stage">
          ${this.renderCenterStage()}
        </div>

        <!-- Far-Right CS:GO Profile, Rank & Friends Bar -->
        <aside class="csgo-right-profile">
          <div class="csgo-profile-card">
            <div class="csgo-avatar-row">
              <div class="csgo-avatar-box">CT</div>
              <div class="csgo-player-info">
                <div class="csgo-player-name">${cfg.misc.playerName}</div>
                <div class="csgo-prime-tag">✔ ПРАЙМ-СТАТУС АКТИВЕН</div>
              </div>
            </div>

            <div class="csgo-rank-row">
              <div class="csgo-rank-icon">★</div>
              <div class="csgo-rank-details">
                <div class="csgo-rank-title">Ранг 40 — Генерал армии</div>
                <div class="csgo-xp-bar"><div class="csgo-xp-fill"></div></div>
              </div>
            </div>

            <div class="csgo-rank-row csgo-skill-group">
              <div class="csgo-ge-badge">🌐</div>
              <div class="csgo-rank-details">
                <div class="csgo-rank-title" style="color:#f59e0b;">Всемирная Элита</div>
                <div class="csgo-rank-sub">The Global Elite • 884 побед</div>
              </div>
            </div>
          </div>

          <div class="csgo-friends-section">
            <div class="csgo-friends-header">БОТЫ В ЛОББИ (${matchConfig.botCount})</div>
            <div class="csgo-friends-list">
              <div class="csgo-friend-item">
                <span class="csgo-friend-dot online"></span>
                <div class="csgo-friend-meta">
                  <div class="csgo-friend-name">gamesense.pub [UID:42]</div>
                  <div class="csgo-friend-status">Готов к игре • ${matchConfig.botBehavior.toUpperCase()}</div>
                </div>
              </div>
              <div class="csgo-friend-item">
                <span class="csgo-friend-dot online"></span>
                <div class="csgo-friend-meta">
                  <div class="csgo-friend-name">skeet.cc_enjoyer</div>
                  <div class="csgo-friend-status">В лобби • ${matchConfig.mapId}</div>
                </div>
              </div>
              <div class="csgo-friend-item">
                <span class="csgo-friend-dot online"></span>
                <div class="csgo-friend-meta">
                  <div class="csgo-friend-name">neverlose_king</div>
                  <div class="csgo-friend-status">Тренировка с ботами</div>
                </div>
              </div>
            </div>
          </div>

          <div class="csgo-right-footer" style="display:flex;flex-direction:column;gap:8px;">
            <button class="csgo-skeet-launch-btn" id="mm-open-skeet-side">
              ОТКРЫТЬ SKEET.CC [INSERT]
            </button>
            <a href="/CSGO_HVH_GAME.html" download="CSGO_HVH_GAME.html" class="csgo-go-btn" style="text-align:center;text-decoration:none;font-size:12px;padding:9px 10px;display:block;">
              ⬇ СКАЧАТЬ ИГРУ НА ПК (.HTML)
            </a>
          </div>
        </aside>
      </div>
    `;

    this.bindEvents();
  }

  renderCenterStage() {
    if (this.activeSection === 'home') {
      return `
        <div class="csgo-home-view">
          <div class="csgo-news-panel">
            <div class="csgo-news-tag">COUNTER-STRIKE: GLOBAL OFFENSIVE</div>
            <h2>ТРЕНИРОВКА С БОТАМИ И HVH ПЕСОЧНИЦА</h2>
            <p>Настоящая физика Source Engine (<code>sv_friction 5.2</code>, <code>sv_accelerate 5.5</code>, <code>sv_airaccelerate</code>), 8 культовых 3D-карт и точная копия меню чита <b>Gamesense.pub (Skeet.cc)</b> на клавишу <kbd>INSERT</kbd>.</p>
            <div class="csgo-home-actions">
              <button class="csgo-go-btn" id="mm-home-play-btn">ПЕРЕЙТИ К ВЫБОРУ КАРТЫ ▶</button>
              <button class="csgo-go-btn" id="mm-start-btn">НАЧАТЬ БЫСТРЫЙ МАТЧ</button>
            </div>
          </div>
        </div>
      `;
    }

    if (this.activeSection === 'play') {
      return `
        <div class="csgo-play-screen">
          <!-- Top Panorama Play Header -->
          <div class="csgo-play-header">
            <div class="csgo-play-title-row">
              <span class="csgo-play-main-title">ИГРАТЬ</span>
              <div class="csgo-play-top-tabs">
                <button class="csgo-top-tab active">ТРЕНИРОВКА С БОТАМИ</button>
                <button class="csgo-top-tab" id="mm-tab-hvh">HVH СЕРВЕР (КРУТИЛКА)</button>
              </div>
            </div>

            <!-- Sub-mode & Bot Settings Bar -->
            <div class="csgo-play-subbar">
              <div class="csgo-submodes">
                <button class="csgo-mode-tab ${matchConfig.gameMode === 'dm' ? 'active' : ''}" data-gm="dm">БОЙ НАСМЕРТЬ</button>
                <button class="csgo-mode-tab ${matchConfig.gameMode === 'rounds' ? 'active' : ''}" data-gm="rounds">СОРЕВНОВАТЕЛЬНЫЙ (РАУНДЫ)</button>
                <button class="csgo-mode-tab ${matchConfig.gameMode === 'hvh_arena' ? 'active' : ''}" data-gm="hvh_arena">НАПАРНИКИ HVH</button>
              </div>

              <div class="csgo-bot-filters">
                <select id="mm-botbehavior" class="csgo-pano-select">
                  <option value="legit" ${matchConfig.botBehavior === 'legit' ? 'selected' : ''}>Обычные боты CS:GO</option>
                  <option value="hvh" ${matchConfig.botBehavior === 'hvh' ? 'selected' : ''}>Опытные боты (HvH Крутилка)</option>
                  <option value="moving" ${matchConfig.botBehavior === 'moving' ? 'selected' : ''}>Бегающие боты (Без стрельбы)</option>
                  <option value="static" ${matchConfig.botBehavior === 'static' ? 'selected' : ''}>Безобидные боты (Стоят)</option>
                </select>

                <select id="mm-botweapons" class="csgo-pano-select">
                  <option value="all" ${matchConfig.botWeapons === 'all' ? 'selected' : ''}>Все оружие</option>
                  <option value="snipers" ${matchConfig.botWeapons === 'snipers' ? 'selected' : ''}>Только снайперки (Scout/Auto/AWP)</option>
                  <option value="pistols" ${matchConfig.botWeapons === 'pistols' ? 'selected' : ''}>Только пистолеты (Deagle/R8)</option>
                  <option value="knives" ${matchConfig.botWeapons === 'knives' ? 'selected' : ''}>Только ножи</option>
                </select>

                <div class="csgo-botcount-box">
                  <span>Ботов: <b id="mm-botcount-val">${matchConfig.botCount}</b></span>
                  <input type="range" id="mm-botcount" min="0" max="10" step="1" value="${matchConfig.botCount}" />
                </div>
              </div>
            </div>
          </div>

          <!-- Center Tall Vertical CS:GO Map Cards Grid -->
          <div class="csgo-map-cards-grid">
            ${MAP_LIST.map((m, idx) => {
              const selected = matchConfig.mapId === m.id;
              return `
                <div class="csgo-map-tall-card ${selected ? 'selected' : ''}" data-map-id="${m.id}">
                  <div class="csgo-map-card-bg" style="background: linear-gradient(180deg, rgba(18,26,38,0.5) 0%, ${m.badgeColor}33 60%, rgba(10,14,22,0.95) 100%);"></div>
                  ${selected ? '<div class="csgo-map-check">✔</div>' : ''}
                  <div class="csgo-map-emblem" style="border-color: ${m.badgeColor}">
                    <span>${m.shortName.replace('de_', '').replace('aim_', '').slice(0, 3).toUpperCase()}</span>
                  </div>
                  <div class="csgo-map-card-footer">
                    <div class="csgo-map-card-name">${m.name.replace('DE_', '')}</div>
                    <div class="csgo-map-card-sub">${m.subtitle}</div>
                  </div>
                </div>
              `;
            }).join('')}
          </div>

          <!-- Bottom Panorama Action Bar with Giant Green "НАЧАТЬ" Button -->
          <div class="csgo-play-bottombar">
            <div class="csgo-bottom-options">
              <label class="csgo-pano-check">
                <input type="checkbox" id="mm-infammo" ${matchConfig.infiniteAmmo ? 'checked' : ''} />
                <span>Бесконечные патроны</span>
              </label>
              <select id="mm-tickrate" class="csgo-pano-select">
                <option value="64" ${matchConfig.tickrate === 64 ? 'selected' : ''}>Сервер 64 Tick</option>
                <option value="128" ${matchConfig.tickrate === 128 ? 'selected' : ''}>Сервер 128 Tick</option>
              </select>
              <select id="mm-hp" class="csgo-pano-select">
                <option value="100" ${matchConfig.startingHp === 100 ? 'selected' : ''}>100 HP</option>
                <option value="200" ${matchConfig.startingHp === 200 ? 'selected' : ''}>200 HP</option>
                <option value="500" ${matchConfig.startingHp === 500 ? 'selected' : ''}>500 HP</option>
              </select>
            </div>

            <div class="csgo-bottom-buttons">
              ${
                this.hasStartedMatchOnce
                  ? `<button class="csgo-resume-btn" id="mm-resume-match">ВЕРНУТЬСЯ В МАТЧ [ESC]</button>`
                  : ''
              }
              <button class="csgo-go-btn" id="mm-start-btn">НАЧАТЬ</button>
            </div>
          </div>
        </div>
      `;
    }

    if (this.activeSection === 'loadout') {
      const skinOptions = Object.entries(SKIN_PALETTES).map(([k, v]) => ({ value: k, label: v.name }));
      return `
        <div class="csgo-play-screen">
          <div class="csgo-play-header">
            <div class="csgo-play-title-row">
              <span class="csgo-play-main-title">ИНВЕНТАРЬ И СНАРЯЖЕНИЕ</span>
            </div>
          </div>
          <div class="csgo-inventory-body">
            <div class="mm-weapons-grid">
              ${WEAPON_ORDER.map((wid) => {
                const w = WEAPONS[wid];
                return `
                  <div class="mm-weapon-card" data-equip-wep="${wid}">
                    <div class="mm-wep-top">
                      <span>СЛОТ ${w.slot}</span>
                      <span>${w.category}</span>
                    </div>
                    <div class="mm-wep-name">${w.name}</div>
                    <div class="mm-wep-stats">
                      <span>Урон: <b>${w.damage}</b> • Скорость: <b>${w.maxSpeed} u/s</b></span>
                    </div>
                    <button class="mm-equip-btn">ЭКИПИРОВАТЬ</button>
                  </div>
                `;
              }).join('')}
            </div>

            <div class="csgo-skin-bar">
              <select id="mm-knife-model" class="csgo-pano-select">
                <option value="karambit" ${cfg.skins.knifeModel === 'karambit' ? 'selected' : ''}>★ Керамбит (Karambit)</option>
                <option value="m9" ${cfg.skins.knifeModel === 'm9' ? 'selected' : ''}>★ Штык-нож M9 (M9 Bayonet)</option>
                <option value="butterfly" ${cfg.skins.knifeModel === 'butterfly' ? 'selected' : ''}>★ Нож-бабочка (Butterfly)</option>
              </select>
              <select id="mm-knife-skin" class="csgo-pano-select">
                ${skinOptions.map((o) => `<option value="${o.value}" ${o.value === cfg.skins.knifeSkin ? 'selected' : ''}>Скин ножа: ${o.label}</option>`).join('')}
              </select>
              <select id="mm-weapon-skin" class="csgo-pano-select">
                ${skinOptions.map((o) => `<option value="${o.value}" ${o.value === cfg.skins.weaponSkin ? 'selected' : ''}>Скин оружия: ${o.label}</option>`).join('')}
              </select>
              <button class="csgo-go-btn" id="mm-start-btn">НАЧАТЬ</button>
            </div>
          </div>
        </div>
      `;
    }

    // Settings
    return `
      <div class="csgo-play-screen">
        <div class="csgo-play-header">
          <div class="csgo-play-title-row">
            <span class="csgo-play-main-title">НАСТРОЙКИ ИГРЫ И ФИЗИКИ SOURCE</span>
          </div>
        </div>
        <div class="csgo-settings-body">
          <div class="mm-field">
            <div class="mm-field-row"><label>Чувствительность мыши</label><span id="mm-sens-val">${matchConfig.mouseSensitivity}</span></div>
            <input type="range" id="mm-sens" min="0.5" max="6.0" step="0.1" value="${matchConfig.mouseSensitivity}" />
          </div>
          <div class="mm-field">
            <div class="mm-field-row"><label>sv_friction (Трение о землю — стандарт CS:GO 5.2)</label><span id="mm-fric-val">${cfg.movement.sv_friction}</span></div>
            <input type="range" id="mm-fric" min="1.0" max="10.0" step="0.1" value="${cfg.movement.sv_friction}" />
          </div>
          <div class="mm-field">
            <div class="mm-field-row"><label>sv_accelerate (Наземное ускорение — стандарт CS:GO 5.5)</label><span id="mm-accel-val">${cfg.movement.sv_accelerate}</span></div>
            <input type="range" id="mm-accel" min="2.0" max="15.0" step="0.5" value="${cfg.movement.sv_accelerate}" />
          </div>
          <div class="mm-field">
            <div class="mm-field-row"><label>sv_airaccelerate (Воздушный стрейф — ММ 12 / Bhop 28–100)</label><span id="mm-airaccel-val">${cfg.movement.sv_airaccelerate}</span></div>
            <input type="range" id="mm-airaccel" min="10" max="150" step="1" value="${cfg.movement.sv_airaccelerate}" />
          </div>
          <div class="mm-field">
            <div class="mm-field-row"><label>sv_gravity (Гравитация — стандарт CS:GO 800)</label><span id="mm-grav-val">${cfg.movement.sv_gravity}</span></div>
            <input type="range" id="mm-grav" min="300" max="1200" step="10" value="${cfg.movement.sv_gravity}" />
          </div>
          <button class="csgo-go-btn" id="mm-start-btn" style="align-self:flex-end;">НАЧАТЬ</button>
        </div>
      </div>
    `;
  }

  bindEvents() {
    this.container.querySelectorAll('.csgo-rail-btn[data-sec]').forEach((btn) => {
      btn.addEventListener('click', () => {
        this.activeSection = btn.dataset.sec;
        this.render();
      });
    });

    const homePlay = document.getElementById('mm-home-play-btn');
    if (homePlay) {
      homePlay.addEventListener('click', () => {
        this.activeSection = 'play';
        this.render();
      });
    }

    const openSkeet1 = document.getElementById('mm-open-skeet');
    const openSkeet2 = document.getElementById('mm-open-skeet-side');
    [openSkeet1, openSkeet2].forEach((b) => {
      if (b) {
        b.addEventListener('click', () => {
          if (this.onOpenCheatMenu) this.onOpenCheatMenu();
        });
      }
    });

    const tabHvh = document.getElementById('mm-tab-hvh');
    if (tabHvh) {
      tabHvh.addEventListener('click', () => {
        matchConfig.gameMode = 'hvh_arena';
        matchConfig.botBehavior = 'hvh';
        this.render();
      });
    }

    this.container.querySelectorAll('[data-gm]').forEach((btn) => {
      btn.addEventListener('click', () => {
        matchConfig.gameMode = btn.dataset.gm;
        if (matchConfig.gameMode === 'hvh_arena') {
          matchConfig.botBehavior = 'hvh';
          cfg.movement.sv_airaccelerate = 32;
        }
        this.render();
      });
    });

    this.container.querySelectorAll('.csgo-map-tall-card').forEach((card) => {
      card.addEventListener('click', () => {
        matchConfig.mapId = card.dataset.mapId;
        this.render();
      });
    });

    const bc = document.getElementById('mm-botcount');
    if (bc) {
      bc.addEventListener('input', () => {
        matchConfig.botCount = Number(bc.value);
        document.getElementById('mm-botcount-val').textContent = `${matchConfig.botCount}`;
      });
    }

    const bb = document.getElementById('mm-botbehavior');
    if (bb) bb.addEventListener('change', () => { matchConfig.botBehavior = bb.value; });

    const bw = document.getElementById('mm-botweapons');
    if (bw) bw.addEventListener('change', () => { matchConfig.botWeapons = bw.value; });

    const hp = document.getElementById('mm-hp');
    if (hp) hp.addEventListener('change', () => { matchConfig.startingHp = Number(hp.value); });

    const tr = document.getElementById('mm-tickrate');
    if (tr) tr.addEventListener('change', () => { matchConfig.tickrate = Number(tr.value); });

    const inf = document.getElementById('mm-infammo');
    if (inf) inf.addEventListener('change', () => { matchConfig.infiniteAmmo = inf.checked; });

    this.container.querySelectorAll('[data-equip-wep]').forEach((card) => {
      card.addEventListener('click', () => {
        if (this.onWeaponSelect) this.onWeaponSelect(card.dataset.equipWep);
        this.hide();
        if (this.onStartMatch) this.onStartMatch();
      });
    });

    const km = document.getElementById('mm-knife-model');
    if (km) km.addEventListener('change', () => { cfg.skins.knifeModel = km.value; if (this.onSkinUpdate) this.onSkinUpdate(); });
    const ks = document.getElementById('mm-knife-skin');
    if (ks) ks.addEventListener('change', () => { cfg.skins.knifeSkin = ks.value; if (this.onSkinUpdate) this.onSkinUpdate(); });
    const ws = document.getElementById('mm-weapon-skin');
    if (ws) ws.addEventListener('change', () => { cfg.skins.weaponSkin = ws.value; if (this.onSkinUpdate) this.onSkinUpdate(); });

    const sens = document.getElementById('mm-sens');
    if (sens) sens.addEventListener('input', () => { matchConfig.mouseSensitivity = Number(sens.value); document.getElementById('mm-sens-val').textContent = `${matchConfig.mouseSensitivity}`; });
    const fric = document.getElementById('mm-fric');
    if (fric) fric.addEventListener('input', () => { cfg.movement.sv_friction = Number(fric.value); document.getElementById('mm-fric-val').textContent = `${cfg.movement.sv_friction}`; });
    const acc = document.getElementById('mm-accel');
    if (acc) acc.addEventListener('input', () => { cfg.movement.sv_accelerate = Number(acc.value); document.getElementById('mm-accel-val').textContent = `${cfg.movement.sv_accelerate}`; });
    const air = document.getElementById('mm-airaccel');
    if (air) air.addEventListener('input', () => { cfg.movement.sv_airaccelerate = Number(air.value); document.getElementById('mm-airaccel-val').textContent = `${cfg.movement.sv_airaccelerate}`; });
    const grav = document.getElementById('mm-grav');
    if (grav) grav.addEventListener('input', () => { cfg.movement.sv_gravity = Number(grav.value); document.getElementById('mm-grav-val').textContent = `${cfg.movement.sv_gravity}`; });

    const resumeBtn = document.getElementById('mm-resume-match');
    if (resumeBtn) {
      resumeBtn.addEventListener('click', () => {
        this.hide();
        if (this.onResumeMatch) this.onResumeMatch();
      });
    }

    const startBtn = document.getElementById('mm-start-btn');
    if (startBtn) {
      startBtn.addEventListener('click', () => {
        this.hasStartedMatchOnce = true;
        this.hide();
        if (this.onStartMatch) this.onStartMatch();
      });
    }
  }
}
