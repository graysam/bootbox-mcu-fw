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
  sysBoot: $('sys-boot'),
  thermChannel: $('therm-channel'),
  thermLow: $('therm-low'),
  thermHigh: $('therm-high'),
  thermNominal: $('therm-nominal'),
  thermStart: $('therm-start'),
  thermCaptureLow: $('therm-capture-low'),
  thermCaptureHigh: $('therm-capture-high'),
  thermSolve: $('therm-solve'),
  thermCancel: $('therm-cancel'),
  thermClear: $('therm-clear'),
  thermMessage: $('therm-message'),
  thermSession: $('therm-session'),
  thermTableBody: $('therm-table-body'),
  dspControls: $('dsp-controls'),
  dspHwStatus: $('dsp-hw-status'),
  dspActiveBundle: $('dsp-active-bundle'),
  dspStatus: $('dsp-status'),
  dspBundleList: $('dsp-bundle-list'),
  dspUploadName: $('dsp-upload-name'),
  dspUploadProgram: $('dsp-upload-program'),
  dspUploadInterface: $('dsp-upload-interface'),
  dspUploadSubmit: $('dsp-upload-submit'),
  dspSchemaState: $('dsp-schema-state'),
  dspPresetList: $('dsp-preset-list'),
  dspPresetName: $('dsp-preset-name'),
  dspPresetSave: $('dsp-preset-save'),
  dspRefreshBundles: $('dsp-refresh-bundles'),
  dspPushActive: $('dsp-push-active'),
  dspRefreshPresets: $('dsp-refresh-presets')
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
    sys: {},
    therm: {
      calibrations: [],
      session: {}
    }
  };
  let thermBusy = false;
  const DSP_CONTROL_TYPES = new Set(['slider', 'knob', 'fader', 'toggle']);
  const dspControlElements = new Map();
  const dspView = {
    schema: [],
    values: {},
    bundles: [],
    presets: [],
    activeBundle: '',
    hwReady: false,
    hwError: ''
  };

  function sanitizeControlSpec(raw = {}) {
    if (!raw || typeof raw !== 'object') return null;
    const id = typeof raw.id === 'string' ? raw.id : '';
    if (!id) return null;
    const typeRaw = typeof raw.type === 'string' ? raw.type.toLowerCase() : 'slider';
    const type = DSP_CONTROL_TYPES.has(typeRaw) ? typeRaw : 'slider';
    let min = Number(raw.min);
    if (!Number.isFinite(min)) {
      min = type === 'toggle' ? 0 : 0;
    }
    let max = Number(raw.max);
    if (!Number.isFinite(max)) {
      max = type === 'toggle' ? 1 : min + 1;
    }
    if (max <= min) {
      max = min + (type === 'toggle' ? 1 : 1);
    }
    const defaultStep = type === 'toggle' ? 1 : 0.1;
    let step = Number(raw.step);
    if (!Number.isFinite(step) || step <= 0) {
      step = defaultStep;
    }
    const decimals = Number.isFinite(raw.decimals) ? Number(raw.decimals) : undefined;
    const defaultValue = Number.isFinite(raw.default) ? Number(raw.default) : NaN;
    return {
      id,
      label: typeof raw.label === 'string' && raw.label.length ? raw.label : id,
      type,
      unit: typeof raw.unit === 'string' ? raw.unit : '',
      min,
      max,
      step,
      decimals,
      defaultValue
    };
  }

  function clampValue(value, min, max) {
    if (!Number.isFinite(value)) return min;
    return Math.min(Math.max(value, min), max);
  }

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

  function formatResistance(ohms) {
    if (!Number.isFinite(ohms) || ohms <= 0) return '—';
    const units = ['Ω', 'kΩ', 'MΩ'];
    let value = ohms;
    let idx = 0;
    while (value >= 1000 && idx < units.length - 1) {
      value /= 1000;
      idx++;
    }
    const decimals = value < 10 ? 2 : value < 100 ? 1 : 0;
    return `${value.toFixed(decimals)} ${units[idx]}`;
  }

  function storeThermState(calibrations, session) {
    if (Array.isArray(calibrations)) {
      stateCache.therm.calibrations = calibrations.map((item) => {
        const copy = { ...item };
        if (copy.channel != null) copy.channel = Number(copy.channel);
        if (copy.nominal_ohms != null) copy.nominal_ohms = Number(copy.nominal_ohms);
        if (copy.beta != null) copy.beta = Number(copy.beta);
        if (copy.series_ohms != null) copy.series_ohms = Number(copy.series_ohms);
        copy.calibrated = !!copy.calibrated;
        return copy;
      });
    }
    if (session && typeof session === 'object') {
      const copy = { ...session };
      if (copy.channel != null) copy.channel = Number(copy.channel);
      copy.has_low = !!copy.has_low;
      copy.has_high = !!copy.has_high;
      if (copy.low_actual_c != null) copy.low_actual_c = Number(copy.low_actual_c);
      if (copy.high_actual_c != null) copy.high_actual_c = Number(copy.high_actual_c);
      if (copy.low_adc != null) copy.low_adc = Number(copy.low_adc);
      if (copy.high_adc != null) copy.high_adc = Number(copy.high_adc);
      copy.active = !!copy.active;
      stateCache.therm.session = copy;
    }
  }

  function showThermMessage(text, variant = '') {
    if (!S.thermMessage) return;
    S.thermMessage.textContent = text;
    S.thermMessage.classList.remove('success', 'error', 'pending');
    if (variant) S.thermMessage.classList.add(variant);
  }

  function renderThermTable(calibrations = []) {
    if (!S.thermTableBody) return;
    if (!Array.isArray(calibrations) || !calibrations.length) {
      S.thermTableBody.innerHTML = '<tr><td colspan="5">Awaiting data…</td></tr>';
      return;
    }
    const rows = calibrations.map((therm) => {
      const channel = therm.channel ?? '?';
      const status = therm.calibrated ? 'Calibrated' : 'Default';
      const nominal = formatResistance(therm.nominal_ohms);
      const beta = Number.isFinite(therm.beta) ? therm.beta.toFixed(0) : '—';
      const series = formatResistance(therm.series_ohms);
      return `<tr>
        <td>${channel}</td>
        <td class="status">${status}</td>
        <td>${nominal}</td>
        <td>${beta}</td>
        <td>${series}</td>
      </tr>`;
    });
    S.thermTableBody.innerHTML = rows.join('');
  }

  function renderThermSession(session = {}) {
    if (!S.thermSession) return;
    if (!session.active) {
      S.thermSession.textContent = 'No calibration session in progress.';
      return;
    }
    const channel = session.channel ?? '?';
    const lowStr = session.has_low && Number.isFinite(session.low_actual_c)
      ? `${session.low_actual_c.toFixed(1)}°C (ADC ${session.low_adc ?? '—'})`
      : 'pending';
    const highStr = session.has_high && Number.isFinite(session.high_actual_c)
      ? `${session.high_actual_c.toFixed(1)}°C (ADC ${session.high_adc ?? '—'})`
      : 'pending';
    const ready = session.has_low && session.has_high ? 'Ready to solve.' : 'Capture remaining reference(s).';
    S.thermSession.textContent = `Session active on channel ${channel}: low ${lowStr}, high ${highStr}. ${ready}`;
  }

  function updateThermButtons() {
    if (!S.thermChannel) return;
    const selected = Number.parseInt(S.thermChannel.value, 10) || 1;
    const session = stateCache.therm.session || {};
    const active = !!session.active && session.channel === selected;
    const hasLow = !!session.has_low;
    const hasHigh = !!session.has_high;
    const disabled = thermBusy;
    if (S.thermStart) S.thermStart.disabled = disabled;
    if (S.thermCaptureLow) S.thermCaptureLow.disabled = disabled || !active;
    if (S.thermCaptureHigh) S.thermCaptureHigh.disabled = disabled || !active;
    if (S.thermSolve) S.thermSolve.disabled = disabled || !active || !hasLow || !hasHigh;
    if (S.thermCancel) S.thermCancel.disabled = disabled || !active;
    if (S.thermClear) S.thermClear.disabled = disabled;
  }

  function refreshThermUI() {
    renderThermTable(stateCache.therm.calibrations);
    renderThermSession(stateCache.therm.session);
    updateThermButtons();
  }

  function handleThermResponse(payload) {
    if (!payload || typeof payload !== 'object') {
      showThermMessage('Controller returned no data', 'error');
      return;
    }
    storeThermState(payload.thermistors, payload.therm_cal_session);
    refreshThermUI();
    if (payload.calibration) {
      const cal = payload.calibration;
      showToast(`Channel ${cal.channel} solved: β ${Number(cal.beta).toFixed(0)}, nominal ${formatResistance(cal.nominal_ohms)}`, 'success', 2600);
    }
    if (payload.message) {
      showThermMessage(payload.message, payload.ok === false ? 'error' : 'success');
    } else if (payload.ok === false) {
      showThermMessage('Calibration action failed', 'error');
    } else {
      showThermMessage('Calibration status updated');
    }
  }

  async function sendThermAction(action, extra = {}) {
    if (!S.thermChannel || thermBusy) return;
    const channel = Number.parseInt(S.thermChannel.value, 10) || 1;
    const body = { action, channel, ...extra };
    thermBusy = true;
    updateThermButtons();
    try {
      const res = await fetch('/api/therm/calibration', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      });
      const data = await res.json();
      handleThermResponse(data);
      if (!res.ok || data.ok === false) {
        showToast(data.message || `Calibration ${action} failed`, 'error', 3200);
      } else if (data.message) {
        showToast(data.message, 'success', 2200);
      }
    } catch (err) {
      const msg = err && err.message ? err.message : 'Calibration request failed';
      showThermMessage(msg, 'error');
      showToast(`Calibration ${action} failed: ${msg}`, 'error', 3200);
    } finally {
      thermBusy = false;
      updateThermButtons();
    }
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

    if (Array.isArray(sys.thermistors) || (sys.therm_cal_session && typeof sys.therm_cal_session === 'object')) {
      storeThermState(sys.thermistors, sys.therm_cal_session);
      refreshThermUI();
    }
  }

  function updateManualSlider(val, pendingLabel = false) {
    const pct = clamp(Number(val) || 0, 0, 100);
    S.manpct.value = pct;
    S.manpctValue.textContent = `${pct}%${pendingLabel ? ' (pending)' : ''}`;
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

    if (dsp.bundle) {
      dspView.activeBundle = dsp.bundle;
      updateBundleBadge();
      renderBundleList();
    }
    if (dsp.values && typeof dsp.values === 'object') {
      dspView.values = { ...dspView.values, ...dsp.values };
      updateDspControlValues();
    }
    if (typeof dsp.hw_ready === 'boolean') {
      dspView.hwReady = !!dsp.hw_ready;
    }
    dspView.hwError = typeof dsp.hw_error === 'string' ? dsp.hw_error : '';
    if (typeof dsp.schema_ready === 'boolean') {
      updateSchemaBadge(dsp.schema_ready);
    }
    updateDspHardwareStatus();
    updateSystemInfo(sys);
  }

  function renderLogs(lines) {
    if (!Array.isArray(lines)) return;
    S.log.textContent = lines.join('\n');
    if (S.autoScroll?.checked) {
      S.log.scrollTop = S.log.scrollHeight;
    }
  }

  async function apiFetch(url, options = {}) {
    const res = await fetch(url, options);
    const text = await res.text();
    let data = {};
    if (text) {
      try {
        data = JSON.parse(text);
      } catch {
        data = {};
      }
    }
    if (!res.ok || (Object.prototype.hasOwnProperty.call(data, 'ok') && data.ok === false)) {
      const message = data.message || res.statusText || 'Request failed';
      throw new Error(message);
    }
    return data;
  }

  function setDspStatus(message, variant = '') {
    if (!S.dspStatus) return;
    S.dspStatus.textContent = message;
    S.dspStatus.classList.remove('success', 'error', 'pending');
    if (variant) S.dspStatus.classList.add(variant);
  }

  function updateDspHardwareStatus() {
    if (!S.dspHwStatus) return;
    const el = S.dspHwStatus;
    el.classList.remove('success', 'error', 'pending');
    if (dspView.hwError) {
      el.textContent = `DSP error: ${dspView.hwError}`;
      el.classList.add('error');
      return;
    }
    if (!dspView.schema.length) {
      el.textContent = 'Waiting for interface upload';
      el.classList.add('pending');
      return;
    }
    if (dspView.hwReady) {
      el.textContent = 'DSP link ready';
      el.classList.add('success');
      return;
    }
    el.textContent = 'DSP link offline';
    el.classList.add('pending');
  }

  async function refreshDspBundles() {
    if (!S.dspBundleList) return;
    try {
      const data = await apiFetch('/api/dsp/bundles');
      dspView.bundles = data.bundles || [];
      if (data.active) dspView.activeBundle = data.active;
      renderBundleList();
    } catch (err) {
      setDspStatus(err.message || 'Failed to load bundles', 'error');
    }
  }

  async function refreshDspSchema() {
    if (!S.dspControls) return;
    try {
      const data = await apiFetch('/api/dsp/schema');
      const controls = Array.isArray(data.controls) ? data.controls : [];
      dspView.schema = controls.map((spec) => sanitizeControlSpec(spec)).filter(Boolean);
      dspView.values = { ...(data.values || {}) };
      if (data.active) dspView.activeBundle = data.active;
      dspView.presets = Array.isArray(data.presets) ? data.presets : [];
      if (typeof data.hw_ready === 'boolean') {
        dspView.hwReady = !!data.hw_ready;
      }
      dspView.hwError = typeof data.hw_error === 'string' ? data.hw_error : '';
      const schemaReady = typeof data.schema_ready === 'boolean' ? data.schema_ready : dspView.schema.length > 0;
      renderDspControls();
      renderPresetList();
      updateBundleBadge();
      updateSchemaBadge(schemaReady);
      updateDspHardwareStatus();
    } catch (err) {
      updateSchemaBadge(false);
      setDspStatus(err.message || 'Failed to load schema', 'error');
      updateDspHardwareStatus();
    }
  }

  function updateBundleBadge() {
    if (S.dspActiveBundle) {
      S.dspActiveBundle.textContent = dspView.activeBundle || '—';
    }
  }

  function updateSchemaBadge(ready) {
    if (!S.dspSchemaState) return;
    S.dspSchemaState.textContent = ready ? 'schema ready' : 'schema pending';
  }

  function renderBundleList() {
    if (!S.dspBundleList) return;
    S.dspBundleList.innerHTML = '';
    if (!dspView.bundles.length) {
      S.dspBundleList.innerHTML = '<div class=\"muted\">No bundles uploaded yet.</div>';
      return;
    }
    dspView.bundles.forEach((bundle) => {
      const row = document.createElement('div');
      row.className = 'bundle-row';
      if (bundle.active) row.classList.add('active');
      const left = document.createElement('div');
      left.innerHTML = `<strong>${bundle.name}</strong><br><span class=\"muted\">${bundle.has_interface ? 'Interface' : 'No interface'}</span>`;
      const actions = document.createElement('div');
      actions.className = 'actions';

      const activate = document.createElement('button');
      activate.className = 'secondary';
      activate.textContent = 'Activate';
      activate.disabled = bundle.active;
      activate.addEventListener('click', () => changeBundle(bundle.name));

      const pushBtn = document.createElement('button');
      pushBtn.className = 'secondary';
      pushBtn.textContent = 'Push';
      pushBtn.addEventListener('click', () => triggerPush(bundle.name));

      const renameBtn = document.createElement('button');
      renameBtn.className = 'secondary';
      renameBtn.textContent = 'Rename';
      renameBtn.addEventListener('click', () => renameBundle(bundle.name));

      const deleteBtn = document.createElement('button');
      deleteBtn.className = 'secondary';
      deleteBtn.textContent = 'Delete';
      deleteBtn.addEventListener('click', () => deleteBundle(bundle.name));

      actions.append(activate, pushBtn, renameBtn, deleteBtn);
      row.append(left, actions);
      S.dspBundleList.appendChild(row);
    });
  }

  async function changeBundle(name) {
    try {
      await dspAction('select_bundle', { name });
      setDspStatus(`Activated bundle ${name}`, 'success');
      await Promise.all([refreshDspBundles(), refreshDspSchema()]);
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
  }

  async function triggerPush(name) {
    try {
      setDspStatus(`Pushing ${name} to DSP...`, 'pending');
      await dspAction('push_bundle', { name });
      showToast(`Pushed ${name} (self-boot)`, 'info', 2000);
      setDspStatus(`Bundle ${name} pushed`, 'success');
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
    await refreshDspSchema();
  }

  async function renameBundle(oldName) {
    const newName = prompt('Enter new bundle name', oldName);
    if (!newName || newName === oldName) return;
    try {
      await dspAction('rename_bundle', { name: oldName, new_name: newName });
      setDspStatus(`Renamed to ${newName}`, 'success');
      await refreshDspBundles();
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
  }

  async function deleteBundle(name) {
    if (!confirm(`Delete bundle ${name}?`)) return;
    try {
      await dspAction('delete_bundle', { name });
      setDspStatus(`Deleted bundle ${name}`, 'success');
      await Promise.all([refreshDspBundles(), refreshDspSchema()]);
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
  }

  function renderDspControls() {
    if (!S.dspControls) return;
    dspControlElements.clear();
    S.dspControls.innerHTML = '';
    if (!dspView.schema.length) {
      S.dspControls.innerHTML = '<div class=\"muted\">Upload a bundle with an interface file to begin.</div>';
      return;
    }
    dspView.schema.forEach((spec) => {
      if (!spec || !spec.id) return;
      const card = document.createElement('div');
      card.className = 'dsp-control-card';
      const label = document.createElement('div');
      label.className = 'label';
      label.textContent = spec.label || spec.id;
      card.appendChild(label);
      const valueLabel = document.createElement('div');
      valueLabel.className = 'value-sm';
      valueLabel.textContent = '—';
      let input;
      const min = Number.isFinite(spec.min) ? spec.min : 0;
      const max = Number.isFinite(spec.max) ? spec.max : min + 1;
      const defaultValue = Number.isFinite(spec.defaultValue) ? spec.defaultValue : min;
      if (spec.type === 'toggle') {
        input = document.createElement('input');
        input.type = 'checkbox';
        const midpoint = (min + max) / 2;
        input.checked = defaultValue >= midpoint;
        valueLabel.textContent = formatDspValue(spec, input.checked ? max : min);
        input.addEventListener('change', () => {
          const nextValue = input.checked ? max : min;
          sendDspValue(spec.id, nextValue);
          valueLabel.textContent = formatDspValue(spec, nextValue);
        });
      } else {
        input = document.createElement('input');
        input.type = 'range';
        input.min = min;
        input.max = max;
        input.step = Number.isFinite(spec.step) && spec.step > 0 ? spec.step : 0.1;
        const startValue = clampValue(defaultValue, min, max);
        input.value = startValue;
        valueLabel.textContent = formatDspValue(spec, startValue);
        input.addEventListener('input', () => {
          valueLabel.textContent = formatDspValue(spec, Number(input.value));
        });
        input.addEventListener('change', () => {
          sendDspValue(spec.id, Number(input.value));
        });
      }
      card.appendChild(input);
      card.appendChild(valueLabel);
      dspControlElements.set(spec.id, { spec, input, valueLabel });
      S.dspControls.appendChild(card);
    });
    updateDspControlValues();
  }

  function formatDspValue(spec, value) {
    if (!Number.isFinite(value)) return '—';
    if (spec.type === 'toggle') {
      return value >= ((spec.min + spec.max) / 2) ? 'On' : 'Off';
    }
    const decimals = Number.isFinite(spec.decimals)
      ? spec.decimals
      : (spec.step && spec.step < 1 ? 2 : 0);
    const val = Number(value).toFixed(decimals);
    return spec.unit ? `${val} ${spec.unit}` : val;
  }

  function updateDspControlValues() {
    dspControlElements.forEach((entry, id) => {
      if (!entry || !entry.spec || !entry.input) return;
      const value = Number(dspView.values[id]);
      if (!Number.isFinite(value)) {
        if (entry.valueLabel) entry.valueLabel.textContent = '—';
        return;
      }
      if (entry.input.type === 'checkbox') {
        const midpoint = (entry.spec.min + entry.spec.max) / 2;
        entry.input.checked = value >= midpoint;
      } else {
        entry.input.value = clampValue(value, entry.spec.min, entry.spec.max);
      }
      if (entry.valueLabel) {
        entry.valueLabel.textContent = formatDspValue(entry.spec, value);
      }
    });
  }

  async function sendDspValue(id, value) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      showToast('Controller offline – unable to update DSP', 'warn', 2000);
      return;
    }
    let nextValue = value;
    const entry = dspControlElements.get(id);
    if (entry && entry.spec && entry.input?.type !== 'checkbox') {
      nextValue = clampValue(Number(value), entry.spec.min, entry.spec.max);
    }
    send('set_dsp', { id, value: nextValue });
  }

  function renderPresetList() {
    if (!S.dspPresetList) return;
    S.dspPresetList.innerHTML = '';
    if (!dspView.presets.length) {
      S.dspPresetList.innerHTML = '<div class=\"muted\">No presets saved.</div>';
      return;
    }
    dspView.presets.forEach((preset) => {
      const chip = document.createElement('div');
      chip.className = 'preset-chip';
      chip.textContent = preset;
      const loadBtn = document.createElement('button');
      loadBtn.className = 'secondary';
      loadBtn.textContent = 'Load';
      loadBtn.addEventListener('click', () => loadPreset(preset));
      const delBtn = document.createElement('button');
      delBtn.className = 'secondary';
      delBtn.textContent = '✕';
      delBtn.addEventListener('click', () => deletePreset(preset));
      chip.append(loadBtn, delBtn);
      S.dspPresetList.appendChild(chip);
    });
  }

  async function savePresetFromUi() {
    const name = S.dspPresetName?.value?.trim();
    if (!name) {
      showToast('Enter a preset name first', 'warn', 2000);
      return;
    }
    try {
      await dspAction('save_preset', { bundle: dspView.activeBundle, preset: name });
      showToast(`Preset ${name} saved`, 'success', 2000);
      S.dspPresetName.value = '';
      refreshDspSchema();
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
  }

  async function loadPreset(name) {
    try {
      await dspAction('load_preset', { bundle: dspView.activeBundle, preset: name });
      setDspStatus(`Preset ${name} loaded`, 'success');
      await refreshDspSchema();
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
  }

  async function deletePreset(name) {
    if (!confirm(`Delete preset ${name}?`)) return;
    try {
      await dspAction('delete_preset', { bundle: dspView.activeBundle, preset: name });
      showToast(`Preset ${name} deleted`, 'info', 2000);
      refreshDspSchema();
    } catch (err) {
      setDspStatus(err.message, 'error');
    }
  }

  async function dspAction(action, payload = {}) {
    payload.action = action;
    const res = await fetch('/api/dsp/action', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!res.ok || data.ok === false) {
      throw new Error(data.message || 'DSP action failed');
    }
    return data;
  }

  async function handleBundleUpload() {
    if (!S.dspUploadName || !S.dspUploadProgram || !S.dspUploadInterface) return;
    const bundle = S.dspUploadName.value.trim();
    const programFile = S.dspUploadProgram.files[0];
    const interfaceFile = S.dspUploadInterface.files[0];
    if (!bundle || !programFile || !interfaceFile) {
      setDspStatus('Provide bundle name, program, and interface files', 'error');
      return;
    }
    try {
      await uploadBundleFile(bundle, 'program', programFile);
      await uploadBundleFile(bundle, 'interface', interfaceFile);
      setDspStatus(`Bundle ${bundle} uploaded`, 'success');
      S.dspUploadProgram.value = '';
      S.dspUploadInterface.value = '';
      await Promise.all([refreshDspBundles(), refreshDspSchema()]);
    } catch (err) {
      setDspStatus(err.message || 'Upload failed', 'error');
    }
  }

  async function uploadBundleFile(bundle, kind, file) {
    const url = `/api/upload/adau?bundle=${encodeURIComponent(bundle)}&kind=${encodeURIComponent(kind)}`;
    const form = new FormData();
    form.append('file', file, file.name);
    const res = await fetch(url, { method: 'POST', body: form });
    if (!res.ok) {
      const text = await res.text();
      throw new Error(text || `Failed to upload ${kind}`);
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

  S.dspRefreshBundles?.addEventListener('click', refreshDspBundles);
  S.dspRefreshPresets?.addEventListener('click', refreshDspSchema);
  S.dspPushActive?.addEventListener('click', () => {
    if (dspView.activeBundle) triggerPush(dspView.activeBundle);
  });
  S.dspUploadSubmit?.addEventListener('click', handleBundleUpload);
  S.dspPresetSave?.addEventListener('click', savePresetFromUi);

  if (S.thermNominal && !S.thermNominal.value) {
    S.thermNominal.value = '25.0';
  }
  if (S.thermChannel) {
    S.thermChannel.addEventListener('change', () => updateThermButtons());
  }
  if (S.thermStart) {
    S.thermStart.addEventListener('click', () => {
      showThermMessage('Starting calibration session…', 'pending');
      sendThermAction('start');
    });
  }
  if (S.thermCaptureLow) {
    S.thermCaptureLow.addEventListener('click', () => {
      const raw = S.thermLow ? S.thermLow.value : '';
      const actual = Number.parseFloat(raw);
      if (!Number.isFinite(actual)) {
        showThermMessage('Enter the cold reference temperature before capturing.', 'error');
        return;
      }
      showThermMessage('Capturing cold reference…', 'pending');
      sendThermAction('capture', { point: 'low', actual_c: actual });
    });
  }
  if (S.thermCaptureHigh) {
    S.thermCaptureHigh.addEventListener('click', () => {
      const raw = S.thermHigh ? S.thermHigh.value : '';
      const actual = Number.parseFloat(raw);
      if (!Number.isFinite(actual)) {
        showThermMessage('Enter the hot reference temperature before capturing.', 'error');
        return;
      }
      showThermMessage('Capturing hot reference…', 'pending');
      sendThermAction('capture', { point: 'high', actual_c: actual });
    });
  }
  if (S.thermSolve) {
    S.thermSolve.addEventListener('click', () => {
      const nominalRaw = S.thermNominal ? S.thermNominal.value : '';
      const nominal = Number.parseFloat(nominalRaw);
      const session = stateCache.therm.session || {};
      if (!session.active) {
        showThermMessage('Start a calibration session before solving.', 'error');
        return;
      }
      if (!session.has_low || !session.has_high) {
        showThermMessage('Capture both cold and hot references before solving.', 'error');
        return;
      }
      const payload = Number.isFinite(nominal) ? { nominal_c: nominal } : {};
      showThermMessage('Solving calibration…', 'pending');
      sendThermAction('solve', payload);
    });
  }
  if (S.thermCancel) {
    S.thermCancel.addEventListener('click', () => {
      showThermMessage('Cancelling calibration session…', 'pending');
      sendThermAction('cancel');
    });
  }
  if (S.thermClear) {
    S.thermClear.addEventListener('click', () => {
      showThermMessage('Clearing calibration…', 'pending');
      sendThermAction('clear');
    });
  }

  async function refreshThermStatus() {
    try {
      const res = await fetch('/api/therm/calibration');
      if (!res.ok) return;
      const data = await res.json();
      storeThermState(data.thermistors, data.therm_cal_session);
      refreshThermUI();
    } catch {
      // ignore initial fetch errors
    }
  }

  refreshThermUI();
  refreshThermStatus();

  setInterval(() => send('ping'), 5000);
  setInterval(updateLastUpdateLabel, 1000);

  refreshDspBundles();
  refreshDspSchema();
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
  const defaultTab = tabButtons.length > 0 ? tabButtons[0].dataset.tab : null;
  setActiveTab(storedTab || defaultTab);

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
