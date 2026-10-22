// sSystem_ for the browser: the slice of the werkkzeug3 system layer that the
// kkrieger player uses, implemented on WebGL2 (via the JavaScript functions
// declared in webgl.hpp), plus input, timing and sound plumbing.
//
// The renderer-facing behaviour follows the Direct3D 9 implementation in
// werkkzeug3_kkrieger/_start.cpp: texture conversion and mip generation are
// the same code, render states arrive as D3D state tokens, and shaders as
// D3D vs_1_1/ps_1_x bytecode, which the JavaScript side translates to GLSL.

#include "_types.hpp"
#include "_start.hpp"
#include "_startdx.hpp"
#include "webgl.hpp"
#include <string.h>
#include <stdarg.h>

static sSystem_ WebSystem;
sSystem_ *sSystem = &WebSystem;
sInt sFatality;
sBroker_ *sBroker = 0;
sInt DebugTexMem;
sBool IntroStereo3D = sFALSE;

static sInt FrameTime;                    // ms, set every frame by the host

/****************************************************************************/
/***                                                                      ***/
/***   Startup                                                            ***/
/***                                                                      ***/
/****************************************************************************/

void WebSystemInit(sInt xs,sInt ys)
{
  sSystem_ *s = sSystem;
  sSetMem(s,0,sizeof(sSystem_));
  s->WScreenCount = 1;
  s->Screen[0].XSize = xs;
  s->Screen[0].YSize = ys;
  s->ConfigX = xs;
  s->ConfigY = ys;
  s->ViewportX = xs;
  s->ViewportY = ys;
  s->CpuMask = sCPU_CMOVE|sCPU_RDTSC|sCPU_MMX|sCPU_MMX2|sCPU_SSE|sCPU_SSE2;
  s->GpuMask = sGPU_TWOSIDESTENCIL;
  for(sInt i=0;i<MAX_GEOBUFFER;i++)
    s->GeoBuffer[i].Init();
  s->MtrlClearCaches();
  s->CurrentTarget = -100;
  s->CurrentViewport.Init();
}

void WebSystemResize(sInt xs,sInt ys)
{
  sSystem->Screen[0].XSize = xs;
  sSystem->Screen[0].YSize = ys;
  sSystem->ConfigX = xs;
  sSystem->ConfigY = ys;
}

void WebSystemSetTime(sInt ms)
{
  FrameTime = ms;
}

/****************************************************************************/
/***                                                                      ***/
/***   General                                                            ***/
/***                                                                      ***/
/****************************************************************************/

void sSystem_::Log(sChar *s)
{
  js_log(s,(sInt) strlen(s));
}

void sSystem_::Abort(sChar *msg)
{
  js_abort(msg ? msg : "abort",msg ? (sInt) strlen(msg) : 5);
  __builtin_trap();
}

void sSystem_::Exit()
{
  js_exit();
}

void sSystem_::Tag()
{
}

sChar *sSystem_::GetCmdLine()
{
  static sChar empty[1] = { 0 };
  return empty;
}

void sSystem_::Progress(sInt done,sInt max)
{
  js_progress(done,max);
}

void sSystem_::WaitForKey()
{
}

sInt sSystem_::GetTime()
{
  return FrameTime;
}

void sSystem_::InitScreens()
{
}

sBool sSystem_::GetScreenInfo(sInt i,sScreenInfo &info)
{
  info.XSize = Screen[0].XSize;
  info.YSize = Screen[0].YSize;
  info.FullScreen = 0;
  info.LowQuality = 0;
  info.ShaderLevel = sPS_11;
  info.PixelRatio = 1.0f*Screen[0].XSize/Screen[0].YSize;
  return i==0;
}

sU8 *sSystem_::LoadFile(const sChar *name,sInt &size)
{
  size = 0;
  return 0;
}

sU8 *sSystem_::LoadFile(const sChar *name)
{
  sInt size;
  return LoadFile(name,size);
}

sChar *sSystem_::LoadText(const sChar *name)
{
  return 0;
}

sBool sSystem_::SaveFile(const sChar *name,const sU8 *data,sInt size)
{
  return sFALSE;
}

sBool sSystem_::LoadBitmapCore(const sU8 *data,sInt size,sInt &x,sInt &y,sU8 *&d)
{
  return sFALSE;
}

void sSystem_::SetWinMouse(sInt x,sInt y)
{
}

