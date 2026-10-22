// This file is distributed under a BSD license. See LICENSE.txt for details.
//
// The kkrieger player main loop (player_kkrieger/kkrieger_main.cpp), driven by
// the browser host: kk_init loads pno0001.exe and precalculates, kk_frame runs
// one frame (the original sAPPCODE_PAINT), kk_key/kk_mouse deliver input.

#include "_types.hpp"
#include "_start.hpp"
#include "_viruz2.hpp"
#include "kdoc.hpp"
#include "kkriegergame.hpp"
#include "engine.hpp"
#include "genoverlay.hpp"
#include "kkload.hpp"
#include "webgl.hpp"

extern sBool KKriegerRelease2004;         // kkriegergame.cpp

/****************************************************************************/

static CV2MPlayer *Sound;
static sInt SoundTimer;
KDoc *Document;
KEnvironment *Environment;
KKriegerGame *Game;
sF32 GlobalFps;



/****************************************************************************/

/****************************************************************************/

struct VFXEntry
{
  sU8 MajorId;
  sU8 MinorId;
  sS8 Gain;
  sU8 reserved;
  sU32 StartPos;
  sU32 EndPos;
  sU32 LoopSize;
};

static void RenderSoundEffects(KDoc *doc,sU8 *data)
{
  const sInt maxsamples = 0x10000;
  static sF32 smpf[maxsamples*2];
  static sS16 *smps = (sS16 *) smpf;
  sU32 *data32,size;
  sU8 *v2m;
  sInt i,j,count,start,len,fade;
  sF32 amp,ampx;
  VFXEntry *ent;

  sVERIFY(Sound);

  data32 = (sU32 *) data;
  sVERIFY(data32[0] == sMAKE4('V','F','X','0'));
  size = data32[1];
  v2m = (sU8 *) &data32[2];
  data32 = (sU32 *) (((sU8 *)data32)+size+12);
  count = *data32++;
  ent = (VFXEntry *) data32;

  if(Sound->Open(v2m))
  {
    for(i=0;i<count;i++)
    {
      start = ent->StartPos;
      if(ent->LoopSize && !KKriegerRelease2004)
        start = ent->EndPos - ent->LoopSize;
      len = sMin<sInt>(ent->EndPos - start,maxsamples);
      ampx = sFPow(10.0f,(ent->Gain / 5.0f) / 20.0f);

      Sound->Play(ent->StartPos);
      Sound->Render(smpf,len);
      fade = 1000;
      if((ent->MajorId & 1) && !KKriegerRelease2004)
        fade = 44100;

      for(j=0;j<len;j++)
      {
        amp = ampx;
        if(j<fade)
          amp = amp*j/fade;
        if(j>len-fade)
          amp = amp*(len-j)/fade;

        smps[j*2+0] = sRange<sInt>(smpf[j*2+0]*amp*0x7fff,0x7fff,-0x7fff);
        smps[j*2+1] = sRange<sInt>(smpf[j*2+1]*amp*0x7fff,0x7fff,-0x7fff);
      }

      sSystem->SampleAdd(smps,len,4,ent->MinorId,!(ent->MajorId & 1) && !KKriegerRelease2004);   // the 2004 sound layer has no 3D sound
      sSystem->Progress(doc->Ops.Count+i,doc->Ops.Count+count);
      ent++;
    }

    Sound->Close();
  }
}

/****************************************************************************/

void IntroSoundHandler(sS16 *stream,sInt left,void *user)
{
  static sF32 buffer[4096*2];
  sF32 *fp;
  sInt count;
  sInt i;

  SoundTimer += left;
  if(Sound)
  {
    while(left>0)
    {
      count = sMin<sInt>(4096,left);
      Sound->Render(buffer,count);
      fp = buffer;
      for(i=0;i<count*2;i++)
        //*stream++ = 0;
        *stream++ = sRange<sInt>(*fp++*0x7fff,0x7fff,-0x7fff);
      left-=count;
    }
#pragma lekktor(off)
    if(SoundTimer>=(2*60+25)*44100)
    {
      Sound->Open(Document->SongData);
      Sound->Play(0);
      SoundTimer = 0;
    }
#pragma lekktor(on)
  }
  else
  {
    for(i=0;i<left*2;i++)
      *stream++ = 0;
    SoundTimer = 0;
  }
}



