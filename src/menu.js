// 1:1 Pixel-Accurate Gamesense.pub (Skeet.cc) Cheat Menu UI (Toggled on INSERT)
// Matches /home/user/uploads/image-1.png down to the double-bevel frame,
// 2px rainbow line, 6 left sidebar icons (including two interlocking gears),
// groupbox titles overlapping the top border, 8x8 checkboxes, right-aligned
// [-]/[KEY] bind tags, and 21px-indented sliders/dropdowns/buttons.

import { cfg, SKIN_PALETTES, applyPreset, resetConfigToClean } from './config.js';

export class CheatMenuUI {
  constructor({ getBots, onSkinChange, onStealName, onDumpWins, onLogEvent }) {
    this.getBots = getBots;
    this.onSkinChange = onSkinChange;
    this.onStealName = onStealName;
    this.onDumpWins = onDumpWins;
    this.onLogEvent = onLogEvent;

    this.isOpen = false;
    this.activeTab = 'misc'; // Opens on Misc tab to showcase 1:1 match with image-1.png
    this.selectedPlayerIndex = 0;
    this.waitingForBindKey = null;

    this.pos = { x: Math.max(40, Math.round((window.innerWidth - 660) * 0.5)), y: Math.max(30, Math.round((window.innerHeight - 560) * 0.5)) };
    this.dragging = false;
    this.dragOffset = { x: 0, y: 0 };

    this.overlay = document.getElementById('skeet-overlay');
    this.buildDOM();
    this.bindGlobalKeyCapture();
  }

  toggle(forceState) {
    this.isOpen = typeof forceState === 'boolean' ? forceState : !this.isOpen;
    if (this.isOpen) {
      this.overlay.classList.remove('hidden');
      if (document.pointerLockElement) {
        document.exitPointerLock();
      }
      this.renderContent();
    } else {
      this.overlay.classList.add('hidden');
      this.waitingForBindKey = null;
    }
  }

  formatKeyName(code) {
    if (!code || code === 'None') return '-';
    return code
      .replace('Key', '')
      .replace('Digit', '')
      .replace('ShiftLeft', 'LSHIFT')
      .replace('ShiftRight', 'RSHIFT')
      .replace('ControlLeft', 'LCTRL')
      .replace('AltLeft', 'LALT')
      .toUpperCase();
  }

  bindGlobalKeyCapture() {
    window.addEventListener('keydown', (e) => {
      if (!this.waitingForBindKey) return;
      e.preventDefault();
      e.stopPropagation();
      const bindKey = this.waitingForBindKey;
      if (e.code === 'Escape' || e.code === 'Backspace') {
        cfg.binds[bindKey] = 'None';
      } else if (e.code !== 'Insert') {
        cfg.binds[bindKey] = e.code;
      }
      this.waitingForBindKey = null;
      this.renderContent();
    });
  }

  buildDOM() {
    this.overlay.innerHTML = '';

    const win = document.createElement('div');
    win.className = 'skeet-window';
    win.style.left = `${this.pos.x}px`;
    win.style.top = `${this.pos.y}px`;
    this.winEl = win;

    // Outer Bevel -> Dark Steel Frame -> Inner Black Line -> Rainbow Bar -> Body
    win.innerHTML = `
      <div class="skeet-bevel-frame">
        <div class="skeet-inner-frame">
          <div class="skeet-rainbow-line" id="skeet-drag-bar" title="Drag Gamesense.pub window"></div>
          <div class="skeet-main-area">
            <aside class="skeet-sidebar" id="skeet-sidebar"></aside>
            <main class="skeet-body" id="skeet-content"></main>
          </div>
        </div>
      </div>
    `;

    this.overlay.appendChild(win);

    // Drag window from rainbow bar or empty sidebar area
    const dragBar = win.querySelector('#skeet-drag-bar');
    const sidebar = win.querySelector('#skeet-sidebar');
    const startDrag = (e) => {
      if (e.target.closest('.skeet-tab-btn')) return;
      this.dragging = true;
      this.dragOffset.x = e.clientX - this.pos.x;
      this.dragOffset.y = e.clientY - this.pos.y;
    };
    dragBar.addEventListener('mousedown', startDrag);
    sidebar.addEventListener('mousedown', startDrag);

    window.addEventListener('mousemove', (e) => {
      if (!this.dragging) return;
      this.pos.x = Math.max(6, Math.min(window.innerWidth - 670, e.clientX - this.dragOffset.x));
      this.pos.y = Math.max(6, Math.min(window.innerHeight - 570, e.clientY - this.dragOffset.y));
      this.winEl.style.left = `${this.pos.x}px`;
      this.winEl.style.top = `${this.pos.y}px`;
    });

    window.addEventListener('mouseup', () => {
      this.dragging = false;
    });

    this.renderSidebar();
    this.renderContent();
  }