/****************************************************************************/
/***                                                                      ***/
/***   Input                                                              ***/
/***                                                                      ***/
/****************************************************************************/

static sInt MouseX,MouseY,MouseZ;
static sU32 MouseButtonState;

void WebSystemMouse(sInt dx,sInt dy,sInt dz,sU32 buttons)
{
  MouseX += dx;
  MouseY += dy;
  MouseZ += dz;
  MouseButtonState = buttons;
}

void sSystem_::GetInput(sInt id,sInputData &data)
{
  sSetMem(&data,0,sizeof(data));
  if(id==0)
  {
    data.Type = sIDT_MOUSE;
    data.AnalogCount = 3;
    data.DigitalCount = 3;
    data.Analog[0] = MouseX;
    data.Analog[1] = MouseY;
    data.Analog[2] = MouseZ;
    data.Digital = MouseButtonState;
  }
}

/****************************************************************************/
/***                                                                      ***/
/***   Fonts (canvas 2D on the JavaScript side)                           ***/
/***                                                                      ***/
/****************************************************************************/

sInt sSystem_::FontBegin(sInt pagex,sInt pagey,const sChar *name,sInt xs,sInt ys,sInt style)
{
  // the text is drawn on a canvas and copied into FontMem (32-bit, blue in the low byte) after each print
  FontMem = new sU32[pagex*pagey];
  sSetMem(FontMem,0,pagex*pagey*4);
  return js_font_begin(pagex,pagey,name,(sInt) strlen(name),xs,ys,style,FontMem);
}

sInt sSystem_::FontWidth(const sChar *string,sInt count)
{
  if(count==-1) count = (sInt) strlen(string);
  return js_font_width(string,count);
}

void sSystem_::FontCharWidth(sInt ch,sInt *widths)
{
  js_font_charwidth(ch,widths);
}

void sSystem_::FontPrint(sInt x,sInt y,const sChar *string,sInt count)
{
  if(count==-1) count = (sInt) strlen(string);
  js_font_print(x,y,string,count);
}

void sSystem_::FontEnd()
{
  js_font_end();
  delete[] FontMem;
  FontMem = 0;
}

/****************************************************************************/
/***                                                                      ***/
/***   Textures                                                           ***/
/***                                                                      ***/
/****************************************************************************/

static sInt FindFreeTexture()
{
  for(sInt i=0;i<MAX_TEXTURE;i++)
    if(sSystem->Textures[i].Flags==0)
      return i;
  return sINVALID;
}

sInt sSystem_::AddTexture(const sTexInfo &ti)
{
  sInt i = FindFreeTexture();
  if(i==sINVALID) return sINVALID;
  sHardTex *tex = &Textures[i];
  tex->RefCount = 1;
  tex->XSize = ti.XSize;
  tex->YSize = ti.YSize;
  tex->Flags = ti.Flags|sTIF_ALLOCATED;
  tex->Format = sTF_A8R8G8B8;
  tex->MipLevels = 1;
  if(tex->Flags & sTIF_RENDERTARGET)
  {
    if(tex->XSize==0)
    {
      tex->XSize = Screen[0].XSize;
      tex->YSize = Screen[0].YSize;
    }
    gl_texture_create(i,tex->XSize,tex->YSize,sTF_A8R8G8B8,1,1);
  }
  else
  {
    gl_texture_create(i,tex->XSize,tex->YSize,sTF_A8R8G8B8,1,0);
    UpdateTexture(i,ti.Bitmap);
  }
  return i;
}

sInt sSystem_::AddTexture(sInt xs,sInt ys,sInt format,sU16 *data,sInt mipcount,sInt miptresh)
{
  sInt i = FindFreeTexture();
  if(i==sINVALID) return sINVALID;
  sHardTex *tex = &Textures[i];

  mipcount = sMin(mipcount,sGetPower2(xs));
  mipcount = sMin(mipcount,sGetPower2(ys));
  if(xs & (xs-1) || ys & (ys-1))
    mipcount = 1;

  tex->RefCount = 1;
  tex->XSize = xs;
  tex->YSize = ys;
  tex->Flags = sTIF_ALLOCATED;
  tex->Format = format;
  tex->MipLevels = mipcount;

  if(data==0)
  {
    if(tex->XSize==0)
    {
      tex->XSize = Screen[0].XSize;
      tex->YSize = Screen[0].YSize;
    }
    tex->Flags |= sTIF_RENDERTARGET;
    gl_texture_create(i,tex->XSize,tex->YSize,format,1,1);
  }
  else
  {
    // mipcount 0 means a full chain
    sInt levels = 1;
    for(sInt x=xs,y=ys;x>1 && y>1;x>>=1,y>>=1) levels++;
    if(mipcount) levels = sMin(levels,mipcount);
    tex->MipLevels = levels;
    gl_texture_create(i,xs,ys,format,levels,0);
    UpdateTexture(i,data,miptresh);
  }
  return i;
}

