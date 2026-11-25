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
  function showTab(name) {
    const b = document.querySelector('.tab[data-panel="' + name + '"]');
    if (!b || b.hidden) return;
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t === b));
    document.querySelectorAll('.panel').forEach((p) => p.classList.toggle('active', p.id === name));
    if (name === 'info') refreshInfo();
    if (name === 'wifi') loadWifi();
    if (name === 'settings') loadSettings();
    if (name === 'device') loadStatus();
    if (name === 'console') cmdEl.focus();
  }
  document.querySelectorAll('.tab[data-panel]').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.panel)));

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
    else if (m.t === 'out') {
      const text = (m.m || '').replace(/\r/g, '').replace(/\n$/, '');
      push({ l: 'O', m: text });
      // The answer to a Device button goes under the buttons too.
      if (Date.now() - buttonAt < 5000) { buttonAt = 0; msg('#devicemsg', text || 'done'); loadStatus(); }
    }
  }

  // ---- command input with history -------------------------------------
  // Commands carrying passwords stay in this session's history but never reach localStorage.
  // So do "config set" commands for the project's secret settings.
  const SECRET = /^\s*(wifi\s+(set|add)|config\s+set\s+\S*pass)\b/i;
  const isSecretCmd = (c) => SECRET.test(c) || secretKeys.some((k) => new RegExp('^\\s*config\\s+set\\s+' + k + '\\b', 'i').test(c));
  // Nothing is stored until /api/settings has named the secret keys (secretKeys is null till then).
  const keepHist = () => { if (secretKeys) save('hist', JSON.stringify(hist.filter((h) => !isSecretCmd(h)))); };
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
    keepHist();
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
  let lastInfo = null;
  async function refreshInfo() {
    let info;
    try { info = await (await fetch('/api/info', { cache: 'no-store' })).json(); } catch (e) { return null; }
    lastInfo = info;
    document.querySelector('.tab[data-panel="update"]').hidden = !info.ota;
    const app = $('#applink');  // the project's own page; the device only names paths under /app/
    app.hidden = !info.app;
    if (info.app) { app.href = info.app.path; app.textContent = info.app.title; }
    $('#host').textContent = info.hostname;
    document.title = info.hostname + ' console';
    $('#s-wifi').textContent = info.wifi.state.replace(/_/g, ' ') + (info.wifi.ap ? ' + AP' : '');
    $('#s-ip').textContent = info.wifi.ip || (info.wifi.ap ? info.wifi.ap.ip : '–');
    $('#s-rssi').textContent = info.wifi.rssi ? info.wifi.rssi + ' dBm' : '–';
    $('#s-heap').textContent = fmtBytes(info.heap.free) + ' free';
    $('#s-uptime').textContent = fmtUp(info.uptime);
    $('#infobox').textContent = JSON.stringify(info, null, 2);
    return info;
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
    // Over the device's own access point this page is about to go: settings typed and not saved would go with it.
    if (connect && viaClosingAp() && settingsDirty()) { msg('#wifimsg', 'Settings has unsaved changes. Save them first.', true); return; }
    if (connect) body.slot = 1;
    try {
      const j = await post('/api/wifi', body);
      if (!j.ok) { msg('#wifimsg', j.error || 'rejected', true); return; }
      // Over the device's own access point the page is about to lose it: say where the device will be, and what a wrong password looks like.
      msg('#wifimsg', !connect ? 'stored as backup in slot ' + j.slot
        : viaClosingAp() ? 'Joining ' + body.ssid + '. When it connects, the ' + lastInfo.wifi.ap.ssid + ' network disappears: put your phone back on ' + body.ssid
          + ' and open http://' + lastInfo.hostname + '.local/. If this page is still here after a minute, check the password and save again.'
        : 'saved as primary, connecting — progress is in the console log');
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

  // ---- settings ----------------------------------------------------------
  // The project's own settings (/api/settings). Labels, hints and values go in
  // as text, never as markup. A secret's value never reaches the page: its
  // field is empty and only says whether one is saved.
  let shown = {};       // key -> the value its field was given, so only changes are posted
  let secretKeys = null;  // for the console's history, which must not keep them; null until the device names them
  let saving = false;
  const settingsFields = () => [...$('#settingsform').querySelectorAll('[name]')];
  const settingsDirty = () => settingsFields().some((f) => f.value !== shown[f.name]);
  // True while the page is served over the device's own access point and that
  // access point closes as soon as the station connects (mode "sta").
  const viaClosingAp = () => !!(lastInfo && lastInfo.wifi.ap && lastInfo.wifi.mode === 'sta');
  async function loadSettings() {
    let r;
    try { r = await api('/api/settings'); } catch (e) { return; }
    const list = r.settings || [];
    document.querySelector('.tab[data-panel="settings"]').hidden = !list.length;
    if (r.settings) {  // an error answer names no keys: keep not storing
      secretKeys = list.filter((s) => s.kind === 'secret').map((s) => s.key);
      keepHist();  // also scrubs what an earlier page stored before it knew the keys
    }
    if (settingsDirty()) return;  // never write over what was typed and not saved
    const form = $('#settingsform');
    const row = form.querySelector('.row');
    // The fields are built once and then updated in place, so the cursor (and
    // a phone's keyboard) stays where it is when the page reads again.
    if (settingsFields().map((f) => f.name).join() !== list.map((s) => s.key).join()) {
      form.querySelectorAll('label').forEach((l) => l.remove());
      list.forEach((s) => {
        const label = document.createElement('label');
        label.textContent = s.label;
        let f;
        if (s.kind === 'choice') {
          f = document.createElement('select');
          (s.choices || []).forEach((c) => {
            const o = document.createElement('option');
            o.value = c;
            o.textContent = c;
            f.appendChild(o);
          });
        } else {
          f = document.createElement('input');
          f.maxLength = s.maxLen;
          if (s.kind === 'secret') {
            f.type = 'password';
            f.autocomplete = 'off';
          }
        }
        f.name = s.key;
        label.appendChild(f);
        if (s.hint) {
          const h = document.createElement('span');
          h.className = 'hint';
          h.textContent = s.hint;
          label.appendChild(h);
        }
        form.insertBefore(label, row);
      });
    }
    list.forEach((s) => {
      const f = form.querySelector('[name="' + s.key + '"]');
      if (s.kind === 'secret') {
        f.value = '';
        f.placeholder = s.set ? 'saved, type to replace' : 'not set yet';
      } else {
        f.value = s.value;
      }
      shown[s.key] = f.value;
    });
  }
  $('#settingsform').addEventListener('input', () => msg('#settingsmsg', ''));
  $('#settingsform').addEventListener('submit', async (e) => {
    e.preventDefault();
    if (saving) return;
    const fields = settingsFields().filter((f) => f.value !== shown[f.name]);
    if (!fields.length) { msg('#settingsmsg', 'Nothing to save.'); return; }
    const body = {};
    fields.forEach((f) => { body[f.name] = f.value; });
    const btn = $('#settingsform button[type="submit"]');
    saving = true;
    btn.disabled = true;
    msg('#settingsmsg', 'Saving\u2026');
    try {
      const j = await post('/api/settings', body);
      if (!j.ok) {
        // The device answers "<key>: <reason>": say the field's label and go to it.
        const err = j.error || 'rejected';
        const f = settingsFields().find((x) => err.startsWith(x.name + ': '));
        msg('#settingsmsg', f ? f.parentNode.firstChild.textContent + err.slice(f.name.length) : err, true);
        if (f) f.focus();
        return;
      }
      fields.forEach((f) => {
        if (f.type === 'password') { f.placeholder = 'saved, type to replace'; f.value = ''; }
        shown[f.name] = f.value;
      });
      msg('#settingsmsg', viaClosingAp() ? 'Saved. Next: open the WiFi tab and add your home network.' : 'Saved.');
      // The device applies a change a moment later; then show what it holds
      // (a cleared field falls back to its default).
      setTimeout(loadSettings, 700);
    } catch (err) {
      msg('#settingsmsg', 'Could not reach the device. Try again.', true);
    } finally {
      saving = false;
      btn.disabled = false;
    }
  });

  // ---- device -----------------------------------------------------------
  // The project's values and buttons (/api/status), as text, never markup.
  // Rows and buttons are built once and then only the values change, so a
  // button keeps its focus while the tab refreshes.
  let statusShape = '';
  let buttonAt = 0;  // when a Device button sent its command, so its answer shows here
  async function loadStatus() {
    let r;
    try { r = await api('/api/status'); } catch (e) { return; }
    const values = r.values || [];
    const buttons = r.buttons || [];
    document.querySelector('.tab[data-panel="device"]').hidden = !values.length && !buttons.length;
    const t = $('#values');
    const shape = JSON.stringify([values.map((v) => [v.key, v.label]), buttons]);
    if (shape !== statusShape) {
      statusShape = shape;
      t.textContent = '';
      values.forEach((v) => { const tr = t.insertRow(); tr.insertCell().textContent = v.label; tr.insertCell(); });
      $('#buttons').replaceChildren(...buttons.map((b) => button(b.label, () => {
        if (!ws || ws.readyState !== 1) { msg('#devicemsg', 'Not connected to the device. Try again in a moment.', true); return; }
        buttonAt = Date.now();
        msg('#devicemsg', 'running ' + b.cmd + '…');
        send(b.cmd);
      })));
    }
    values.forEach((v, i) => { t.rows[i].cells[1].textContent = v.value || '–'; });
  }
  setInterval(() => { if ($('#device').classList.contains('active')) loadStatus(); }, 2000);

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
  cmdEl.focus();
  // While the device is its own access point, the project may name the tab to open on.
  Promise.all([loadSettings(), loadStatus()]).then(refreshInfo).then((info) => { if (info && info.landing) showTab(info.landing); });
})();