  // Exact 6 Sidebar Icons from Gamesense.pub / Skeet.cc screenshot (image-1.png)
  getSidebarTabs() {
    return [
      {
        id: 'rage',
        title: 'Ragebot & Anti-Aim',
        svg: `
          <svg viewBox="0 0 40 40" width="34" height="34">
            <circle cx="20" cy="20" r="12" fill="none" stroke="currentColor" stroke-width="3"/>
            <circle cx="20" cy="20" r="6" fill="none" stroke="currentColor" stroke-width="2.2"/>
            <circle cx="20" cy="20" r="2" fill="currentColor"/>
            <line x1="20" y1="3" x2="20" y2="11" stroke="currentColor" stroke-width="2.8"/>
            <line x1="20" y1="29" x2="20" y2="37" stroke="currentColor" stroke-width="2.8"/>
            <line x1="3" y1="20" x2="11" y2="20" stroke="currentColor" stroke-width="2.8"/>
            <line x1="29" y1="20" x2="37" y2="20" stroke="currentColor" stroke-width="2.8"/>
          </svg>
        `
      },
      {
        id: 'aa',
        title: 'Anti-Aim / Legit',
        svg: `
          <svg viewBox="0 0 40 40" width="34" height="34">
            <path d="M7 22h8l3-5h8l5-4v5l-4 3v7h-5l-3-4h-4l-3 8H6l3-10z" fill="currentColor"/>
          </svg>
        `
      },
      {
        id: 'visuals',
        title: 'Visuals & Chams',
        svg: `
          <svg viewBox="0 0 40 40" width="34" height="34">
            <circle cx="20" cy="12" r="5.5" fill="currentColor"/>
            <path d="M8 17c7 1.5 17 1.5 24 0l-2 4-5-0.8v7l3 9h-4.5l-2.5-8-2.5 8H14l3-9v-7L12 21l-4-4z" fill="currentColor"/>
          </svg>
        `
      },
      {
        // Exact Two Interlocking Gears Icon (active in image-1.png)
        id: 'misc',
        title: 'Miscellaneous & Settings',
        svg: `
          <svg viewBox="0 0 42 42" width="36" height="36">
            <g fill="currentColor">
              <!-- Bottom-Left Larger Gear -->
              <path d="M16 15l1.8 2.2 2.7-.6.9 2.6 2.5 1.2-1.1 2.5 1.1 2.5-2.5 1.2-.9 2.6-2.7-.6L16 31l-1.8-2.2-2.7.6-.9-2.6-2.5-1.2 1.1-2.5-1.1-2.5 2.5-1.2.9-2.6 2.7.6L16 15zm0 5a3 3 0 1 0 0 6 3 3 0 0 0 0-6z"/>
              <!-- Top-Right Smaller Gear -->
              <path d="M28 8l1.3 1.7 2.1-.4.7 2 1.9.9-.8 2 .8 2-1.9.9-.7 2-2.1-.4L28 20.5l-1.3-1.7-2.1.4-.7-2-1.9-.9.8-2-.8-2 1.9-.9.7-2 2.1.4L28 8zm0 3.8a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 0 0 0-4.8z"/>
            </g>
          </svg>
        `
      },
      {
        id: 'skins',
        title: 'Skins & Knife Changer',
        svg: `
          <svg viewBox="0 0 40 40" width="34" height="34">
            <path d="M20 5l12 5v9c0 8.5-5.2 14.5-12 17-6.8-2.5-12-8.5-12-17v-9l12-5zm0 4.2L11.5 13v5.5c0 6.2 3.6 10.9 8.5 13 4.9-2.1 8.5-6.8 8.5-13V13L20 9.2z" fill="currentColor"/>
            <path d="M15 16l5 3 5-3v6l-5 3-5-3v-6z" fill="currentColor"/>
          </svg>
        `
      },
      {
        id: 'players',
        title: 'Players List',
        svg: `
          <svg viewBox="0 0 40 40" width="34" height="34">
            <circle cx="15" cy="14" r="4.5" fill="currentColor"/>
            <path d="M7 28c0-4.2 3.6-7 8-7s8 2.8 8 7v2H7v-2z" fill="currentColor"/>
            <circle cx="26" cy="12" r="3.8" fill="currentColor" opacity="0.85"/>
            <path d="M23 26c1.2-3 3.6-5 7-5 2.5 0 4.5 1.2 5.5 3.5V27H23v-1z" fill="currentColor" opacity="0.85"/>
          </svg>
        `
      }
    ];
  }