void sSystem_::AddRefTexture(sInt handle)
{
  if(handle>=0 && handle<MAX_TEXTURE)
    Textures[handle].RefCount++;
}

void sSystem_::RemTexture(sInt handle)
{
  if(handle<0 || handle>=MAX_TEXTURE) return;
  sHardTex *tex = &Textures[handle];
  if(tex->Flags==0) return;
  if(--tex->RefCount > 0) return;
  gl_texture_delete(handle);
  tex->Flags = 0;
  for(sInt i=0;i<MAX_TEXSTAGE;i++)
    if(CurrentTextures[i]==handle)
      CurrentTextures[i] = sINVALID;
}

void sSystem_::GetTextureSize(sInt handle,sInt &w,sInt &h)
{
  w = Textures[handle].XSize;
  h = Textures[handle].YSize;
}

void sSystem_::UpdateTexture(sInt handle,sBitmap *bm)
{
  sHardTex *tex = &Textures[handle];
  // sBitmap is 32-bit ARGB, top row first
  sU32 *src = bm->Data;
  sInt n = tex->XSize*tex->YSize;
  sU8 *rgba = new sU8[n*4];
  for(sInt i=0;i<n;i++)
  {
    sU32 c = src[i];
    rgba[i*4+0] = c>>16;
    rgba[i*4+1] = c>>8;
    rgba[i*4+2] = c;
    rgba[i*4+3] = c>>24;
  }
  gl_texture_upload(handle,0,tex->XSize,tex->YSize,rgba);
  delete[] rgba;
}

// Same conversion and mip chain as the Direct3D version (_start.cpp):
// 15 bits per channel down to 8, Q8W8V8U8 as signed, box-filtered mips from
// level miptresh on (or the other way round with mipdir).
void sSystem_::UpdateTexture(sInt handle,sU16 *source,sInt miptresh)
{
  sHardTex *tex = &Textures[handle];
  if(tex->Flags & sTIF_RENDERTARGET)
    return;

  sInt mipdir = miptresh & 16;
  miptresh &= 15;
  sInt xs = tex->XSize;
  sInt ys = tex->YSize;
  sInt oxs = xs;
  sInt level = 0;

  sU32 *data = new sU32[xs*ys*2];
  sCopyMem(data,source,xs*ys*8);
  sU8 *out = new sU8[xs*ys*4];

  for(;;)
  {
    sU32 *s = data;
    sU8 *d = out;
    for(sInt y=0;y<ys;y++)
    {
      sU16 *s16 = (sU16 *)s;
      if(tex->Format==sTF_Q8W8V8U8)
      {
        for(sInt x=0;x<xs;x++)
        {
          d[0] = (((sInt)s16[2]-0x4000)>>7);   // U
          d[1] = (((sInt)s16[1]-0x4000)>>7);   // V
          d[2] = (((sInt)s16[0]-0x4000)>>7);   // W
          d[3] = (((sInt)s16[3]-0x4000)>>7);   // Q
          d += 4;
          s16 += 4;
        }
      }
      else
      {
        for(sInt x=0;x<xs;x++)
        {
          d[0] = s16[2]>>7;                    // R (source is B,G,R,A)
          d[1] = s16[1]>>7;
          d[2] = s16[0]>>7;
          d[3] = s16[3]>>7;
          d += 4;
          s16 += 4;
        }
      }
      s += oxs*2;
    }
    gl_texture_upload(handle,level,xs,ys,out);

    if(xs<=1 || ys<=1 || (tex->MipLevels && level+1>=tex->MipLevels))
      break;

    sInt filter = (miptresh <= level);
    if(mipdir) filter = !filter;

    s = data;
    sU32 *dd = data;
    for(sInt y=0;y<ys/2;y++)
    {
      sU16 *s16 = (sU16 *) s;
      sU16 *d16 = (sU16 *) dd;
      s += oxs*4;
      dd += oxs*2;
      for(sInt x=0;x<xs/2;x++)
      {
        if(filter)
        {
          d16[0] = (s16[0]+s16[4]+s16[oxs*4+0]+s16[oxs*4+4]+2)>>2;
          d16[1] = (s16[1]+s16[5]+s16[oxs*4+1]+s16[oxs*4+5]+2)>>2;
          d16[2] = (s16[2]+s16[6]+s16[oxs*4+2]+s16[oxs*4+6]+2)>>2;
          d16[3] = (s16[3]+s16[7]+s16[oxs*4+3]+s16[oxs*4+7]+2)>>2;
        }
        else
        {
          d16[0] = s16[0];
          d16[1] = s16[1];
          d16[2] = s16[2];
          d16[3] = s16[3];
        }
        d16 += 4;
        s16 += 8;
      }
    }
    xs >>= 1;
    ys >>= 1;
    level++;
  }
  gl_texture_finish(handle,level+1);
  delete[] out;
  delete[] data;
}