/****************************************************************************/
/***                                                                      ***/
/***   Browser entry points                                               ***/
/***                                                                      ***/
/****************************************************************************/

void WebSystemInit(sInt xs,sInt ys);
void WebSystemResize(sInt xs,sInt ys);
void WebSystemSetTime(sInt ms);
void WebGeoNewFrame();
void WebSystemMouse(sInt dx,sInt dy,sInt dz,sU32 buttons);

static sU8 *Image;
static sInt ImageSize;
static sInt FirstTime,ThisTime,LastTime;
static sF32 oldfps;
static sInt LastGameState = -1;

extern "C" void *kk_alloc(sInt size)
{
  return malloc(size);
}

extern "C" void kk_free(void *p)
{
  free(p);
}

// load the game from the original executable; returns 0 on success
extern "C" sInt kk_load(const sU8 *exe,sInt size,sInt xs,sInt ys)
{
  sU32 va;
  WebSystemInit(xs,ys);
  if(!KKUnpack(exe,size,Image,ImageSize,va))
    return 1;
  Document = new KDoc;
  sSetMem(Document,0,sizeof(KDoc));
  if(!KKLoad2004(Document,Image,ImageSize))
    return 2;
  return 0;
}

// load the later build exported from the published werkkzeug3 source
// (kkrieger3383.kx); returns 0 on success
extern "C" sInt kk_load_kx(const sU8 *kx,sInt size,sInt xs,sInt ys)
{
  WebSystemInit(xs,ys);
  // the export starts at the song size; the player's loader expects the flags
  // word the packer put in front of it (2: sound effects follow the song)
  sU8 *data = new sU8[size+4];
  *(sU32 *)data = 2;
  sCopyMem(data+4,kx,size);
  Document = new KDoc;
  sSetMem(Document,0,sizeof(KDoc));
  const sU8 *p = data;
  extern sBool KDocByteClassIds;
  KDocByteClassIds = sTRUE;
  Document->Init(p);
  if(!Document->Ops.Count)
    return 2;
  sChar buf[96];
  sSPrintF(buf,sizeof(buf),"kx: %d ops, %d splines, read %d of %d bytes",Document->Ops.Count,Document->Splines.Count,(sInt)(p-data),size+4);
  sSystem->Log(buf);
  return 0;
}

// generate everything (blocking; reports progress through sSystem->Progress)
extern "C" sInt kk_init(sInt timeMs)
{
  WebSystemSetTime(timeMs);
  Environment = new KEnvironment;
  Sound = 0;

  sInitPerlin();
  GenOverlayInit();

  Engine = new Engine_;

  Game = new KKriegerGame;
  Game->Init();

  sFloatFix();
  Environment->Splines = &Document->Splines.Array;
  Environment->SplineCount = Document->Splines.Count;
  Environment->Game = Game;
  Environment->InitView();
  Environment->InitFrame(0,0);
  Document->Precalc(Environment);
  Environment->ExitFrame();

  if(Document->SongSize)
  {
    Sound = new CV2MPlayer;

    if(Document->SampleSize)
      RenderSoundEffects(Document,Document->SampleData);

    Sound->Open(Document->SongData);
    Sound->Play(0);
    SoundTimer = 0;
    sSystem->SetSoundHandler(IntroSoundHandler,64);
  }

  Game->ResetRoot(Environment,Document->RootOps[Document->CurrentRoot],1);

  FirstTime = sSystem->GetTime();
  LastTime = 0;
  oldfps = 0.0f;
  return 0;
}

extern "C" void kk_resize(sInt xs,sInt ys)
{
  WebSystemResize(xs,ys);
}

