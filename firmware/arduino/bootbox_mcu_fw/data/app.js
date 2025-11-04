(() => {
  const S = {
    conn: document.getElementById('conn'),
    temp1: document.getElementById('temp1'),
    temp2: document.getElementById('temp2'),
    fanrpm: document.getElementById('fanrpm'),
    fantarget: document.getElementById('fantarget'),
    mode: document.getElementById('mode'),
    manpct: document.getElementById('manpct'),
    sp1: document.getElementById('sp1'),
    sp2: document.getElementById('sp2'),
    kp: document.getElementById('kp'),
    ki: document.getElementById('ki'),
    kd: document.getElementById('kd'),
    save: document.getElementById('save'),
    refresh: document.getElementById('refresh'),
    getlogs: document.getElementById('getlogs'),
    clearlog: document.getElementById('clearlog'),
    log: document.getElementById('log')
  };

  let ws, seq = 1, pending = new Map();
  function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${proto}://${location.host}/ws`;
    let backoff = 500;
    function attempt() {
      setStatus('connecting…');
      try {
        ws = new WebSocket(url);
      } catch (e) {
        schedule(); return;
      }
      ws.onopen = () => setStatus('connected');
      ws.onclose = () => { setStatus('disconnected'); schedule(); };
      ws.onerror = () => { setStatus('error'); };
      ws.onmessage = (ev) => {
        let msg; try { msg = JSON.parse(ev.data); } catch { return; }
        if (msg.type === 'state') applyState(msg.data);
        if (msg.type === 'pong') ack(msg.id);
        if (msg.type === 'error') console.warn('server error', msg);
        if (msg.type === 'logs') renderLogs(msg.data);
      };
    }
    function schedule() {
      backoff = Math.min(backoff * 2, 30000);
      setTimeout(attempt, backoff + Math.floor(Math.random()*250));
    }
    attempt();
  }

  function setStatus(text) { S.conn.textContent = text; }

  function send(type, data) {
    if (!ws || ws.readyState !== 1) return;
    const id = seq++;
    const payload = JSON.stringify({ type, id, data });
    ws.send(payload);
    const t = setTimeout(() => {
      if (pending.has(id)) {
        // simple retry once
        ws.send(payload);
      }
    }, 2000);
    pending.set(id, t);
  }
  function ack(id) {
    const t = pending.get(id);
    if (t) { clearTimeout(t); pending.delete(id); }
  }

  function applyState(d) {
    S.temp1.textContent = d.temp1 == null ? '—' : `${d.temp1.toFixed(1)} °C`;
    S.temp2.textContent = d.temp2 == null ? '—' : `${d.temp2.toFixed(1)} °C`;
    S.fanrpm.textContent = d.fan_rpm ?? '—';
    S.fantarget.textContent = `${d.fan_target_pct ?? 0}%`;
    S.mode.value = d.pid_enabled ? 'pid' : 'manual';
    S.sp1.value = d.sp1 ?? '';
    S.sp2.value = d.sp2 ?? '';
    S.kp.value = d.kp ?? '';
    S.ki.value = d.ki ?? '';
    S.kd.value = d.kd ?? '';
  }

  S.save.onclick = () => {
    const pid_enabled = S.mode.value === 'pid';
    const data = {
      pid_enabled,
      sp1: parseFloat(S.sp1.value),
      sp2: parseFloat(S.sp2.value),
      fan_manual_pct: parseInt(S.manpct.value || '0', 10),
      kp: parseFloat(S.kp.value),
      ki: parseFloat(S.ki.value),
      kd: parseFloat(S.kd.value)
    };
    send('set_settings', data);
  };
  S.refresh.onclick = () => send('get_state');
  S.getlogs.onclick = () => send('get_logs');
  S.clearlog.onclick = () => { S.log.textContent = '(empty)'; };
  function renderLogs(lines) {
    if (!Array.isArray(lines)) return;
    S.log.textContent = lines.join('\n');
    S.log.scrollTop = S.log.scrollHeight;
  }

  // periodic ping to keep alive and test ack path
  setInterval(() => send('ping'), 5000);
  connectWS();
})();