void sSystem_::ReadTexture(sInt handle,sU16 *dest)
{
  sHardTex *tex = &Textures[handle];
  sInt n = tex->XSize*tex->YSize;
  sU8 *rgba = new sU8[n*4];
  gl_texture_read(handle,tex->XSize,tex->YSize,rgba);
  for(sInt i=0;i<n;i++)
  {
    dest[i*4+0] = rgba[i*4+2]<<7;
    dest[i*4+1] = rgba[i*4+1]<<7;
    dest[i*4+2] = rgba[i*4+0]<<7;
    dest[i*4+3] = rgba[i*4+3]<<7;
  }
  delete[] rgba;
}

/****************************************************************************/
/***                                                                      ***/
/***   Viewport and transforms                                            ***/
/***                                                                      ***/
/****************************************************************************/

void sSystem_::Clear(sU32 flags,sU32 color)
{
  if(flags & (sVCF_COLOR|sVCF_Z|sVCF_STENCIL))
    gl_clear(flags,color);
}

void sSystem_::SetViewport(const sViewport &vp)
{
  sInt txs,tys,tgtid;

  CurrentViewport = vp;
  if(vp.RenderTarget == sINVALID)
  {
    txs = Screen[0].XSize;
    tys = Screen[0].YSize;
    tgtid = -1;
  }
  else
  {
    txs = Textures[vp.RenderTarget].XSize;
    tys = Textures[vp.RenderTarget].YSize;
    tgtid = vp.RenderTarget;
  }
  if(!vp.Window.XSize() || !vp.Window.YSize())
  {
    CurrentViewport.Window.x0 = 0;
    CurrentViewport.Window.y0 = 0;
    CurrentViewport.Window.x1 = txs;
    CurrentViewport.Window.y1 = tys;
  }
  ViewportX = CurrentViewport.Window.XSize();
  ViewportY = CurrentViewport.Window.YSize();
  CurrentTarget = tgtid;
  gl_viewport(tgtid,txs,tys,CurrentViewport.Window.x0,CurrentViewport.Window.y0,ViewportX,ViewportY);
}

void sSystem_::SetScissor(const sFRect *scissor)
{
  if(!scissor)
    gl_scissor(0,0,0,0,0);
  else
  {
    sInt x0 = CurrentViewport.Window.x0 + (CurrentViewport.Window.x1 - CurrentViewport.Window.x0) * (1.0f + scissor->x0) * 0.5f;
    sInt x1 = CurrentViewport.Window.x0 + (CurrentViewport.Window.x1 - CurrentViewport.Window.x0) * (1.0f + scissor->x1) * 0.5f;
    sInt y0 = CurrentViewport.Window.y0 + (CurrentViewport.Window.y1 - CurrentViewport.Window.y0) * (1.0f - scissor->y1) * 0.5f;
    sInt y1 = CurrentViewport.Window.y0 + (CurrentViewport.Window.y1 - CurrentViewport.Window.y0) * (1.0f - scissor->y0) * 0.5f;
    gl_scissor(1,x0,y0,x1-x0,y1-y0);
  }
}

void sSystem_::GrabScreen(sInt sourceRT,const sRect &win,sInt tex,sBool stretch)
{
  gl_grab(sourceRT,win.x0,win.y0,win.x1,win.y1,tex,Textures[tex].XSize,Textures[tex].YSize,stretch);
}

