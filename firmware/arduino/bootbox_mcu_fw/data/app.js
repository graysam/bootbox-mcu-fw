(() => {
  const $ = (id) => document.getElementById(id);

const S = {
  conn: $('conn'),
  connDot: $('conn-dot'),
  connChip: $('conn-chip'),
  modeBadge: $('mode-badge'),
  temp1: $('temp1'),
  temp2: $('temp2'),
  fanrpm: $('fanrpm'),
  fantarget: $('fantarget'),
  lastUpdate: $('last-update'),
  modePid: $('mode-pid'),
  modeManual: $('mode-manual'),
  manpct: $('manpct'),
  manpctValue: $('manpct-value'),
  sp1: $('sp1'),
  sp2: $('sp2'),
  kp: $('kp'),
  ki: $('ki'),
  kd: $('kd'),
  save: $('save'),
  refresh: $('refresh'),
  statusLine: $('status-line'),
  log: $('log'),
  autoScroll: $('auto-scroll'),
  getlogs: $('getlogs'),
  clearlog: $('clearlog'),
  uploadBtn: $('upload-btn'),
  uploadInput: $('upload-input'),
  uploadStatus: $('upload-status'),
  toastStack: $('toast-stack'),
  crossLink: $('cross-link'),
  liveSp1: $('live-sp1'),
  liveSp2: $('live-sp2'),
  liveKp: $('live-kp'),
  liveKi: $('live-ki'),
  liveKd: $('live-kd'),
  sysUptime: $('sys-uptime'),
  sysHeap: $('sys-heap'),
  sysCpu: $('sys-cpu'),
  sysClients: $('sys-clients'),
  sysFs: $('sys-fs'),
  sysFsHint: $('sys-fs-hint'),
  sysFw: $('sys-fw'),
  sysBuild: $('sys-build'),
  sysSdk: $('sys-sdk'),
  sysChip: $('sys-chip'),
  sysIp: $('sys-ip'),
  sysReset: $('sys-reset'),
  sysBoot: $('sys-boot')
};

  const stateCache = {
    fanCount: 1,
    pidEnabled: true,
    manualPct: 30,
    awaitingApply: false,
    modeDirty: false,
    manualDirty: false,
    appliedPid: true,
    appliedManualPct: 30,
    dsp: {},
    sys: {}
  };

  const DSP_META = {
    master_db: { unit: 'dB', decimals: 1, min: -60, max: 12, step: 0.5 },
    stereo_db: { unit: 'dB', decimals: 1, min: -40, max: 12, step: 0.5 },
    sub_lo_db: { unit: 'dB', decimals: 1, min: -40, max: 12, step: 0.5 },
    sub_hi_db: { unit: 'dB', decimals: 1, min: -40, max: 12, step: 0.5 },
    cross_mains_hz: { unit: 'Hz', decimals: 0, min: 40, max: 300, step: 1 },
    cross_sub_hz: { unit: 'Hz', decimals: 0, min: 30, max: 240, step: 1 },
    sub_lo_hp_hz: { unit: 'Hz', decimals: 0, min: 15, max: 180, step: 1 },
    sub_lo_lp_hz: { unit: 'Hz', decimals: 0, min: 30, max: 220, step: 1 },
    sub_hi_hp_hz: { unit: 'Hz', decimals: 0, min: 40, max: 240, step: 1 },
    sub_hi_lp_hz: { unit: 'Hz', decimals: 0, min: 60, max: 260, step: 1 },
    cross_linked: { unit: '', type: 'bool' }
  };

  const dspControls = {};
  const thermalFields = {
    sp1: {
      el: S.sp1,
      live: S.liveSp1,
      format: (v) => `${v.toFixed(1)} °C`,
      toInput: (v) => v.toFixed(1),
      epsilon: 0.05
    },
    sp2: {
      el: S.sp2,
      live: S.liveSp2,
      format: (v) => `${v.toFixed(1)} °C`,
      toInput: (v) => v.toFixed(1),
      epsilon: 0.05
    },
    kp: {
      el: S.kp,
      live: S.liveKp,
      format: (v) => v.toFixed(2),
      toInput: (v) => v.toFixed(2),
      epsilon: 0.01
    },
    ki: {
      el: S.ki,
      live: S.liveKi,
      format: (v) => v.toFixed(3),
      toInput: (v) => v.toFixed(3),
      epsilon: 0.002
    },
    kd: {
      el: S.kd,
      live: S.liveKd,
      format: (v) => v.toFixed(3),
      toInput: (v) => v.toFixed(3),
      epsilon: 0.002
    }
  };
  let ws;
  let seq = 1;
  const pending = new Map();
  let lastUpdateTs = 0;
  let reconnectBackoff = 500;
  let pendingDsp = {};
  let dspFlushTimer = null;
  const TAB_STORAGE_KEY = 'bootbox:tab';
  const CARD_STORAGE_KEY = 'bootbox:cards';

  function loadJSON(key, fallback) {
    if (typeof localStorage === 'undefined') return fallback;
    try {
      const raw = localStorage.getItem(key);
      if (!raw) return fallback;
      const parsed = JSON.parse(raw);
      return typeof parsed === 'object' && parsed !== null ? parsed : fallback;
    } catch {
      return fallback;
    }
  }

  function setConnection(text, level = 'idle') {
    S.conn.textContent = text;
    S.connChip.classList.remove('good', 'bad', 'pending', 'idle');
    S.connChip.classList.add(level);
  }

  function showToast(message, variant = 'info', timeout = 2600) {
    if (!S.toastStack) return;
    const toast = document.createElement('div');
    toast.className = `toast ${variant}`;
    toast.textContent = message;
    S.toastStack.appendChild(toast);
    setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateY(8px)';
      setTimeout(() => toast.remove(), 220);
    }, timeout);
  }

  function clamp(value, min, max) {
    return Math.min(Math.max(value, min), max);
  }

  function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) return '—';
    const units = ['B', 'KB', 'MB', 'GB'];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit++;
    }
    return `${value.toFixed(value < 10 && unit > 0 ? 1 : 0)} ${units[unit]}`;
  }

  function formatUptime(ms) {
    if (!Number.isFinite(ms) || ms <= 0) return '0s';
    let seconds = Math.floor(ms / 1000);
    const days = Math.floor(seconds / 86400);
    seconds -= days * 86400;
    const hours = Math.floor(seconds / 3600);
    seconds -= hours * 3600;
    const minutes = Math.floor(seconds / 60);
    seconds -= minutes * 60;
    const parts = [];
    if (days) parts.push(`${days}d`);
    if (hours) parts.push(`${hours}h`);
    if (minutes && parts.length < 3) parts.push(`${minutes}m`);
    if (parts.length < 2) parts.push(`${seconds}s`);
    return parts.join(' ');
  }

  function queueDspUpdate(param, value, immediate = false) {
    pendingDsp[param] = value;
    if (immediate) {
      flushDspUpdates();
      return;
    }
    if (dspFlushTimer) return;
    dspFlushTimer = setTimeout(flushDspUpdates, 70);
  }

  function flushDspUpdates() {
    if (dspFlushTimer) {
      clearTimeout(dspFlushTimer);
      dspFlushTimer = null;
    }
    const keys = Object.keys(pendingDsp);
    if (!keys.length) return;
    send('set_dsp', { ...pendingDsp });
    pendingDsp = {};
  }

  function formatValue(param, value) {
    const meta = DSP_META[param];
    if (!meta) return `${value}`;
    const decimals = meta.decimals ?? 2;
    const rounded = value.toFixed(decimals);
    const cleaned = decimals ? parseFloat(rounded).toFixed(decimals) : Math.round(value).toString();
    if (meta.unit === 'dB') return `${parseFloat(cleaned)} dB`;
    if (meta.unit === 'Hz') return `${Math.round(value)} Hz`;
    return cleaned;
  }

  function hasDirtyThermalInputs() {
    return Object.values(thermalFields).some((field) => field.el?.dataset?.dirty === 'true');
  }

  function setFormStatus(text, variant = '') {
    S.statusLine.textContent = text;
    S.statusLine.classList.remove('success', 'error', 'pending');
    if (variant) S.statusLine.classList.add(variant);
  }

  function setModeUI(pidEnabled, options = {}) {
    const { fromState = false } = options;
    if (fromState) {
      stateCache.appliedPid = pidEnabled;
      if (stateCache.modeDirty && stateCache.pidEnabled !== pidEnabled) {
        refreshModeBadge();
        return;
      }
      stateCache.modeDirty = false;
    } else {
      stateCache.modeDirty = pidEnabled !== stateCache.appliedPid;
    }
    stateCache.pidEnabled = pidEnabled;
    if (!fromState && pidEnabled) {
      stateCache.manualDirty = false;
      updateManualSlider(stateCache.manualPct, false);
    }
    S.modePid.classList.toggle('active', pidEnabled);
    S.modeManual.classList.toggle('active', !pidEnabled);
    refreshModeBadge();
    S.manpct.disabled = pidEnabled;
    S.manpctValue.style.opacity = pidEnabled ? '0.5' : '1';
  }

  function refreshModeBadge() {
    const modeLabel = stateCache.pidEnabled ? 'PID' : 'Manual';
    const fansLabel = `${stateCache.fanCount} fan${stateCache.fanCount === 1 ? '' : 's'}`;
    S.modeBadge.textContent = `${modeLabel} · ${fansLabel}`;
  }

  function updateThermalField(key, value) {
    const field = thermalFields[key];
    if (!field || !Number.isFinite(value)) return;
    const { el, live, toInput, format, epsilon = 0.05 } = field;
    field.applied = value;
    el.dataset.applied = value;
    const dirty = el.dataset.dirty === 'true';
    if (!dirty) {
      el.value = toInput(value);
    } else {
      const current = Number.parseFloat(el.value);
      if (Number.isFinite(current) && Math.abs(current - value) <= epsilon) {
        el.dataset.dirty = '';
        el.value = toInput(value);
      }
    }
    const isDirty = el.dataset.dirty === 'true';
    if (isDirty) {
      const pendingVal = Number.parseFloat(el.value);
      if (Number.isFinite(pendingVal)) {
        live.textContent = `Pending: ${format(pendingVal)}`;
      } else {
        live.textContent = 'Pending…';
      }
      live.classList.add('pending');
    } else {
      live.textContent = `Applied: ${format(value)}`;
      live.classList.remove('pending');
    }
  }

  function updateSystemInfo(sys = {}) {
    stateCache.sys = { ...stateCache.sys, ...sys };

    if (S.sysUptime) {
      const uptime = Number(sys.uptime_ms);
      S.sysUptime.textContent = Number.isFinite(uptime) ? formatUptime(uptime) : '—';
    }

    if (S.sysHeap) {
      const heapFree = Number(sys.free_heap);
      const heapSize = Number(sys.heap_size);
      if (Number.isFinite(heapFree) && Number.isFinite(heapSize) && heapSize > 0) {
        const used = heapSize - heapFree;
        const pctFree = Math.round((heapFree / heapSize) * 100);
        S.sysHeap.textContent = `${formatBytes(heapFree)} free (${pctFree}% free)`;
        if (typeof S.sysHeap.setAttribute === 'function') {
          S.sysHeap.setAttribute('title', `${formatBytes(used)} used of ${formatBytes(heapSize)}`);
        }
      } else {
        S.sysHeap.textContent = '—';
        if (typeof S.sysHeap.removeAttribute === 'function') {
          S.sysHeap.removeAttribute('title');
        }
      }
    }

    if (S.sysCpu) {
      const cpu = Number(sys.cpu_freq_mhz);
      S.sysCpu.textContent = Number.isFinite(cpu) && cpu > 0 ? `${cpu} MHz` : '—';
    }

    if (S.sysClients) {
      const clients = Number(sys.wifi_clients);
      S.sysClients.textContent = Number.isFinite(clients) ? `${clients}` : '0';
    }

    if (S.sysFw) S.sysFw.textContent = sys.fw_version || '—';
    if (S.sysBuild) S.sysBuild.textContent = sys.fw_build || '—';
    if (S.sysSdk) S.sysSdk.textContent = sys.sdk || '—';
    if (S.sysChip) {
      const chip = sys.chip || '';
      const rev = Number.isFinite(sys.chip_revision) ? ` (rev ${sys.chip_revision})` : '';
      S.sysChip.textContent = chip ? `${chip}${rev}` : '—';
    }
    if (S.sysIp) S.sysIp.textContent = sys.ap_ip || '—';
    if (S.sysReset) S.sysReset.textContent = sys.reset_reason || '—';
    if (S.sysBoot) {
      const boot = Number(sys.boot_count);
      S.sysBoot.textContent = Number.isFinite(boot) ? `${boot}` : '—';
    }

    if (S.sysFs && S.sysFsHint) {
      const total = Number(sys.fs_total);
      const used = Number(sys.fs_used);
      if (Number.isFinite(total) && total > 0 && Number.isFinite(used) && used >= 0) {
        const pct = Math.round(clamp((used / total) * 100, 0, 100));
        S.sysFs.textContent = `${formatBytes(used)} / ${formatBytes(total)} (${pct}%)`;
        S.sysFsHint.textContent = 'LittleFS mounted';
      } else {
        S.sysFs.textContent = '—';
        S.sysFsHint.textContent = 'Filesystem not mounted';
      }
    }
  }

  function updateManualSlider(val, pendingLabel = false) {
    const pct = clamp(Number(val) || 0, 0, 100);
    S.manpct.value = pct;
    S.manpctValue.textContent = `${pct}%${pendingLabel ? ' (pending)' : ''}`;
  }

  function updateKnobVisual(ctrl) {
    const meta = DSP_META[ctrl.param];
    const ratio = (ctrl.value - ctrl.min) / (ctrl.max - ctrl.min);
    const angle = -135 + ratio * 270;
    if (ctrl.indicator) ctrl.indicator.style.transform = `rotate(${angle}deg)`;
    if (ctrl.display) ctrl.display.textContent = formatValue(ctrl.param, ctrl.value);
  }

  function updateSliderVisual(ctrl) {
    if (ctrl.input) ctrl.input.value = ctrl.value;
    if (ctrl.display) ctrl.display.textContent = formatValue(ctrl.param, ctrl.value);
  }

  function setControlValue(param, value, options = {}) {
    const ctrl = dspControls[param];
    if (!ctrl) return;
    const { fromState = false, immediate = false, mirror = false } = options;
    if (fromState && ctrl.active) {
      return;
    }
    const meta = DSP_META[param] || {};
    const min = ctrl.min ?? meta.min ?? -100;
    const max = ctrl.max ?? meta.max ?? 100;
    const step = ctrl.step ?? meta.step ?? 0.1;
    const prev = ctrl.value;
    let clamped = clamp(value, min, max);
    clamped = Math.round(clamped / step) * step;
    if (fromState && prev !== undefined && Math.abs(prev - clamped) <= step * 0.25) {
      return;
    }
    if (ctrl.value !== undefined && Math.abs(ctrl.value - clamped) < step * 0.25 && fromState) {
      return;
    }
    ctrl.value = clamped;
    if (ctrl.type === 'knob') updateKnobVisual(ctrl);
    else updateSliderVisual(ctrl);

    if (!fromState) {
      queueDspUpdate(param, clamped, immediate);
    }

    if (!mirror) {
      // linked crossover updates
      if (param === 'cross_mains_hz' && S.crossLink?.checked) {
        setControlValue('cross_sub_hz', clamped, { fromState, immediate, mirror: true });
      } else if (param === 'cross_sub_hz' && S.crossLink?.checked) {
        setControlValue('cross_mains_hz', clamped, { fromState, immediate, mirror: true });
      }
      // Ensure ranges obey ordering visually
      if (param === 'sub_lo_hp_hz') {
        const minGap = 5;
        if ((dspControls.sub_lo_lp_hz?.value ?? clamped) < clamped + minGap) {
          setControlValue('sub_lo_lp_hz', clamped + minGap, { fromState, immediate, mirror: true });
        }
      } else if (param === 'sub_lo_lp_hz') {
        const minGap = 5;
        if ((dspControls.sub_lo_hp_hz?.value ?? clamped) > clamped - minGap) {
          setControlValue('sub_lo_hp_hz', clamped - minGap, { fromState, immediate, mirror: true });
        }
      } else if (param === 'sub_hi_hp_hz') {
        const minGap = 5;
        if ((dspControls.sub_hi_lp_hz?.value ?? clamped) < clamped + minGap) {
          setControlValue('sub_hi_lp_hz', clamped + minGap, { fromState, immediate, mirror: true });
        }
      } else if (param === 'sub_hi_lp_hz') {
        const minGap = 5;
        if ((dspControls.sub_hi_hp_hz?.value ?? clamped) > clamped - minGap) {
          setControlValue('sub_hi_hp_hz', clamped - minGap, { fromState, immediate, mirror: true });
        }
      }
    }
  }

  function initKnobControl(ctrl) {
    const knob = ctrl.element.querySelector('[data-role="knob"]');
    const indicator = knob?.querySelector('.knob-indicator');
    ctrl.knob = knob;
    ctrl.indicator = indicator;
    ctrl.display = ctrl.element.querySelector('[data-role="display"]');
    ctrl.type = 'knob';
    ctrl.active = false;

    if (!knob) return;
    knob.addEventListener('pointerdown', (ev) => {
      ev.preventDefault();
      knob.setPointerCapture(ev.pointerId);
      const startY = ev.clientY;
      const startVal = ctrl.value ?? 0;
      const span = ctrl.max - ctrl.min;
      const sensitivity = span / 250;
      ctrl.active = true;
      if (indicator) indicator.style.transition = 'none';
      const move = (moveEv) => {
        const delta = (startY - moveEv.clientY) * sensitivity;
        const next = startVal + delta;
        setControlValue(ctrl.param, next, { fromState: false });
      };
      const up = (upEv) => {
        knob.releasePointerCapture(upEv.pointerId);
        knob.removeEventListener('pointermove', move);
        knob.removeEventListener('pointerup', up);
        knob.removeEventListener('pointercancel', up);
        ctrl.active = false;
        if (indicator) indicator.style.transition = '';
        setControlValue(ctrl.param, ctrl.value, { fromState: false, immediate: true });
      };
      knob.addEventListener('pointermove', move);
      knob.addEventListener('pointerup', up);
      knob.addEventListener('pointercancel', up);
    });

    if (ctrl.display) {
      ctrl.display.addEventListener('click', () => {
        const current = ctrl.value ?? 0;
        const input = prompt(`Set ${ctrl.param}`, current.toFixed(ctrl.decimals ?? 1));
        if (input === null) return;
        const parsed = Number.parseFloat(input);
        if (Number.isFinite(parsed)) {
          setControlValue(ctrl.param, parsed, { fromState: false, immediate: true });
        } else {
          showToast('Invalid number', 'error', 1800);
        }
      });
    }
  }

  function initSliderControl(ctrl) {
    const slider = ctrl.element.querySelector('input[type="range"]');
    ctrl.input = slider;
    ctrl.display = ctrl.element.querySelector('[data-role="display"]');
    ctrl.type = ctrl.element.dataset.type || 'slider';
    ctrl.active = false;
    if (!slider) return;

    slider.addEventListener('input', () => {
      ctrl.active = true;
      setControlValue(ctrl.param, Number(slider.value), { fromState: false });
    });
    slider.addEventListener('change', () => {
      ctrl.active = false;
      setControlValue(ctrl.param, Number(slider.value), { fromState: false, immediate: true });
    });
  }

  function initDspControls() {
    document.querySelectorAll('.dsp-control').forEach((el) => {
      const param = el.dataset.param;
      if (!param) return;
      const meta = DSP_META[param] || {};
      const ctrl = {
        param,
        element: el,
        min: Number.parseFloat(el.dataset.min ?? meta.min ?? 0),
        max: Number.parseFloat(el.dataset.max ?? meta.max ?? 0),
        step: Number.parseFloat(el.dataset.step ?? meta.step ?? 1),
        decimals: meta.decimals ?? 1,
        unit: meta.unit || ''
      };
      if ((el.dataset.type || '').includes('knob')) {
        initKnobControl(ctrl);
      } else {
        initSliderControl(ctrl);
      }
      dspControls[param] = ctrl;
    });
  }

  function updateDspFromState(dspState) {
    if (!dspState) return;
    Object.keys(dspState).forEach((param) => {
      const value = dspState[param];
      stateCache.dsp[param] = value;
      setControlValue(param, value, { fromState: true });
    });
    if (typeof dspState.cross_linked === 'boolean' && S.crossLink) {
      const checked = !!dspState.cross_linked;
      if (S.crossLink.checked !== checked) {
        S.crossLink.checked = checked;
      }
    }
  }

  function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${proto}://${location.host}/ws`;

    function attempt() {
      setConnection('connecting…', 'pending');
      try {
        ws = new WebSocket(url);
      } catch (err) {
        scheduleReconnect();
        return;
      }
      ws.onopen = () => {
        setConnection('connected', 'good');
        reconnectBackoff = 500;
        setFormStatus('Syncing with controller…', 'pending');
        send('get_state');
        send('get_logs');
        showToast('Connected to controller', 'success', 2000);
      };
      ws.onclose = () => {
        setConnection('disconnected', 'idle');
        showToast('Connection lost – retrying…', 'error', 2400);
        scheduleReconnect();
      };
      ws.onerror = () => setConnection('error', 'bad');
      ws.onmessage = (ev) => {
        let msg;
        try { msg = JSON.parse(ev.data); } catch { return; }
        if (msg.type === 'ack') {
          ack(msg.id);
          return;
        }
        if (typeof msg.id === 'number' && msg.type !== 'ack') {
          ws?.readyState === WebSocket.OPEN && ws.send(JSON.stringify({ type: 'ack', id: msg.id }));
        }
        switch (msg.type) {
          case 'state':
            applyState(msg.data || {}, msg.dsp || {}, msg.sys || {});
            ack(msg.id);
            break;
          case 'pong':
            ack(msg.id);
            break;
          case 'error':
            showToast(msg.error || 'Controller reported an error', 'error', 3200);
            break;
          case 'logs':
            renderLogs(msg.data);
            break;
          default:
            break;
        }
      };
    }

    function scheduleReconnect() {
      reconnectBackoff = Math.min(reconnectBackoff * 2, 20000);
      const jitter = Math.floor(Math.random() * 300);
      setTimeout(attempt, reconnectBackoff + jitter);
    }

    attempt();
  }

  function send(type, data) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      if (type !== 'ping') showToast('Controller offline – retrying…', 'warn', 2400);
      return;
    }
    const id = seq++;
    const payload = JSON.stringify({ type, id, data });
    ws.send(payload);
    const timer = setTimeout(() => {
      if (pending.has(id) && ws?.readyState === WebSocket.OPEN) {
        ws.send(payload);
      }
    }, 2000);
    pending.set(id, timer);
  }

  function ack(id) {
    if (!pending.has(id)) return;
    clearTimeout(pending.get(id));
    pending.delete(id);
  }

  function applyState(core, dsp, sys) {
    lastUpdateTs = Date.now();
    if (typeof core.temp1 === 'number') S.temp1.textContent = `${core.temp1.toFixed(1)} °C`; else S.temp1.textContent = '—';
    if (typeof core.temp2 === 'number') S.temp2.textContent = `${core.temp2.toFixed(1)} °C`; else S.temp2.textContent = '—';
    if (typeof core.fan_rpm === 'number') S.fanrpm.textContent = core.fan_rpm; else S.fanrpm.textContent = '—';
    if (typeof core.fan_target_pct === 'number') S.fantarget.textContent = `${core.fan_target_pct}%`; else S.fantarget.textContent = '—';
    if (typeof core.fan_count === 'number') stateCache.fanCount = Math.max(1, Math.round(core.fan_count));

    const pidEnabled = !!core.pid_enabled;
    setModeUI(pidEnabled, { fromState: true });

    if (typeof core.fan_target_pct === 'number') {
      stateCache.appliedManualPct = core.fan_target_pct;
      if (pidEnabled) {
        stateCache.manualDirty = false;
      } else if (stateCache.manualDirty && Math.round(stateCache.manualPct) === Math.round(core.fan_target_pct)) {
        stateCache.manualDirty = false;
      }
      if (!stateCache.manualDirty) {
        stateCache.manualPct = core.fan_target_pct;
      }
    }

    if (!stateCache.manualDirty) {
      updateManualSlider(stateCache.manualPct, false);
    }

    if (typeof core.sp1 === 'number') updateThermalField('sp1', core.sp1);
    if (typeof core.sp2 === 'number') updateThermalField('sp2', core.sp2);
    if (typeof core.kp === 'number') updateThermalField('kp', core.kp);
    if (typeof core.ki === 'number') updateThermalField('ki', core.ki);
    if (typeof core.kd === 'number') updateThermalField('kd', core.kd);

    const hasDirtyThermal = hasDirtyThermalInputs();

    if (stateCache.awaitingApply) {
      setFormStatus('Settings applied by controller', 'success');
      stateCache.awaitingApply = false;
      showToast('Settings applied', 'success', 2200);
    } else if (!stateCache.modeDirty && !stateCache.manualDirty && !hasDirtyThermal) {
      setFormStatus('Latest data received from controller', 'success');
    }

    updateDspFromState(dsp);
    updateSystemInfo(sys);
  }

  function renderLogs(lines) {
    if (!Array.isArray(lines)) return;
    S.log.textContent = lines.join('\n');
    if (S.autoScroll?.checked) {
      S.log.scrollTop = S.log.scrollHeight;
    }
  }

  async function handleUpload(file) {
    if (!file) return;
    S.uploadStatus.textContent = `Uploading ${file.name} …`;
    showToast(`Uploading ${file.name}`, 'pending', 2600);
    const form = new FormData();
    form.append('file', file, file.name);
    try {
      const res = await fetch('/api/upload/adau', { method: 'POST', body: form });
      if (!res.ok) throw new Error(await res.text());
      S.uploadStatus.textContent = `Uploaded ${file.name} (${(file.size / 1024).toFixed(1)} KB)`;
      showToast('Upload complete', 'success', 2400);
      send('get_logs');
    } catch (err) {
      S.uploadStatus.textContent = `Upload failed: ${err.message}`;
      showToast(`Upload failed: ${err.message}`, 'error', 3600);
    } finally {
      S.uploadInput.value = '';
    }
  }

  function updateLastUpdateLabel() {
    if (!lastUpdateTs) {
      S.lastUpdate.textContent = 'never';
      return;
    }
    const diff = Math.floor((Date.now() - lastUpdateTs) / 1000);
    if (diff < 2) S.lastUpdate.textContent = 'just now';
    else if (diff < 60) S.lastUpdate.textContent = `${diff} sec ago`;
    else if (diff < 3600) S.lastUpdate.textContent = `${Math.floor(diff / 60)} min ago`;
    else S.lastUpdate.textContent = `${Math.floor(diff / 3600)} hr ago`;
  }

  S.modePid.addEventListener('click', () => {
    if (!S.modePid.classList.contains('active')) {
      setModeUI(true);
      setFormStatus('PID mode selected (remember to save)', 'pending');
    }
  });

  S.modeManual.addEventListener('click', () => {
    if (!S.modeManual.classList.contains('active')) {
      setModeUI(false);
      setFormStatus('Manual mode selected (remember to save)', 'pending');
    }
  });

  S.manpct.addEventListener('input', (ev) => {
    stateCache.manualPct = clamp(Number(ev.target.value) || 0, 0, 100);
    const applied = Number.isFinite(stateCache.appliedManualPct) ? stateCache.appliedManualPct : stateCache.manualPct;
    stateCache.manualDirty = Math.round(stateCache.manualPct) !== Math.round(applied);
    updateManualSlider(stateCache.manualPct, stateCache.manualDirty);
    if (stateCache.manualDirty) {
      setFormStatus('Manual fan target pending save', 'pending');
    } else if (!stateCache.modeDirty && !hasDirtyThermalInputs()) {
      setFormStatus('Manual fan target restored', 'success');
    }
  });

  S.save.addEventListener('click', () => {
    const pidEnabled = S.modePid.classList.contains('active');
    const data = {
      pid_enabled: pidEnabled,
      sp1: Number.parseFloat(S.sp1.value),
      sp2: Number.parseFloat(S.sp2.value),
      fan_manual_pct: clamp(Number.parseInt(S.manpct.value, 10) || 0, 0, 100),
      kp: Number.parseFloat(S.kp.value),
      ki: Number.parseFloat(S.ki.value),
      kd: Number.parseFloat(S.kd.value)
    };
    setFormStatus('Sending settings to controller…', 'pending');
    stateCache.awaitingApply = true;
    stateCache.modeDirty = false;
    stateCache.manualDirty = false;
    updateManualSlider(stateCache.manualPct, false);
    send('set_settings', data);
  });

  S.refresh.addEventListener('click', () => {
    setFormStatus('Sync requested…', 'pending');
    send('get_state');
  });

  S.getlogs.addEventListener('click', () => send('get_logs'));
  S.clearlog.addEventListener('click', () => { S.log.textContent = '(empty)'; });

  if (S.uploadBtn && S.uploadInput) {
    S.uploadBtn.addEventListener('click', () => S.uploadInput.click());
    S.uploadInput.addEventListener('change', (ev) => handleUpload(ev.target.files?.[0] ?? null));
  }

  if (S.crossLink) {
    S.crossLink.addEventListener('change', () => {
      const linked = !!S.crossLink.checked;
      queueDspUpdate('cross_linked', linked, true);
      showToast(linked ? 'Crossovers linked' : 'Crossovers unlinked', linked ? 'success' : 'warn', 1600);
    });
  }

  setInterval(() => send('ping'), 5000);
  setInterval(updateLastUpdateLabel, 1000);
  window.addEventListener('beforeunload', flushDspUpdates);

  initDspControls();
  Object.entries(thermalFields).forEach(([key, field]) => {
    if (!field.el || !field.live) return;
    field.el.addEventListener('input', () => {
      field.el.dataset.dirty = 'true';
      const val = Number.parseFloat(field.el.value);
      if (Number.isFinite(val)) {
        field.live.textContent = `Pending: ${field.format(val)}`;
      } else {
        field.live.textContent = 'Pending…';
      }
      field.live.classList.add('pending');
    });
    field.el.addEventListener('blur', () => {
      if (field.el.dataset.dirty !== 'true') return;
      const current = Number.parseFloat(field.el.value);
      if (!Number.isFinite(current) && Number.isFinite(field.applied)) {
        field.el.dataset.dirty = '';
        field.el.value = field.toInput(field.applied);
        field.live.textContent = `Applied: ${field.format(field.applied)}`;
        field.live.classList.remove('pending');
      }
    });
  });

  const tabButtons = Array.from(document.querySelectorAll('.tab-btn'));
  const tabPanes = Array.from(document.querySelectorAll('.tab-pane'));
  const collapseState = loadJSON(CARD_STORAGE_KEY, {});

  function setActiveTab(target) {
    if (!target) return;
    tabButtons.forEach((btn) => {
      const isActive = btn.dataset.tab === target;
      btn.classList.toggle('active', isActive);
    });
    tabPanes.forEach((pane) => {
      const match = pane.dataset.tab === target;
      pane.classList.toggle('active', match);
    });
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem(TAB_STORAGE_KEY, target);
    }
  }

  tabButtons.forEach((btn) => {
    btn.addEventListener('click', () => {
      const target = btn.dataset.tab;
      if (!target || btn.classList.contains('active')) return;
      setActiveTab(target);
    });
  });

  const storedTab = typeof localStorage !== 'undefined' ? localStorage.getItem(TAB_STORAGE_KEY) : null;
  setActiveTab(storedTab || tabButtons[0]?.dataset.tab);

  document.querySelectorAll('[data-card]').forEach((card) => {
    const toggle = card.querySelector('.collapse-toggle');
    if (!toggle) return;
    const cardId = card.dataset.card;
    if (cardId && collapseState[cardId]) {
      card.classList.add('collapsed');
    }
    toggle.setAttribute('aria-expanded', (!card.classList.contains('collapsed')).toString());
    toggle.addEventListener('click', () => {
      const collapsed = card.classList.toggle('collapsed');
      toggle.setAttribute('aria-expanded', (!collapsed).toString());
      if (!cardId || typeof localStorage === 'undefined') return;
      if (collapsed) {
        collapseState[cardId] = true;
      } else {
        delete collapseState[cardId];
      }
      localStorage.setItem(CARD_STORAGE_KEY, JSON.stringify(collapseState));
    });
  });
  setConnection('connecting…', 'pending');
  updateManualSlider(stateCache.manualPct, false);
  connectWS();
})();
