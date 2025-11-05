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
    crossLink: $('cross-link')
  };

  const stateCache = {
    fanCount: 1,
    pidEnabled: true,
    manualPct: 30,
    awaitingApply: false,
    dsp: {}
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
  let ws;
  let seq = 1;
  const pending = new Map();
  let lastUpdateTs = 0;
  let reconnectBackoff = 500;
  let pendingDsp = {};
  let dspFlushTimer = null;

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

  function setFormStatus(text, variant = '') {
    S.statusLine.textContent = text;
    S.statusLine.classList.remove('success', 'error', 'pending');
    if (variant) S.statusLine.classList.add(variant);
  }

  function setModeUI(pidEnabled) {
    stateCache.pidEnabled = pidEnabled;
    S.modePid.classList.toggle('active', pidEnabled);
    S.modeManual.classList.toggle('active', !pidEnabled);
    const modeLabel = pidEnabled ? 'PID' : 'Manual';
    const fansLabel = `${stateCache.fanCount} fan${stateCache.fanCount === 1 ? '' : 's'}`;
    S.modeBadge.textContent = `${modeLabel} · ${fansLabel}`;
    S.manpct.disabled = pidEnabled;
    S.manpctValue.style.opacity = pidEnabled ? '0.5' : '1';
  }

  function updateManualSlider(val) {
    const pct = clamp(Number(val) || 0, 0, 100);
    S.manpct.value = pct;
    S.manpctValue.textContent = `${pct}%`;
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
    const meta = DSP_META[param];
    const min = ctrl.min ?? meta?.min ?? -100;
    const max = ctrl.max ?? meta?.max ?? 100;
    const step = ctrl.step ?? meta?.step ?? 0.1;
    let clamped = clamp(value, min, max);
    clamped = Math.round(clamped / step) * step;
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
            applyState(msg.data || {}, msg.dsp || {});
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

  function applyState(core, dsp) {
    lastUpdateTs = Date.now();
    if (typeof core.temp1 === 'number') S.temp1.textContent = `${core.temp1.toFixed(1)} °C`; else S.temp1.textContent = '—';
    if (typeof core.temp2 === 'number') S.temp2.textContent = `${core.temp2.toFixed(1)} °C`; else S.temp2.textContent = '—';
    if (typeof core.fan_rpm === 'number') S.fanrpm.textContent = core.fan_rpm; else S.fanrpm.textContent = '—';
    if (typeof core.fan_target_pct === 'number') S.fantarget.textContent = `${core.fan_target_pct}%`; else S.fantarget.textContent = '—';
    if (typeof core.fan_count === 'number') stateCache.fanCount = Math.max(1, Math.round(core.fan_count));

    const pidEnabled = !!core.pid_enabled;
    setModeUI(pidEnabled);
    if (!pidEnabled && typeof core.fan_target_pct === 'number') {
      stateCache.manualPct = core.fan_target_pct;
    }
    updateManualSlider(stateCache.manualPct);

    if (typeof core.sp1 === 'number') S.sp1.value = core.sp1;
    if (typeof core.sp2 === 'number') S.sp2.value = core.sp2;
    if (typeof core.kp === 'number') S.kp.value = core.kp;
    if (typeof core.ki === 'number') S.ki.value = core.ki;
    if (typeof core.kd === 'number') S.kd.value = core.kd;

    if (stateCache.awaitingApply) {
      setFormStatus('Settings applied by controller', 'success');
      stateCache.awaitingApply = false;
      showToast('Settings applied', 'success', 2200);
    } else {
      setFormStatus('Latest data received from controller', 'success');
    }

    updateDspFromState(dsp);
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
    updateManualSlider(stateCache.manualPct);
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
  setConnection('connecting…', 'pending');
  updateManualSlider(stateCache.manualPct);
  connectWS();
})();