void sSystem_::GetTransform(sInt mode,sMatrix &mat)
{
  switch(mode)
  {
  case sGT_VIEW:
    mat = LastCamera;
    break;
  case sGT_MODELVIEW:
    mat = LastMatrix;
    mat.TransR();
    mat.MulA(mat,LastCamera);
    break;
  case sGT_MODEL:
    mat = LastMatrix;
    break;
  case sGT_PROJECT:
    mat = LastProjection;
    break;
  default:
    mat.Init();
    break;
  }
}

void sSystem_::SetViewProject(const sMaterialEnv *env)
{
  env->MakeProjectionMatrix(LastProjection);
  LastCamera = env->CameraSpace;
  LastCamera.TransR();
  LastViewProject.Mul4(LastCamera,LastProjection);
}

/****************************************************************************/
/***                                                                      ***/
/***   Render states                                                      ***/
/***                                                                      ***/
/****************************************************************************/

void sSystem_::SetStates(sU32 *stream)
{
  while(*stream!=0xffffffff)
  {
    SetState(stream[0],stream[1]);
    stream+=2;
  }
}

void sSystem_::SetState(sU32 token,sU32 value)
{
  if(token>=0x310) return;
  if(CurrentStates[token] == value)
    return;
  CurrentStates[token] = value;
  gl_state(token,value);
}

/****************************************************************************/
/***                                                                      ***/
/***   Geometry                                                           ***/
/***                                                                      ***/
/****************************************************************************/

// Each handle keeps its vertices and indices in its own GL buffers. Geometry
// stays valid until the handle is flushed or rewritten, like the original's
// static buffers; dynamic handles are re-requested after GeoFlush.

static const sInt FVFSize[] = { 0,32,32,64,16,16,32,48 };

struct WebGeo
{
  sU8 *VB;
  sInt VBAlloc;
  sU8 *IB;
  sInt IBAlloc;
  sInt Valid;                     // sGEO_VERTEX|sGEO_INDEX uploaded
};
static WebGeo Geo[MAX_GEOHANDLE];

void sGeoBuffer::Init()   { Type = 0; Size = 0; Used = 0; VB = 0; UserCount = 0; }
sBool sGeoBuffer::Alloc(sInt count,sInt size,sInt &firstp,sInt type) { return sFALSE; }
void sGeoBuffer::Free()   { }

sInt sSystem_::GeoAdd(sInt fvf,sInt prim)
{
  for(sInt i=1;i<MAX_GEOHANDLE;i++)
  {
    sGeoHandle *gh = &GeoHandle[i];
    if(gh->Mode==0)
    {
      sSetMem(gh,0,sizeof(*gh));
      gh->VertexSize = FVFSize[fvf];
      gh->FVF = fvf;
      gh->Mode = prim;
      Geo[i].Valid = 0;
      gl_geo_create(i,fvf,prim);
      return i;
    }
  }
  sFatal("GeoAdd() ran out of handles");
  return 0;
}

void sSystem_::GeoRem(sInt handle)
{
  GeoHandle[handle].Mode = 0;
  delete[] Geo[handle].VB;
  delete[] Geo[handle].IB;
  sSetMem(&Geo[handle],0,sizeof(WebGeo));
  gl_geo_delete(handle);
}

void sSystem_::GeoFlush()
{
  for(sInt i=1;i<MAX_GEOHANDLE;i++)
  {
    GeoHandle[i].Vertex.Count = 0;
    Geo[i].Valid = 0;
  }
}

// Start of a frame. The original keeps dynamic geometry in ring buffers that
// are discarded whenever they fill up, which in practice happens every frame
// or two, so the engine never sees dynamic data from an earlier frame as
// still loaded. Some paths depend on that: an animated mesh whose shadow
// volume had no silhouette for its last light keeps its vertex data marked as
// loaded, and without a discard the next frame would extrude its shadow from
// the previous pose (the light inside a monster then leaks out every other
// frame). Discarding all dynamic data here matches the original's behaviour.
void WebGeoNewFrame()
{
  for(sInt i=1;i<MAX_GEOHANDLE;i++)
  {
    sInt mode = sSystem->GeoHandle[i].Mode;
    if(!mode)
      continue;
    if(!(mode & sGEO_STATVB))
      Geo[i].Valid &= ~sGEO_VERTEX;
    if(!(mode & sGEO_STATIB))
      Geo[i].Valid &= ~sGEO_INDEX;
  }
}

void sSystem_::GeoFlush(sInt handle,sInt what)
{
  Geo[handle].Valid &= ~what;
}

