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
    frameStart() { if (snap) snap.done = false; if (recording !== null && recording-- === 0) { frameDraws = []; recording = null; } },
    frameEnd() {
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
    show(rt, last = false) { showRT = rt; showLast = last; snap = snap || { done: false }; return 'showing ' + rt; },
    setup(id) { const s = KK.internals().setups[id]; return s ? s.vsSrc + '\n--- PS ---\n' + s.psSrc : 'none'; },
  };
})();
self.DBG = DBG;
