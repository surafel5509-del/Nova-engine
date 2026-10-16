// A stand-in for the browser.
//
// The editor front end is three static files and a WebSocket; this drives the
// same contract from Node so the whole path — HTTP, the upgrade, a PNG frame,
// telemetry, commands, and key events — is exercised by something other than
// the engine's own tests. It is what "the editor works" means in a build.
//
//   node tools/browser-check.js [base-url]

'use strict';

const http = require('http');
const crypto = require('crypto');

const BASE = process.argv[2] || 'http://127.0.0.1:8090';
const HOST = BASE.replace(/^https?:\/\//, '');

let failures = 0;
function check(ok, what, detail) {
  const mark = ok ? '  ok  ' : ' FAIL ';
  if (!ok) failures += 1;
  console.log(mark + what + (detail && !ok ? '  — ' + detail : ''));
}

function get(path) {
  return new Promise((resolve, reject) => {
    const req = http.get(BASE + path, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => resolve({
        status: res.statusCode,
        headers: res.headers,
        body: Buffer.concat(chunks),
      }));
    });
    req.on('error', reject);
    req.setTimeout(8000, () => req.destroy(new Error('timeout')));
  });
}

function post(path, body) {
  return new Promise((resolve, reject) => {
    const req = http.request(BASE + path, {
      method: 'POST',
      headers: { 'Content-Length': Buffer.byteLength(body), 'Connection': 'close' },
    }, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => resolve({
        status: res.statusCode,
        body: Buffer.concat(chunks).toString('utf8'),
      }));
    });
    req.on('error', reject);
    req.end(body);
  });
}

function waitFor(fn, seconds, label) {
  const deadline = Date.now() + seconds * 1000;
  return (function poll() {
    if (fn()) return Promise.resolve(true);
    if (Date.now() > deadline) {
      console.log('  ....  timed out waiting for ' + label);
      return Promise.resolve(false);
    }
    return new Promise((r) => setTimeout(() => r(poll()), 50));
  })();
}

// --- a minimal RFC 6455 client --------------------------------------------

function encodeFrame(text, binary) {
  const payload = Buffer.from(text);
  const opcode = binary ? 2 : 1;
  let head;
  if (payload.length < 126) {
    head = Buffer.from([0x80 | opcode, 0x80 | payload.length]);
  } else if (payload.length < 65536) {
    head = Buffer.alloc(4);
    head[0] = 0x80 | opcode;
    head[1] = 0x80 | 126;
    head.writeUInt16BE(payload.length, 2);
  } else {
    head = Buffer.alloc(10);
    head[0] = 0x80 | opcode;
    head[1] = 0x80 | 127;
    head.writeBigUInt64BE(BigInt(payload.length), 2);
  }
  const mask = crypto.randomBytes(4);
  const masked = Buffer.alloc(payload.length);
  for (let i = 0; i < payload.length; i += 1) masked[i] = payload[i] ^ mask[i & 3];
  return Buffer.concat([head, mask, masked]);
}

/// Pulls frames out of a growing buffer. Server frames are never masked.
function makeParser(onFrame) {
  let buf = Buffer.alloc(0);
  return {
    push(chunk) {
      buf = Buffer.concat([buf, chunk]);
      for (;;) {
        if (buf.length < 2) return;
        const opcode = buf[0] & 0x0f;
        let length = buf[1] & 0x7f;
        let offset = 2;
        if (length === 126) {
          if (buf.length < 4) return;
          length = buf.readUInt16BE(2);
          offset = 4;
        } else if (length === 127) {
          if (buf.length < 10) return;
          length = Number(buf.readBigUInt64BE(2));
          offset = 10;
        }
        if (buf.length < offset + length) return;
        const payload = buf.slice(offset, offset + length);
        buf = buf.slice(offset + length);
        onFrame(opcode, payload);
      }
    },
  };
}

