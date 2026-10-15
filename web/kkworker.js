// Web Worker hosting the kkrieger WebAssembly player.
//
// The engine runs here with an OffscreenCanvas (WebGL2) so that its long,
// blocking precalculation does not freeze the page; progress, input and audio
// travel by postMessage. Imports for the wasm module: math, logging, fonts
// (canvas 2D standing in for Windows GDI), WebGL (glbackend.js) and sound.

/* global KK */
'use strict';
importScripts('glbackend.js');

let wasm = null;
let memory = null;
let canvas = null;
let gl = null;
let running = false;
let t0 = 0;
let lastProgress = 0;

const mem = () => memory.buffer;
const str = (ptr, len) => {
  const b = new Uint8Array(mem(), ptr, len);
  let s = '';
  for (let i = 0; i < len; i++) s += String.fromCharCode(b[i]);
  return s;
};
const post = (m, t) => self.postMessage(m, t || []);
const now = () => Math.floor(performance.now() - t0);

/****************************************************************************/
// Fonts: GDI CreateFont(-ys, xs, ...) / ExtTextOut on a 32-bit DIB.

const FONT_METRICS = {          // Windows ascent/descent and average width, per em (OS/2 table)
  'arial': [0.905, 0.212, 0.4414],
  'times new roman': [0.891, 0.216, 0.4009],
  'verdana': [1.005, 0.210, 0.5083],
  'courier new': [0.833, 0.300, 0.6001],
};
let font = null;

function fontBegin(pagex, pagey, namePtr, nameLen, xs, ys, style, memPtr) {
  let name = str(namePtr, nameLen).toLowerCase() || 'arial';
  const m = FONT_METRICS[name] || FONT_METRICS.arial;
  const c = new OffscreenCanvas(pagex, pagey);
  const ctx = c.getContext('2d', { willReadFrequently: true });
  const css = (style & 1 ? 'italic ' : '') + (style & 2 ? 'bold ' : '') + ys + 'px "' + name + '", Arial, sans-serif';
  ctx.font = css;
  ctx.fillStyle = '#fff';
  ctx.textBaseline = 'alphabetic';
  const scaleX = xs > 0 ? xs / (m[2] * ys) : 1;
  font = { c, ctx, pagex, pagey, ys, asc: m[0] * ys, scaleX, memPtr };
  return Math.round(m[0] * ys) + Math.round(m[1] * ys);
}

function fontWidth(ptr, len) {
  return Math.round(font.ctx.measureText(str(ptr, len)).width * font.scaleX);
}

function fontCharWidth(ch, wptr) {
  const t = font.ctx.measureText(String.fromCharCode(ch & 255));
  const sx = font.scaleX;
  const a = Math.floor(-t.actualBoundingBoxLeft * sx);
  const b = Math.ceil(t.actualBoundingBoxRight * sx) - a;
  const w = new Int32Array(mem(), wptr, 3);
  w[0] = a; w[1] = Math.max(b, 0); w[2] = Math.round(t.width * sx) - a - Math.max(b, 0);
}

function fontPrint(x, y, ptr, len) {
  const ctx = font.ctx;
  ctx.setTransform(font.scaleX, 0, 0, 1, 0, 0);
  ctx.fillText(str(ptr, len), x / font.scaleX, y + font.asc);
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  // GDI drew without antialiasing; the Text operator supersamples itself
  const img = ctx.getImageData(0, 0, font.pagex, font.pagey).data;
  const out = new Uint32Array(mem(), font.memPtr, font.pagex * font.pagey);
  for (let i = 0, n = out.length; i < n; i++) {
    const v = img[i * 4 + 3] >= 128 ? 255 : 0;
    out[i] = v | (v << 8) | (v << 16);
  }
}

/****************************************************************************/
// Sound. The music (and the intro's sound handler) is rendered here by the V2
// synthesizer ahead of what the page has played; sound effects are sent to the
// page once and then started by handle. Nothing is rendered until the page
// reports that its audio output runs.

const sampleUses = [];              // per handle: voices started so far (the game's voice ids)
let soundPos = 0;                   // frames played by the page
let soundWritten = 0;               // frames rendered and sent
let soundOn = false;
let soundBuf = 0;                   // wasm buffer for one chunk (16-bit stereo)
const SOUND_CHUNK = 1024, SOUND_LEAD = 6144;
let listener = null;
const sound3d = [];

function pumpSound() {
  if (!soundOn || !wasm) return;
  if (!soundBuf) soundBuf = wasm.kk_alloc(SOUND_CHUNK * 4);
  while (soundWritten - soundPos < SOUND_LEAD) {
    wasm.kk_render_sound(soundBuf, SOUND_CHUNK);
    const s16 = new Int16Array(mem(), soundBuf, SOUND_CHUNK * 2);
    const f = new Float32Array(SOUND_CHUNK * 2);
    for (let i = 0; i < f.length; i++) f[i] = s16[i] / 32768;
    post({ type: 'audio', data: f }, [f.buffer]);
    soundWritten += SOUND_CHUNK;
  }
  if (listener) { post({ type: 'listener', v: listener }); listener = null; }
  if (sound3d.length) post({ type: 'sound3d', list: sound3d.splice(0) });
}