extern "C" void kk_mouse(sInt dx,sInt dy,sInt dz,sU32 buttons)
{
  WebSystemMouse(dx,dy,dz,buttons);
}

// the active lights of the last frame (x,y,z,amplify,range,importance each);
// used by web/debug.js
extern sF32 EngDebugLights[64*6];
extern sInt EngDebugLightCount;
extern sInt EngDebugPaintCount;
extern "C" sInt kk_light_info(sF32 *out)
{
  sCopyMem(out,EngDebugLights,sizeof(EngDebugLights));
  return EngDebugLightCount;
}

// debug.js: player position and the moving collision cells (doors, hit
// boxes): count, then per cell mode, centre, blocked, monster, and the
// first particle of each registered particle group (position, kind)
extern "C" sInt kk_debug_coll(sF32 *out,sInt max)
{
  KKriegerParticle *p = &Game->Player.Body.Part[0];
  out[0] = p->Pos.x; out[1] = p->Pos.y; out[2] = p->Pos.z;
  sInt n = 0;
  for(sInt i=0;i<Game->DCellUsed && n<max;i++,n++)
  {
    KKriegerCellDynamic *c = Game->DCellPtr[i];
    sF32 *d = out+3+n*10;
    d[0] = c->Mode; d[1] = c->Center.x; d[2] = c->Center.y; d[3] = c->Center.z;
    d[4] = c->Blocked; d[5] = c->Monster ? 1 : 0;
    d[6] = c->Monster ? c->Monster->Life : 0;
    d[7] = c->Monster ? c->Monster->State : 0;
    d[8] = d[9] = 0;
  }
  for(sInt i=0;i<Game->PartUsed && n<max;i++,n++)
  {
    KKriegerParticle *q = Game->PartPtr[i];
    sF32 *d = out+3+n*10;
    d[0] = -1; d[1] = q->Pos.x; d[2] = q->Pos.y; d[3] = q->Pos.z;
    d[4] = q->Kind; d[5] = q->ShotEvent ? 1 : 0;
    d[6] = q->OldCollPos.x; d[7] = q->OldCollPos.y; d[8] = q->OldCollPos.z; d[9] = q->Cell ? 1 : 0;
  }
  return n;
}

// debug.js: planes of moving cell i (count, mode, then x,y,z,w per plane)
extern "C" sInt kk_debug_cell(sInt i,sF32 *out)
{
  if(i<0 || i>=Game->DCellUsed) return 0;
  KKriegerCellDynamic *c = Game->DCellPtr[i];
  out[0] = c->PlaneCount; out[1] = c->Mode;
  for(sInt k=0;k<c->PlaneCount;k++)
  {
    out[2+k*4] = c->Planes[k].x; out[3+k*4] = c->Planes[k].y;
    out[4+k*4] = c->Planes[k].z; out[5+k*4] = c->Planes[k].w;
  }
  return 1;
}

// debug.js: a ray through the level from the player's cell, as the game's
// probes cast it; returns 1 and the hit point in out when something is hit
extern "C" sInt kk_debug_ray(sF32 x0,sF32 y0,sF32 z0,sF32 x1,sF32 y1,sF32 z1,sF32 *out)
{
  sVector p0,p1,plane;
  p0.Init(x0,y0,z0,1);
  p1.Init(x1,y1,z1,1);
  plane.Init(0,0,0,0);
  KKriegerCellAdd *cell = Game->FindCell(p0);
  if(!cell) return -1;
  sBool hit = Game->FindFirstIntersect(p0,p1,cell,&plane);
  out[0] = p1.x; out[1] = p1.y; out[2] = p1.z;
  out[3] = plane.x; out[4] = plane.y; out[5] = plane.z;
  return hit;
}

