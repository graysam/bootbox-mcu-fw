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
    toastStack: $('toast-stack')
  };

  const stateCache = {
    pidEnabled: true,
    manualPct: 30,
    awaitingApply: false,
    fanCount: 1
  };
  let ws;
  let seq = 1;
  let lastUpdate = 0;
  const pending = new Map();

  function clamp(val, min, max) {
    return Math.min(Math.max(val, min), max);
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
      setTimeout(() => toast.remove(), 250);
    }, timeout);
  }

  function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${proto}://${location.host}/ws`;
    let backoff = 500;

    function attempt() {
      setConnection('connecting…', 'pending');
      try {
        ws = new WebSocket(url);
      } catch (err) {
        schedule(); return;
      }
      ws.onopen = () => {
        setConnection('connected', 'good');
        backoff = 500;
        setFormStatus('Syncing with controller…', 'pending');
        send('get_state');
        send('get_logs');
        showToast('Connected to controller', 'success', 2000);
      };
      ws.onclose = () => {
        setConnection('disconnected', 'idle');
        showToast('Connection lost – retrying…', 'error', 2400);
        schedule();
      };
      ws.onerror = () => setConnection('error', 'bad');
      ws.onmessage = (ev) => {
        let msg;
        try { msg = JSON.parse(ev.data); }
        catch { return; }

        if (msg.type === 'ack') {
          ack(msg.id);
          return;
        }
        if (typeof msg.id === 'number' && msg.type !== 'ack') {
          // acknowledge reliable frames coming from the controller
          ws?.readyState === 1 && ws.send(JSON.stringify({ type: 'ack', id: msg.id }));
        }

        switch (msg.type) {
          case 'state':
            applyState(msg.data || {});
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
    function schedule() {
      backoff = Math.min(backoff * 2, 20000);
      const jitter = Math.floor(Math.random() * 300);
      setTimeout(attempt, backoff + jitter);
    }
    attempt();
  }

  function send(type, data) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      if (type !== 'ping') {
        showToast('Controller offline – retrying…', 'warn', 2400);
      }
      return;
    }
    const id = seq++;
    const payload = JSON.stringify({ type, id, data });
    ws.send(payload);
    const timer = setTimeout(() => {
      if (pending.has(id)) {
        ws.readyState === WebSocket.OPEN && ws.send(payload);
      }
    }, 2000);
    pending.set(id, timer);
  }

  function ack(id) {
    if (!id) return;
    const timer = pending.get(id);
    if (timer) {
      clearTimeout(timer);
      pending.delete(id);
    }
  }

  function fmtTemp(temp) {
    if (typeof temp !== 'number' || Number.isNaN(temp)) return '—';
    return `${temp.toFixed(1)} °C`;
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

  function applyState(d) {
    lastUpdate = Date.now();

    if (typeof d.temp1 === 'number') S.temp1.textContent = fmtTemp(d.temp1);
    else S.temp1.textContent = '—';
    if (typeof d.temp2 === 'number') S.temp2.textContent = fmtTemp(d.temp2);
    else S.temp2.textContent = '—';

    S.fanrpm.textContent = (d.fan_rpm ?? '—');
    const tgt = typeof d.fan_target_pct === 'number' ? clamp(d.fan_target_pct, 0, 100) : 0;
    S.fantarget.textContent = `${tgt}%`;

    if (typeof d.fan_count === 'number' && d.fan_count > 0) {
      stateCache.fanCount = Math.min(Math.max(Math.round(d.fan_count), 1), 4);
    }

    const pidEnabled = !!d.pid_enabled;
    setModeUI(pidEnabled);
    if (!pidEnabled && typeof tgt === 'number') {
      stateCache.manualPct = tgt;
    }
    updateManualSlider(stateCache.manualPct);

    if (typeof d.sp1 === 'number') S.sp1.value = d.sp1;
    if (typeof d.sp2 === 'number') S.sp2.value = d.sp2;
    if (typeof d.kp === 'number') S.kp.value = d.kp;
    if (typeof d.ki === 'number') S.ki.value = d.ki;
    if (typeof d.kd === 'number') S.kd.value = d.kd;

    if (stateCache.awaitingApply) {
      setFormStatus('Settings applied by controller', 'success');
      stateCache.awaitingApply = false;
      showToast('Settings applied', 'success', 2200);
    } else {
      setFormStatus('Latest data received from controller', 'success');
    }
  }

  function updateManualSlider(val) {
    const pct = clamp(Number(val) || 0, 0, 100);
    S.manpct.value = pct;
    S.manpctValue.textContent = `${pct}%`;
  }

  function setFormStatus(text, variant = '') {
    S.statusLine.textContent = text;
    S.statusLine.classList.remove('success', 'error', 'pending');
    if (variant) S.statusLine.classList.add(variant);
  }

  function clearValidation() {
    [S.sp1, S.sp2, S.kp, S.ki, S.kd].forEach((el) => el.classList.remove('invalid'));
  }

  function collectSettings() {
    clearValidation();
    const errors = [];

    const pidEnabled = S.modePid.classList.contains('active');
    const sp1 = Number.parseFloat(S.sp1.value);
    if (!Number.isFinite(sp1)) { S.sp1.classList.add('invalid'); errors.push('Setpoint 1'); }
    const sp2 = Number.parseFloat(S.sp2.value);
    if (!Number.isFinite(sp2)) { S.sp2.classList.add('invalid'); errors.push('Setpoint 2'); }
    const kp = Number.parseFloat(S.kp.value);
    if (!Number.isFinite(kp)) { S.kp.classList.add('invalid'); errors.push('Kp'); }
    const ki = Number.parseFloat(S.ki.value);
    if (!Number.isFinite(ki)) { S.ki.classList.add('invalid'); errors.push('Ki'); }
    const kd = Number.parseFloat(S.kd.value);
    if (!Number.isFinite(kd)) { S.kd.classList.add('invalid'); errors.push('Kd'); }

    const fan_manual_pct = clamp(Number.parseInt(S.manpct.value, 10) || 0, 0, 100);

    if (errors.length) {
      setFormStatus(`Please review: ${errors.join(', ')}`, 'error');
      showToast('Fill in all control fields before saving', 'error', 3200);
      return null;
    }

    stateCache.manualPct = fan_manual_pct;
    return { pid_enabled: pidEnabled, sp1, sp2, fan_manual_pct, kp, ki, kd };
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
    const payload = collectSettings();
    if (!payload) return;
    setFormStatus('Sending settings to controller…', 'pending');
    stateCache.awaitingApply = true;
    send('set_settings', payload);
  });

  S.refresh.addEventListener('click', () => {
    setFormStatus('Sync requested…', 'pending');
    send('get_state');
  });

  S.getlogs.addEventListener('click', () => {
    send('get_logs');
  });

  S.clearlog.addEventListener('click', () => {
    S.log.textContent = '(empty)';
  });

  function renderLogs(lines) {
    if (!Array.isArray(lines)) return;
    S.log.textContent = lines.join('\n');
    if (S.autoScroll?.checked) {
      S.log.scrollTop = S.log.scrollHeight;
    }
  }

  function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) return '';
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
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
      S.uploadStatus.textContent = `Uploaded ${file.name} (${formatBytes(file.size)})`;
      showToast('Upload complete', 'success', 2400);
      send('get_logs');
    } catch (err) {
      S.uploadStatus.textContent = `Upload failed: ${err.message}`;
      showToast(`Upload failed: ${err.message}`, 'error', 3600);
    } finally {
      S.uploadInput.value = '';
    }
  }

  S.uploadBtn.addEventListener('click', () => S.uploadInput.click());
  S.uploadInput.addEventListener('change', (ev) => handleUpload(ev.target.files?.[0]));

  function updateLastUpdateLabel() {
    if (!lastUpdate) {
      S.lastUpdate.textContent = 'never';
      return;
    }
    const diff = Math.floor((Date.now() - lastUpdate) / 1000);
    if (diff < 2) {
      S.lastUpdate.textContent = 'just now';
    } else if (diff < 60) {
      S.lastUpdate.textContent = `${diff} sec ago`;
    } else if (diff < 3600) {
      const mins = Math.floor(diff / 60);
      S.lastUpdate.textContent = `${mins} min${mins > 1 ? 's' : ''} ago`;
    } else {
      const hrs = Math.floor(diff / 3600);
      S.lastUpdate.textContent = `${hrs} hr${hrs > 1 ? 's' : ''} ago`;
    }
  }

  setInterval(updateLastUpdateLabel, 1000);
  setInterval(() => send('ping'), 5000);

  window.addEventListener('beforeunload', () => {
    try { ws?.close(); } catch (err) { /* ignore */ }
  });

  setConnection('connecting…', 'pending');
  updateManualSlider(stateCache.manualPct);
  connectWS();
})();
