// Functions provided by the JavaScript host (kkworker.js) to the WebAssembly player.

#ifndef __WEBGL_HPP__
#define __WEBGL_HPP__

#include "_types.hpp"

#define JS(name) extern "C" __attribute__((import_module("env"),import_name(#name)))

JS(js_log)            void js_log(const sChar *s,sInt len);
JS(js_abort)          void js_abort(const sChar *s,sInt len);
JS(js_exit)           void js_exit();
JS(js_progress)       void js_progress(sInt done,sInt max);

JS(js_font_begin)     sInt js_font_begin(sInt pagex,sInt pagey,const sChar *name,sInt namelen,sInt xs,sInt ys,sInt style,sU32 *mem);
JS(js_font_width)     sInt js_font_width(const sChar *s,sInt len);
JS(js_font_charwidth) void js_font_charwidth(sInt ch,sInt *widths);
JS(js_font_print)     void js_font_print(sInt x,sInt y,const sChar *s,sInt len);
JS(js_font_end)       void js_font_end();

JS(gl_texture_create) void gl_texture_create(sInt handle,sInt xs,sInt ys,sInt format,sInt levels,sInt rendertarget);
JS(gl_texture_upload) void gl_texture_upload(sInt handle,sInt level,sInt xs,sInt ys,const sU8 *rgba);
JS(gl_texture_finish) void gl_texture_finish(sInt handle,sInt levels);
JS(gl_texture_delete) void gl_texture_delete(sInt handle);
JS(gl_texture_read)   void gl_texture_read(sInt handle,sInt xs,sInt ys,sU8 *rgba);
JS(gl_texture_bind)   void gl_texture_bind(sInt stage,sInt handle);

JS(gl_clear)          void gl_clear(sU32 flags,sU32 color);
JS(gl_viewport)       void gl_viewport(sInt target,sInt txs,sInt tys,sInt x,sInt y,sInt w,sInt h);
JS(gl_scissor)        void gl_scissor(sInt enable,sInt x,sInt y,sInt w,sInt h);
JS(gl_grab)           void gl_grab(sInt srcrt,sInt x0,sInt y0,sInt x1,sInt y1,sInt tex,sInt txs,sInt tys,sInt stretch);
JS(gl_state)          void gl_state(sU32 token,sU32 value);

JS(gl_geo_create)     void gl_geo_create(sInt handle,sInt fvf,sInt prim);
JS(gl_geo_delete)     void gl_geo_delete(sInt handle);
JS(gl_geo_upload)     void gl_geo_upload(sInt handle,sInt kind,const void *data,sInt bytes);
JS(gl_geo_draw)       void gl_geo_draw(sInt handle,sInt vcount,sInt icount);

JS(gl_setup_create)   sInt gl_setup_create(sInt id,const sU32 *vs,sInt vslen,const sU32 *ps,sInt pslen);
JS(gl_setup_delete)   void gl_setup_delete(sInt id);
JS(gl_setup_use)      void gl_setup_use(sInt id);
JS(gl_constants)      void gl_constants(sInt pixel,const sF32 *data,sInt count);

JS(js_sound_position) sInt js_sound_position();
JS(js_sample_add)     void js_sample_add(sInt handle,const sS16 *data,sInt samples,sInt is3d);
JS(js_sample_play)    sInt js_sample_play(sInt handle,sF32 left,sF32 right,sInt freq);
JS(js_sample_3d)      void js_sample_3d(sInt handle,sInt buffer,sF32 x,sF32 y,sF32 z,sF32 mindist,sF32 maxdist);
JS(js_listener)       void js_listener(sF32 x,sF32 y,sF32 z,sF32 fx,sF32 fy,sF32 fz,sF32 ux,sF32 uy,sF32 uz);

#undef JS

#endif