/****************************************************************************/

function imports() {
  const env = {
    js_log: (p, n) => post({ type: 'log', text: str(p, n) }),
    js_abort: (p, n) => { throw new Error(str(p, n)); },
    js_exit: () => post({ type: 'exit' }),
    js_progress: (done, max) => {
      const t = performance.now();
      if (t - lastProgress > 100 || done >= max) {
        lastProgress = t;
        post({ type: 'progress', done, max });
      }
    },
    js_time_of_year: () => {
      const d = new Date();
      const y0 = Date.UTC(d.getUTCFullYear(), 0, 1), y1 = Date.UTC(d.getUTCFullYear() + 1, 0, 1);
      return (d.getTime() - y0) / (y1 - y0);
    },
    js_font_begin: fontBegin,
    js_font_width: fontWidth,
    js_font_charwidth: fontCharWidth,
    js_font_print: fontPrint,
    js_font_end: () => { font = null; },
    js_sound_position: () => soundPos,
    js_sample_add: (h, ptr, n, is3d) => {
      sampleUses[h] = 0;
      post({ type: 'sample', handle: h, data: new Int16Array(new Int16Array(mem(), ptr, n * 2)), is3d });
    },
    js_sample_play: (h, vol, pan, freq) => {
      const voice = ++sampleUses[h];
      if (soundOn) post({ type: 'play', handle: h, voice, vol, pan });
      return voice;
    },
    js_sample_3d: (h, voice, x, y, z, mn, mx) => { if (soundOn) sound3d.push([h, voice, x, y, z, mn, mx]); },
    js_listener: (x, y, z, fx, fy, fz, ux, uy, uz) => { listener = [x, y, z, fx, fy, fz, ux, uy, uz]; },
  };
  for (const k of Object.keys(KK)) if (k.startsWith('gl_')) env[k] = KK[k];
  const math = {};
  for (const f of ['sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'atan2', 'exp', 'log', 'log10', 'pow', 'sinh', 'cosh', 'tanh'])
    math[f] = Math[f];
  math.fmod = (a, b) => a % b;
  return { env, math };
}

function frame() {
  if (!running) return;
  try {
    if (self.DBG) self.DBG.frameStart();
    wasm.kk_frame(now());
    if (self.DBG) self.DBG.frameEnd();
    pumpSound();
  } catch (e) {
    running = false;
    post({ type: 'error', text: String(e && e.stack || e) });
    return;
  }
  self.requestAnimationFrame(frame);
}

async function start(msg) {
  canvas = msg.canvas;
  if (msg.debug) importScripts('debug.js');
  gl = canvas.getContext('webgl2', { antialias: false, depth: true, stencil: true, alpha: false, preserveDrawingBuffer: false });
  if (!gl) throw new Error('WebGL2 is not available');
  const bytes = await (await fetch('kkrieger.wasm')).arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes, imports());
  wasm = instance.exports;
  memory = wasm.memory;
  KK.init(gl, mem);
  wasm.__wasm_call_ctors();
  t0 = performance.now();

  const data = new Uint8Array(msg.exe || msg.kx);
  const p = wasm.kk_alloc(data.length);
  new Uint8Array(mem(), p, data.length).set(data);
  if (msg.kx) {
    if (wasm.kk_load_kx(p, data.length, canvas.width, canvas.height)) throw new Error('could not read the .kx data');
  } else {
    const err = wasm.kk_load(p, data.length, canvas.width, canvas.height);
    if (err) throw new Error(err === 1 ? 'not a kkrunchy-packed kkrieger executable' : 'could not read the game data in the executable');
  }
  wasm.kk_init(now());
  const unknown = KK.unknownOps();
  if (unknown.length) post({ type: 'log', text: 'untranslated shader ops: ' + unknown.join(' ') });
  post({ type: 'ready', heap: wasm.kk_heap_peak() });
  running = true;
  self.requestAnimationFrame(frame);
}

self.onmessage = (ev) => {
  const m = ev.data;
  switch (m.type) {
    case 'start':
      start(m).catch((e) => post({ type: 'error', text: String(e && e.stack || e) }));
      break;
    case 'key':
      if (wasm && running) wasm.kk_key(m.code >>> 0);
      break;
    case 'mouse':
      if (wasm && running) wasm.kk_mouse(m.dx | 0, m.dy | 0, 0, m.buttons >>> 0);
      break;
    case 'resize':
      if (canvas) { canvas.width = m.w; canvas.height = m.h; }
      if (wasm) wasm.kk_resize(m.w, m.h);
      break;
    case 'eval':                      // diagnostics, only with ?debug
      if (self.DBG) try { post({ type: 'log', text: 'eval: ' + String((0, eval)(m.code)) }); } catch (e) { post({ type: 'log', text: 'eval error: ' + e }); }
      break;
    case 'soundpos':
      soundPos = m.pos;
      break;
    case 'sound':                     // the page's audio output started or stopped
      soundOn = !!m.on;
      if (soundOn) soundWritten = soundPos = 0;
      break;
  }
};