// debug.js: the sweep a player particle would make from p0 to p1; returns
// MoveParticle's result and the end point in out
extern "C" sInt kk_debug_sweep(sF32 x0,sF32 y0,sF32 z0,sF32 x1,sF32 y1,sF32 z1,sF32 *out)
{
  static KKriegerParticle part;
  sVector p0,p1,zero;
  p0.Init(x0,y0,z0,1);
  p1.Init(x1,y1,z1,1);
  zero.Init(0,0,0,0);
  part.Init(p0,zero,Game->FindCell(p0));
  if(!part.Cell) return -1;
  part.Kind = KPK_PLAYERAUX;
  part.EndMarker = 1;
  part.SkipCell = &Game->Player.Body.HitCell;
  part.Pos = p1;
  sInt r = Game->MoveParticle(&part,0);
  out[0] = part.Pos.x; out[1] = part.Pos.y; out[2] = part.Pos.z;
  return r;
}

// debug.js: count registered particles whose last safe position is outside
// the walkable space or inside a solid box (static subs of their add, or moving boxes
// they don't skip); returns the total and writes the first offender to out
extern "C" sInt kk_debug_check(sF32 *out)
{
  sInt outside=0,inside=0,total=0;
  out[0] = out[1] = out[2] = out[3] = 0;
  for(sInt i=0;i<Game->PartUsed;i++)
  {
    for(KKriegerParticle *p=Game->PartPtr[i];;p++)
    {
      total++;
      const sVector &q = p->OldCollPos;
      if(!Game->FindCell(q))
      {
        if(!outside && !inside) { out[0]=q.x; out[1]=q.y; out[2]=q.z; out[3]=p->Kind; }
        outside++;
      }
      else if(p->Cell)
      {
        sBool in = sFALSE;
        for(sInt k=0;k<p->Cell->Subs.Count;k++)
          if(p->Cell->Subs[k]->Mode==KCM_SUB && !(p->Cell->Subs[k]->OnlyForShot && p->Kind>2) && p->Cell->Subs[k]->OutsideMask(q,-0.0002f)==0)
            in = sTRUE;
        for(sInt k=0;k<Game->DCellUsed;k++)
        {
          KKriegerCellDynamic *c = Game->DCellPtr[k];
          if(c!=p->SkipCell && (c->Mode&~0x20)==KCM_SUB && !c->Monster && c!=&Game->Player.Body.HitCell && c->OutsideMask(q,-0.0002f)==0)
            in = sTRUE;
        }
        if(in)
        {
          if(!outside && !inside) { out[0]=q.x; out[1]=q.y; out[2]=q.z; out[3]=p->Kind+100; }
          inside++;
        }
      }
      if(p->EndMarker) break;
    }
  }
  out[4] = outside; out[5] = inside;
  return total;
}

// debug.js: particle k of particle group g (Pos, OldCollPos, OldPos, cell inside)
extern "C" sInt kk_debug_part(sInt g,sInt k,sF32 *out)
{
  if(g<0 || g>=Game->PartUsed) return 0;
  KKriegerParticle *p = Game->PartPtr[g];
  for(sInt i=0;i<k;i++)
    if((p++)->EndMarker) return 0;
  out[0] = p->Pos.x; out[1] = p->Pos.y; out[2] = p->Pos.z;
  out[3] = p->OldCollPos.x; out[4] = p->OldCollPos.y; out[5] = p->OldCollPos.z;
  out[6] = p->OldPos.x; out[7] = p->OldPos.y; out[8] = p->OldPos.z;
  out[9] = p->Cell ? (p->Cell->OutsideMask(p->OldCollPos)==0 ? 1 : 2) : 0;
  out[10] = p->Kind; out[11] = p->VerifyPos;
  return 1;
}

extern "C" void kk_debug_teleport(sF32 x,sF32 y,sF32 z,sF32 dir)
{
  sVector p; p.Init(x,y,z,1);
  Game->SetPlayer(p,dir,0);
}

// debug.js: keeps the player alive while inspecting the game (DBG.god)
extern "C" void kk_debug_life(sInt life)
{
  if(Game) Game->Player.Life = life;
}

