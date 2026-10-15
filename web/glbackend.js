// WebGL2 backend for the kkrieger WebAssembly player.
//
// The player's system layer (web_system.cpp) calls these functions with the
// same information the original Direct3D 9 implementation received: render
// states as D3D state tokens, shaders as D3D vs_1_1 / ps_1_x bytecode, vertex
// buffers in the D3D vertex layouts. Shaders are translated to GLSL ES 3.0 here.
//
// Coordinate conventions handled here:
// - D3D clip z runs 0..w, GL -w..w: the vertex shader remaps z.
// - D3D9 samples pixel centres at integer coordinates; the engine's projection
//   shifts by half a pixel to compensate, which is undone here (u_halfpix).
// - Render-target contents are stored top row first like D3D: rendering into a
//   texture flips y in clip space (and the cull winding with it).
// - D3DCOLOR vertex fields are byte-swapped to RGBA on upload.

/* global self */
'use strict';

const KK = {};

(function () {

  let gl = null;
  let d3dDepth = false;            // EXT_clip_control: clip z in 0..w as in Direct3D
  let mem = null;                   // () => ArrayBuffer of wasm memory
  const textures = [];              // handle -> {tex, w, h, levels, format, rt, fbo}
  const geos = [];                  // handle -> {vao, vbo, ibo, fvf, prim, itype}
  const setups = [];                // id -> {prog, uniforms}
  let depthRB = null, depthW = 0, depthH = 0;
  let quadIBO = null;

  // current D3D state (only the parts we interpret)
  const rs = new Uint32Array(0x310).fill(0xfefefefe);
  const stageTex = [-1, -1, -1, -1, -1, -1, -1, -1];
  const vsConst = new Float32Array(96 * 4);
  const psConst = new Float32Array(8 * 4);
  let curSetup = -1;
  let target = -1;                  // -1 = screen
  let targetH = 0;
  let viewportX = 1, viewportY = 1;
  let statesDirty = true;
  let unknownOps = new Set();

  const D3DCOLOR_FIELDS = {         // byte offsets of D3DCOLOR fields per vertex format
    1: [], 2: [12], 3: [24, 28], 4: [12], 5: [], 6: [12, 16, 20], 7: [36]
  };
  const FVF_SIZE = [0, 32, 32, 64, 16, 16, 32, 48];
  // attribute locations by D3D usage: position 0, normal 1, texcoord0 2, color0 3,
  // color1 4, texcoord1 5, tangent 6, binormal 7
  const FVF_ATTRIBS = {
    1: [[0, 3, 'f', 0], [1, 3, 'f', 12], [2, 2, 'f', 24]],
    2: [[0, 3, 'f', 0], [3, 4, 'c', 12], [2, 2, 'f', 16], [5, 2, 'f', 24]],
    3: [[0, 3, 'f', 0], [1, 3, 'f', 12], [3, 4, 'c', 24], [4, 4, 'c', 28], [2, 2, 'f', 32], [6, 3, 'f', 40], [7, 3, 'f', 52]],
    4: [[0, 3, 'f', 0], [3, 4, 'c', 12]],
    5: [[0, 4, 'f', 0]],
    6: [[0, 3, 'f', 0], [1, 4, 'c', 12], [6, 4, 'c', 16], [3, 4, 'c', 20], [2, 2, 'f', 24]],
    7: [[0, 3, 'f', 0], [1, 3, 'f', 12], [6, 3, 'f', 24], [3, 4, 'c', 36], [2, 2, 'f', 40]],
  };

  /**************************************************************************/
  /*   Shader translation: D3D9 vs_1_1 / ps_1_x bytecode -> GLSL ES 3.0     */
  /**************************************************************************/

  const OPS = {          // opcode -> [name, operand count (dst + sources)]
    0: ['nop', 0], 1: ['mov', 2], 2: ['add', 3], 3: ['sub', 3], 4: ['mad', 4], 5: ['mul', 3],
    6: ['rcp', 2], 7: ['rsq', 2], 8: ['dp3', 3], 9: ['dp4', 3], 10: ['min', 3], 11: ['max', 3],
    12: ['slt', 3], 13: ['sge', 3], 14: ['exp', 2], 15: ['log', 2], 16: ['lit', 2], 17: ['dst', 3],
    18: ['lrp', 4], 19: ['frc', 2], 20: ['m4x4', 3], 21: ['m4x3', 3], 22: ['m3x4', 3], 23: ['m3x3', 3],
    24: ['m3x2', 3], 31: ['dcl', 2], 64: ['texcoord', 1], 65: ['texkill', 1], 66: ['tex', 1],
    67: ['texbem', 2], 68: ['texbeml', 2], 69: ['texreg2ar', 2], 70: ['texreg2gb', 2],
    71: ['texm3x2pad', 2], 72: ['texm3x2tex', 2], 73: ['texm3x3pad', 2], 74: ['texm3x3tex', 2],
    76: ['texm3x3spec', 3], 77: ['texm3x3vspec', 2], 78: ['expp', 2], 79: ['logp', 2], 80: ['cnd', 4],
    81: ['def', 5], 82: ['texreg2rgb', 2], 83: ['texdp3tex', 2], 84: ['texm3x2depth', 2],
    85: ['texdp3', 2], 86: ['texm3x3', 2], 87: ['texdepth', 1], 88: ['cmp', 4], 89: ['bem', 3],
  };

  const DCL_USAGE_LOC = { 0: 0, 3: 1, 5: 2, 10: 3, 6: 6, 7: 7 };   // position normal texcoord color tangent binormal

  function regType(tok) { return ((tok >> 28) & 7) | (((tok >> 11) & 3) << 3); }

  function translate(vsCode, psCode) {
    const vs = translateVS(vsCode);
    const ps = psCode ? translatePS(psCode, vs.writesT) : fixedPS();
    return { vs: vs.src, ps: ps.src, used: ps.used };
  }

  function swz(tok) {
    const s = (tok >> 16) & 0xff;
    const c = 'xyzw';
    return c[s & 3] + c[(s >> 2) & 3] + c[(s >> 4) & 3] + c[(s >> 6) & 3];
  }

  function maskStr(tok) {
    const m = (tok >> 16) & 15;
    let s = '';
    if (m & 1) s += 'x';
    if (m & 2) s += 'y';
    if (m & 4) s += 'z';
    if (m & 8) s += 'w';
    return s || 'xyzw';
  }

  function srcMod(expr, mod) {
    switch (mod) {
      case 0: return expr;
      case 1: return '(-' + expr + ')';
      case 2: return '(' + expr + '-vec4(0.5))';
      case 3: return '(-(' + expr + '-vec4(0.5)))';
      case 4: return '(2.0*' + expr + '-vec4(1.0))';
      case 5: return '(-(2.0*' + expr + '-vec4(1.0)))';
      case 6: return '(vec4(1.0)-' + expr + ')';
      case 7: return '(2.0*' + expr + ')';
      case 8: return '(-2.0*' + expr + ')';
      case 11: return 'abs(' + expr + ')';
      case 12: return '(-abs(' + expr + '))';
      default: return expr;
    }
  }

  function translateVS(code) {
    const out = [];
    const inputs = {};           // v# -> attribute location
    let p = 1;
    let maxTemp = 0;
    const writesT = new Set();
    const body = [];
    let hasDef = false;
    const reg = (tok, isDst) => {
      const t = regType(tok), n = tok & 0x7ff;
      switch (t) {
        case 0: maxTemp = Math.max(maxTemp, n + 1); return 'r' + n;
        case 1: return 'v' + n;
        case 2: return (tok & 0x2000) ? 'c[int(a0.x)+' + n + ']' : 'c[' + n + ']';
        case 3: return 'a0';
        case 4: return ['oPos', 'oFog', 'oPts'][n] || 'oPos';
        case 5: return 'oD' + n;
        case 6: writesT.add(n); return 'oT' + n;
        default: return 'r0';
      }
    };
    const src = (tok) => srcMod(reg(tok, false) + '.' + swz(tok), (tok >> 24) & 15);
    while (p < code.length) {
      const ins = code[p];
      const op = ins & 0xffff;
      if (ins === 0x0000ffff) break;
      if (op === 0xfffe) { p += 1 + ((ins >> 16) & 0x7fff); continue; }
      const info = OPS[op];
      let n = (ins >> 24) & 15;
      if (!info) { unknownOps.add('vs:' + op); p += 1 + n; continue; }
      if (!n) n = info[1];
      const a = code.slice(p + 1, p + 1 + n);
      p += 1 + n;
      const name = info[0];
      if (name === 'dcl') {
        const usage = a[0] & 31, idx = (a[0] >> 16) & 15;
        let loc = DCL_USAGE_LOC[usage];
        if (usage === 5 && idx === 1) loc = 5;
        if (usage === 10 && idx === 1) loc = 4;
        if (loc === undefined) loc = 0;
        inputs[a[1] & 0x7ff] = loc;
        continue;
      }
      if (name === 'def') {
        const f = new Float32Array(new Uint32Array(a.slice(1)).buffer);
        body.push('c[' + (a[0] & 0x7ff) + '] = vec4(' + Array.from(f).map(fmt).join(',') + ');');
        hasDef = true;
        continue;
      }
      const d = reg(a[0], true), m = maskStr(a[0]);
      const s = a.slice(1).map(src);
      let e;
      switch (name) {
        case 'mov':
          if (d === 'a0') { body.push('a0 = floor(' + s[0] + ');'); continue; }
          e = s[0]; break;
        case 'add': e = s[0] + '+' + s[1]; break;
        case 'sub': e = s[0] + '-' + s[1]; break;
        case 'mul': e = s[0] + '*' + s[1]; break;
        case 'mad': e = s[0] + '*' + s[1] + '+' + s[2]; break;
        case 'rcp': e = 'vec4(rcp1((' + s[0] + ').x))'; break;
        case 'rsq': e = 'vec4(rsq1((' + s[0] + ').x))'; break;
        case 'dp3': e = 'vec4(dot((' + s[0] + ').xyz,(' + s[1] + ').xyz))'; break;
        case 'dp4': e = 'vec4(dot(' + s[0] + ',' + s[1] + '))'; break;
        case 'min': e = 'min(' + s[0] + ',' + s[1] + ')'; break;
        case 'max': e = 'max(' + s[0] + ',' + s[1] + ')'; break;
        case 'slt': e = 'vec4(lessThan(' + s[0] + ',' + s[1] + '))'; break;
        case 'sge': e = 'vec4(greaterThanEqual(' + s[0] + ',' + s[1] + '))'; break;
        case 'exp': case 'expp': e = 'vec4(exp2((' + s[0] + ').w))'; break;
        case 'log': case 'logp': e = 'vec4(log2(abs((' + s[0] + ').w)))'; break;
        case 'frc': e = 'fract(' + s[0] + ')'; break;
        case 'lit': e = 'lit4(' + s[0] + ')'; break;
        case 'dst': e = 'vec4(1.0,(' + s[0] + ').y*(' + s[1] + ').y,(' + s[0] + ').z,(' + s[1] + ').w)'; break;
        case 'lrp': e = 'mix(' + s[2] + ',' + s[1] + ',' + s[0] + ')'; break;
        case 'm4x4': case 'm4x3': case 'm3x4': case 'm3x3': case 'm3x2': {
          const rows = { m4x4: 4, m4x3: 3, m3x4: 4, m3x3: 3, m3x2: 2 }[name];
          const four = name[1] === '4';
          const cn = a[2] & 0x7ff, rel = (a[2] & 0x2000) ? 'int(a0.x)+' : '';
          const comps = [];
          for (let i = 0; i < 4; i++) {
            if (i >= rows) { comps.push('0.0'); continue; }
            const cr = 'c[' + rel + (cn + i) + ']';
            comps.push(four ? 'dot(' + s[0] + ',' + cr + ')' : 'dot((' + s[0] + ').xyz,' + cr + '.xyz)');
          }
          e = 'vec4(' + comps.join(',') + ')';
          break;
        }
        default: unknownOps.add('vs:' + name); continue;
      }
      body.push(d + '.' + m + ' = (' + e + ').' + m + ';');
    }
    out.push('#version 300 es', 'precision highp float;', 'precision highp int;', 'invariant gl_Position;');
    for (const v in inputs) out.push('layout(location=' + inputs[v] + ') in vec4 in' + inputs[v] + ';');
    out.push(hasDef ? 'uniform vec4 cu[96];' : 'uniform vec4 c[96];', 'uniform vec3 u_clip;');   // x,y: half-pixel undo, z: y flip
    for (let i = 0; i < 8; i++) out.push('out vec4 vT' + i + ';');
    out.push('out vec4 vD0;', 'out vec4 vD1;', 'out float vFog;');
    out.push('float rcp1(float x){ return x==0.0 ? 3.4e38 : 1.0/x; }');
    out.push('float rsq1(float x){ x=abs(x); return x==0.0 ? 3.4e38 : inversesqrt(x); }');
    out.push('vec4 lit4(vec4 s){ float d=max(s.x,0.0); float sp = s.x>0.0 ? pow(max(s.y,0.0),clamp(s.w,-127.9961,127.9961)) : 0.0; return vec4(1.0,d,sp,1.0); }');
    out.push('void main(){');
    out.push('vec4 oPos=vec4(0.0), oFog=vec4(1.0), oPts=vec4(1.0), oD0=vec4(1.0), oD1=vec4(0.0), a0=vec4(0.0);');
    for (let i = 0; i < 8; i++) out.push('vec4 oT' + i + '=vec4(0.0,0.0,0.0,1.0);');
    for (let i = 0; i < Math.max(maxTemp, 1); i++) out.push('vec4 r' + i + '=vec4(0.0);');
    for (const v in inputs) out.push('vec4 v' + v + '=in' + inputs[v] + ';');
    if (hasDef) out.push('vec4 c[96]; for(int i=0;i<96;i++) c[i]=cu[i];');
    out.push(...body);
    out.push('gl_Position = oPos;');
    out.push('gl_Position.xy += u_clip.xy*oPos.w;');
    out.push('gl_Position.y *= u_clip.z;');
    if (!d3dDepth) out.push('gl_Position.z = 2.0*oPos.z - oPos.w;');
    for (let i = 0; i < 8; i++) out.push('vT' + i + '=oT' + i + ';');
    out.push('vD0=clamp(oD0,0.0,1.0); vD1=clamp(oD1,0.0,1.0); vFog=clamp(oFog.x,0.0,1.0);');
    out.push('}');
    return { src: out.join('\n'), writesT };
  }

  function fmt(f) {
    if (!isFinite(f)) return f > 0 ? '3.4e38' : '-3.4e38';
    let s = f.toString();
    if (!/[.e]/.test(s)) s += '.0';
    return s;
  }

  // shared fragment shader prologue: samplers, texture kinds, fog, alpha test
  function psPrologue(out) {
    out.push('#version 300 es', 'precision highp float;', 'precision highp int;', 'precision highp sampler3D;');
    for (let i = 0; i < 4; i++) out.push('uniform sampler2D s2d' + i + ';');
    out.push('uniform vec4 c[8];');
    out.push('uniform ivec4 u_kind;');       // per stage: 0 = 2D, 2 = normalisation cube, 3 = attenuation volume, -1 = none
    out.push('uniform ivec4 u_proj;');       // per stage: projected texture coordinates
    out.push('uniform vec4 u_bump[4];');     // bump env matrix per stage
    out.push('uniform vec4 u_fogcol;', 'uniform ivec4 u_misc;'); // misc.x fog, y alpha func, z alpha ref
    out.push('in vec4 vT0; in vec4 vT1; in vec4 vT2; in vec4 vT3; in vec4 vT4; in vec4 vT5; in vec4 vT6; in vec4 vT7;');
    out.push('in vec4 vD0; in vec4 vD1; in float vFog;');
    out.push('out vec4 fragColor;');
    out.push('vec4 samp(int s, vec4 tc){');
    out.push('  int k = s==0?u_kind.x:s==1?u_kind.y:s==2?u_kind.z:u_kind.w;');
    out.push('  int pj = s==0?u_proj.x:s==1?u_proj.y:s==2?u_proj.z:u_proj.w;');
    out.push('  if(k==2) return vec4(normalize(tc.xyz)*(127.0/255.0)+vec3(128.0/255.0),0.0);');
    out.push('  if(k==3){ vec3 v=(clamp(tc.xyz,0.0,1.0)*32.0-vec3(16.5))*(2.0/30.0); return vec4(max(1.0-dot(v,v),0.0)); }');
    out.push('  if(k<0) return vec4(0.0);');
    out.push('  vec2 uv = pj!=0 ? tc.xy/tc.w : tc.xy;');
    out.push('  if(s==0) return texture(s2d0,uv); if(s==1) return texture(s2d1,uv); if(s==2) return texture(s2d2,uv); return texture(s2d3,uv);');
    out.push('}');
    out.push('bool atest(float a){ int f=u_misc.y; float r=float(u_misc.z)/255.0; a=floor(a*255.0+0.5)/255.0;');
    out.push('  if(f==1) return false; if(f==2) return a<r; if(f==3) return a==r; if(f==4) return a<=r; if(f==5) return a>r;');
    out.push('  if(f==6) return a!=r; if(f==7) return a>=r; return true; }');
  }

  function psEpilogue(out, color) {
    out.push('vec4 res = clamp(' + color + ',0.0,1.0);');
    out.push('if(u_misc.y!=0 && !atest(res.a)) discard;');
    out.push('if(u_misc.x!=0) res.rgb = mix(u_fogcol.rgb,res.rgb,vFog);');
    // u_misc.w selects a diagnostic output for debug.js (0 = normal)
    out.push('fragColor = u_misc.w==0 ? res : u_misc.w==1 ? vec4(1.0,0.0,0.0,1.0) : u_misc.w==2 ? vec4(t0.rgb,0.0) : u_misc.w==3 ? vec4(t1.rgb,0.0) : u_misc.w==4 ? vec4(t2.rgb,0.0) : u_misc.w==5 ? vec4(t3.rgb,0.0) : u_misc.w==7 ? vec4(floor(gl_FragCoord.z*16777215.0/65536.0)/255.0, mod(floor(gl_FragCoord.z*16777215.0/256.0),256.0)/255.0, mod(floor(gl_FragCoord.z*16777215.0),256.0)/255.0, 1.0) : vec4(r0.rgb,0.0);');
  }

  function translatePS(code, writesT) {
    const out = [];
    const body = [];
    const used = new Set();
    let p = 1;
    const version = code[0] & 0xffff;
    const reg = (tok) => {
      const t = regType(tok), n = tok & 0x7ff;
      switch (t) {
        case 0: return 'r' + n;
        case 1: return 'v' + n;
        case 2: return 'c[' + n + ']';
        case 3: return 't' + n;
        default: return 'r0';
      }
    };
    const src = (tok) => srcMod(reg(tok) + '.' + swz(tok), (tok >> 24) & 15);
    const shift = (tok) => {
      const sh = (tok >> 24) & 15;
      return { 0: '', 1: '*2.0', 2: '*4.0', 3: '*8.0', 15: '*0.5', 14: '*0.25', 13: '*0.125' }[sh] || '';
    };
    while (p < code.length) {
      const ins = code[p];
      const op = ins & 0xffff;
      if (ins === 0x0000ffff) break;
      if (op === 0xfffe) { p += 1 + ((ins >> 16) & 0x7fff); continue; }
      if (op === 0xfffd) { p++; continue; }          // phase (ps 1.4)
      const info = OPS[op];
      let n = (ins >> 24) & 15;
      if (!info) { unknownOps.add('ps:' + op); p += 1 + n; continue; }
      if (!n) n = info[1];
      if (version >= 0x104 && (op === 66 || op === 64)) n = 2;   // ps 1.4 texld/texcrd take a source
      const a = code.slice(p + 1, p + 1 + n);
      p += 1 + n;
      const name = info[0];
      if (name === 'def') {
        const f = new Float32Array(new Uint32Array(a.slice(1)).buffer);
        body.push('c[' + (a[0] & 0x7ff) + '] = vec4(' + Array.from(f).map(fmt).join(',') + ');');
        continue;
      }
      if (name === 'dcl') continue;
      const dn = a[0] & 0x7ff;
      const d = reg(a[0]), m = maskStr(a[0]);
      const sat = (a[0] >> 20) & 1;
      const tc = (i) => 'vT' + i;
      let e;
      switch (name) {
        case 'tex':
          used.add(dn);
          if (version >= 0x104) e = 'samp(' + dn + ',' + 'vT' + (a[1] & 0x7ff) + ')';
          else e = 'samp(' + dn + ',' + tc(dn) + ')';
          break;
        case 'texcoord':
          e = 'vec4(clamp(' + tc(version >= 0x104 ? (a[1] & 0x7ff) : dn) + '.xyz,0.0,1.0),1.0)'; break;
        case 'texkill':
          body.push('if(any(lessThan(' + tc(dn) + '.xyz,vec3(0.0)))) discard;'); continue;
        case 'texbem': case 'texbeml': {
          used.add(dn);
          const sr = reg(a[1]);
          e = 'samp(' + dn + ',vec4(' + tc(dn) + '.xy + vec2(u_bump[' + dn + '].x*' + sr + '.x+u_bump[' + dn + '].z*' + sr + '.y,u_bump[' + dn + '].y*' + sr + '.x+u_bump[' + dn + '].w*' + sr + '.y),' + tc(dn) + '.zw))';
          break;
        }
        case 'texreg2ar': used.add(dn); e = 'samp(' + dn + ',vec4(' + reg(a[1]) + '.wx,0.0,1.0))'; break;
        case 'texreg2gb': used.add(dn); e = 'samp(' + dn + ',vec4(' + reg(a[1]) + '.yz,0.0,1.0))'; break;
        case 'texm3x2pad': body.push('vec4 m3pad' + dn + ' = vec4(dot(' + tc(dn) + '.xyz,' + reg(a[1]) + '.xyz));'); continue;
        case 'texm3x2tex': used.add(dn); e = 'samp(' + dn + ',vec4(m3pad' + (dn - 1) + '.x,dot(' + tc(dn) + '.xyz,' + reg(a[1]) + '.xyz),0.0,1.0))'; break;
        case 'texm3x3pad': body.push('vec4 m3pad' + dn + ' = vec4(dot(' + tc(dn) + '.xyz,' + reg(a[1]) + '.xyz));'); continue;
        case 'texm3x3tex': used.add(dn); e = 'samp(' + dn + ',vec4(m3pad' + (dn - 2) + '.x,m3pad' + (dn - 1) + '.x,dot(' + tc(dn) + '.xyz,' + reg(a[1]) + '.xyz),1.0))'; break;
        case 'mov': e = src(a[1]); break;
        case 'add': e = src(a[1]) + '+' + src(a[2]); break;
        case 'sub': e = src(a[1]) + '-' + src(a[2]); break;
        case 'mul': e = src(a[1]) + '*' + src(a[2]); break;
        case 'mad': e = src(a[1]) + '*' + src(a[2]) + '+' + src(a[3]); break;
        case 'lrp': e = 'mix(' + src(a[3]) + ',' + src(a[2]) + ',' + src(a[1]) + ')'; break;
        case 'dp3': e = 'vec4(dot((' + src(a[1]) + ').xyz,(' + src(a[2]) + ').xyz))'; break;
        case 'dp4': e = 'vec4(dot(' + src(a[1]) + ',' + src(a[2]) + '))'; break;
        case 'cnd':
          if (version >= 0x104) e = 'mix(' + src(a[3]) + ',' + src(a[2]) + ',vec4(greaterThan(' + src(a[1]) + ',vec4(0.5))))';
          else e = '((' + src(a[1]) + ').w>0.5 ? ' + src(a[2]) + ' : ' + src(a[3]) + ')';
          break;
        case 'cmp': e = 'mix(' + src(a[3]) + ',' + src(a[2]) + ',vec4(greaterThanEqual(' + src(a[1]) + ',vec4(0.0))))'; break;
        case 'min': e = 'min(' + src(a[1]) + ',' + src(a[2]) + ')'; break;
        case 'max': e = 'max(' + src(a[1]) + ',' + src(a[2]) + ')'; break;
        default: unknownOps.add('ps:' + name); continue;
      }
      e = '(' + e + ')' + shift(a[0]);
      e = sat ? 'clamp(' + e + ',0.0,1.0)' : 'clamp(' + e + ',-1.0,1.0)';
      body.push(d + '.' + m + ' = (' + e + ').' + m + ';');
    }
    psPrologue(out);
    out.push('void main(){');
    out.push('vec4 r0=vec4(0.0),r1=vec4(0.0),r2=vec4(0.0),r3=vec4(0.0),r4=vec4(0.0),r5=vec4(0.0);');
    out.push('vec4 t0=vec4(0.0),t1=vec4(0.0),t2=vec4(0.0),t3=vec4(0.0),t4=vec4(0.0),t5=vec4(0.0);');
    out.push('vec4 v0=vD0, v1=vD1;');
    out.push('vec4 c[8]; for(int i=0;i<8;i++) c[i]=clamp(PS_C[i],-1.0,1.0);'.replace('PS_C', 'cc'));
    out.push(...body);
    psEpilogue(out, 'r0');
    out.push('}');
    // rename the uniform constant array so local defs can override it
    return { src: out.join('\n').replace('uniform vec4 c[8];', 'uniform vec4 cc[8];'), used };
  }

  // no pixel shader: texture stage 0 modulated by the vertex colour (sSimpleMaterial)
  function fixedPS() {
    const out = [];
    psPrologue(out);
    out.push('void main(){');
    out.push('vec4 col = u_kind.x>=0 ? samp(0,vT0)*vD0 : vD0;');
    out.push('vec4 t0=col,t1=col,t2=col,t3=col,r0=col;');
    psEpilogue(out, 'col');
    out.push('}');
    return { src: out.join('\n').replace('uniform vec4 c[8];', 'uniform vec4 cc[8];'), used: new Set([0]) };
  }

  function compile(type, src) {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      console.error(gl.getShaderInfoLog(sh) + '\n' + src);
      return null;
    }
    return sh;
  }

  /**************************************************************************/
  /*   Exported functions                                                   */
  /**************************************************************************/

  KK.init = function (glctx, memoryGetter) {
    gl = glctx;
    mem = memoryGetter;
    // Use Direct3D's clip depth range (0..w) where the browser allows it, so
    // depth is computed as in the original instead of through a -w..w remap.
    const clip = gl.getExtension('EXT_clip_control');
    if (clip) {
      clip.clipControlEXT(clip.LOWER_LEFT_EXT, clip.ZERO_TO_ONE_EXT);
      d3dDepth = true;
    }
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    // index buffer for quad lists (sGEO_QUAD), as in the original's GeoBuffer[3]
    const idx = new Uint16Array(0x8000 / 4 * 6);
    for (let i = 0; i < 0x8000 / 6; i++) {
      idx.set([i * 4, i * 4 + 1, i * 4 + 2, i * 4, i * 4 + 2, i * 4 + 3], i * 6);
    }
    quadIBO = idx;
  };

  KK.unknownOps = () => Array.from(unknownOps);
  KK.internals = () => ({ gl, textures, setups, geos, target, rs });

  const u8 = (ptr, n) => new Uint8Array(mem(), ptr, n);
  const u32 = (ptr, n) => new Uint32Array(mem(), ptr, n);
  const f32 = (ptr, n) => new Float32Array(mem(), ptr, n);

  KK.gl_texture_create = function (h, xs, ys, format, levels, rt) {
    if (textures[h]) gl.deleteTexture(textures[h].tex);
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    const ifmt = format === 5 ? gl.RGBA8_SNORM : gl.RGBA8;
    gl.texStorage2D(gl.TEXTURE_2D, rt ? 1 : levels, ifmt, xs, ys);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, rt || levels <= 1 ? gl.LINEAR : gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    textures[h] = { tex, w: xs, h: ys, levels: rt ? 1 : levels, format, rt, fbo: null, snorm: format === 5 };
    if (rt) {
      const fbo = gl.createFramebuffer();
      textures[h].fbo = fbo;
    }
  };

  KK.gl_texture_upload = function (h, level, xs, ys, ptr) {
    const t = textures[h];
    if (!t || level >= t.levels) return;
    gl.bindTexture(gl.TEXTURE_2D, t.tex);
    if (t.snorm)
      gl.texSubImage2D(gl.TEXTURE_2D, level, 0, 0, xs, ys, gl.RGBA, gl.BYTE, new Int8Array(mem(), ptr, xs * ys * 4));
    else
      gl.texSubImage2D(gl.TEXTURE_2D, level, 0, 0, xs, ys, gl.RGBA, gl.UNSIGNED_BYTE, u8(ptr, xs * ys * 4));
  };

  KK.gl_texture_finish = function (h, levels) {
    const t = textures[h];
    if (!t) return;
    gl.bindTexture(gl.TEXTURE_2D, t.tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, Math.max(0, Math.min(levels, t.levels) - 1));
  };

  KK.gl_texture_delete = function (h) {
    const t = textures[h];
    if (!t) return;
    gl.deleteTexture(t.tex);
    if (t.fbo) gl.deleteFramebuffer(t.fbo);
    textures[h] = null;
  };

  KK.gl_texture_read = function (h, xs, ys, ptr) {
    const t = textures[h];
    if (!t) return;
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t.tex, 0);
    gl.readPixels(0, 0, xs, ys, gl.RGBA, gl.UNSIGNED_BYTE, u8(ptr, xs * ys * 4));
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
    gl.deleteFramebuffer(fbo);
  };

  KK.gl_texture_bind = function (stage, h) {
    stageTex[stage] = h;
  };

  function ensureDepth(w, h) {
    if (depthRB && depthW === w && depthH === h) return depthRB;
    if (depthRB) gl.deleteRenderbuffer(depthRB);
    depthRB = gl.createRenderbuffer();
    gl.bindRenderbuffer(gl.RENDERBUFFER, depthRB);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH24_STENCIL8, w, h);
    depthW = w; depthH = h;
    return depthRB;
  }

  // render targets share one depth/stencil buffer per size, like the original's single z-buffer
  const depthBySize = {};
  function depthFor(w, h) {
    const key = w + 'x' + h;
    if (!depthBySize[key]) {
      const rb = gl.createRenderbuffer();
      gl.bindRenderbuffer(gl.RENDERBUFFER, rb);
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH24_STENCIL8, w, h);
      depthBySize[key] = rb;
    }
    return depthBySize[key];
  }

  KK.gl_viewport = function (tgt, txs, tys, x, y, w, h) {
    target = tgt;
    if (tgt < 0) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      targetH = gl.drawingBufferHeight;
      gl.viewport(x, targetH - (y + h), w, h);
    } else {
      const t = textures[tgt];
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t.tex, 0);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_STENCIL_ATTACHMENT, gl.RENDERBUFFER, depthFor(t.w, t.h));
      targetH = t.h;
      gl.viewport(x, y, w, h);      // render targets are stored top row first (y flipped in clip space)
    }
    viewportX = w; viewportY = h;
    statesDirty = true;
  };

  KK.gl_clear = function (flags, color) {
    let bits = 0;
    gl.disable(gl.SCISSOR_TEST);
    if (rs[174]) { /* scissor stays as set by the engine */ gl.enable(gl.SCISSOR_TEST); }
    if (flags & 1) {
      bits |= gl.COLOR_BUFFER_BIT;
      gl.colorMask(true, true, true, true);
      gl.clearColor(((color >> 16) & 255) / 255, ((color >> 8) & 255) / 255, (color & 255) / 255, ((color >>> 24) & 255) / 255);
    }
    if (flags & 2) {
      bits |= gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT;
      gl.depthMask(true);
      gl.clearDepth(1.0);
      gl.stencilMask(0xff);
      gl.clearStencil(0);
    }
    if (flags & 4) {
      bits |= gl.STENCIL_BUFFER_BIT;
      gl.stencilMask(0xff);
      gl.clearStencil(0);
    }
    gl.clear(bits);
    statesDirty = true;
  };

  let scissorRect = [0, 0, 0, 0];
  KK.gl_scissor = function (enable, x, y, w, h) {
    rs[174] = enable ? 1 : 0;
    scissorRect = [x, y, w, h];
    statesDirty = true;
  };

  KK.gl_grab = function (srcrt, x0, y0, x1, y1, tex, txs, tys, stretch) {
    const t = textures[tex];
    if (!t) return;
    const dst = gl.createFramebuffer();
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, dst);
    gl.framebufferTexture2D(gl.DRAW_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t.tex, 0);
    let sx0 = x0, sx1 = x1, sy0, sy1;
    if (srcrt < 0) {
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
      const H = gl.drawingBufferHeight;
      sy0 = H - y0; sy1 = H - y1;            // screen is bottom-up; textures are stored top row first
    } else {
      const s = textures[srcrt];
      const rf = gl.createFramebuffer();
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, rf);
      gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, s.tex, 0);
      sy0 = y0; sy1 = y1;
    }
    const dw = stretch ? txs : (x1 - x0), dh = stretch ? tys : (y1 - y0);
    gl.disable(gl.SCISSOR_TEST);
    gl.blitFramebuffer(sx0, sy0, sx1, sy1, 0, 0, dw, dh, gl.COLOR_BUFFER_BIT, gl.LINEAR);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    gl.deleteFramebuffer(dst);
    // restore the current target
    if (target < 0) gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    else gl.bindFramebuffer(gl.FRAMEBUFFER, textures[target].fbo);
    statesDirty = true;
  };

  KK.gl_state = function (token, value) {
    rs[token] = value >>> 0;
    statesDirty = true;
  };

  /**************************************************************************/

  KK.gl_geo_create = function (h, fvf, prim) {
    const g = { vao: gl.createVertexArray(), vbo: gl.createBuffer(), ibo: null, fvf, prim, itype: 0, quadIbo: null };
    gl.bindVertexArray(g.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, g.vbo);
    const stride = FVF_SIZE[fvf];
    for (const [loc, n, t, off] of FVF_ATTRIBS[fvf]) {
      gl.enableVertexAttribArray(loc);
      if (t === 'f') gl.vertexAttribPointer(loc, n, gl.FLOAT, false, stride, off);
      else gl.vertexAttribPointer(loc, 4, gl.UNSIGNED_BYTE, true, stride, off);
    }
    gl.bindVertexArray(null);
    geos[h] = g;
  };

  KK.gl_geo_delete = function (h) {
    const g = geos[h];
    if (!g) return;
    gl.deleteVertexArray(g.vao);
    gl.deleteBuffer(g.vbo);
    if (g.ibo) gl.deleteBuffer(g.ibo);
    geos[h] = null;
  };

  KK.gl_geo_upload = function (h, kind, ptr, bytes) {
    const g = geos[h];
    if (!g) return;
    if (kind === 0) {
      let data = u8(ptr, bytes);
      const cols = D3DCOLOR_FIELDS[g.fvf];
      if (cols.length) {                       // D3DCOLOR is B,G,R,A in memory: swap to R,G,B,A
        data = new Uint8Array(data);
        const stride = FVF_SIZE[g.fvf];
        for (let v = 0; v + stride <= bytes; v += stride)
          for (const o of cols) { const t = data[v + o]; data[v + o] = data[v + o + 2]; data[v + o + 2] = t; }
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, g.vbo);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    } else {
      if (!g.ibo) g.ibo = gl.createBuffer();
      g.itype = kind === 2 ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
      gl.bindVertexArray(g.vao);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ibo);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, u8(ptr, bytes), gl.STATIC_DRAW);
      gl.bindVertexArray(null);
    }
  };

  /**************************************************************************/

  KK.gl_setup_create = function (id, vsptr, vslen, psptr, pslen) {
    if (!vsptr) return 0;
    const vs = Array.from(u32(vsptr, vslen));
    const ps = psptr ? Array.from(u32(psptr, pslen)) : null;
    const src = translate(vs, ps);
    const v = compile(gl.VERTEX_SHADER, src.vs);
    const f = compile(gl.FRAGMENT_SHADER, src.ps);
    if (!v || !f) return 0;
    const prog = gl.createProgram();
    gl.attachShader(prog, v);
    gl.attachShader(prog, f);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      console.error(gl.getProgramInfoLog(prog));
      return 0;
    }
    const U = (n) => gl.getUniformLocation(prog, n);
    const u = {
      c: U('c') || U('cu'), cc: U('cc'), clip: U('u_clip'), kind: U('u_kind'), proj: U('u_proj'),
      bump: U('u_bump'), fogcol: U('u_fogcol'), misc: U('u_misc'),
    };
    gl.useProgram(prog);
    for (let i = 0; i < 4; i++) gl.uniform1i(U('s2d' + i), i);
    setups[id] = { prog, u, used: src.used, vsSrc: src.vs, psSrc: src.ps };
    return 1;
  };

  KK.gl_setup_delete = function (id) {
    const s = setups[id];
    if (s) gl.deleteProgram(s.prog);
    setups[id] = null;
  };

  KK.gl_setup_use = function (id) {
    curSetup = id;
  };

  KK.gl_constants = function (pixel, ptr, count) {
    const src = f32(ptr, count * 4);
    if (pixel) psConst.set(src.subarray(0, Math.min(count, 8) * 4));
    else vsConst.set(src.subarray(0, Math.min(count, 96) * 4));
  };

  /**************************************************************************/
  /*   Render state application                                             */
  /**************************************************************************/

  const BLEND = [0, 'ZERO', 'ONE', 'SRC_COLOR', 'ONE_MINUS_SRC_COLOR', 'SRC_ALPHA', 'ONE_MINUS_SRC_ALPHA',
    'DST_ALPHA', 'ONE_MINUS_DST_ALPHA', 'DST_COLOR', 'ONE_MINUS_DST_COLOR', 'SRC_ALPHA_SATURATE',
    'SRC_ALPHA', 'ONE_MINUS_SRC_ALPHA', 'CONSTANT_COLOR', 'ONE_MINUS_CONSTANT_COLOR'];
  const CMP = [0, 'NEVER', 'LESS', 'EQUAL', 'LEQUAL', 'GREATER', 'NOTEQUAL', 'GEQUAL', 'ALWAYS'];
  const SOP = [0, 'KEEP', 'ZERO', 'REPLACE', 'INCR', 'DECR', 'INVERT', 'INCR_WRAP', 'DECR_WRAP'];
  const g_ = (name, def) => (name && gl[name] !== undefined) ? gl[name] : def;
  const st = (t, def) => (rs[t] === 0xfefefefe ? def : rs[t]);

  function applyStates() {
    const rt = target >= 0;
    // depth
    const zenable = st(7, 1);
    if (zenable) gl.enable(gl.DEPTH_TEST); else gl.disable(gl.DEPTH_TEST);
    // D3D's light passes test depth EQUAL against a separate z pass; GLSL only promises identical
    // depth for invariant positions computed the same way, so EQUAL becomes LEQUAL
    let zf = st(23, 4);
    if (zf === 3) zf = 4;
    gl.depthFunc(g_(CMP[zf], gl.LEQUAL));
    gl.depthMask(!!st(14, 1));
    // depth bias (D3D: bias in depth units; GL: polygonOffset units are implementation-scaled)
    const bias = new Float32Array(new Uint32Array([st(195, 0)]).buffer)[0];
    const slope = new Float32Array(new Uint32Array([st(175, 0)]).buffer)[0];
    if (bias || slope) {
      gl.enable(gl.POLYGON_OFFSET_FILL);
      gl.polygonOffset(slope, bias * (1 << 24));
    } else gl.disable(gl.POLYGON_OFFSET_FILL);
    // culling: D3D's front faces appear clockwise on the screen, GL's (with
    // frontFace CCW) counter-clockwise, so D3DCULL_CCW (cull D3D back faces)
    // culls GL front faces. Render targets are drawn upside down (y flipped
    // in clip space), which reverses the on-screen winding once more.
    const cull = st(22, 3);
    if (cull === 1) gl.disable(gl.CULL_FACE);
    else {
      gl.enable(gl.CULL_FACE);
      gl.frontFace(gl.CCW);
      let front = cull === 3;
      if (rt) front = !front;
      gl.cullFace(front ? gl.FRONT : gl.BACK);
    }
    // blending
    if (st(27, 0)) {
      gl.enable(gl.BLEND);
      const sep = st(206, 0);
      const s = g_(BLEND[st(19, 2)], gl.ONE), d = g_(BLEND[st(20, 1)], gl.ZERO);
      if (sep) gl.blendFuncSeparate(s, d, g_(BLEND[st(207, 2)], gl.ONE), g_(BLEND[st(208, 1)], gl.ZERO));
      else gl.blendFunc(s, d);
      const op = st(171, 1);
      const eq = op === 2 ? gl.FUNC_SUBTRACT : op === 3 ? gl.FUNC_REVERSE_SUBTRACT : op === 4 ? gl.MIN : op === 5 ? gl.MAX : gl.FUNC_ADD;
      gl.blendEquation(eq);
      const bf = st(193, 0xffffffff);
      gl.blendColor(((bf >> 16) & 255) / 255, ((bf >> 8) & 255) / 255, (bf & 255) / 255, ((bf >>> 24) & 255) / 255);
    } else gl.disable(gl.BLEND);
    const cw = st(168, 15);
    gl.colorMask(!!(cw & 1), !!(cw & 2), !!(cw & 4), !!(cw & 8));
    // stencil (two-sided as in D3DRS_TWOSIDEDSTENCILMODE)
    if (st(52, 0)) {
      gl.enable(gl.STENCIL_TEST);
      const ref = st(57, 0), mask = st(58, 0xffffffff), wmask = st(59, 0xffffffff);
      // D3D's clockwise (front) faces are GL back faces on screen, GL front faces in render targets
      const frontFace = rt ? gl.FRONT : gl.BACK, backFace = rt ? gl.BACK : gl.FRONT;
      gl.stencilFuncSeparate(frontFace, g_(CMP[st(56, 8)], gl.ALWAYS), ref, mask);
      gl.stencilOpSeparate(frontFace, g_(SOP[st(53, 1)], gl.KEEP), g_(SOP[st(54, 1)], gl.KEEP), g_(SOP[st(55, 1)], gl.KEEP));
      if (st(185, 0)) {
        gl.stencilFuncSeparate(backFace, g_(CMP[st(189, 8)], gl.ALWAYS), ref, mask);
        gl.stencilOpSeparate(backFace, g_(SOP[st(186, 1)], gl.KEEP), g_(SOP[st(187, 1)], gl.KEEP), g_(SOP[st(188, 1)], gl.KEEP));
      } else {
        gl.stencilFuncSeparate(backFace, g_(CMP[st(56, 8)], gl.ALWAYS), ref, mask);
        gl.stencilOpSeparate(backFace, g_(SOP[st(53, 1)], gl.KEEP), g_(SOP[st(54, 1)], gl.KEEP), g_(SOP[st(55, 1)], gl.KEEP));
      }
      gl.stencilMask(wmask & 0xff);
    } else gl.disable(gl.STENCIL_TEST);
    // scissor
    if (rs[174] === 1) {
      gl.enable(gl.SCISSOR_TEST);
      const [x, y, w, h] = scissorRect;
      if (target < 0) gl.scissor(x, gl.drawingBufferHeight - (y + h), w, h);
      else gl.scissor(x, y, w, h);
    } else gl.disable(gl.SCISSOR_TEST);
  }

  function applySamplers(used) {
    for (let s = 0; s < 4; s++) {
      const h = stageTex[s];
      gl.activeTexture(gl.TEXTURE0 + s);
      // a texture bound while it is the render target is a feedback loop in WebGL (the draw fails)
      const t = (h >= 0 && used.has(s) && h !== target) ? textures[h] : null;
      gl.bindTexture(gl.TEXTURE_2D, t ? t.tex : null);
      if (!t) continue;
      const base = 0x200 + (s << 4);
      const addr = (m) => m === 3 || m === 5 ? gl.CLAMP_TO_EDGE : m === 2 ? gl.MIRRORED_REPEAT : m === 4 ? gl.CLAMP_TO_EDGE : gl.REPEAT;
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, addr(st(base + 1, 1)));
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, addr(st(base + 2, 1)));
      const mag = st(base + 5, 1), min = st(base + 6, 1), mip = st(base + 7, 0);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, mag >= 2 ? gl.LINEAR : gl.NEAREST);
      let mf;
      if (t.levels <= 1 || mip === 0) mf = min >= 2 ? gl.LINEAR : gl.NEAREST;
      else if (mip === 1) mf = min >= 2 ? gl.LINEAR_MIPMAP_NEAREST : gl.NEAREST_MIPMAP_NEAREST;
      else mf = min >= 2 ? gl.LINEAR_MIPMAP_LINEAR : gl.NEAREST_MIPMAP_LINEAR;
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mf);
    }
  }

  const PRIM = [0, 'POINTS', 'LINES', 'TRIANGLES', 'LINE_STRIP', 'TRIANGLE_STRIP', 'TRIANGLES'];
  const kindBuf = new Int32Array(4), projBuf = new Int32Array(4), bumpBuf = new Float32Array(16);

  KK.gl_geo_draw = function (h, vcount, icount) {
    const g = geos[h];
    const s = setups[curSetup];
    if (!g || !s) return;
    // optional inspection hook (debug.js): may skip the draw or pick a
    // diagnostic output (see psEpilogue)
    let look = 0;
    if (KK.onDraw) {
      look = KK.onDraw({ setup: curSetup, geo: h, fvf: g.fvf, prim: g.prim, vcount, icount, target, tex: stageTex.slice(0, 4),
        z: [st(7, 1), st(14, 1), st(23, 4)], blend: [st(27, 0), st(19, 2), st(20, 1)], cull: st(22, 3), stencil: st(52, 0),
        vs: vsConst.slice(0, 32), ps: psConst.slice(0, 8) });
      if (look === false) return;
      look |= 0;
    }
    if (statesDirty) { applyStates(); statesDirty = false; }
    applySamplers(s.used);
    gl.useProgram(s.prog);
    const u = s.u;
    if (u.c) gl.uniform4fv(u.c, vsConst);
    if (u.cc) gl.uniform4fv(u.cc, psConst);
    const flip = target >= 0 ? -1 : 1;
    gl.uniform3f(u.clip, 1 / viewportX, -1 / viewportY, flip);
    for (let i = 0; i < 4; i++) {
      const th = stageTex[i];
      kindBuf[i] = th === -2 ? 2 : th === -3 ? 3 : (th >= 0 && textures[th] && th !== target) ? 0 : -1;
      projBuf[i] = (st(0x100 + (i << 5) + 24, 0) & 256) ? 1 : 0;
      const m = (k) => new Float32Array(new Uint32Array([st(0x100 + (i << 5) + k, 0)]).buffer)[0];
      bumpBuf.set([m(7), m(8), m(9), m(10)], i * 4);
    }
    gl.uniform4iv(u.kind, kindBuf);
    gl.uniform4iv(u.proj, projBuf);
    if (u.bump) gl.uniform4fv(u.bump, bumpBuf);
    const fc = st(34, 0);
    gl.uniform4f(u.fogcol, ((fc >> 16) & 255) / 255, ((fc >> 8) & 255) / 255, (fc & 255) / 255, 1);
    gl.uniform4i(u.misc, st(28, 0) ? 1 : 0, st(15, 0) ? st(25, 8) : 0, st(24, 0), look);
    gl.bindVertexArray(g.vao);
    const mode = gl[PRIM[g.prim & 7]];
    if ((g.prim & 7) === 6) {
      if (!g.quadIbo) {
        g.quadIbo = gl.createBuffer();
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.quadIbo);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, quadIBO, gl.STATIC_DRAW);
      } else gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.quadIbo);
      gl.drawElements(gl.TRIANGLES, (vcount >> 2) * 6, gl.UNSIGNED_SHORT, 0);
      if (g.ibo) gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ibo);
    } else if (icount) {
      gl.drawElements(mode, icount, g.itype, 0);
    } else if (vcount) {
      gl.drawArrays(mode, 0, vcount);
    }
    gl.bindVertexArray(null);
  };

})();

if (typeof self !== 'undefined') self.KK = KK;