function readPngSize(png) {
  // 8-byte signature, then an IHDR chunk whose data starts at byte 16.
  if (png.length < 24) return null;
  if (png.readUInt32BE(0) !== 0x89504e47) return null;
  return { width: png.readUInt32BE(16), height: png.readUInt32BE(20) };
}

// --- the run ---------------------------------------------------------------

async function main() {
  console.log('nova editor browser check against ' + BASE + '\n');

  // 1. the static front end
  const page = await get('/');
  check(page.status === 200, 'GET / serves the editor', 'status ' + page.status);
  check(/text\/html/.test(page.headers['content-type'] || ''), '/ is text/html');
  const html = page.body.toString('utf8');
  check(html.includes('id="frame"'), 'the page has a canvas to draw frames into');
  check(html.includes('app.js'), 'the page loads the front end script');

  const css = await get('/style.css');
  check(css.status === 200 && /text\/css/.test(css.headers['content-type'] || ''),
        'the stylesheet is served with the right type');
  const js = await get('/app.js');
  check(js.status === 200 && js.body.length > 1000, 'the front end script is served');

  // 2. the REST surface
  const status = await get('/api/status');
  check(status.status === 200, 'GET /api/status answers');
  const described = JSON.parse(status.body.toString('utf8'));
  check(described.engine === 'nova', 'status names the engine', JSON.stringify(described).slice(0, 80));
  check(typeof described.width === 'number' && described.width > 0, 'status reports a framebuffer size');
  check(Array.isArray(described.actions) && described.actions.length > 0, 'status lists the action map');

  const telemetry = await get('/api/telemetry');
  check(telemetry.status === 200, 'GET /api/telemetry answers');
  const tele = JSON.parse(telemetry.body.toString('utf8'));
  check(Array.isArray(tele.bodies), 'telemetry carries the body list the editor draws');

  const scene = await get('/api/scene');
  check(scene.status === 200, 'GET /api/scene answers');
  const sceneDoc = JSON.parse(scene.body.toString('utf8'));
  check(Array.isArray(sceneDoc.nodes) && sceneDoc.nodes.length > 0,
        'the scene graph has nodes in it');

  // 3. the upgrade
  const key = crypto.randomBytes(16).toString('base64');
  const socket = new (require('net').Socket)();
  socket.connect(Number(HOST.split(':')[1]), HOST.split(':')[0]);
  await new Promise((resolve, reject) => {
    socket.once('connect', resolve);
    socket.once('error', reject);
  });

  let binaryFrames = 0;
  let textFrames = 0;
  let lastPng = null;
  let hello = null;
  let lastTelemetry = null;
  const parser = makeParser((opcode, payload) => {
    if (opcode === 2) {
      binaryFrames += 1;
      lastPng = payload;
    } else if (opcode === 1) {
      textFrames += 1;
      try {
        const doc = JSON.parse(payload.toString('utf8'));
        if (doc.type === 'hello') hello = doc;
        else if (doc.bodies) lastTelemetry = doc;
      } catch (err) { /* the editor logs these; so do we */ }
    }
  });

  // The upgrade response and the first websocket frames can land in the same
  // TCP segment. Feeding the HTTP headers to the frame parser desynchronises it
  // for the rest of the session -- every later frame is garbage and every
  // command appears to be ignored -- so the handshake is separated out first
  // and only the bytes after the blank line are parsed.
  const early = [];
  let handshake = '';
  let upgrading = true;
  const onEarlyData = (chunk) => {
    early.push(chunk);
    const buf = Buffer.concat(early);
    const end = buf.indexOf('\r\n\r\n');
    if (upgrading && end < 0) return;
    if (upgrading) {
      handshake = buf.slice(0, end).toString('latin1');
      upgrading = false;
      socket.removeListener('data', onEarlyData);
      socket.on('data', (rest) => parser.push(rest));
      parser.push(buf.slice(end + 4));
    } else {
      parser.push(chunk);
    }
  };
  socket.on('data', onEarlyData);

  socket.write(
    'GET /ws HTTP/1.1\r\n' +
    'Host: ' + HOST + '\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    'Sec-WebSocket-Key: ' + key + '\r\n' +
    'Sec-WebSocket-Version: 13\r\n\r\n');

  await new Promise((resolve) => {
    const tick = setInterval(() => {
      if (!upgrading) { clearInterval(tick); resolve(); }
    }, 20);
    setTimeout(() => { clearInterval(tick); resolve(); }, 5000);
  });

  const expectedAccept = crypto.createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64');
  check(handshake.includes(expectedAccept), 'Sec-WebSocket-Accept is the RFC 6455 value');

  await waitFor(() => hello !== null, 3, 'the hello frame');
  check(hello !== null && hello.engine === 'nova', 'the server greets the client on connect');

  // 4. frames
  await waitFor(() => binaryFrames > 0, 10, 'the first frame');
  check(binaryFrames > 0, 'frames arrive as binary websocket messages');
  check(lastPng !== null && lastPng.length > 100, 'a frame carries real bytes',
        lastPng ? lastPng.length + ' bytes' : 'nothing');
  const size = lastPng ? readPngSize(lastPng) : null;
  check(size !== null, 'a frame is a PNG');
  if (size) {
    check(size.width === described.width && size.height === described.height,
          'the frame matches the configured framebuffer size',
          size.width + 'x' + size.height + ' vs ' + described.width + 'x' + described.height);
  }
  check(textFrames > 0, 'telemetry arrives as text alongside the frames');

  // The still-image endpoint must serve the same picture for the editor's
  // thumbnail and its "download this frame" button.
  const still = await get('/api/frame.png');
  check(still.status === 200, 'GET /api/frame.png answers once a frame exists');
  check((still.headers['content-type'] || '') === 'image/png', 'the still is image/png');
  check(readPngSize(still.body) !== null, 'the still is a PNG too');

  // 5. commands over the websocket
  socket.write(encodeFrame('pause'));
  await waitFor(() => lastTelemetry && lastTelemetry.paused === true, 4, 'the pause to land');
  check(lastTelemetry !== null && lastTelemetry.paused === true,
        'a text command pauses the running game');
  const frameWhilePaused = binaryFrames;
  await new Promise((r) => setTimeout(r, 600));
  check(binaryFrames === frameWhilePaused, 'a paused game stops producing frames');
  socket.write(encodeFrame('resume'));
  await waitFor(() => lastTelemetry && lastTelemetry.paused === false, 4, 'the resume to land');
  check(lastTelemetry !== null && lastTelemetry.paused === false, 'resume starts it again');

  const bad = await post('/api/command', 'nonsense');
  check(bad.status === 400, 'an unknown command over HTTP is a 400');
  check(JSON.parse(bad.body).ok === false, 'the 400 body says so');

  // 6. input over the websocket
  const pusherX = () => {
    const doc = JSON.parse(JSON.stringify(lastTelemetry || {}));
    const ball = (doc.bodies || []).find((b) => b.tag === 'Pusher');
    return ball ? ball.x : 0;
  };
  const before = pusherX();
  socket.write(encodeFrame(JSON.stringify({ type: 'key', key: 'Right', down: true })));
  await new Promise((r) => setTimeout(r, 700));
  socket.write(encodeFrame(JSON.stringify({ type: 'key', key: 'Right', down: false })));
  await new Promise((r) => setTimeout(r, 300));
  const after = pusherX();
  check(after > before + 0.05, 'holding a key moves the player character',
        'x went ' + before.toFixed(3) + ' -> ' + after.toFixed(3));

  // A released key must not stay held.
  const parked = after;
  await new Promise((r) => setTimeout(r, 700));
  check(Math.abs(pusherX() - parked) < 0.5, 'releasing the key stops it again',
        'drifted ' + (pusherX() - parked).toFixed(3));

  socket.end();

  console.log('\n' + (failures === 0
    ? 'browser check passed'
    : failures + ' check(s) failed'));
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error('\nbrowser check crashed: ' + err.message);
  process.exit(2);
});