sInt sSystem_::GeoDraw(sInt &handle)
{
  sGeoHandle *gh = &GeoHandle[handle];
  sInt update = 0;
  if(gh->Vertex.Count == 0 || !(Geo[handle].Valid & sGEO_VERTEX))
    update |= sGEO_VERTEX | sGEO_INDEX;
  if(gh->Index.Count && !(Geo[handle].Valid & sGEO_INDEX))
    update |= sGEO_INDEX;
  if(update)
    return update;

  gl_geo_draw(handle,gh->Vertex.Count,gh->Index.Count);
  return 0;
}

void sSystem_::GeoBegin(sInt handle,sInt vc,sInt ic,sF32 **fp,void **ip,sInt upd)
{
  sGeoHandle *gh = &GeoHandle[handle];
  WebGeo *g = &Geo[handle];
  if(fp) *fp = 0;
  if(ip) *ip = 0;

  if(upd & sGEO_VERTEX)
  {
    gh->Vertex.Count = vc;
    sInt bytes = vc*gh->VertexSize;
    if(bytes > g->VBAlloc)
    {
      delete[] g->VB;
      g->VBAlloc = bytes + bytes/4 + 64;
      g->VB = new sU8[g->VBAlloc];
    }
    if(fp) *fp = (sF32 *) g->VB;
    gh->Locked |= sGEO_VERTEX;
  }
  if(upd & sGEO_INDEX)
  {
    gh->Index.Count = ic;
    if(ic)
    {
      sInt bytes = ic * ((gh->Mode & sGEO_IND32B) ? 4 : 2);
      if(bytes > g->IBAlloc)
      {
        delete[] g->IB;
        g->IBAlloc = bytes + bytes/4 + 64;
        g->IB = new sU8[g->IBAlloc];
      }
      if(ip) *ip = g->IB;
      gh->Locked |= sGEO_INDEX;
    }
  }
}

void sSystem_::GeoEnd(sInt handle,sInt vc,sInt ic)
{
  sGeoHandle *gh = &GeoHandle[handle];
  WebGeo *g = &Geo[handle];
  if(vc!=-1) gh->Vertex.Count = vc;
  if(ic!=-1) gh->Index.Count = ic;
  if(gh->Locked & sGEO_VERTEX)
  {
    gl_geo_upload(handle,0,g->VB,gh->Vertex.Count*gh->VertexSize);
    g->Valid |= sGEO_VERTEX;
  }
  if(gh->Locked & sGEO_INDEX)
  {
    gl_geo_upload(handle,(gh->Mode & sGEO_IND32B) ? 2 : 1,g->IB,gh->Index.Count*((gh->Mode & sGEO_IND32B) ? 4 : 2));
    g->Valid |= sGEO_INDEX;
  }
  if(!gh->Index.Count)
    g->Valid |= sGEO_INDEX;
  gh->Locked = 0;
}

/****************************************************************************/
/***                                                                      ***/
/***   Materials                                                          ***/
/***                                                                      ***/
/****************************************************************************/

static sInt ShaderLength(const sU32 *code)
{
  if(!code) return 0;
  sInt n = 1;
  while(code[n-1] != 0x0000ffff && n < 0x10000) n++;
  return n;
}

sInt sSystem_::MtrlAddSetup(const sU32 *states,const sU32 *vs,const sU32 *ps)
{
  sInt stateLen;
  for(stateLen=0;states[stateLen] != ~0U;stateLen+=2);
  stateLen += 2;

  sInt id;
  for(id=0;id<MAX_SETUPS;id++)
    if(!Setups[id].RefCount)
      break;
  if(id==MAX_SETUPS)
    return sINVALID;

  sMaterialSetup *setup = &Setups[id];
  setup->RefCount = 1;
  setup->States = new sU32[stateLen];
  sCopyMem(setup->States,states,stateLen*4);
  setup->VSCode = setup->PSCode = 0;
  setup->VS = 0;
  setup->PS = 0;
  if(!gl_setup_create(id,vs,ShaderLength(vs),ps,ShaderLength(ps)))
  {
    delete[] setup->States;
    setup->States = 0;
    setup->RefCount = 0;
    return sINVALID;
  }
  return id;
}

void sMaterialSetup::Cleanup()
{
  delete[] States;
  States = 0;
  gl_setup_delete(this - sSystem->Setups);
}

