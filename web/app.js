// Nova Editor — the browser front end.
//
// The engine lives in another process and is reached two ways:
//
//   WebSocket  /ws   every frame arrives as one binary message carrying a PNG,
//                    telemetry arrives as JSON text, and keyboard and pointer
//                    events go back as JSON
//   HTTP       /api  one-shot state: status, telemetry, the scene graph, the
//                    action map, host counters, and the current frame as a PNG
//
// The split is deliberate. A PNG in a binary frame survives proxies and
// reconnects, and the still-image endpoint means the editor still shows
// something useful when the live channel is down.

'use strict';

(function () {

  // --- DOM -----------------------------------------------------------------

  const $ = (id) => document.getElementById(id);
  const els = {
    version: $('version'),
    toolbar: $('toolbar'),
    dot: $('dot'),
    statusText: $('statusText'),
    hierarchy: $('hierarchy'),
    inspector: $('inspector'),
    canvas: $('frame'),
    overlay: $('overlay'),
    placeholder: $('placeholder'),
    paused: $('paused'),
    consoleLog: $('consoleLog'),
    consoleForm: $('consoleForm'),
    consoleInput: $('consoleInput'),
    telemetry: $('telemetry'),
    actions: $('actions'),
    hostStats: $('hostStats'),
    footerRight: $('footerRight'),
  };

  const ctx = els.canvas.getContext('2d', { alpha: false });
  const state = {
    socket: null,
    connected: false,
    frames: 0,
    droppedFrames: 0,
    lastFrameAt: 0,
    fps: 0,
    selected: null,
    scene: [],
    paused: false,
    reconnects: 0,
    command: 'play',
  };

  // --- console -------------------------------------------------------------

  function log(text, cls) {
    const line = document.createElement('div');
    line.className = 'line' + (cls ? ' ' + cls : '');
    line.textContent = text;
    els.consoleLog.appendChild(line);
    while (els.consoleLog.childElementCount > 400) {
      els.consoleLog.removeChild(els.consoleLog.firstChild);
    }
    els.consoleLog.scrollTop = els.consoleLog.scrollHeight;
  }

  // --- connection ----------------------------------------------------------

  function setStatus(kind, text) {
    els.dot.className = 'dot ' + kind;
    els.statusText.textContent = text;
  }

  function connect() {
    const url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws';
    setStatus('connecting', 'connecting…');

    let socket;
    try {
      socket = new WebSocket(url);
    } catch (err) {
      setStatus('dead', 'blocked: ' + err.message);
      return;
    }
    socket.binaryType = 'arraybuffer';
    state.socket = socket;

    socket.onopen = () => {
      state.connected = true;
      setStatus('live', 'live');
      log('connected to ' + url, 'ok');
      refreshStatus();
    };

    socket.onmessage = (event) => {
      if (typeof event.data === 'string') {
        handleText(event.data);
      } else {
        handleFrame(event.data);
      }
    };

    socket.onclose = () => {
      state.connected = false;
      state.reconnects += 1;
      setStatus('dead', 'disconnected — retrying');
      // Back off, but not much: the engine is local and comes back fast.
      setTimeout(connect, Math.min(4000, 250 * state.reconnects));
    };

    socket.onerror = () => {
      setStatus('dead', 'connection error');
    };
  }

  function send(text) {
    if (!state.socket || state.socket.readyState !== WebSocket.OPEN) {
      log('not connected; command was not sent', 'err');
      return false;
    }
    state.socket.send(text);
    return true;
  }

  // --- frames --------------------------------------------------------------

  function handleFrame(buffer) {
    const blob = new Blob([buffer], { type: 'image/png' });
    const url = URL.createObjectURL(blob);
    const img = new Image();
    img.onload = () => {
      // Only resize the canvas when the frame really is a different size, so a
      // normal frame does not clear the canvas and flicker.
      if (els.canvas.width !== img.width || els.canvas.height !== img.height) {
        els.canvas.width = img.width;
        els.canvas.height = img.height;
      }
      ctx.drawImage(img, 0, 0);
      URL.revokeObjectURL(url);

      state.frames += 1;
      els.overlay.classList.add('hidden');
      trackRate();
    };
    img.onerror = () => {
      state.droppedFrames += 1;
      URL.revokeObjectURL(url);
    };
    img.src = url;
  }

  function trackRate() {
    const now = performance.now();
    if (state.lastFrameAt === 0) {
      state.lastFrameAt = now;
      return;
    }
    const dt = now - state.lastFrameAt;
    state.lastFrameAt = now;
    if (dt > 0 && dt < 5000) {
      const instant = 1000 / dt;
      state.fps = state.fps === 0 ? instant : state.fps * 0.8 + instant * 0.2;
    }
  }

  // --- telemetry -----------------------------------------------------------

  function handleText(text) {
    let doc;
    try {
      doc = JSON.parse(text);
    } catch (err) {
      log('bad JSON from engine: ' + text.slice(0, 120), 'err');
      return;
    }
    if (doc.type === 'error') {
      log('engine: ' + doc.error, 'err');
      return;
    }
    if (doc.bodies) applyTelemetry(doc);
  }

  function applyTelemetry(t) {
    setStat(els.telemetry, 'Frame', Math.round(t.frame), t.frame > 0);
    setStat(els.telemetry, 'Sim fps', fmt(t.fps, 1));
    setStat(els.telemetry, 'Frame time', fmt(t.frameMs, 2) + ' ms', t.frameMs > 33);
    setStat(els.telemetry, 'Simulate', fmt(t.simulateMs, 2) + ' ms', t.simulateMs > 8);
    setStat(els.telemetry, 'Render', fmt(t.renderMs, 2) + ' ms', t.renderMs > 16);
    setStat(els.telemetry, 'Encode', fmt(t.encodeMs, 2) + ' ms', t.encodeMs > 16);
    setStat(els.telemetry, 'Bodies', t.bodies + ' / ' + t.activeBodies + ' awake');
    setStat(els.telemetry, 'Contacts', t.contacts);
    setStat(els.telemetry, 'Draw calls', t.drawCalls);

    if (t.paused !== state.paused) setPaused(t.paused);
    els.footerRight.textContent =
      state.fps.toFixed(1) + ' fps · ' + state.frames + ' frames · ' +
      (state.droppedFrames ? state.droppedFrames + ' undecodable' : 'no decode errors');
  }

  function setStat(list, label, value, hot) {
    let row = list.querySelector('[data-k="' + label + '"]');
    if (!row) {
      row = document.createElement('div');
      row.dataset.k = label;
      const dt = document.createElement('dt');
      dt.textContent = label;
      const dd = document.createElement('dd');
      row.append(dt, dd);
      list.appendChild(row);
    }
    const dd = row.querySelector('dd');
    dd.textContent = value;
    dd.className = hot ? 'hot' : '';
  }

  function fmt(v, digits) {
    return (typeof v === 'number' && isFinite(v) ? v.toFixed(digits) : '—');
  }

  // --- one-shot HTTP -------------------------------------------------------

  async function getJson(path) {
    try {
      const response = await fetch(path, { cache: 'no-store' });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      return await response.json();
    } catch (err) {
      return null;
    }
  }

  async function refreshStatus() {
    const status = await getJson('/api/status');
    if (status) {
      els.version.textContent = status.version || 'engine';
      setPaused(!!status.paused);
      renderActions(status.actions || []);
    }
    await refreshScene();
    await refreshHost();
  }

  async function refreshScene() {
    const doc = await getJson('/api/scene');
    if (!doc || !doc.nodes) return;
    state.scene = doc.nodes;
    renderHierarchy();
  }

  async function refreshHost() {
    const doc = await getJson('/api/host');
    if (!doc) return;
    setStat(els.hostStats, 'Frames stepped', doc.framesRendered);
    setStat(els.hostStats, 'Frames drawn', doc.framesDrawn);
    setStat(els.hostStats, 'Frames sent', doc.framesSent);
    setStat(els.hostStats, 'Frames dropped', doc.framesDropped, doc.framesDropped > 0);
    setStat(els.hostStats, 'Bytes sent', fmtBytes(doc.bytesSent));
    setStat(els.hostStats, 'Clients', doc.clients);
    setStat(els.hostStats, 'Commands', doc.commands);
    setStat(els.hostStats, 'Port', doc.port);
  }

  function fmtBytes(n) {
    if (!n) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    let i = 0;
    let v = n;
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i += 1; }
    return v.toFixed(i ? 1 : 0) + ' ' + units[i];
  }

  // --- hierarchy -----------------------------------------------------------

  function renderHierarchy() {
    const byId = new Map(state.scene.map((n) => [n.id, n]));
    els.hierarchy.textContent = '';
    for (const node of state.scene) {
      const li = document.createElement('li');
      for (let i = 0; i < node.depth; i += 1) {
        const indent = document.createElement('span');
        indent.className = 'indent';
        li.appendChild(indent);
      }
      const label = document.createElement('span');
      label.textContent = node.name;
      li.appendChild(label);

      const kind = document.createElement('span');
      kind.className = 'kind';
      kind.textContent = node.depth === 0 ? 'root' : '#' + node.id;
      li.appendChild(kind);

      if (!node.visible) li.classList.add('hidden-node');
      if (state.selected === node.id) li.classList.add('selected');
      li.addEventListener('click', () => selectNode(node.id));
      els.hierarchy.appendChild(li);
    }
    // byId is only used to validate the selection; keep the reference alive so
    // a future inspector pass can resolve parents without another pass.
    state.sceneById = byId;
  }

  function selectNode(id) {
    state.selected = id;
    renderHierarchy();
    const node = (state.sceneById || new Map()).get(id);
    if (!node) return;
    els.inspector.textContent = '';
    const rows = [
      ['Name', node.name],
      ['Id', node.id],
      ['Parent', node.parent < 0 ? '—' : node.parent],
      ['Depth', node.depth],
      ['Visible', node.visible ? 'yes' : 'no'],
      ['Active', node.active ? 'yes' : 'no'],
      ['Position', node.position.map((v) => v.toFixed(2)).join(', ')],
    ];
    for (const [label, value] of rows) {
      const row = document.createElement('div');
      row.className = 'row';
      const a = document.createElement('span');
      a.textContent = label;
      const b = document.createElement('span');
      b.textContent = String(value);
      row.append(a, b);
      els.inspector.appendChild(row);
    }
  }

  function renderActions(actions) {
    if (!actions.length) return;
    els.actions.textContent = '';
    for (const name of actions) {
      const li = document.createElement('li');
      const left = document.createElement('span');
      left.textContent = name;
      const right = document.createElement('span');
      right.textContent = '—';
      right.dataset.action = name;
      li.append(left, right);
      els.actions.appendChild(li);
    }
  }

  function markAction(name, down) {
    const cell = els.actions.querySelector('[data-action="' + name + '"]');
    if (!cell) return;
    cell.textContent = down ? 'down' : '—';
    cell.parentElement.classList.toggle('down', down);
  }

  // --- toolbar + console ---------------------------------------------------

  function setPaused(paused) {
    state.paused = paused;
    els.paused.classList.toggle('hidden', !paused);
    const play = els.toolbar.querySelector('[data-cmd="play"]');
    if (play) {
      play.textContent = paused ? 'Resume' : 'Pause';
      play.classList.toggle('running', paused);
    }
  }

  els.toolbar.addEventListener('click', (event) => {
    const button = event.target.closest('button.tool');
    if (!button) return;
    const command = button.dataset.cmd;
    if (command === 'play') {
      send(state.paused ? 'resume' : 'pause');
      return;
    }
    if (command === 'screenshot') {
      downloadFrame();
      return;
    }
    send(command);
  });

  els.consoleForm.addEventListener('submit', (event) => {
    event.preventDefault();
    const text = els.consoleInput.value.trim();
    if (!text) return;
    els.consoleInput.value = '';
    log('> ' + text, 'cmd');
    if (!send(text)) return;
    // The engine echoes nothing back on success, so confirm optimistically and
    // let a failure arrive as an error line.
    setTimeout(refreshStatus, 120);
  });

  function downloadFrame() {
    const canvas = els.canvas;
    canvas.toBlob((blob) => {
      if (!blob) return;
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'nova-frame-' + Date.now() + '.png';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      log('saved a PNG of the current frame', 'ok');
    }, 'image/png');
  }

  // --- input ---------------------------------------------------------------

  // Maps the browser's event.code onto the engine's key names. Anything that
  // does not map is simply not forwarded; the engine ignores what it does not
  // know as well.
  const KEY_NAMES = {
    KeyA: 'A', KeyB: 'B', KeyC: 'C', KeyD: 'D', KeyE: 'E', KeyF: 'F', KeyG: 'G',
    KeyH: 'H', KeyI: 'I', KeyJ: 'J', KeyK: 'K', KeyL: 'L', KeyM: 'M', KeyN: 'N',
    KeyO: 'O', KeyP: 'P', KeyQ: 'Q', KeyR: 'R', KeyS: 'S', KeyT: 'T', KeyU: 'U',
    KeyV: 'V', KeyW: 'W', KeyX: 'X', KeyY: 'Y', KeyZ: 'Z',
    ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down',
    Space: 'Space', Enter: 'Enter', ShiftLeft: 'Shift', ShiftRight: 'Shift',
    ControlLeft: 'Control', ControlRight: 'Control', AltLeft: 'Alt', AltRight: 'Alt',
    Tab: 'Tab', Backspace: 'Backspace', Escape: 'Escape',
    Digit0: 'Zero', Digit1: 'One', Digit2: 'Two', Digit3: 'Three', Digit4: 'Four',
    Digit5: 'Five', Digit6: 'Six', Digit7: 'Seven', Digit8: 'Eight', Digit9: 'Nine',
  };

  const held = new Set();

  window.addEventListener('keydown', (event) => {
    if (event.target === els.consoleInput) return;
    const name = KEY_NAMES[event.code];
    if (!name) return;
    // The browser would otherwise scroll the page out from under the editor.
    if (event.code === 'Space' || event.code.startsWith('Arrow') || event.code === 'Tab') {
      event.preventDefault();
    }
    if (event.repeat) return;
    held.add(name);
    send(JSON.stringify({ type: 'key', key: name, down: true }));
    if (name === 'Space') markAction('push', true);
    if (name === 'A' || name === 'Left') markAction('left', true);
    if (name === 'D' || name === 'Right') markAction('right', true);
  });

  window.addEventListener('keyup', (event) => {
    const name = KEY_NAMES[event.code];
    if (!name) return;
    held.delete(name);
    send(JSON.stringify({ type: 'key', key: name, down: false }));
    if (name === 'Space') markAction('push', false);
    if (name === 'A' || name === 'Left') markAction('left', false);
    if (name === 'D' || name === 'Right') markAction('right', false);
  });

  // A tab that loses focus must not leave a key stuck down in the engine.
  window.addEventListener('blur', () => {
    if (held.size === 0) return;
    const keys = Array.from(held);
    held.clear();
    send(JSON.stringify({ type: 'release-all' }));
    log('released ' + keys.length + ' held key(s) after losing focus', 'dim');
  });

  els.canvas.addEventListener('pointerdown', (event) => {
    els.canvas.setPointerCapture(event.pointerId);
    send(JSON.stringify({
      type: 'pointer', id: event.pointerId,
      x: event.offsetX, y: event.offsetY, down: true,
    }));
  });
  els.canvas.addEventListener('pointermove', (event) => {
    if (event.buttons === 0) return;
    send(JSON.stringify({
      type: 'pointer', id: event.pointerId,
      x: event.offsetX, y: event.offsetY, down: true,
    }));
  });
  els.canvas.addEventListener('pointerup', (event) => {
    send(JSON.stringify({
      type: 'pointer', id: event.pointerId,
      x: event.offsetX, y: event.offsetY, down: false,
    }));
  });
  els.canvas.addEventListener('contextmenu', (event) => event.preventDefault());

  // --- housekeeping --------------------------------------------------------

  // The scene graph only changes when something is spawned or reset, but the
  // editor still has to notice; one poll a second is cheap and honest.
  setInterval(() => {
    if (!state.connected) return;
    refreshScene();
    refreshHost();
  }, 1000);

  document.addEventListener('visibilitychange', () => {
    if (document.hidden && held.size) {
      held.clear();
      send(JSON.stringify({ type: 'release-all' }));
    }
  });

  log('nova editor — connecting to the engine on this host', 'dim');
  connect();
  refreshStatus();

})();