extern "C" void kk_key(sU32 key)
{
  // The intro runs for 144 beats before its own state operator opens the
  // main menu; let a key or click skip there directly.
  if(KKriegerRelease2004 && Game->Switches[KGS_GAME]==KGS_GAME_INTRO &&
     (key==sKEY_ENTER || key==' ' || key==sKEY_ESCAPE || key==sKEY_MOUSEL))
  {
    Game->Switches[KGS_GAME] = KGS_GAME_START;
    return;
  }
  // Menus react to the last key pressed when the next frame runs; a release
  // arriving before that frame would hide the press, so releases keep it.
  sU32 pressed = Game->LastKey;
  Game->OnKey(key);
  if(key & sKEYQ_BREAK)
    Game->LastKey = pressed;
}

// one frame: the original sAPPCODE_PAINT
extern "C" void kk_frame(sInt timeMs)
{
  sInt beat;
  sViewport vp,clearvp;
  sInt max;
  sF32 curfps;
  KOp *root;

  WebSystemSetTime(timeMs);
  WebGeoNewFrame();
  ThisTime = sSystem->GetTime() - FirstTime;
  EngDebugLightCount = 0;
  EngDebugPaintCount = 0;

  beat = sMulDiv(ThisTime,Document->SongBPM,60000);

  curfps = ThisTime - LastTime;
  oldfps += (curfps - oldfps) * 0.1f;
  GlobalFps = 1000.0f / oldfps;

  max = ThisTime/10-LastTime/10;
  if(max>10) max = 10;

  sFloatFix();
  Game->OnTick(Environment,max);
  sSystem->Sample3DCommit();
  LastTime = ThisTime;

  sFloatFix();

  // root-switching logic and outermost stuff

  vp.Init();
  vp.Window.Init(0,sSystem->ConfigY*1/6,sSystem->ConfigX,sSystem->ConfigY*5/6);
  GenOverlayManager->SetMasterViewport(vp);

  sInt mode;
  mode = Game->GetNewRoot();
  if(KKriegerRelease2004)           // one root; If operators in the data select the scene by game state
    mode = Document->CurrentRoot;
  sInt state = Game->Switches[KGS_GAME];
  sBool enterGame = KKriegerRelease2004 && state==KGS_GAME_RUN && LastGameState!=KGS_GAME_RUN && LastGameState!=KGS_GAME_INGAME;
  LastGameState = state;

  if(mode!=Document->CurrentRoot)
  {
    sSystem->SetSoundHandler(0,0);
    Document->CurrentRoot = mode;
    Environment->InitView();
    Environment->InitFrame(0,0);
    Document->Precalc(Environment);
    Environment->ExitFrame();
    Game->ResetRoot(Environment,Document->RootOps[Document->CurrentRoot],0);

    if(Sound)
    {
      Sound->Open(Document->SongData);
      Sound->Play(0);
    }
    sSystem->SetSoundHandler(IntroSoundHandler,64);
  }

  // timing

  Environment->InitFrame(beat,ThisTime);
  Document->AddEvents(Environment);
  Game->AddEvents(Environment);
  Game->FlushPhysic();

  // game painting

  clearvp.Init();
  sSystem->SetViewport(clearvp);
  sSystem->Clear(sVCF_ALL,0);

  Environment->GameCam.Init();
  Game->GetCamera(Environment->GameCam);
  Environment->GameCam.ZoomY = 1.0f*vp.Window.XSize()/vp.Window.YSize();

  root = Document->RootOps[Document->CurrentRoot];

  if(root->Cache && root->Cache->ClassId==KC_DEMO)
  {
    GenOverlayManager->Reset(Environment);
    GenOverlayManager->RealPaint = sTRUE;
    GenOverlayManager->Game = Game;

    sFloatFix();
    root->Exec(Environment);

    GenOverlayManager->RealPaint = sFALSE;
    GenOverlayManager->Reset(Environment);
  }

  // The 2004 data has a second player operator (for the menu scene) whose
  // initialisation overwrites the start position; the game scene's operator
  // has just run and set the right one, so place the player again.
  if(enterGame)
    Game->Restart();

  sFloatFix();
  Environment->ExitFrame();
  Environment->Mem.Flush();
}