void sSystem_::MtrlAddRefSetup(sInt sid)
{
  Setups[sid].AddRef();
}

void sSystem_::MtrlRemSetup(sInt sid)
{
  if(sid>=0 && sid<MAX_SETUPS && Setups[sid].RefCount)
    Setups[sid].Release();
}

void sSystem_::MtrlClearCaches()
{
  CurrentSetupId = sINVALID;
  for(sInt i=0;i<MAX_TEXSTAGE;i++)
    CurrentTextures[i] = sINVALID;
  for(sInt i=0;i<0x310;i++)
    CurrentStates[i] = 0xfefefefe;
}

void sSystem_::MtrlSetSetup(sInt sid)
{
  if(sid == CurrentSetupId || sid < 0)
    return;
  sMaterialSetup *setup = &Setups[sid];
  if(!setup->RefCount)
    return;
  if(CurrentSetupId != sINVALID && Setups[CurrentSetupId].RefCount)
    Setups[CurrentSetupId].Release();
  setup->AddRef();
  for(sU32 *st = setup->States;st[0] != ~0U;st += 2)
    SetState(st[0],st[1]);
  gl_setup_use(sid);
  CurrentSetupId = sid;
}

void sSystem_::MtrlSetInstance(const sMaterialInstance &inst)
{
  for(sInt i=0;i<inst.NumTextures;i++)
  {
    sInt texIndex = inst.Textures[i];
    if(texIndex != CurrentTextures[i])
    {
      gl_texture_bind(i,texIndex);
      CurrentTextures[i] = texIndex;
    }
  }
  if(inst.NumVSConstants)
    gl_constants(0,(const sF32 *) inst.VSConstants,inst.NumVSConstants);
  if(inst.NumPSConstants)
    gl_constants(1,(const sF32 *) inst.PSConstants,inst.NumPSConstants);
}

/****************************************************************************/
/***                                                                      ***/
/***   Sound                                                              ***/
/***                                                                      ***/
/****************************************************************************/

// The music handler is called from the host's audio pump (WebSystemRenderSound);
// samples are kept in wasm memory and mixed on the JavaScript side.

static sSoundHandler SoundHandler;
static sInt SoundAlign;
static void *SoundUser;
static sInt SoundSamples;

void sSystem_::SetSoundHandler(sSoundHandler hnd,sInt align,void *user)
{
  SoundHandler = hnd;
  SoundAlign = align ? align : 1;
  SoundUser = user;
  SoundSamples = 0;
}

sInt sSystem_::GetCurrentSample()
{
  return js_sound_position();
}

// called by the host with a buffer of stereo 16-bit samples to fill
extern "C" void kk_render_sound(sS16 *buffer,sInt samples)
{
  if(SoundHandler)
  {
    SoundHandler(buffer,samples,SoundUser);
    SoundSamples += samples;
  }
  else
    sSetMem(buffer,0,samples*4);
}

sInt sSystem_::SampleAdd(sS16 *data,sInt size,sInt buffers,sInt usehandle,sBool is3d)
{
  sInt h = usehandle;
  if(h < 0)
  {
    for(h=0;h<sMAXSAMPLEHANDLE;h++)
      if(!Sample[h].Count)
        break;
    if(h==sMAXSAMPLEHANDLE) return -1;
  }
  Sample[h].Count = buffers;
  Sample[h].Is3D = is3d;
  Sample[h].LenMs = size*1000/44100;
  js_sample_add(h,data,size,is3d);
  return h;
}

void sSystem_::SampleRem(sInt handle)
{
  Sample[handle].Count = 0;
}

void sSystem_::SampleRemAll()
{
  for(sInt i=0;i<sMAXSAMPLEHANDLE;i++)
    Sample[i].Count = 0;
}

// DirectSound as the original sets it up: the volume in hundredths of a
// decibel is 2000*ln(volume) (the natural logarithm, so quieter than linear),
// the pan attenuates the other side by pan decibels; 3D samples have no pan.
sInt sSystem_::SamplePlay(sInt handle,sF32 volume,sF32 pan,sInt freq)
{
  sInt d3dvol,d3dpan;
  sF32 gain,left,right;

  if(handle<0 || !Sample[handle].Count) return -1;
  d3dvol = sMax<sInt>(sFLog(sRange(volume,1.0f,1e-20f))*20*100,-10000);
  d3dpan = Sample[handle].Is3D ? 0 : sRange<sInt>(pan*100,10000,-10000);
  gain = sFPow(10.0f,d3dvol/2000.0f);
  left = gain;
  right = gain;
  if(d3dpan>0) left *= sFPow(10.0f,-d3dpan/2000.0f);
  if(d3dpan<0) right *= sFPow(10.0f,d3dpan/2000.0f);
  return js_sample_play(handle,left,right,freq);
}