  renderSidebar() {
    const sb = this.winEl.querySelector('#skeet-sidebar');
    sb.innerHTML = '';

    this.getSidebarTabs().forEach((tab) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = `skeet-tab-btn ${this.activeTab === tab.id ? 'active' : ''}`;
      btn.title = tab.title;
      btn.innerHTML = tab.svg;
      btn.addEventListener('click', () => {
        this.activeTab = tab.id;
        this.renderSidebar();
        this.renderContent();
      });
      sb.appendChild(btn);
    });
  }

  // ==========================================================================
  // SKEET UI PRIMITIVES (1:1 WITH IMAGE-1.PNG)
  // ==========================================================================
  makeGroupBox(title) {
    const box = document.createElement('div');
    box.className = 'skeet-groupbox';
    const titleEl = document.createElement('span');
    titleEl.className = 'skeet-group-title';
    titleEl.textContent = title;
    box.appendChild(titleEl);

    const inner = document.createElement('div');
    inner.className = 'skeet-group-inner';
    box.appendChild(inner);
    return { box, inner };
  }

  addCheckbox(parent, label, checked, onChange, options = {}) {
    const row = document.createElement('div');
    row.className = 'skeet-check-row';

    const left = document.createElement('div');
    left.className = 'skeet-check-left';

    const box = document.createElement('span');
    box.className = `skeet-checkbox ${checked ? 'checked' : ''}`;

    const text = document.createElement('span');
    text.className = 'skeet-check-label';
    text.textContent = label;

    left.appendChild(box);
    left.appendChild(text);
    row.appendChild(left);

    const right = document.createElement('div');
    right.className = 'skeet-check-right';

    if (options.colorValue) {
      const swatch = document.createElement('input');
      swatch.type = 'color';
      swatch.className = 'skeet-color-swatch';
      swatch.value = options.colorValue;
      swatch.addEventListener('input', (e) => {
        e.stopPropagation();
        if (options.onColorChange) options.onColorChange(swatch.value);
      });
      swatch.addEventListener('click', (e) => e.stopPropagation());
      right.appendChild(swatch);
    }

    if (options.bindKey) {
      const bindSpan = document.createElement('span');
      bindSpan.className = 'skeet-keybind';
      const isWaiting = this.waitingForBindKey === options.bindKey;
      bindSpan.textContent = isWaiting ? '[...]' : `[${this.formatKeyName(cfg.binds[options.bindKey])}]`;
      bindSpan.title = 'Left-click to rebind key (ESC to clear)';
      bindSpan.addEventListener('click', (e) => {
        e.stopPropagation();
        this.waitingForBindKey = options.bindKey;
        this.renderContent();
      });
      right.appendChild(bindSpan);
    }

    row.appendChild(right);

    left.addEventListener('click', () => {
      const next = !box.classList.contains('checked');
      box.classList.toggle('checked', next);
      onChange(next);
    });

    parent.appendChild(row);
  }

  addColorRow(parent, label, colorValue, onColorChange) {
    const row = document.createElement('div');
    row.className = 'skeet-color-row';
    const lbl = document.createElement('span');
    lbl.className = 'skeet-indented-label';
    lbl.textContent = label;

    const swatch = document.createElement('input');
    swatch.type = 'color';
    swatch.className = 'skeet-color-swatch';
    swatch.value = colorValue;
    swatch.addEventListener('input', () => onColorChange(swatch.value));

    row.appendChild(lbl);
    row.appendChild(swatch);
    parent.appendChild(row);
  }

  addSlider(parent, label, min, max, step, value, unit, onChange, formatMap = null) {
    const wrap = document.createElement('div');
    wrap.className = 'skeet-control-block';

    if (label) {
      const lbl = document.createElement('div');
      lbl.className = 'skeet-control-label';
      lbl.textContent = label;
      wrap.appendChild(lbl);
    }

    const track = document.createElement('div');
    track.className = 'skeet-slider-track';

    const fill = document.createElement('div');
    fill.className = 'skeet-slider-fill';

    const valOverlay = document.createElement('span');
    valOverlay.className = 'skeet-slider-value';

    const formatVal = (v) => {
      if (formatMap && formatMap[v] !== undefined) return formatMap[v];
      return `${v}${unit || ''}`;
    };

    const updateVisual = (v) => {
      const pct = Math.max(0, Math.min(100, ((v - min) / (max - min)) * 100));
      fill.style.width = `${pct}%`;
      valOverlay.textContent = formatVal(v);
    };
    updateVisual(value);

    track.appendChild(fill);
    track.appendChild(valOverlay);

    const setFromClientX = (clientX) => {
      const rect = track.getBoundingClientRect();
      const frac = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
      const raw = min + frac * (max - min);
      const stepped = Math.round(raw / step) * step;
      const fixed = Number(stepped.toFixed(step < 1 ? 1 : 0));
      updateVisual(fixed);
      onChange(fixed);
    };

    let sliding = false;
    track.addEventListener('mousedown', (e) => {
      e.stopPropagation();
      sliding = true;
      setFromClientX(e.clientX);
      const moveHandler = (me) => {
        if (sliding) setFromClientX(me.clientX);
      };
      const upHandler = () => {
        sliding = false;
        window.removeEventListener('mousemove', moveHandler);
        window.removeEventListener('mouseup', upHandler);
      };
      window.addEventListener('mousemove', moveHandler);
      window.addEventListener('mouseup', upHandler);
    });

    wrap.appendChild(track);
    parent.appendChild(wrap);
  }

  addDropdown(parent, label, options, selectedValue, onChange, bindKey = null) {
    const wrap = document.createElement('div');
    wrap.className = 'skeet-control-block';

    if (label) {
      const hdr = document.createElement('div');
      hdr.className = 'skeet-control-header';
      const lbl = document.createElement('span');
      lbl.className = 'skeet-control-label';
      lbl.textContent = label;
      hdr.appendChild(lbl);

      if (bindKey) {
        const bindSpan = document.createElement('span');
        bindSpan.className = 'skeet-keybind';
        const isWaiting = this.waitingForBindKey === bindKey;
        bindSpan.textContent = isWaiting ? '[...]' : `[${this.formatKeyName(cfg.binds[bindKey])}]`;
        bindSpan.addEventListener('click', (e) => {
          e.stopPropagation();
          this.waitingForBindKey = bindKey;
          this.renderContent();
        });
        hdr.appendChild(bindSpan);
      }
      wrap.appendChild(hdr);
    }

    const selectWrap = document.createElement('div');
    selectWrap.className = 'skeet-select-wrap';

    const select = document.createElement('select');
    select.className = 'skeet-select';
    options.forEach((opt) => {
      const o = document.createElement('option');
      o.value = opt.value;
      o.textContent = opt.label;
      if (String(opt.value) === String(selectedValue)) o.selected = true;
      select.appendChild(o);
    });
    select.addEventListener('change', () => onChange(select.value));

    selectWrap.appendChild(select);
    wrap.appendChild(selectWrap);
    parent.appendChild(wrap);
  }

  addButton(parent, label, onClick) {
    const wrap = document.createElement('div');
    wrap.className = 'skeet-control-block';
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'skeet-button';
    btn.textContent = label;
    btn.addEventListener('click', onClick);
    wrap.appendChild(btn);
    parent.appendChild(wrap);
  }

  // ==========================================================================
  // RENDER TABS
  // ==========================================================================
  renderContent() {
    const container = this.winEl.querySelector('#skeet-content');
    container.innerHTML = '';

    const colLeft = document.createElement('div');
    colLeft.className = 'skeet-col';
    const colRight = document.createElement('div');
    colRight.className = 'skeet-col';
    container.appendChild(colLeft);
    container.appendChild(colRight);

    if (this.activeTab === 'misc') {
      this.renderMiscTab(colLeft, colRight);
    } else if (this.activeTab === 'rage') {
      this.renderRageTab(colLeft, colRight);
    } else if (this.activeTab === 'aa') {
      this.renderAntiAimTab(colLeft, colRight);
    } else if (this.activeTab === 'visuals') {
      this.renderVisualsTab(colLeft, colRight);
    } else if (this.activeTab === 'skins') {
      this.renderSkinsTab(colLeft, colRight);
    } else if (this.activeTab === 'players') {
      this.renderPlayersTab(colLeft, colRight);
    }
  }

  // ==========================================================================
  // TAB 4: MISCELLANEOUS (EXACT 1:1 CLONE OF IMAGE-1.PNG)
  // ==========================================================================
  renderMiscTab(colLeft, colRight) {
    // Left Column: "Miscellaneous" (Full height, exact items from image-1.png)
    const miscGroup = this.makeGroupBox('Miscellaneous');
    miscGroup.box.classList.add('skeet-groupbox-full');
    const m = miscGroup.inner;

    this.addSlider(m, 'Override FOV', 60, 120, 1, cfg.visuals.cameraFov, '°', (v) => {
      cfg.visuals.cameraFov = v;
    });
    this.addCheckbox(m, 'Knifebot', cfg.misc.knifebot, (v) => {
      cfg.misc.knifebot = v;
    });
    this.addCheckbox(m, 'Automatic weapons', cfg.misc.autoWeapons, (v) => {
      cfg.misc.autoWeapons = v;
    });
    this.addCheckbox(m, 'Jump at edge', cfg.movement.edgeJump, (v) => {
      cfg.movement.edgeJump = v;
    }, { bindKey: 'edgeJump' });
    this.addCheckbox(m, 'Bunny hop', cfg.movement.bhop, (v) => {
      cfg.movement.bhop = v;
    });
    this.addCheckbox(m, 'Air strafe', cfg.movement.autoStrafe, (v) => {
      cfg.movement.autoStrafe = v;
    });
    this.addDropdown(
      m,
      'Air strafe',
      [
        { value: 'view', label: 'View angles' },
        { value: 'directional', label: 'WASD directional' },
        { value: 'rage', label: 'Rage circle-strafe' }
      ],
      cfg.movement.autoStrafeMode,
      (v) => {
        cfg.movement.autoStrafeMode = v;
      }
    );
    this.addCheckbox(m, 'Z-Hop', cfg.movement.zHop, (v) => {
      cfg.movement.zHop = v;
    }, { bindKey: 'zHop' });
    this.addCheckbox(m, 'Pre-speed', cfg.movement.preSpeed, (v) => {
      cfg.movement.preSpeed = v;
    }, { bindKey: 'preSpeed' });
    this.addCheckbox(m, 'Duck in air', cfg.movement.duckInAir, (v) => {
      cfg.movement.duckInAir = v;
    });
    this.addDropdown(
      m,
      'Air stuck',
      [
        { value: 'off', label: 'Off' },
        { value: 'hold', label: 'On hotkey' },
        { value: 'toggle', label: 'Toggle' }
      ],
      cfg.movement.airStuckMode,
      (v) => {
        cfg.movement.airStuckMode = v;
      },
      'airStuck'
    );
    this.addCheckbox(m, 'Force thirdperson', cfg.visuals.thirdperson, (v) => {
      cfg.visuals.thirdperson = v;
    }, { bindKey: 'thirdperson' });
    this.addCheckbox(m, 'Reveal competitive ranks', cfg.misc.revealRanks, (v) => {
      cfg.misc.revealRanks = v;
    });
    this.addCheckbox(m, 'Auto-accept matchmaking', cfg.misc.autoAccept, (v) => {
      cfg.misc.autoAccept = v;
    });
    this.addCheckbox(m, 'Clan tag spammer', cfg.misc.clantagSpammer, (v) => {
      cfg.misc.clantagSpammer = v;
    });
    this.addCheckbox(m, 'Log weapon purchases', cfg.misc.eventLogs, (v) => {
      cfg.misc.eventLogs = v;
    });
    this.addCheckbox(m, 'Log damage dealt', cfg.misc.eventLogs, (v) => {
      cfg.misc.eventLogs = v;
    });
    this.addCheckbox(m, 'Quick peek assist', cfg.rage.autoPeek, (v) => {
      cfg.rage.autoPeek = v;
    }, { bindKey: 'autoPeek' });

    this.addButton(m, 'Steal player name', () => {
      if (this.onStealName) this.onStealName();
    });
    this.addButton(m, 'Dump MM wins', () => {
      if (this.onDumpWins) this.onDumpWins();
    });

    colLeft.appendChild(miscGroup.box);

    // Right Column Top: "Settings" (Exact match to image-1.png)
    const setGroup = this.makeGroupBox('Settings');
    const s = setGroup.inner;
    this.addColorRow(s, 'Menu color', '#95b806', (val) => {
      document.documentElement.style.setProperty('--skeet-green', val);
    });
    this.addCheckbox(s, 'Hide from OBS', cfg.misc.hideFromObs, (v) => {
      cfg.misc.hideFromObs = v;
    });
    this.addCheckbox(s, 'Low FPS warning', true, () => {});
    this.addCheckbox(s, 'Anti-untrusted', cfg.misc.antiUntrusted, (v) => {
      cfg.misc.antiUntrusted = v;
    });
    this.addDropdown(
      s,
      'Hitsound',
      [
        { value: 'skeet', label: 'Arena Switch (Skeet)' },
        { value: 'bell', label: 'Metallic Bell' },
        { value: 'cod', label: 'Call of Duty' },
        { value: 'off', label: 'Off' }
      ],
      cfg.misc.hitSound,
      (v) => {
        cfg.misc.hitSound = v;
      }
    );
    colRight.appendChild(setGroup.box);

    // Right Column Bottom: "Presets" (Exact match to image-1.png)
    const preGroup = this.makeGroupBox('Presets');
    preGroup.box.classList.add('skeet-groupbox-grow');
    const p = preGroup.inner;

    this.addDropdown(
      p,
      '',
      [
        { value: 'legit', label: 'Legit' },
        { value: 'rage_hvh', label: 'Rage HvH (Spin + AutoWall)' },
        { value: 'scout_jumpshot', label: 'Jump-Scout HvH' },
        { value: 'bhop_god', label: 'Bhop Movement God' },
        { value: 'clean', label: 'Clean (All Off)' }
      ],
      cfg.misc.selectedPreset || 'legit',
      (v) => {
        cfg.misc.selectedPreset = v;
      }
    );

    this.addButton(p, 'Load', () => {
      applyPreset(cfg.misc.selectedPreset || 'legit');
      if (this.onLogEvent) {
        this.onLogEvent(`<span class="log-tag">[gamesense]</span> Loaded preset: <b>${cfg.misc.selectedPreset || 'legit'}</b>`);
      }
      this.renderContent();
    });
    this.addButton(p, 'Save', () => {
      localStorage.setItem('skeet_saved_cfg', JSON.stringify(cfg));
      if (this.onLogEvent) {
        this.onLogEvent(`<span class="log-tag">[gamesense]</span> Saved configuration to local storage.`);
      }
    });
    this.addButton(p, 'Reset', () => {
      resetConfigToClean();
      if (this.onLogEvent) {
        this.onLogEvent(`<span class="log-tag">[gamesense]</span> Reset configuration to clean defaults.`);
      }
      this.renderContent();
    });
    this.addButton(p, 'Import from clipboard', async () => {
      try {
        const text = await navigator.clipboard.readText();
        const parsed = JSON.parse(text);
        Object.assign(cfg.rage, parsed.rage || {});
        Object.assign(cfg.antiaim, parsed.antiaim || {});
        Object.assign(cfg.movement, parsed.movement || {});
        Object.assign(cfg.visuals, parsed.visuals || {});
        Object.assign(cfg.misc, parsed.misc || {});
        this.renderContent();
        if (this.onLogEvent) this.onLogEvent(`<span class="log-tag">[gamesense]</span> Imported config from clipboard.`);
      } catch {
        if (this.onLogEvent) this.onLogEvent(`<span class="log-tag">[gamesense]</span> Clipboard does not contain valid JSON config.`);
      }
    });
    this.addButton(p, 'Export to clipboard', async () => {
      try {
        await navigator.clipboard.writeText(JSON.stringify(cfg));
        if (this.onLogEvent) this.onLogEvent(`<span class="log-tag">[gamesense]</span> Exported config to clipboard.`);
      } catch {
        // ignore
      }
    });
    this.addButton(p, 'Unload', () => {
      resetConfigToClean();
      this.toggle(false);
    });

    colRight.appendChild(preGroup.box);
  }

  // ==========================================================================
  // TAB 1: RAGEBOT (AIMBOT & OTHER)
  // ==========================================================================
  renderRageTab(colLeft, colRight) {
    const aimGroup = this.makeGroupBox('Aimbot');
    aimGroup.box.classList.add('skeet-groupbox-full');
    const a = aimGroup.inner;

    this.addCheckbox(a, 'Enabled', cfg.rage.enabled, (v) => {
      cfg.rage.enabled = v;
    });
    this.addDropdown(
      a,
      'Target selection',
      [
        { value: 'damage', label: 'Highest damage' },
        { value: 'fov', label: 'Near crosshair' },
        { value: 'distance', label: 'Closest distance' }
      ],
      'damage',
      () => {}
    );
    this.addCheckbox(a, 'Target hitbox: Head', cfg.rage.hitboxes.head, (v) => {
      cfg.rage.hitboxes.head = v;
    });
    this.addCheckbox(a, 'Target hitbox: Chest & Stomach', cfg.rage.hitboxes.chest, (v) => {
      cfg.rage.hitboxes.chest = v;
      cfg.rage.hitboxes.stomach = v;
    });
    this.addCheckbox(a, 'Target hitbox: Arms & Legs', cfg.rage.hitboxes.legs, (v) => {
      cfg.rage.hitboxes.legs = v;
      cfg.rage.hitboxes.arms = v;
    });
    this.addCheckbox(a, 'Multi-point evaluation', cfg.rage.multipoint, (v) => {
      cfg.rage.multipoint = v;
    });
    this.addSlider(a, 'Multi-point scale', 20, 100, 1, cfg.rage.multipointScale, '%', (v) => {
      cfg.rage.multipointScale = v;
    });
    this.addCheckbox(a, 'Automatic fire', cfg.rage.autoFire, (v) => {
      cfg.rage.autoFire = v;
    });
    this.addCheckbox(a, 'Automatic penetration (Auto-Wall)', cfg.rage.autoWall, (v) => {
      cfg.rage.autoWall = v;
    });
    this.addCheckbox(a, 'Silent aim', cfg.rage.silentAim, (v) => {
      cfg.rage.silentAim = v;
    });
    this.addSlider(a, 'Minimum hit chance', 0, 100, 1, cfg.rage.hitchance, '%', (v) => {
      cfg.rage.hitchance = v;
    });
    this.addSlider(
      a,
      'Minimum damage',
      1,
      120,
      1,
      cfg.rage.minDamage,
      '',
      (v) => {
        cfg.rage.minDamage = v;
      },
      { 101: 'HP+1', 105: 'HP+5', 110: 'HP+10', 120: 'HP+20' }
    );
    this.addCheckbox(a, 'Minimum damage override', cfg.rage.minDamageOverrideActive, (v) => {
      cfg.rage.minDamageOverrideActive = v;
    }, { bindKey: 'minDamageOverride' });
    this.addSlider(a, 'Override damage value', 1, 100, 1, cfg.rage.minDamageOverride, ' HP', (v) => {
      cfg.rage.minDamageOverride = v;
    });
    this.addCheckbox(a, 'Automatic scope', cfg.rage.autoScope, (v) => {
      cfg.rage.autoScope = v;
    });
    this.addSlider(a, 'Maximum FOV', 1, 180, 1, cfg.rage.fov, '°', (v) => {
      cfg.rage.fov = v;
    });

    colLeft.appendChild(aimGroup.box);

    const otherGroup = this.makeGroupBox('Other');
    otherGroup.box.classList.add('skeet-groupbox-full');
    const o = otherGroup.inner;

    this.addCheckbox(o, 'Remove recoil', true, () => {});
    this.addDropdown(
      o,
      'Accuracy boost (Backtrack)',
      [
        { value: 'off', label: 'Off' },
        { value: 'low', label: 'Low (6 ticks)' },
        { value: 'high', label: 'Maximum (12 ticks)' }
      ],
      cfg.rage.backtrack ? 'high' : 'off',
      (v) => {
        cfg.rage.backtrack = v !== 'off';
        cfg.rage.backtrackTicks = v === 'high' ? 12 : 6;
      }
    );
    this.addCheckbox(o, 'Quick stop (Auto-stop)', cfg.rage.autoStop, (v) => {
      cfg.rage.autoStop = v;
    });
    this.addCheckbox(o, 'Quick peek assist (3D Ring)', cfg.rage.autoPeek, (v) => {
      cfg.rage.autoPeek = v;
    }, { bindKey: 'autoPeek' });
    this.addCheckbox(o, 'Anti-aim correction (Resolver)', cfg.rage.resolver, (v) => {
      cfg.rage.resolver = v;
    });
    this.addCheckbox(o, 'Prefer body aim', cfg.rage.preferBaim, (v) => {
      cfg.rage.preferBaim = v;
    });
    this.addCheckbox(o, 'Force body aim', cfg.rage.forceBaimActive, (v) => {
      cfg.rage.forceBaimActive = v;
    }, { bindKey: 'forceBaim' });
    this.addCheckbox(o, 'Double tap', cfg.rage.doubleTap, (v) => {
      cfg.rage.doubleTap = v;
    }, { bindKey: 'doubleTap' });
    this.addCheckbox(o, 'Hide shots (On-shot AA)', cfg.rage.hideShots, (v) => {
      cfg.rage.hideShots = v;
    });

    colRight.appendChild(otherGroup.box);
  }

  // ==========================================================================
  // TAB 2: ANTI-AIM & FAKE LAG
  // ==========================================================================
  renderAntiAimTab(colLeft, colRight) {
    const aaGroup = this.makeGroupBox('Anti-aimbot angles');
    aaGroup.box.classList.add('skeet-groupbox-full');
    const a = aaGroup.inner;

    this.addCheckbox(a, 'Enabled', cfg.antiaim.enabled, (v) => {
      cfg.antiaim.enabled = v;
    });
    this.addDropdown(
      a,
      'Pitch',
      [
        { value: 'none', label: 'Off' },
        { value: 'down', label: 'Down (89° Emotion)' },
        { value: 'up', label: 'Up (-89°)' },
        { value: 'zero', label: 'Zero (0°)' },
        { value: 'jitter', label: 'Random Jitter' }
      ],
      cfg.antiaim.pitch,
      (v) => {
        cfg.antiaim.pitch = v;
      }
    );
    this.addDropdown(
      a,
      'Yaw base',
      [
        { value: 'local', label: 'Local view' },
        { value: 'at_targets', label: 'At targets' }
      ],
      cfg.antiaim.yawBase,
      (v) => {
        cfg.antiaim.yawBase = v;
      }
    );
    this.addDropdown(
      a,
      'Yaw',
      [
        { value: 'backward', label: '180 (Backward)' },
        { value: 'spin', label: 'Spin (Крутилка 360°)' },
        { value: 'jitter', label: '180 Jitter' },
        { value: 'sideways', label: 'Sideways' },
        { value: 'random', label: 'Random' }
      ],
      cfg.antiaim.yawMode,
      (v) => {
        cfg.antiaim.yawMode = v;
      }
    );
    this.addSlider(a, 'Spin speed / Yaw offset', -180, 180, 1, cfg.antiaim.spinSpeed, '°', (v) => {
      cfg.antiaim.spinSpeed = Math.max(2, Math.abs(v));
      cfg.antiaim.yawOffset = v;
    });
    this.addDropdown(
      a,
      'Yaw jitter',
      [
        { value: 'off', label: 'Off' },
        { value: 'offset', label: 'Offset' },
        { value: 'center', label: 'Center' },
        { value: '3way', label: '3-Way' },
        { value: 'random', label: 'Random' }
      ],
      cfg.antiaim.jitterMode,
      (v) => {
        cfg.antiaim.jitterMode = v;
      }
    );
    this.addSlider(a, 'Jitter range', -180, 180, 1, cfg.antiaim.jitterRange, '°', (v) => {
      cfg.antiaim.jitterRange = Math.abs(v);
    });
    this.addCheckbox(a, 'Body yaw (Desync)', cfg.antiaim.bodyYaw, (v) => {
      cfg.antiaim.bodyYaw = v;
    }, { bindKey: 'invertDesync' });
    this.addDropdown(
      a,
      'Body yaw mode',
      [
        { value: 'static', label: 'Static' },
        { value: 'jitter', label: 'Jitter' },
        { value: 'sway', label: 'Sway (LBY)' },
        { value: 'opposite', label: 'Opposite' }
      ],
      cfg.antiaim.bodyYawMode,
      (v) => {
        cfg.antiaim.bodyYawMode = v;
      }
    );
    this.addSlider(a, 'Fake yaw limit', 0, 60, 1, cfg.antiaim.desyncLimit, '°', (v) => {
      cfg.antiaim.desyncLimit = v;
    });
    this.addCheckbox(a, 'Freestanding (Hide head behind wall)', cfg.antiaim.freestanding, (v) => {
      cfg.antiaim.freestanding = v;
    });

    colLeft.appendChild(aaGroup.box);

    const flGroup = this.makeGroupBox('Fake lag');
    const f = flGroup.inner;
    this.addCheckbox(f, 'Enabled', cfg.antiaim.fakeLag, (v) => {
      cfg.antiaim.fakeLag = v;
    });
    this.addDropdown(
      f,
      'Amount',
      [
        { value: 'dynamic', label: 'Dynamic' },
        { value: 'max', label: 'Maximum' },
        { value: 'fluctuate', label: 'Fluctuate' },
        { value: 'break_lc', label: 'Break lag compensation' }
      ],
      cfg.antiaim.fakeLagMode === 'off' ? 'dynamic' : cfg.antiaim.fakeLagMode,
      (v) => {
        cfg.antiaim.fakeLagMode = v;
      }
    );
    this.addSlider(f, 'Limit', 1, 16, 1, cfg.antiaim.fakeLagLimit, ' ticks', (v) => {
      cfg.antiaim.fakeLagLimit = v;
    });
    colRight.appendChild(flGroup.box);

    const otherAa = this.makeGroupBox('Other');
    otherAa.box.classList.add('skeet-groupbox-grow');
    const oa = otherAa.inner;
    this.addCheckbox(oa, 'Slow motion (Slowwalk)', cfg.antiaim.slowWalk, (v) => {
      cfg.antiaim.slowWalk = v;
    });
    this.addSlider(oa, 'Slow motion limit', 25, 110, 1, cfg.antiaim.slowWalkSpeed, ' u/s', (v) => {
      cfg.antiaim.slowWalkSpeed = v;
    });
    this.addCheckbox(oa, 'Fake duck', cfg.antiaim.fakeDuck, (v) => {
      cfg.antiaim.fakeDuck = v;
    }, { bindKey: 'fakeDuck' });
    this.addCheckbox(oa, 'Leg movement (Slide walk)', cfg.antiaim.slideWalk, (v) => {
      cfg.antiaim.slideWalk = v;
    });
    colRight.appendChild(otherAa.box);
  }

  // ==========================================================================
  // TAB 3: VISUALS (PLAYER ESP, CHAMS & EFFECTS)
  // ==========================================================================
  renderVisualsTab(colLeft, colRight) {
    const espGroup = this.makeGroupBox('Player ESP');
    const e = espGroup.inner;

    this.addCheckbox(e, 'Enable ESP', cfg.visuals.espEnabled, (v) => {
      cfg.visuals.espEnabled = v;
    });
    this.addCheckbox(e, 'Bounding box', cfg.visuals.espBox, (v) => {
      cfg.visuals.espBox = v;
    }, {
      colorValue: cfg.visuals.espBoxColor,
      onColorChange: (c) => { cfg.visuals.espBoxColor = c; }
    });
    this.addCheckbox(e, 'Health bar', cfg.visuals.espHealth, (v) => {
      cfg.visuals.espHealth = v;
    });
    this.addCheckbox(e, 'Name', cfg.visuals.espName, (v) => {
      cfg.visuals.espName = v;
    });
    this.addCheckbox(e, 'Flags (HK, FAKE, LC, BT)', cfg.visuals.espFlags, (v) => {
      cfg.visuals.espFlags = v;
    });
    this.addCheckbox(e, 'Weapon text & icon', cfg.visuals.espWeapon, (v) => {
      cfg.visuals.espWeapon = v;
    });
    this.addCheckbox(e, 'Skeleton', cfg.visuals.espSkeleton, (v) => {
      cfg.visuals.espSkeleton = v;
    });
    this.addCheckbox(e, 'Out of FOV arrow', cfg.visuals.espOofArrows, (v) => {
      cfg.visuals.espOofArrows = v;
    });
    colLeft.appendChild(espGroup.box);

    const chamsGroup = this.makeGroupBox('Colored models');
    chamsGroup.box.classList.add('skeet-groupbox-grow');
    const c = chamsGroup.inner;

    this.addCheckbox(c, 'Player (Visible Chams)', cfg.visuals.enemyChams, (v) => {
      cfg.visuals.enemyChams = v;
    }, {
      colorValue: cfg.visuals.enemyVisColor,
      onColorChange: (col) => { cfg.visuals.enemyVisColor = col; }
    });
    this.addCheckbox(c, 'Player behind wall (XQZ)', cfg.visuals.enemyChamsXqz, (v) => {
      cfg.visuals.enemyChamsXqz = v;
    }, {
      colorValue: cfg.visuals.enemyXqzColor,
      onColorChange: (col) => { cfg.visuals.enemyXqzColor = col; }
    });
    this.addDropdown(
      c,
      'Chams material',
      [
        { value: 'metallic', label: 'Metallic' },
        { value: 'flat', label: 'Flat Neon' },
        { value: 'wireframe', label: 'Wireframe' },
        { value: 'textured', label: 'Shaded' }
      ],
      cfg.visuals.enemyChamsStyle,
      (v) => {
        cfg.visuals.enemyChamsStyle = v;
      }
    );
    this.addCheckbox(c, 'Fake desync ghost chams', cfg.visuals.desyncGhostChams, (v) => {
      cfg.visuals.desyncGhostChams = v;
    }, {
      colorValue: cfg.visuals.desyncGhostColor,
      onColorChange: (col) => { cfg.visuals.desyncGhostColor = col; }
    });
    this.addCheckbox(c, 'Fake-lag ghost chams', cfg.visuals.fakeLagGhostChams, (v) => {
      cfg.visuals.fakeLagGhostChams = v;
    });
    this.addCheckbox(c, 'Shadow (Backtrack chams)', cfg.visuals.backtrackChams, (v) => {
      cfg.visuals.backtrackChams = v;
    });
    this.addCheckbox(c, 'On-shot matrix chams', cfg.visuals.shotChams, (v) => {
      cfg.visuals.shotChams = v;
    });
    colLeft.appendChild(chamsGroup.box);

    const effGroup = this.makeGroupBox('Effects');
    effGroup.box.classList.add('skeet-groupbox-full');
    const ef = effGroup.inner;

    this.addCheckbox(ef, 'Remove scope overlay', cfg.visuals.removeScopeOverlay, (v) => {
      cfg.visuals.removeScopeOverlay = v;
    });
    this.addCheckbox(ef, 'Remove smoke grenades (Wireframe)', cfg.visuals.removeSmoke, (v) => {
      cfg.visuals.removeSmoke = v;
    });
    this.addCheckbox(ef, 'Bullet tracers', cfg.visuals.bulletTracers, (v) => {
      cfg.visuals.bulletTracers = v;
    }, {
      colorValue: cfg.visuals.tracerColor,
      onColorChange: (col) => { cfg.visuals.tracerColor = col; }
    });
    this.addCheckbox(ef, 'Bullet impacts (Client/Server)', cfg.visuals.showImpacts, (v) => {
      cfg.visuals.showImpacts = v;
    });
    this.addCheckbox(ef, 'Hit marker & Damage indicator', cfg.visuals.hitmarkers, (v) => {
      cfg.visuals.hitmarkers = v;
      cfg.visuals.damageNumbers = v;
    });
    this.addCheckbox(ef, 'Night mode', cfg.visuals.nightmode, (v) => {
      cfg.visuals.nightmode = v;
    });
    this.addSlider(ef, 'Transparent walls (ASUS)', 20, 100, 1, cfg.visuals.asusWalls, '%', (v) => {
      cfg.visuals.asusWalls = v;
    });
    this.addCheckbox(ef, 'Force thirdperson (Alive)', cfg.visuals.thirdperson, (v) => {
      cfg.visuals.thirdperson = v;
    }, { bindKey: 'thirdperson' });
    this.addSlider(ef, 'Thirdperson distance', 60, 220, 1, cfg.visuals.thirdpersonDist, ' u', (v) => {
      cfg.visuals.thirdpersonDist = v;
    });

    colRight.appendChild(effGroup.box);
  }

  // ==========================================================================
  // TAB 5: SKINS & KNIFE CHANGER
  // ==========================================================================
  renderSkinsTab(colLeft, colRight) {
    const kGroup = this.makeGroupBox('Knife options');
    kGroup.box.classList.add('skeet-groupbox-full');
    const k = kGroup.inner;

    this.addDropdown(
      k,
      'Knife model',
      [
        { value: 'karambit', label: '★ Karambit' },
        { value: 'm9', label: '★ M9 Bayonet' },
        { value: 'butterfly', label: '★ Butterfly Knife' }
      ],
      cfg.skins.knifeModel,
      (v) => {
        cfg.skins.knifeModel = v;
        if (this.onSkinChange) this.onSkinChange(true);
      }
    );

    const skinOpts = Object.entries(SKIN_PALETTES).map(([id, s]) => ({
      value: id,
      label: s.name
    }));

    this.addDropdown(k, 'Knife paint kit', skinOpts, cfg.skins.knifeSkin, (v) => {
      cfg.skins.knifeSkin = v;
      if (this.onSkinChange) this.onSkinChange(true);
    });

    this.addButton(k, 'Inspect weapon in-game [G]', () => {
      if (this.onSkinChange) this.onSkinChange(true);
    });

    colLeft.appendChild(kGroup.box);

    const wGroup = this.makeGroupBox('Weapon skin');
    wGroup.box.classList.add('skeet-groupbox-full');
    const w = wGroup.inner;

    this.addDropdown(w, 'Global weapon paint kit', skinOpts, cfg.skins.weaponSkin, (v) => {
      cfg.skins.weaponSkin = v;
      if (this.onSkinChange) this.onSkinChange(false);
    });

    this.addColorRow(w, 'Glove accent color', cfg.skins.gloveColor, (col) => {
      cfg.skins.gloveColor = col;
      if (this.onSkinChange) this.onSkinChange(false);
    });

    colRight.appendChild(wGroup.box);
  }

  // ==========================================================================
  // TAB 6: PLAYERS LIST (PER-TARGET RESOLVER)
  // ==========================================================================
  renderPlayersTab(colLeft, colRight) {
    const pGroup = this.makeGroupBox('Players');
    pGroup.box.classList.add('skeet-groupbox-full');
    const p = pGroup.inner;

    const bots = this.getBots ? this.getBots() : [];
    const listEl = document.createElement('div');
    listEl.className = 'skeet-player-list';

    bots.forEach((b, idx) => {
      const item = document.createElement('div');
      item.className = `skeet-player-row ${this.selectedPlayerIndex === idx ? 'active' : ''}`;
      item.textContent = `${b.name} (${b.weapon.shortName})`;
      item.addEventListener('click', () => {
        this.selectedPlayerIndex = idx;
        this.renderContent();
      });
      listEl.appendChild(item);
    });
    p.appendChild(listEl);
    colLeft.appendChild(pGroup.box);

    const adjGroup = this.makeGroupBox('Adjustments');
    adjGroup.box.classList.add('skeet-groupbox-full');
    const adj = adjGroup.inner;

    const target = bots[this.selectedPlayerIndex];
    if (target) {
      this.addCheckbox(adj, 'Add to whitelist (Ignore target)', !!target.whitelisted, (v) => {
        target.whitelisted = v;
      });
      this.addCheckbox(adj, 'High priority target', !!target.highPriority, (v) => {
        target.highPriority = v;
      });
      this.addCheckbox(adj, 'Force body aim on player', !!target.forceBaim, (v) => {
        target.forceBaim = v;
      });
      this.addSlider(adj, 'Force body yaw value', -60, 60, 1, target.overrideYaw || 0, '°', (v) => {
        target.overrideYaw = v;
      });
      this.addButton(adj, 'Apply to all players', () => {
        bots.forEach((b) => {
          b.forceBaim = target.forceBaim;
          b.highPriority = target.highPriority;
        });
      });
    }

    colRight.appendChild(adjGroup.box);
  }
}
