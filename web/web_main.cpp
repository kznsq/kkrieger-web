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
      if(ent->LoopSize)
        start = ent->EndPos - ent->LoopSize;
      len = sMin<sInt>(ent->EndPos - start,maxsamples);
      ampx = sFPow(10.0f,(ent->Gain / 5.0f) / 20.0f);

      Sound->Play(ent->StartPos);
      Sound->Render(smpf,len);
      fade = 1000;
      if(ent->MajorId & 1)
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

      sSystem->SampleAdd(smps,len,4,ent->MinorId,!(ent->MajorId & 1));
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

// debug.js: player position and the moving collision cells (doors, monster
// hit boxes): count, then per cell mode, centre and bounding box
extern "C" sInt kk_debug_coll(sF32 *out,sInt max)
{
  out[0] = Game->Player.Collider.Pos.x; out[1] = Game->Player.Collider.Pos.y; out[2] = Game->Player.Collider.Pos.z;
  sInt n = 0;
  for(sInt i=0;i<Game->DCellUsed && n<max;i++,n++)
  {
    KKriegerCellDynamic *c = Game->DCellPtr[i];
    sF32 *d = out+3+n*10;
    sVector mn = c->Vertices[0], mx = c->Vertices[0];
    for(sInt k=1;k<8;k++)
    {
      mn.x = sMin(mn.x,c->Vertices[k].x); mn.y = sMin(mn.y,c->Vertices[k].y); mn.z = sMin(mn.z,c->Vertices[k].z);
      mx.x = sMax(mx.x,c->Vertices[k].x); mx.y = sMax(mx.y,c->Vertices[k].y); mx.z = sMax(mx.z,c->Vertices[k].z);
    }
    d[0] = c->Mode; d[1] = c->Center.x; d[2] = c->Center.y; d[3] = c->Center.z;
    d[4] = mn.x; d[5] = mn.y; d[6] = mn.z; d[7] = mx.x; d[8] = mx.y; d[9] = mx.z;
  }
  return n;
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
