(() => {
  'use strict';
  const $ = (s) => document.querySelector(s);
  const logEl = $('#log');
  const cmdEl = $('#cmd');
  const dot = $('#dot');
  const MAX_LINES = 2000;
  const lines = [];
  let ws = null;
  let retryMs = 1000;
  let filter = '';

  const load = (k, d) => { try { return localStorage.getItem(k) ?? d; } catch (e) { return d; } };
  const save = (k, v) => { try { localStorage.setItem(k, v); } catch (e) { /* private mode */ } };

  // ---- tabs -------------------------------------------------------------
  document.querySelectorAll('.tab').forEach((b) => b.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t === b));
    document.querySelectorAll('.panel').forEach((p) => p.classList.toggle('active', p.id === b.dataset.panel));
    if (b.dataset.panel === 'info') refreshInfo();
    if (b.dataset.panel === 'wifi') loadWifi();
    if (b.dataset.panel === 'console') cmdEl.focus();
  }));

  // ---- log pane ---------------------------------------------------------
  const fmtMs = (ms) => (Math.floor(ms / 1000) + '.' + String(ms % 1000).padStart(3, '0')).padStart(10, ' ');
  function lineEl(x) {
    const d = document.createElement('div');
    d.className = 'l-' + x.l;
    if (x.l === 'O') d.textContent = x.m;
    else if (x.l === 'C') d.textContent = '> ' + x.m;
    else d.textContent = fmtMs(x.ms) + ' ' + x.l + ' ' + x.m;
    return d;
  }
  const visible = (x) => !filter || (x.m || '').toLowerCase().includes(filter);
  function push(x) {
    lines.push(x);
    if (lines.length > MAX_LINES) lines.shift();
    if (!visible(x)) return;
    logEl.appendChild(lineEl(x));
    while (logEl.childElementCount > MAX_LINES) logEl.removeChild(logEl.firstChild);
    if ($('#autoscroll').checked) logEl.scrollTop = logEl.scrollHeight;
  }
  function rerender() {
    logEl.textContent = '';
    const f = document.createDocumentFragment();
    lines.filter(visible).forEach((x) => f.appendChild(lineEl(x)));
    logEl.appendChild(f);
    logEl.scrollTop = logEl.scrollHeight;
  }
  $('#level').addEventListener('change', (e) => { logEl.className = 'log min-' + e.target.value; save('level', e.target.value); });
  $('#filter').addEventListener('input', (e) => { filter = e.target.value.toLowerCase(); rerender(); });
  $('#clear').addEventListener('click', () => { lines.length = 0; logEl.textContent = ''; });

  // ---- websocket ------------------------------------------------------
  async function connect() {
    // The socket authenticates with a single-use ticket from the API.
    let ticket = '';
    try { ticket = (await api('/api/ws-ticket')).ticket || ''; } catch (e) { /* unreachable: the socket fails and retries */ }
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws?t=' + encodeURIComponent(ticket));
    ws.onopen = () => { dot.classList.add('on'); retryMs = 1000; push({ l: 'C', m: 'console connected' }); };
    ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }
      (Array.isArray(msg) ? msg : [msg]).forEach(handle);
    };
    ws.onclose = () => {
      dot.classList.remove('on');
      setTimeout(connect, retryMs);
      retryMs = Math.min(retryMs * 2, 10000);
    };
    ws.onerror = () => ws.close();
  }
  function handle(m) {
    if (m.t === 'log') push({ l: m.l, ms: m.ms, m: m.m });
    else if (m.t === 'out') push({ l: 'O', m: (m.m || '').replace(/\r/g, '').replace(/\n$/, '') });
  }

  // ---- command input with history -------------------------------------
  // Commands carrying passwords stay in this session's history but never reach localStorage.
  const SECRET = /^\s*(wifi\s+(set|add)|config\s+set\s+\S*pass)\b/i;
  let hist = [];
  try { hist = JSON.parse(load('hist', '[]')); } catch (e) { hist = []; }
  let hi = hist.length;
  function send(c) {
    push({ l: 'C', m: c });
    if (ws && ws.readyState === 1) ws.send(JSON.stringify({ t: 'cmd', c }));
    else push({ l: 'E', ms: 0, m: 'not connected' });
  }
  $('#cmdform').addEventListener('submit', (e) => {
    e.preventDefault();
    const c = cmdEl.value.trim();
    if (!c) return;
    send(c);
    cmdEl.value = '';
    if (hist[hist.length - 1] !== c) hist.push(c);
    if (hist.length > 50) hist.shift();
    hi = hist.length;
    save('hist', JSON.stringify(hist.filter((h) => !SECRET.test(h))));
  });
  cmdEl.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowUp') {
      if (hi > 0) cmdEl.value = hist[--hi];
      e.preventDefault();
    } else if (e.key === 'ArrowDown') {
      if (hi < hist.length - 1) cmdEl.value = hist[++hi];
      else { hi = hist.length; cmdEl.value = ''; }
      e.preventDefault();
    }
  });

  // ---- status bar and info ----------------------------------------------
  function fmtBytes(b) {
    if (b >= 1048576) return (b / 1048576).toFixed(1) + ' MB';
    if (b >= 1024) return Math.round(b / 1024) + ' KB';
    return b + ' B';
  }
  const fmtUp = (s) => {
    const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
    return (d ? d + 'd ' : '') + String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
  };
  async function refreshInfo() {
    let info;
    try { info = await (await fetch('/api/info', { cache: 'no-store' })).json(); } catch (e) { return; }
    $('#host').textContent = info.hostname;
    document.title = info.hostname + ' console';
    $('#s-wifi').textContent = info.wifi.state.replace(/_/g, ' ') + (info.wifi.ap ? ' + AP' : '');
    $('#s-ip').textContent = info.wifi.ip || (info.wifi.ap ? info.wifi.ap.ip : '–');
    $('#s-rssi').textContent = info.wifi.rssi ? info.wifi.rssi + ' dBm' : '–';
    $('#s-heap').textContent = fmtBytes(info.heap.free) + ' free';
    $('#s-uptime').textContent = fmtUp(info.uptime);
    $('#infobox').textContent = JSON.stringify(info, null, 2);
  }
  setInterval(refreshInfo, 5000);

  // ---- wifi -------------------------------------------------------------
  function msg(sel, text, isErr) {
    const el = $(sel);
    el.textContent = text;
    el.className = 'msg' + (isErr ? ' err' : text ? ' ok' : '');
  }
  async function api(url, opts) {
    const r = await fetch(url, Object.assign({ cache: 'no-store' }, opts || {}));
    let j = {};
    try { j = await r.json(); } catch (e) { /* no body */ }
    if (!r.ok && !j.error) j.error = r.status + ' ' + r.statusText;
    return j;
  }
  const post = (url, body) => api(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  function button(text, onclick) {
    const b = document.createElement('button');
    b.type = 'button';
    b.textContent = text;
    b.onclick = onclick;
    return b;
  }
  async function loadWifi() {
    let w;
    try { w = await api('/api/wifi'); } catch (e) { return; }
    const t = $('#netlist');
    t.textContent = '';
    if (!w.networks.length) {
      t.insertRow().insertCell().textContent = 'no stored networks';
    }
    w.networks.forEach((n) => {
      const tr = t.insertRow();
      if (w.state === 'sta_connected' && n.ssid === w.ssid) tr.className = 'cur';
      tr.insertCell().textContent = 'slot ' + n.slot;
      tr.insertCell().textContent = n.ssid;
      tr.insertCell().textContent = n.hasPassword ? 'password set' : 'open';
      tr.insertCell().append(
        button('Connect', async () => { const j = await post('/api/wifi', { slot: n.slot }); msg('#wifimsg', j.ok ? 'connecting to ' + n.ssid + ', progress is in the console log' : j.error, !j.ok); }),
        button('Remove', async () => { const j = await api('/api/wifi?slot=' + n.slot, { method: 'DELETE' }); msg('#wifimsg', j.ok ? 'slot ' + n.slot + ' removed' : j.error, !j.ok); loadWifi(); }),
      );
    });
    $('#mode').value = w.mode;
    $('#staTimeout').value = Math.round(w.settings.staTimeoutMs / 1000);
    $('#reconnTimeout').value = Math.round(w.settings.reconnectTimeoutMs / 1000);
    $('#apRetry').value = Math.round(w.settings.apRetryIntervalMs / 1000);
  }
  function renderScan(nets) {
    const t = $('#scanlist');
    const dl = $('#ssids');
    t.textContent = '';
    dl.textContent = '';
    nets.forEach((n) => {
      const tr = t.insertRow();
      tr.className = 'net';
      tr.insertCell().textContent = n.ssid || '(hidden)';
      tr.insertCell().textContent = n.rssi + ' dBm';
      tr.insertCell().textContent = 'ch ' + n.ch;
      tr.insertCell().textContent = n.enc ? 'secured' : 'open';
      tr.onclick = () => { $('#ssid').value = n.ssid; $('#pass').focus(); };
      const o = document.createElement('option');
      o.value = n.ssid;
      dl.appendChild(o);
    });
  }
  async function pollScan(tries) {
    let r;
    try { r = await api('/api/wifi/scan'); } catch (e) { msg('#wifimsg', 'request failed', true); return; }
    if (r.scanning && tries < 20) { setTimeout(() => pollScan(tries + 1), 1500); return; }
    renderScan(r.networks || []);
    msg('#wifimsg', (r.networks || []).length + ' networks found, click one to fill in the form');
  }
  $('#scan').addEventListener('click', async () => {
    msg('#wifimsg', 'scanning…');
    let r;
    try { r = await fetch('/api/wifi/scan', { method: 'POST' }); } catch (e) { msg('#wifimsg', 'request failed', true); return; }
    if (!r.ok) { msg('#wifimsg', 'scan busy (a connection attempt is running), try again in a moment', true); return; }
    setTimeout(() => pollScan(0), 1500);
  });
  async function saveNetwork(connect) {
    if (!$('#ssid').value) { msg('#wifimsg', 'enter a network name', true); return; }
    const body = { ssid: $('#ssid').value, pass: $('#pass').value, connect };
    if (connect) body.slot = 1;
    try {
      const j = await post('/api/wifi', body);
      if (!j.ok) { msg('#wifimsg', j.error || 'rejected', true); return; }
      msg('#wifimsg', connect ? 'saved as primary, connecting — progress is in the console log' : 'stored as backup in slot ' + j.slot);
      $('#pass').value = '';
      loadWifi();
    } catch (err) { msg('#wifimsg', 'request failed', true); }
  }
  $('#wifiform').addEventListener('submit', (e) => { e.preventDefault(); saveNetwork(true); });
  $('#addnet').addEventListener('click', () => saveNetwork(false));
  $('#setform').addEventListener('submit', async (e) => {
    e.preventDefault();
    const body = {
      mode: $('#mode').value,
      staTimeoutMs: $('#staTimeout').value * 1000,
      reconnectTimeoutMs: $('#reconnTimeout').value * 1000,
      apRetryIntervalMs: $('#apRetry').value * 1000,
    };
    try {
      const j = await post('/api/wifi/settings', body);
      msg('#setmsg', j.ok ? 'applied' : j.error || 'rejected', !j.ok);
      if (j.ok) loadWifi();
    } catch (err) { msg('#setmsg', 'request failed', true); }
  });

  // ---- ota --------------------------------------------------------------
  function waitForReboot() {
    let tries = 0;
    const t = setInterval(async () => {
      tries++;
      try {
        const r = await fetch('/api/info', { cache: 'no-store' });
        if (r.ok) { clearInterval(t); msg('#otamsg', 'device is back online'); refreshInfo(); }
      } catch (e) { /* still rebooting */ }
      if (tries > 60) clearInterval(t);
    }, 2000);
  }
  $('#otaform').addEventListener('submit', (e) => {
    e.preventDefault();
    const f = $('#fw').files[0];
    if (!f) return;
    const fd = new FormData();
    fd.append('firmware', f, f.name);
    const xhr = new XMLHttpRequest();
    const p = $('#otaprog');
    p.hidden = false;
    p.value = 0;
    msg('#otamsg', 'uploading…');
    xhr.upload.onprogress = (ev) => { if (ev.lengthComputable) p.value = Math.round((ev.loaded * 100) / ev.total); };
    xhr.onload = () => {
      let j = {};
      try { j = JSON.parse(xhr.responseText); } catch (err) { /* not json */ }
      if (xhr.status === 200 && j.ok) { msg('#otamsg', 'update written, device is rebooting…'); waitForReboot(); }
      else msg('#otamsg', 'failed: ' + (j.error || xhr.status + ' ' + xhr.statusText), true);
    };
    xhr.onerror = () => msg('#otamsg', 'upload failed', true);
    xhr.open('POST', '/update');
    xhr.send(fd);
  });

  // ---- init -------------------------------------------------------------
  const lvl = load('level', '4');
  $('#level').value = lvl;
  logEl.className = 'log min-' + lvl;
  connect();
  refreshInfo();
  cmdEl.focus();
})();
