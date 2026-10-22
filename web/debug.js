// Rendering diagnostics for the worker, loaded only with ?debug. Use from the
// page console: worker.postMessage({ type: 'eval', code: '...' }) with the
// helpers below (DBG.record(), DBG.mode(fn), DBG.show(rt)), results go to the log.

/* global KK, post */
'use strict';

const DBG = (() => {
  let recording = null, frameDraws = null;
  let modeFn = null;                         // (draw) => false (skip) | 0 normal | 1 red | 2..5 stage samples | 6 r0
  let showRT = null, showLast = false;
  let snap = null;

  KK.onDraw = (d) => {
    if (frameDraws) frameDraws.push(d);
    return modeFn ? modeFn(d) : 0;
  };

  const vp0 = KK.gl_viewport, clr0 = KK.gl_clear;
  KK.gl_viewport = function (tgt, txs, tys, x, y, w, h) {
    const I = KK.internals();
    if (DBG.onViewport) DBG.onViewport(tgt, I.target);
    if (showRT != null && I.target === showRT && tgt !== I.target && (!snap.done || showLast)) copy(I, showRT);
    if (frameDraws) frameDraws.push('vp ' + tgt + ' ' + w + 'x' + h);
    return vp0(tgt, txs, tys, x, y, w, h);
  };
  KK.gl_clear = function (flags, color) {
    if (frameDraws) frameDraws.push('clear ' + flags + ' ' + (color >>> 0).toString(16));
    return clr0(flags, color);
  };

  function copy(I, h) {
    const gl = I.gl, t = I.textures[h];
    if (!t) return;
    if (!snap.tex || snap.w !== t.w || snap.h !== t.h) {
      snap.tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, snap.tex);
      gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, t.w, t.h);
      snap.fbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, snap.fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, snap.tex, 0);
      snap.w = t.w; snap.h = t.h;
    }
    const src = gl.createFramebuffer();
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, src);
    gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t.tex, 0);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, snap.fbo);
    gl.disable(gl.SCISSOR_TEST);
    gl.blitFramebuffer(0, 0, t.w, t.h, 0, 0, t.w, t.h, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.deleteFramebuffer(src);
    snap.done = true;
  }

  const fmt = (d) => typeof d === 'string' ? d :
    `draw s${d.setup} g${d.geo} fvf${d.fvf} v${d.vcount} i${d.icount} t${d.target} tex ${d.tex} z ${d.z} bl ${d.blend} cull ${d.cull} st ${d.stencil} ps0 ${Array.from(d.ps.slice(0, 4)).map((v) => v.toFixed(2))}`;

  return {
    // called by the worker around each frame
    // frame times for n frames: game frame (kk_frame) and the time to the next frame
    perf(n = 300) { DBG._perf = { n, game: [], gap: [], t0: 0, t1: 0 }; return 'measuring'; },
    frameStart() {
      const P = DBG._perf;
      if (P) { P.t0 = performance.now(); if (P.t1) P.gap.push(P.t0 - P.t1); }
      if (snap) snap.done = false; if (recording !== null && recording-- === 0) { frameDraws = []; recording = null; } },
    frameEnd() {
      const P = DBG._perf;
      if (P) {
        P.t1 = performance.now(); P.game.push(P.t1 - P.t0);
        if (P.game.length >= P.n) {
          const st = (a) => { const s = a.slice().sort((x, y) => x - y); return `mean ${(a.reduce((x, y) => x + y, 0) / a.length).toFixed(1)} p50 ${s[s.length >> 1].toFixed(1)} p95 ${s[(s.length * 0.95) | 0].toFixed(1)} max ${s[s.length - 1].toFixed(1)}`; };
          post({ type: 'log', text: `perf: game ${st(P.game)} | gap ${st(P.gap)} ms` });
          DBG._perf = null;
        }
      }
      if (DBG._lightFrame) DBG._lightFrame();
      if (frameDraws) { post({ type: 'log', text: 'frame:\n' + frameDraws.map(fmt).join('\n') }); frameDraws = null; }
      if (showRT != null && snap && snap.tex) {
        const gl = KK.internals().gl;
        gl.disable(gl.SCISSOR_TEST);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, snap.fbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, snap.w, snap.h, 0, gl.drawingBufferHeight, gl.drawingBufferWidth, 0, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      }
    },
    record(afterFrames = 1) { recording = afterFrames; return 'recording'; },
    // active lights of every Paint call for n frames: position, amplify, range, paint index
    lights(n = 30) {
      const out = [];
      let frames = 0;
      const buf = wasm.kk_alloc(64 * 6 * 4);
      DBG._lightFrame = () => {
        const cnt = wasm.kk_light_info(buf);
        const f = new Float32Array(mem(), buf, 64 * 6);
        const ls = [];
        for (let i = 0; i < cnt; i++) ls.push('(' + [0, 1, 2].map((k) => f[i * 6 + k].toFixed(1)).join(',') + ' a' + f[i * 6 + 3].toFixed(2) + ' r' + f[i * 6 + 4].toFixed(0) + ' p' + f[i * 6 + 5] + ')');
        out.push(cnt + ': ' + ls.join(' '));
        if (++frames >= n) { DBG._lightFrame = null; post({ type: 'log', text: 'lights per frame:\n' + out.join('\n') }); }
      };
      return 'collecting';
    },
    mode(fn) { modeFn = fn; return 'mode set'; },
    // keep the player's life at 100 (on) or stop doing so (off)
    god(on = true) { clearInterval(DBG._god); DBG._god = on ? setInterval(() => wasm.kk_debug_life(100), 30) : 0; return 'god ' + on; },
    // where the player is: the five body particles, monsters and moving cells
    // within r, and whether any particle is outside the level or inside a solid
    where(r = 4) {
      const parts = [0, 1, 2, 3, 4].map((k) => 'p' + k + ' ' + DBG.part(0, k).replace(/ kind.*/, ''));
      const near = DBG.coll(256).split('\n');
      const p = near[0].match(/(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)/).slice(1).map(Number);
      const close = near.slice(1).filter((l) => l.startsWith('cell') && (() => { const m = l.match(/at (-?[\d.]+),(-?[\d.]+),(-?[\d.]+)/); return m && Math.hypot(m[1] - p[0], m[3] - p[2]) < r; })());
      const b = wasm.kk_alloc(24); wasm.kk_debug_check(b); const f = new Float32Array(mem(), b, 6);
      const chk = 'outside ' + f[4] + ' inside ' + f[5]; wasm.kk_free(b);
      return [near[0], ...parts, chk, ...close].join('\n');
    },
    // kill every monster now in play (collision tests without monster bodies)
    killall() { return 'killed ' + wasm.kk_debug_killall(); },
    show(rt, last = false) { showRT = rt; showLast = last; snap = snap || { done: false }; return 'showing ' + rt; },
    // player position, moving cells (mode centre blocked monster) and particle groups (pos kind shot)
    coll(max = 64) {
      const buf = wasm.kk_alloc((3 + max * 10) * 4);
      const n = wasm.kk_debug_coll(buf, max);
      const f = new Float32Array(mem(), buf, 3 + max * 10);
      const r = (k) => f[k].toFixed(2);
      const out = ['player ' + [0, 1, 2].map(r).join(',')];
      for (let i = 0; i < n; i++) {
        const b = 3 + i * 10;
        out.push(f[b] < 0 ? `part ${r(b + 1)},${r(b + 2)},${r(b + 3)} kind ${f[b + 4]} shot ${f[b + 5]} cell ${f[b + 9]}`
                           : `cell mode ${f[b]} at ${r(b + 1)},${r(b + 2)},${r(b + 3)} blocked ${f[b + 4]} monster ${f[b + 5]}` + (f[b + 5] ? ` life ${f[b + 6]} awake ${f[b + 7]}` : ''));
      }
      wasm.kk_free(buf);
      return out.join('\n');
    },
    // static solid cells whose box comes within r of the player: index, min, max, planes
    subs(r = 6) {
      const max = 4096, buf = wasm.kk_alloc(max * 8 * 4);
      const n = wasm.kk_debug_subs(buf, max);
      const f = new Float32Array(mem(), buf, n * 8);
      const pb = wasm.kk_alloc(3 * 4); wasm.kk_debug_coll(pb, 0);
      const p = new Float32Array(mem(), pb, 3);
      const out = [];
      for (let i = 0; i < n; i++) {
        const b = i * 8, d = [0, 1, 2].map((k) => Math.max(f[b + k] - p[k], 0, p[k] - f[b + 3 + k]));
        if (Math.hypot(...d) <= r) out.push(i + ': ' + [0, 1, 2, 3, 4, 5].map((k) => f[b + k].toFixed(2)).join(',') + ' planes ' + f[b + 6] + (f[b + 7] ? ' player-only' : ''));
      }
      wasm.kk_free(buf); wasm.kk_free(pb);
      return 'player ' + Array.from(p).map((v) => v.toFixed(2)).join(',') + '\n' + out.join('\n');
    },
    subplanes(i) {
      const buf = wasm.kk_alloc(64 * 4);
      const ok = wasm.kk_debug_subplanes(i, buf);
      const f = new Float32Array(mem(), buf, 64);
      const out = ok ? ['planes ' + f[0]] : ['no cell'];
      for (let k = 0; ok && k < f[0]; k++) out.push([0, 1, 2, 3].map((j) => f[1 + k * 4 + j].toFixed(4)).join(' '));
      wasm.kk_free(buf);
      return out.join('\n');
    },
    cell(i) {
      const buf = wasm.kk_alloc(64 * 4);
      const ok = wasm.kk_debug_cell(i, buf);
      const f = new Float32Array(mem(), buf, 64);
      const out = ok ? ['planes ' + f[0] + ' mode ' + f[1]] : ['no cell'];
      for (let k = 0; ok && k < f[0]; k++) out.push([0, 1, 2, 3].map((j) => f[2 + k * 4 + j].toFixed(3)).join(' '));
      wasm.kk_free(buf);
      return out.join('\n');
    },
    ray(x0, y0, z0, x1, y1, z1) {
      const buf = wasm.kk_alloc(6 * 4);
      const hit = wasm.kk_debug_ray(x0, y0, z0, x1, y1, z1, buf);
      const f = new Float32Array(mem(), buf, 6);
      const s = 'hit ' + hit + ' at ' + Array.from(f.slice(0, 3)).map((v) => v.toFixed(3)).join(',') + ' plane ' + Array.from(f.slice(3)).map((v) => v.toFixed(2)).join(',');
      wasm.kk_free(buf);
      return s;
    },
    part(g, k) {
      const buf = wasm.kk_alloc(12 * 4);
      const ok = wasm.kk_debug_part(g, k, buf);
      const f = new Float32Array(mem(), buf, 12);
      const v = (a) => [a, a + 1, a + 2].map((j) => f[j].toFixed(3)).join(',');
      const s = ok ? `pos ${v(0)} coll ${v(3)} old ${v(6)} cell ${f[9]} kind ${f[10]} verify ${f[11]}` : 'none';
      wasm.kk_free(buf);
      return s;
    },
    sweep(x0, y0, z0, x1, y1, z1) {
      const buf = wasm.kk_alloc(3 * 4);
      const r = wasm.kk_debug_sweep(x0, y0, z0, x1, y1, z1, buf);
      const f = new Float32Array(mem(), buf, 3);
      const s = 'sweep ' + r + ' to ' + Array.from(f).map((v) => v.toFixed(3)).join(',');
      wasm.kk_free(buf);
      return s;
    },
    // every ms milliseconds for n times: particles outside the level or inside solids
    watch(ms = 100, n = 100) {
      const buf = wasm.kk_alloc(6 * 4);
      let worst = 'none', bad = 0, runs = 0, total = 0;
      const t = setInterval(() => {
        total = wasm.kk_debug_check(buf);
        const f = new Float32Array(mem(), buf, 6);
        if (f[4] + f[5] > 0) { bad++; worst = `outside ${f[4]} inside ${f[5]} first ${f[0].toFixed(2)},${f[1].toFixed(2)},${f[2].toFixed(2)} kind ${f[3]}`; }
        if (++runs >= n) { clearInterval(t); wasm.kk_free(buf); post({ type: 'log', text: `watch: ${runs} checks, ${bad} with problems, ${total} particles, last ${worst}` }); }
      }, ms);
      return 'watching';
    },
    tp(x, y, z, dir = 0) { wasm.kk_debug_teleport(x, y, z, dir); return 'teleported'; },
    // press a key (code as kk_key takes it) and release it after ms
    hold(key, ms) { wasm.kk_key(key); setTimeout(() => wasm.kk_key(key | 0x80000000), ms); return 'holding ' + key; },
    setup(id) { const s = KK.internals().setups[id]; return s ? s.vsSrc + '\n--- PS ---\n' + s.psSrc : 'none'; },
  };
})();
self.DBG = DBG;