void sSystem_::Sample3DParam(sInt handle,sInt buffer,const sVector &pos,const sVector &vel,sF32 minDist,sF32 maxDist)
{
  js_sample_3d(handle,buffer,pos.x,pos.y,pos.z,minDist,maxDist);
}

void sSystem_::Sample3DListener(const sVector &pos,const sVector &vel,const sVector &up,const sVector &fwd,sF32 doppler,sF32 rolloff)
{
  js_listener(pos.x,pos.y,pos.z,fwd.x,fwd.y,fwd.z,up.x,up.y,up.z);
}

void sSystem_::Sample3DCommit()
{
}

/****************************************************************************/
/***                                                                      ***/
/***   Helpers from _start.cpp                                            ***/
/***                                                                      ***/
/****************************************************************************/

void sTexInfo::Init(sBitmap *bm,sInt format,sU32 flags)
{
  XSize = bm->XSize;
  YSize = bm->YSize;
  Bitmap = bm;
  Flags = flags;
  Format = format;
}

void sTexInfo::InitRT(sInt xs,sInt ys)
{
  XSize = xs;
  YSize = ys;
  Flags = sTIF_RENDERTARGET;
  Format = 0;
}

void sMaterialEnv::Init()
{
  sSetMem(this,0,sizeof(*this));
  ModelSpace.Init();
  CameraSpace.Init();
  NearClip = 0.125f;
  FarClip = 4096.0f;
  ZoomX = 1.0f;
  ZoomY = 1.0f;
  CenterX = 0.0f;
  CenterY = 0.0f;
  FogStart = 0;
  FogEnd = FarClip;
  FogColor = 0xff808080;
}

void sMaterialEnv::MakeProjectionMatrix(sMatrix &pmat) const
{
  sF32 q;
  sF32 shiftX = 1.0f / sSystem->ViewportX;
  sF32 shiftY = 1.0f / sSystem->ViewportY;

  switch(Orthogonal)
  {
  case sMEO_PIXELS:
    pmat.i.Init(2.0f/sSystem->ViewportX,0                       ,0 ,0);
    pmat.j.Init(0                      ,-2.0f/sSystem->ViewportY,0 ,0);
    pmat.k.Init(0                      ,0                       ,1 ,0);
    pmat.l.Init(-1 - (2*CenterX+1)*shiftX,1 + (2*CenterY+1)*shiftY,0 ,1);
    break;

  case sMEO_NORMALISED:
    pmat.i.Init(ZoomX                  ,0                       ,0 ,0);
    pmat.j.Init(0                      ,ZoomY                   ,0 ,0);
    pmat.k.Init(0                      ,0                       ,1.0f/FarClip,0);
    pmat.l.Init(-shiftX                ,shiftY                  ,0 ,1);
    break;

  case sMEO_PERSPECTIVE:
    q = 1.0f;
    pmat.i.Init(ZoomX         ,0               , 0              ,0);
    pmat.j.Init(0             ,ZoomY           , 0              ,0);
    pmat.k.Init(CenterX       ,CenterY         , q              ,1);
    pmat.l.Init(-shiftX       ,shiftY          ,-q*NearClip     ,0);
    break;
  }
}

sMaterial::~sMaterial()
{
}

sMaterialInstance sMaterialInstance::Null = { 0 };

void sViewport::Init(sInt screen)
{
  Screen = screen;
  RenderTarget = -1;
  Window.Init(0,0,sSystem->Screen[screen].XSize,sSystem->Screen[screen].YSize);
}

void sViewport::InitTex(sInt handle)
{
  Screen = -1;
  RenderTarget = handle;
  Window.x0 = 0;
  Window.y0 = 0;
  sSystem->GetTextureSize(handle,Window.x1,Window.y1);
}

/****************************************************************************/

void __cdecl sFatal(const sChar *format,...)
{
  static sChar buffer[1024];
  va_list ap;
  va_start(ap,format);
  sFormatString(buffer,sizeof(buffer),format,&ap);
  va_end(ap);
  sSystem->Abort(buffer);
}
