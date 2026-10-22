// This file is distributed under a BSD license. See LICENSE.txt for details.

#include "kkriegergame.hpp"
#include "genmesh.hpp"
#include "engine.hpp"
#include "genoverlay.hpp"

#if sPROFILE
#include "_util.hpp"

sMAKEZONE(GameLogic   ,"GameLogic"  ,0xff224466);
sMAKEZONE(GameCollide ,"GameCollide",0xff336699);
sMAKEZONE(GamePhysic  ,"GamePhysic" ,0xff55aaff);
sMAKEZONE(GameAI      ,"GameAI"     ,0xff77eeff);
#endif

#define OVERLAP   (0.005f)
#define EPSILON   (0.0001f)

#if !sPROFILE
#define GAMEPERF(x) ;
#else
#define GAMEPERF(x) x++;
sInt KKriegerGame::CallFindFirstIntersect;
sInt KKriegerGame::CallFindSubIntersect;
sInt KKriegerGame::CallMoveParticle;
sInt KKriegerGame::CallOutsideMask;
sInt KKriegerGame::CallIntersectOut;
sInt KKriegerGame::CallIntersectIn;
#endif

#pragma lekktor(off)

static sInt SomethingBadHasHappened;    // collision panic

/****************************************************************************/

KKriegerMesh::KKriegerMesh(GenMesh *mesh)
{
  CellCount = mesh->Coll.Count;
  Cell = new KKriegerMeshCell[CellCount];

  for(sInt i=0;i<CellCount;i++)
  {
    GenMeshColl *inCell = &mesh->Coll[i];
    KKriegerMeshCell *outCell = &Cell[i];

    for(sInt j=0;j<8;j++)
      outCell->Vert[j] = mesh->VertPos(inCell->Vert[j]);

    outCell->Mode = inCell->Mode;
    outCell->Logic = inCell->Logic;
  }
}

KKriegerMesh::~KKriegerMesh()
{
  delete[] Cell;
}

void  KKriegerMesh::Copy(KObject *)
{
  // KKriegerMeshes should *never* get copied around
  sVERIFYFALSE;
}

/****************************************************************************/

#define LOGGING 0

/****************************************************************************/
/****************************************************************************/

void KKriegerMonster::Hit(sInt hits,KKriegerGame *game)
{
  if(Life>0)
  {
    game->ActivateMonster(this,1);
    if(hits>Armor)
      Life -= (hits-Armor)+(Armor/4);
    else
      Life -= hits/4;
    if(Life<=0)
    {
      Life = 0;
      if(KillSwitch>0)
        game->Switches[KillSwitch]++;
    }
  }
}

/****************************************************************************/

void KKriegerPlayer::Init()
{
  Life = 100;
  Armor = 0;
  Alpha = 0;
  Beta = 0;
  Ammo[0] = 100;
  Ammo[1] = 50;
  Ammo[2] = 0;
  Ammo[3] = 0;
  Weapon[0] = 1;
  Weapon[1] = 1;
  Weapon[2] = 1;
  Weapon[3] = 0;
  Weapon[4] = 0;
  Weapon[5] = 0;
  Weapon[6] = 0;
  Weapon[7] = 0;
  LifeMax = 100;
  ArmorMax = 100;
  AlphaMax = 100;
  BetaMax = 8;
  AmmoMax[0] = 500;
  AmmoMax[1] = 200;
  AmmoMax[2] = 100;
  AmmoMax[3] = 1000;
  WeaponMax[0] = 1;
  WeaponMax[1] = 1;
  WeaponMax[2] = 1;
  WeaponMax[3] = 1;
  WeaponMax[4] = 1;
  WeaponMax[5] = 1;
  WeaponMax[6] = 1;
  WeaponMax[7] = 1;

  CurrentWeapon = 1;
  NextWeapon = 0;
  FireKey = 0;
  UseKey = 0;
  CoolTimer = 0;
}

void KKriegerPlayer::Hit(sInt hits)
{
  if(hits>Armor)
    Life -= (hits-Armor)+(Armor/4);
  else
    Life -= hits/4;
  if(Life<0)
    Life = 0;
  if(hits>4)
    sSystem->SamplePlay(10);
}

/****************************************************************************/
/****************************************************************************/

void KKriegerGame::Init()
{
  sInputData id;

#if sPROFILE
  sREGZONE(GameLogic);
  sREGZONE(GameCollide);
  sREGZONE(GamePhysic);
  sREGZONE(GameAI);
#endif

  CellAdd.Init(128);
  CellSub.Init(4096);
  CellZone.Init(1024);

  Monsters.Init(64);

  PlayerPos.Init();
  PlayerDir = 0;
  PlayerLook = 0;
  PlayerMat.Init();
  PlayerCell = 0;
  Player.Init();

  LastTime = sSystem->GetTime();
  sSystem->GetInput(0,id);
  LastMouseX = id.Analog[0];
  LastMouseY = id.Analog[1];
  LastCamY = 0;
  CamPos.Init();
  Environment = 0;
  TickCount = 0;
  MonsterResetTicks = 0;

#if !sPLAYER
  TickPerFrame = 0;
  MaxPlanes = 0;
#endif

  sSetMem(Respawn,0,sizeof(Respawn));

#if !sPLAYER
  FlatMat = new sSimpleMaterial(sINVALID,sMBF_ZOFF|sMBF_BLENDADD|sMBF_DOUBLESIDED,0,0);
  QuadGeo = sSystem->GeoAdd(sFVF_COMPACT,sGEO_QUAD);
  LineGeo = sSystem->GeoAdd(sFVF_COMPACT,sGEO_LINE);
#endif

  WeaponEvent.Init();
  Flush();
  Panic();

  sSetMem(Switches,0,KKRIEGER_SWITCHES);
  Switches[KGS_GAME] = KGS_GAME_INTRO;
  Switches[KGS_ONE] = 1;
}

void KKriegerGame::Exit()
{
#if !sPLAYER
  if(QuadGeo!=sINVALID) sSystem->GeoRem(QuadGeo);
  if(LineGeo!=sINVALID) sSystem->GeoRem(LineGeo);
  QuadGeo = sINVALID;
  LineGeo = sINVALID;
  sRelease(FlatMat);
#endif

  Flush();
  CellAdd.Exit();
  CellSub.Exit();
  CellZone.Exit();
  Monsters.Exit();
  WeaponEvent.Exit();
}

void KKriegerGame::Flush()
{
  sInt i,j;
  for(i=0;i<CellAdd.Count;i++)
  {
    CellAdd[i]->Adds.Exit();
    CellAdd[i]->Subs.Exit();
    CellAdd[i]->Zones.Exit();
    delete CellAdd[i];
  }
  for(i=0;i<CellSub.Count;i++)
    delete CellSub[i];
  for(i=0;i<CellZone.Count;i++)
  {
    for(j=0;j<2;j++)
    {
      if(CellZone[i]->Event[j])
      {
        CellZone[i]->Event[j]->Exit();
        delete CellZone[i]->Event[j];
        CellZone[i]->Event[j] = 0;
      }
    }
    delete CellZone[i];
  }

  for(i=0;i<8;i++)
  {
    WeaponShot[i] = 0;
    WeaponOptics[i] = 0;
    WeaponExplode[0][i] = 0;
    WeaponExplode[1][i] = 0;
  }
  CellAdd.Count = 0;
  CellSub.Count = 0;
  CellZone.Count = 0;
#if !sPLAYER
  PlayerCell = 0;
  MaxPlanes = 0;
#endif

  PartUsed = 0;
  ConsUsed = 0;
  WeaponTimer = 0;
  StepTimer = 0;
  StepFoot = 0;
  OffGroundTime = 0;
  Player.CurrentWeapon = 1;
  Player.NextWeapon = 0;
}

#pragma lekktor(on)

void KKriegerGame::Panic()
{
  sVector speed,scale;
  sInputData id;

  SpeedForw = 0;
  AccelForw = 0;
  SpeedSide = 0;
  AccelSide = 0;
  SpeedUp = 0;
  OnGround = 0;
  ResetByOp = 1;
  JumpTimer = 0;
  CollisionForMonsterMode = 0;

  Gravity = 0.0002f;
  GravityPlayer = 0.01f;
  DampPlayerAir = 0.05f;
  DampPlayerCam = 0.05f;
  DampPlayerGround = 0.05f;
  DampPlayerHeight = 0.05f;
  StairHeight = 0.55f;
  EyeHeight = 1.60f;
  MouseTurnSpeed = 0.01f;
  MouseLookSpeed = 0.01f;
  AccelSideFactor = 0.005f;
  AccelForwFactor = 0.005f;
  AccelBackFactor = 0.005f;
  JumpForce = 0.35f;
  FlyMode =0;
  SplashEnable = 0;

  CamOffset = EyeHeight-StairHeight;
  CamZoomPos = 0;
     
  scale.Init(0.5f,1.0f/4,0.5f);
  speed.Init();

//  PlayerPart = MakeBody((const sS8 *)playerv,7-2,(const sU8 *)playere,18-8,scale,0x09,PlayerPos,speed);
//  PlayerPart->Kind = 0x08;

  sSystem->GetInput(0,id);
  LastMouseX = id.Analog[0];
  LastMouseY = id.Analog[1];

  // gameplay init

#if LOGGING
  sDPrintF("KKriegerGame::Panic() called\n");
#endif
}

/****************************************************************************/
/****************************************************************************/

void KKriegerGame::SetPainter(KOp *op,KEnvironment *kenv)
{
  while(op && op->Cache)
  {
    if(op->Cache->ClassId == KC_IPP || op->Cache->ClassId == KC_DEMO)
      op = op->GetInput(0);
    else if(op->CheckOutput(KC_SCENE))
    {
      SetScene((GenScene *)op->Cache);
      break;
    }
    else
      break;
  }
}

void KKriegerGame::SetScene(GenScene *scene)
{
  sMatrix mat;

  Flush();
  mat.Init();

  SetSceneR(scene,mat,scene,0);             // adds first, so that subs and zones find them
  SetSceneR(scene,mat,scene,1);
}

void KKriegerGame::SetMesh(GenMesh *mesh)
{
  sMatrix mat;
  KKriegerMesh *tempMesh;

  Flush();
  mat.Init();

  tempMesh = new KKriegerMesh(mesh);
  AddMesh(tempMesh,mat,0,0);
  AddMesh(tempMesh,mat,0,1);
  tempMesh->Release();
}

void KKriegerGame::SetPlayer(const sVector &p,sF32 d,sF32 l)
{
  sInputData id;

  sSystem->GetInput(0,id);
  LastMouseX = id.Analog[0];
  LastMouseY = id.Analog[1];

  PlayerPos = p;
  PlayerDir = d;
  PlayerLook = l;
  PlayerCell = FindCell(PlayerPos);
  CamPos = p;
  LastCamY = p.y;

  Player.Body.Init(PlayerPos,PlayerCell,KPK_PLAYER);
}

/****************************************************************************/

void KKriegerGame::SetSceneR(GenScene *scene,const sMatrix &base,GenScene *usescene,sInt phase)
{
  sMatrix mat,srt,tmat;
  sInt i,j,max;

  srt.InitSRT(scene->SRT);

  max = scene->Count;
  if(max==0)
    max = 1;

  if(scene->IsSector)
    usescene = scene;

  mat = base;

  for(i=0;i<max;i++)
  {
    if(scene->Count==0 || i!=0)
    {
      tmat.MulA(srt,mat);
      mat = tmat;
    }

    for(j=0;j<scene->Childs.Count;j++)
      SetSceneR(scene->Childs.Get(j),mat,usescene,phase);

#if sLINK_KKRIEGER
    if(scene->CollMesh)
      AddMesh(scene->CollMesh,mat,usescene,phase);
#endif
  }
}

// do two cells touch? a is an add, bv are the corners of b.

static sBool CellOverlap(KKriegerCellAdd *a,KKriegerCell *b,const sVector *bv)
{
  sInt i,mask,andmask,ormask;

  if(a->BBMax.x<b->BBMin.x || a->BBMin.x>b->BBMax.x) return sFALSE;
  if(a->BBMax.y<b->BBMin.y || a->BBMin.y>b->BBMax.y) return sFALSE;
  if(a->BBMax.z<b->BBMin.z || a->BBMin.z>b->BBMax.z) return sFALSE;

  andmask = 0xffff;
  ormask = 0;
  for(i=0;i<8;i++)
  {
    mask = a->OutsideMask(bv[i]);
    andmask &= mask;
    ormask |= mask;
  }
  if(andmask)                               // all of b outside one plane of a
    return sFALSE;
  if(ormask)
    return sTRUE;

  andmask = 0xffff;                         // b completely inside a
  for(i=0;i<8;i++)
    andmask &= b->OutsideMask(a->Vertices[i]);
  return andmask==0;
}

// phase 0 creates the adds, phase 1 the subs and zones. each new cell is
// connected to the adds it touches.

void KKriegerGame::AddMesh(KKriegerMesh *mesh,const sMatrix &mat,GenScene *usescene,sInt phase)
{
  sInt i,j,k;
  KKriegerCell *kc;
  KKriegerCellAdd *kca;
  KKriegerMeshCell *mc;
  sVector va[8];
  sInt mode;

  for(j=0;j<mesh->CellCount;j++)
  {
    mc = &mesh->Cell[j];
    mode = mc->Mode & 3;
    if((mode==KCM_ADD) != (phase==0))
      continue;

    for(k=0;k<8;k++)
      va[k].Rotate34(mat,mc->Vert[k]);

    kca = 0;
    switch(mode)
    {
    default:
    case KCM_ADD:
      kca = new KKriegerCellAdd;
      *CellAdd.Add() = kca;
      kca->Adds.Init();
      kca->Subs.Init();
      kca->Zones.Init();
      for(k=0;k<8;k++)
        kca->Vertices[k] = va[k];
      kc = kca;
      break;
    case KCM_SUB:
      kc = new KKriegerCell;
      *CellSub.Add() = kc;
      break;
    case KCM_ZONE:
      kc = new KKriegerCell;
      *CellZone.Add() = kc;
      break;
    }

    kc->Init(va,mc->Mode);
    kc->Logic = mc->Logic;
    kc->Scene = usescene;

    for(i=0;i<2;i++)
    {
      kc->Event[i] = 0;
      if(kc->Logic.Event[i])
      {
        kc->Event[i] = new KEvent;
        kc->Event[i]->Init();
        kc->Event[i]->Op = kc->Logic.Event[i];
        kc->Event[i]->Translate.x = (kc->BBMin.x + kc->BBMax.x)/2;
        kc->Event[i]->Translate.y =  kc->BBMin.y;
        kc->Event[i]->Translate.z = (kc->BBMin.z + kc->BBMax.z)/2;
        kc->Event[i]->CullDist = 35;
      }
    }

#if !sPLAYER
    MaxPlanes = sMax(MaxPlanes,kc->PlaneCount);
#endif

    switch(mode)
    {
    case KCM_SUB:
      for(i=0;i<CellAdd.Count;i++)
        if(CellOverlap(CellAdd[i],kc,va))
          *CellAdd[i]->Subs.Add() = kc;
      break;
    case KCM_ZONE:
      for(i=0;i<CellAdd.Count;i++)
        if(CellOverlap(CellAdd[i],kc,va))
          *CellAdd[i]->Zones.Add() = kc;
      break;
    default:
      for(i=0;i<CellAdd.Count-1;i++)
      {
        if(CellOverlap(CellAdd[i],kca,va))
        {
          *CellAdd[i]->Adds.Add() = kca;
          *kca->Adds.Add() = CellAdd[i];
        }
      }
      break;
    }
  }
}

/****************************************************************************/
/****************************************************************************/

#pragma lekktor(off)

sBool KKriegerGame::OnKey(sU32 key)
{
  sInt i;
  static sInt CheatOn;
  static sS8 weaponswap[8] = {-1,0,1,2,4,6,-1,-1};

  LastKey = key&0x8001ffff;

  switch(key&(0x8001ffff))
  {
  case ' ':
    if(OnGround && JumpTimer==0)
    {
      SpeedUp = JumpForce;
      sSystem->SamplePlay(4,0.9f);
      JumpTimer = 100;
    }
    break;
  case 'k':
  case 'K':
    Player.Hit(10);
    break;
#if !sPLAYER
  case 'i':
  case 'I':
    Switches[KGS_GAME] = KGS_GAME_INTRO;
    break;
  case 'r':
  case 'R':
    Restart();
    break;
#endif
  case 'w':
  case 'W':
    AccelForw = AccelForwFactor;
    CheatOn = 0;
    return sTRUE;
  case 's':
  case 'S':
    AccelForw = -AccelBackFactor;
    CheatOn = 0;
    return sTRUE;
  case 'd':
  case 'D':
    AccelSide = AccelSideFactor;
    CheatOn = 0;
    return sTRUE;
  case 'a':
  case 'A':
    AccelSide = -AccelSideFactor;
    CheatOn = 0;
    return sTRUE;
  case 'w'|sKEYQ_BREAK:
  case 'W'|sKEYQ_BREAK:
  case 's'|sKEYQ_BREAK:
  case 'S'|sKEYQ_BREAK:
    AccelForw = 0;
    CheatOn = 0;
    return sTRUE;
  case 'd'|sKEYQ_BREAK:
  case 'D'|sKEYQ_BREAK:
  case 'a'|sKEYQ_BREAK:
  case 'A'|sKEYQ_BREAK:
    AccelSide = 0;
    CheatOn = 0;
    return sTRUE;

  case 'y':
  case 'Y':
    FlyMode = !FlyMode;
    return sTRUE;

  case sKEY_SHIFTL:
    Player.UseKey = 1;
    return sTRUE;

  case sKEY_CTRLR:
  case sKEY_MOUSEL:
    Player.FireKey = 1;
    return sTRUE;
  case sKEY_CTRLR|sKEYQ_BREAK:
  case sKEY_MOUSEL|sKEYQ_BREAK:
    Player.FireKey = 0;
    return sTRUE;

#if !sPLAYER
  case 'z':
    CamZoomPos = (CamZoomPos+1)%2;
    return sTRUE;
#endif

  case 'm':
  case 'M':
    CheatOn = 1;
    return sTRUE;
  case 'n':
  case 'N':
    if(CheatOn)
      sCopyMem(&Player.Life,&Player.LifeMax,16*4);
    CheatOn = 0;
    return sTRUE;

  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    if(CheatOn)
    {
      RespawnAt(key&15);
      CheatOn = 0;
    }
    else
    {
      i = weaponswap[key&7];
      if(i>=0 && Player.Weapon[i])
        Player.NextWeapon = i;
    }
    return sTRUE;
  }
  return sFALSE;
}

/****************************************************************************/

void KKriegerGame::AddEvents(KEnvironment *kenv)
{
  sInt i;
  KKriegerCell *cell;

  for(i=0;i<CellZone.Count;i++)
  {
    cell = CellZone[i];
    if(cell->Event[0] && cell->Respawn==0)
      kenv->AddStaticEvent(cell->Event[0]);
    if(cell->Event[1] && cell->Respawn>0)
      kenv->AddStaticEvent(cell->Event[1]);
  }

  if(Player.NextWeapon!=Player.CurrentWeapon && WeaponTimer>=1.0f)
  {
    Player.CurrentWeapon = Player.NextWeapon;
    WeaponEvent.Exit();
    WeaponEvent.Init();
    WeaponEvent.Op = WeaponOptics[Player.CurrentWeapon];
    WeaponTimer = 0.0f;
  }

  if(WeaponEvent.Op)
  {
    WeaponEvent.Translate = PlayerPos;
    WeaponEvent.Translate.y = LastCamY+CamOffset,
    WeaponEvent.Rotate.Init(PlayerLook/sPI2F,PlayerDir/sPI2F,0,0);
    WeaponEvent.Velocity = WeaponTimer;
    WeaponEvent.Modulation = 0;
    if(WeaponCool[Player.CurrentWeapon]>0.01f)
      WeaponEvent.Modulation = (WeaponCool[Player.CurrentWeapon]-Player.CoolTimer)/WeaponCool[Player.CurrentWeapon];
    kenv->AddStaticEvent(&WeaponEvent);
  }
}

/****************************************************************************/

sBool KKriegerRelease2004 = sFALSE;        // set by the loader for data from the Breakpoint 2004 executable

void KKriegerGame::ResetRoot(KEnvironment *env,KOp *root,sBool firsttime)
{
  SetPainter(root,env);
  Restart();
  if(firsttime)
    Switches[KGS_GAME] = KGS_GAME_INTRO;
}

sInt KKriegerGame::GetNewRoot()
{
#if sLINK_KKRIEGER
  sInt sw = Switches[KGS_GAME];
  sInt mode = 1;

  if(sw==KGS_GAME_RUN || sw==KGS_GAME_INGAME || sw==KGS_GAME_RESTART)
    mode = 2;
  if(sw==KGS_GAME_INTRO)
    mode = 0;
  if(sw==KGS_GAME_QUIT)
  {
    sSystem->Exit();
    Switches[KGS_GAME] = KGS_GAME_START;
  }
  if(sw==KGS_GAME_RESTART)
  {
    Switches[KGS_GAME] = KGS_GAME_RUN;
    Restart();
  }


  return mode;
#else
  return 0;
#endif
}

void KKriegerGame::Restart()
{
  sInt i;

  ResetByOp = 1;
  for(i=0;i<CellZone.Count;i++)
    CellZone[i]->Respawn = 0;

  SetPlayer(PlayerStartPos,0,0);
  LastCamY = PlayerPos.y;
  CamPos = PlayerPos;

  sSetMem(Switches+2,0,KKRIEGER_SWITCHES-2);
  Switches[KGS_GAME] = KGS_GAME_RUN;
  MonsterResetTicks = 2;
}

void KKriegerGame::Continue()
{
  sInt i;
  SetPlayer(PlayerStartPos,0,0);
  for(i=0;i<16;i++)
  {
    if(Switches[KGS_RESPAWN+i] && Respawn[i].Valid)
      SetPlayer(Respawn[i].Pos,Respawn[i].Dir,0);
  }

  Player.Life = 100;
  if(Player.Ammo[0]<200)
    Player.Ammo[0] = 200;
}

sInt KKriegerGame::SplashCalc(const sVector &pos,KKriegerCellAdd *cell)
{
  sVector v,p1;
  sF32 dist;

  v.Sub3(pos,SplashPos);
  dist = v.Abs3();
  if(dist<SplashRange)
  {
    p1 = pos;
    p1.y += 0.5f;
    if(cell==0 || !FindFirstIntersect(SplashPos,p1,cell))
    {
      dist = 1-(dist/SplashRange);
      return SplashHits*dist;
    }
  }
  return 0;
}

/****************************************************************************/

void KKriegerGame::OnTick(KEnvironment *kenv,sInt slices)
{
  sInputData id;
  sInt time;
  sF32 d,testh,dist;
  sVector speed,dir,plane,p0,p1;
  sInt i,sl;
  KKriegerMonster *mon;
  KKriegerParticle *part;
  KKriegerCellAdd *cell;
  KEvent *event;

  // check if everything is safe.

  if(PlayerCell==0 || Player.Body.Part[0].Cell==0)
    return;

  TickCount += slices;

  switch(Switches[KGS_GAME])
  {
  case KGS_GAME_RUN:
    break;
  case KGS_GAME_RESTART:
    Restart();
    return;
  default:
    return;
  }

  // get mouse looking

  Environment = kenv;

#if !sPLAYER
  TickPerFrame = slices;
#endif
  time = sSystem->GetTime()-LastTime;   // don't get mouse movement if a lot of time ticked away
  LastTime += time;
  sSystem->GetInput(0,id);
  if(time<1000)
  {
    PlayerDir  += (id.Analog[0] - LastMouseX)*MouseTurnSpeed;
    PlayerLook += (id.Analog[1] - LastMouseY)*MouseLookSpeed;
    PlayerLook = sRange<sF32>(PlayerLook,sPIF/2,-sPIF/2);
  }
  LastMouseX = id.Analog[0];
  LastMouseY = id.Analog[1];
  PlayerMat.InitEuler(FlyMode?PlayerLook:0,PlayerDir,0);

  // weapon animation

  if(Player.NextWeapon!=Player.CurrentWeapon)
  {
    if(WeaponTimer<0.75f)
      WeaponTimer += slices*0.05f;
    WeaponTimer += slices*0.01f;
  }
  else
  {
    if(WeaponTimer<0.25f)
    {
      WeaponTimer+=slices*0.01f;
      if(WeaponTimer>0.25f)
        WeaponTimer = 0.25f;
    }
    else
    {
      if(WeaponTimer>0.251f || AccelForw>0)
      {
        WeaponTimer+=slices*0.003f;
        if(WeaponTimer>0.75f)
        {
          if(AccelForw>0)
          {
            WeaponTimer-=0.5f;
            if(WeaponTimer>0.75)
              WeaponTimer = 0.25f;
          }
          else
            WeaponTimer = 0.25f;
        }
      }
    }
  }

  // player step

  if(AccelForw || AccelSide)
  {
    StepTimer+=slices*0.01f;
    if(StepTimer>0.5f)
    {
      StepTimer -= 0.5f;
      StepFoot = 1-StepFoot;
      sSystem->SamplePlay(StepFoot,0.8f);
    }
  }

  // splash damage

  if(SplashEnable)
  {
    cell = FindCell(SplashPos);

    for(i=0;i<Monsters.Count;i++)
    {
      mon = Monsters[i];
      mon->Hit(SplashCalc(mon->Matrix.l,cell),this);
    }
    SplashHits = SplashHits/2;
    Player.Hit(SplashCalc(PlayerPos,cell));
    SplashEnable = 0;
  }

  // fire? the shot operator makes the projectile from the event

  if(Player.FireKey)
  {
    if(!Player.Ammo[Player.CurrentWeapon/2])
      sSystem->SamplePlay(15,0.8f);

    if(Player.Ammo[Player.CurrentWeapon/2]>0 && Player.CurrentWeapon==Player.NextWeapon && Player.CoolTimer<=0 && WeaponTimer>=0.25f)
    {
      Player.Ammo[Player.CurrentWeapon/2]--;
      Player.CoolTimer = WeaponCool[Player.CurrentWeapon];
      if(!(WeaponFlags[Player.CurrentWeapon]&KWF_CONTINUOUS))
        Player.FireKey = 0;

      if(WeaponShot[Player.CurrentWeapon])
      {
        event = new KEvent;
        event->Init();
        event->Rotate.Init(PlayerLook*0.159155f,PlayerDir*0.159155f,0,0);
        event->Translate = PlayerPos;
        event->Translate.y = LastCamY+CamOffset;
        event->Op = WeaponShot[Player.CurrentWeapon];
        kenv->AddDynamicEvent(event);
      }
      if(!(WeaponFlags[Player.CurrentWeapon]&KWF_CONTINUOUS))
        Player.FireKey = 0;
    }
  }
  if(Player.CoolTimer>=0)
    Player.CoolTimer -= slices*0.01f;

  // particles moved by operators keep their speed

  for(i=0;i<PartUsed;i++)
  {
    for(part=PartPtr[i];;part++)
    {
      if(part->UseNewPos)
      {
        speed.Sub3(part->Pos,part->OldPos);
        speed.Scale3(slices);
        speed.Add3(part->Pos);
        speed.Sub3(part->NewPos);
        part->OldPos.AddScale3(speed,1.0f/slices);
        part->UseNewPos = 0;
      }
      if(part->EndMarker)
        break;
    }
  }

  // the timesliced-loop ...

  for(sl=0;sl<slices;sl++)
  {
    // find height

    if(JumpTimer>0)
      JumpTimer--;

    testh = GravityPlayer+StairHeight;
    d = testh;
    if(Player.Body.Part[0].Cell)
    {
      p0 = Player.Body.Part[0].Pos;
      p1 = p0;
      p1.y -= testh;
      Player.Body.HitCell.Mode |= KCM_DISABLED;
      cell = PlayerCell;
      dir.Init(0,-1,0,0);
      if(FindFirstIntersect(dir,p1,p0,cell,plane,dist,0,0))
        d += dist;
      Player.Body.HitCell.Mode &= ~KCM_DISABLED;
    }

    // move player

    speed.Init(0,0,0,0);
    speed.AddScale3(PlayerMat.i,AccelSide);
    speed.AddScale3(PlayerMat.k,AccelForw);
    speed.y += SpeedUp;
    OnGround = 0;
    SpeedUp = 0;

    if(testh>d)
    {
      if(OffGroundTime>=0.4f)   // player was flying, issue "land" sample
      {
        sSystem->SamplePlay(5);
        StepFoot = 0;
        StepTimer = 0;
      }
      OnGround = 1;
      speed.y += (StairHeight-GravityPlayer-d)*DampPlayerHeight+GravityPlayer;
      OffGroundTime = 0.0f;
    }
    else
    {
      OffGroundTime += slices*0.01f;
    }
    Player.Body.Part[0].Control(speed);

    Player.Body.HitCell.Matrix1.InitEuler(0,PlayerDir,0);
    Player.Body.HitCell.Matrix1.l = PlayerPos;
    Player.Body.HitCell.Matrix1.l.y += 0.85f;
    Player.Body.HitCell.Matrix0.l = Player.Body.HitCell.Matrix1.l;

    // slice

    Physic((sl+1.0f)/slices);
    PlayerPos = Player.Body.Part[0].Pos;
    Zones(kenv);

    // cam

    LastCamY = (PlayerPos.y-LastCamY)*DampPlayerCam+LastCamY;
    CamPos = PlayerPos;
    CamPos.y = LastCamY+CamOffset;

    // AI

    for(i=0;i<Monsters.Count;i++)
      MonsterAI(Monsters[i],kenv);

    Player.UseKey = 0;

  // ensure we always have a correct in-volume for player position (portals)

    if(PlayerCell==0 || PlayerCell->OutsideMask(PlayerPos)!=0)
    {
      cell = FindCell(PlayerPos);
      if(cell!=0)
        PlayerCell = cell;
    }
  }

  // finally move collisions

  for(i=0;i<DCellUsed;i++)
    if(!DCellPtr[i]->Blocked)
      DCellPtr[i]->Matrix0 = DCellPtr[i]->Matrix1;

  // 3d sound listener for the sample operators

  speed.Sub3(Player.Body.Part[0].Pos,Player.Body.Part[0].OldPos);
  speed.Scale3(100.0f);
  sSystem->Sample3DListener(CamPos,speed,PlayerMat.j,PlayerMat.k);

  Environment = 0;

  // check death of player

  if(Player.Life==0 && Switches[KGS_GAME]==KGS_GAME_RUN)
    Continue();

  if(MonsterResetTicks>0)
    MonsterResetTicks--;
}

/****************************************************************************/

// check all collision zones...

void KKriegerGame::Zones(KEnvironment *kenv)
{
  sInt *valp;
  sInt i,j,action,out;
  KKriegerCell *cell;
  KKriegerCellDynamic *dcell;

#if sPROFILE
  sZONE(GameLogic);
#endif

  for(i=0;i<CellZone.Count;i++)
  {
    cell = CellZone[i];
    sVERIFY(cell->Mode==KCM_ZONE);

    // respawn

    if(cell->Respawn>=0 && cell->Respawn<0x40000000)
    {
      cell->Respawn -= 10;
      if(cell->Respawn<0)
        cell->Respawn = 0;
    }

    // logic

    if(Switches[cell->Logic.Condition])
    {
      action = 0;
      switch(cell->Logic.Code&0xf0)
      {
      case KKEE_ENTER:
        if(cell->Enter&KPM_PLAYER)
          action = 1;
        break;
      case KKEE_LEAVE:
        if(cell->Leave&KPM_PLAYER)
          action = 1;
        break;
      case KKEE_COLLECT:
        if((cell->Collided&KPM_PLAYER) && cell->Respawn==0)
        {
          action = 1;
          cell->Respawn = 0x7fffffff;
        }
        break;
      case KKEE_RESPAWN0:
        if((cell->Collided&KPM_PLAYER) && cell->Respawn==0)
        {
          action = 1;
          cell->Respawn = 10*1000;
        }
        break;
      case KKEE_RESPAWN1:
        if((cell->Collided&KPM_PLAYER) && cell->Respawn==0)
        {
          action = 1;
          cell->Respawn = 30*1000;
        }
        break;
      case KKEE_RESPAWN2:
        if((cell->Collided&KPM_PLAYER) && cell->Respawn==0)
        {
          action = 1;
          cell->Respawn = 60*1000;
        }
        break;
      case KKEE_RESPAWN3:
        if((cell->Collided&KPM_PLAYER) && cell->Respawn==0)
        {
          action = 1;
          cell->Respawn = 5*60000;
        }
        break;
      case KKEE_HIT:
        if(cell->Enter&KPM_WEAPONS)
          action = 1;
        break;
      case KKEE_USE:
        if((cell->Collided&KPM_PLAYER) && Player.UseKey)
          action = 1;
        break;
      case KKEE_ALWAYS:
        action = 1;
        break;
      case KKEE_INSIDE:
        action = (cell->Collided&KPM_PLAYER)?1:0;
        break;
      case KKEE_HITORENTER:
        if(cell->Enter&(KPM_PLAYER|KPM_WEAPONS))
          action = 1;
        break;
      }

      valp = (&Player.Life)+(cell->Logic.Output&15);
      switch(cell->Logic.Code&0x0f)
      {
      case KKEC_ENABLESWITCH:
        if(action)
          Switches[cell->Logic.Output] = 1;
        break;
      case KKEC_DISABLESWITCH:
        if(action)
          Switches[cell->Logic.Output] = 0;
        break;
      case KKEC_TOGGLESWITCH:
        if(action)
          Switches[cell->Logic.Output] = !Switches[cell->Logic.Output];
        break;
      case KKEC_SETSWITCH:
        Switches[cell->Logic.Output] = action;
        break;
      case KKEC_SETVALUE:
        if(action)
          *valp = cell->Logic.Value;
        break;
      case KKEC_MAXVALUE:
        if(action)
          *valp = sMax<sInt>(*valp,cell->Logic.Value);
        break;
      case KKEC_MINVALUE:
        if(action)
          *valp = sMin<sInt>(*valp,cell->Logic.Value);
        break;
      case KKEC_INCVALUE:
        if(action)
        {
          out = cell->Logic.Output&15;
          if((&Player.Life)[out]==0 && out>=8)    // switch to a weapon when it is picked up the first time
            Player.NextWeapon = out-8;
          *valp = sMin<sInt>(*valp+cell->Logic.Value,(&Player.LifeMax)[out]);
        }
        break;
      case KKEC_DECVALUE:
        if(action)
          *valp = sMax<sInt>(*valp-cell->Logic.Value,0);
        break;

      case KKEC_JUMP:
        if(action)
          SpeedUp += 0.001f * cell->Logic.Value;
        break;
      case KKEC_RESPAWN:
        if(action)
          Switches[KGS_RESPAWN+(cell->Logic.Value&15)]=1;
        break;
      }
    }

    // done

    cell->Enter = 0;
    cell->Leave = 0;
    cell->Collided = 0;
  }

  // shots that touched a hit cell

  for(j=0;j<KKRIEGER_WEAPONKIND;j++)
    if((Player.Body.HitCell.Enter|Player.Body.HitCell.Collided) & (1<<(KPK_WEAPON0+j)))
      Player.Hit(WeaponHits[j]);

  for(i=0;i<DCellUsed;i++)
  {
    dcell = DCellPtr[i];
    if(dcell->Monster)
    {
      for(j=0;j<KKRIEGER_WEAPONKIND;j++)
        if((dcell->Enter|dcell->Collided) & (1<<(KPK_WEAPON0+j)))
          dcell->Monster->Hit(WeaponHits[j],this);
    }

    dcell->Enter = 0;
    dcell->Leave = 0;
    dcell->Collided = 0;
  }
}

/****************************************************************************/

void KKriegerGame::RespawnAt(sInt i)
{
  if(Respawn[i].Valid || i==1)
  {
    SetPlayer(Respawn[i].Pos,Respawn[i].Dir,0);
    sSystem->SamplePlay(11);
  }
}

/****************************************************************************/

void KKriegerGame::OnPaint(sMaterialEnv *env)
{
#if !sINTRO

  sInt qc,qcmax;

  sVector v0,v1;
  sMatrix mat;
  sU32 col;

  qcmax = 0x4000;
  qc = 0;
  col = 0xff301020;
  sSystem->GetTransform(sGT_MODELVIEW,mat);
  mat.Trans3();
  v0 = mat.i; v0.Scale3(0.125f);
  v1 = mat.j; v1.Scale3(0.125f);

#endif
}

/****************************************************************************/

void KKriegerGame::GetCamera(sMaterialEnv &env)
{
  if(CamZoomPos)
  {
    env.CameraSpace.InitEuler(0,PlayerDir,0);
    env.CameraSpace.l = PlayerPos;
    env.CameraSpace.l.w = 1.0f;
    env.CameraSpace.l.AddScale3(env.CameraSpace.k,-2.0f);
  }
  else
  {
    env.CameraSpace.InitEuler(PlayerLook,PlayerDir,0);
    env.CameraSpace.l = CamPos;
    env.CameraSpace.l.w = 1.0f;
  }
  env.ZoomX = env.ZoomY = 1.0f;
  env.Orthogonal =  sMEO_PERSPECTIVE;
}

void KKriegerGame::GetPlayer(sMatrix &m)
{
  m.Init();
  m.l.z = -5;
}

/****************************************************************************/
/***                                                                      ***/
/***   Game Logic                                                         ***/
/***                                                                      ***/
/****************************************************************************/

// a monster only wakes up where its body is inside the world

void KKriegerGame::ActivateMonster(KKriegerMonster *mon,sInt active)
{
  KKriegerCellAdd *cell;
  sInt i;

  if(active && mon->State==KMS_ASLEEP)
  {
    cell = FindCell(mon->Body.Part[0].Pos);
    if(cell)
    {
      mon->State = active;
      for(i=0;i<5;i++)
        mon->Body.Part[i].Cell = cell;
    }
  }
  else
  {
    mon->State = active;
  }
}

// the body is moved here; the walk operator animates the legs around it and
// reports the matrix back through the event.

void KKriegerGame::MonsterAI(KKriegerMonster *mon,KEnvironment *kenv)
{
  sF32 dist,d,h,hitdist;
  sVector pos,delta,speed;
  sVector eye0,eye1,p0,p1,dir,plane;
  KKriegerCellAdd *cell;
  KEvent *event;

  if(mon->Life<=0)
    return;

  mon->Matrix = mon->Event.ReturnMatrix;

  pos = mon->Body.Part[0].Pos;
  delta.Sub3(pos,PlayerPos);
  delta.y = 0;
  dist = delta.Abs3();
  if(dist>1e-6f)
    delta.Scale3(1.0f/dist);

  // wake up when the player is near and visible, sleep when far and hidden

  eye0 = PlayerPos;
  eye0.y += 0.5f;
  eye1 = pos;
  eye1.y += 0.5f;
  cell = PlayerCell;

  if(mon->State==KMS_ASLEEP)
  {
    if(dist<mon->NoticeRadius && !FindFirstIntersect(eye0,eye1,cell))
      ActivateMonster(mon,KMS_AWAKE);
  }
  if(mon->State!=KMS_ASLEEP)
  {
    if(dist>mon->NoticeRadius && FindFirstIntersect(eye0,eye1,cell))
    {
      ActivateMonster(mon,KMS_ASLEEP);
      mon->State = KMS_ASLEEP;
    }
  }

  if(mon->State!=KMS_ASLEEP)
  {
    // attack

    mon->MeeleeTimer += 0.01f/mon->MeeleeTime;
    if(mon->MeeleeTimer>=1.0f)
      mon->MeeleeTimer = 1.0f;
    mon->RangedTimer += 0.01f/mon->RangedTime;
    if(mon->RangedTimer>=1.0f)
      mon->RangedTimer = 1.0f;

    if(dist<1.5f)
    {
      if(mon->RangedTimer==1.0f && mon->MeeleeTimer==1.0f && mon->MeeleeHits>0)
      {
        mon->MeeleeTimer = 0.0f;
        Player.Hit(mon->MeeleeHits);
      }
    }
    else
    {
      if(mon->RangedTimer==1.0f && mon->MeeleeTimer==1.0f && mon->RangedHits>0 && mon->ShotOp)
      {
        mon->RangedTimer = 0.0f;
        p0 = PlayerPos;
        p0.y += 1.0f;
        p1 = mon->Matrix.l;
        p1.y += 1.0f;
        if(!FindFirstIntersect(p0,p1,PlayerCell))
        {
          event = new KEvent;
          event->Init();
          event->Rotate = mon->Event.Rotate;
          event->Translate = mon->Event.Translate;
          event->Op = mon->ShotOp;
          event->Monster = mon;
          kenv->AddDynamicEvent(event);
        }
      }
    }

    // walk towards or away from the player

    if(dist<mon->ChargeRadius)
      d = dist-mon->MinRadius;
    else
      d = dist-mon->WaitRadius;
    d = sRange<sF32>(d,mon->Speed,-mon->Speed);

    speed = pos;
    speed.AddScale3(delta,-d);
    speed.Sub3(mon->Body.Part[0].Pos);

    // stand on the ground like the player

    h = GravityPlayer+StairHeight;
    p0 = mon->Body.Part[0].Pos;
    p1 = p0;
    p1.y -= h;
    cell = mon->Body.Part[0].Cell;
    mon->Body.HitCell.Mode |= KCM_DISABLED;
    dir.Init(0,-1,0,0);
    if(FindFirstIntersect(dir,p1,p0,cell,plane,hitdist,0,0))
      h += hitdist;
    mon->Body.HitCell.Mode &= ~KCM_DISABLED;
    if(GravityPlayer+StairHeight>h)
      speed.y += (StairHeight-GravityPlayer-h)*DampPlayerHeight+GravityPlayer;

    mon->Body.Part[0].Control(speed);
  }

  // event variables for painting

  mon->Event.Translate = mon->Body.Part[0].Pos;
  mon->Event.Rotate.Init(0,sFATan2(-delta.x,-delta.z)*0.159155f,0,0);
  mon->Event.Scale.x = mon->MeeleeTimer-0.5f;
  if(mon->Event.Scale.x<0)
    mon->Event.Scale.x += 1.0f;
  mon->Event.Scale.y = mon->RangedTimer-0.5f;
  if(mon->Event.Scale.y<0)
    mon->Event.Scale.y += 1.0f;
  mon->Event.CullDist = 35;
  mon->Event.Scale.z = 1.0f*mon->Life/mon->LifeMax;
}

/****************************************************************************/
/***                                                                      ***/
/***   Particles and Collision                                            ***/
/***                                                                      ***/
/****************************************************************************/

void KKriegerParticle::Init(const sVector &pos,const sVector &speed,KKriegerCellAdd *cell)
{
  sSetMem(this,0,sizeof(*this));
  Cell = cell;
  Pos = pos;
  NewPos = pos;
  VerifyPos = 1;
  OldCollPos = pos;
  OldPos.Sub3(pos,speed);
}

// a shot touched something

void KKriegerParticle::Hit(KKriegerGame *game,const sVector &pos,const sVector &plane,sF32 dist,sInt hitmonster)
{
  KEvent *event;
  KOp *op;

  if(!ShotEvent)
    return;

  if(Kind>=KPK_WEAPON0 && Kind<KPK_WEAPON0+KKRIEGER_WEAPONKIND && ShotEvent->Start>0 && ShotEvent->Select==0)
  {
    op = game->WeaponExplode[hitmonster][Kind-KPK_WEAPON0];
    if(op && (hitmonster || ShotEvent->ShotTime+10<game->TickCount))
    {
      ShotEvent->Velocity = 5.0f;
      event = new KEvent;
      event->Init();
      event->Op = op;
      event->Translate = pos;
      event->Rotate.Init(sFATan2(-plane.y,sFSqrt(plane.x*plane.x+plane.z*plane.z))*0.159155f,sFATan2(plane.x,plane.z)*0.159155f,0,0);
      game->Environment->AddDynamicEvent(event);
    }
  }

  if(!(game->WeaponFlags[Kind-KPK_WEAPON0]&KWF_REFLECT) || hitmonster)
  {
    ShotEvent->Select = 1;
    ShotEvent->Stop();
  }
  ShotEvent->ShotTime = game->TickCount;
}

void KKriegerParticle::Control(const sVector &v)
{
  KKriegerParticle *p;

  for(p=this;;p++)
  {
    p->Pos.Add3(v);
    if(p->EndMarker)
      break;
  }
}

/****************************************************************************/

static const sS8 BodyPartPos[5][3] =
{
  {  0,0, 0 },
  { -1,0, 0 },
  {  0,0,-1 },
  {  1,0, 0 },
  {  0,0, 1 },
};

static const sU8 BodyCons[10][2] =
{
  { 0,1 },{ 0,2 },{ 0,3 },{ 0,4 },          // spokes
  { 1,2 },{ 2,3 },{ 3,4 },{ 4,1 },          // ring
  { 1,3 },{ 2,4 },                          // diagonals
};

void KKriegerBody::Init(const sVector &pos,KKriegerCellAdd *cell,sInt kpk)
{
  sVector scale,speed,v;
  sMatrix mat;
  sInt i;

  scale.Init(0.5f,0.25f,0.5f,0);
  speed.Init(0,0,0,0);
  for(i=0;i<5;i++)
  {
    v.Init(BodyPartPos[i][0],BodyPartPos[i][1],BodyPartPos[i][2],0);
    v.Mul3(scale);
    v.Add3(pos);
    Part[i].Init(v,speed,cell);
    Part[i].Kind = kpk+1;
    Part[i].OldCollPos = pos;               // the particles move out from the center: none starts in a wall
    Part[i].Mass = 1.0f;
    Part[i].SkipCell = &HitCell;
  }
  Part[0].Kind = kpk;
  Part[4].EndMarker = 1;

  for(i=0;i<10;i++)
  {
    Cons[i].EndMarker = 0;
    Cons[i].Next = 0;
    Cons[i].PartA = &Part[BodyCons[i][0]];
    Cons[i].PartB = &Part[BodyCons[i][1]];
    v.Sub3(Cons[i].PartA->Pos,Cons[i].PartB->Pos);
    Cons[i].Dist = v.Abs3();
  }
  Cons[9].EndMarker = 1;

  mat.Init();
  scale.Init(0.5f,1.5f,0.5f,0);
  HitCell.Init(mat,scale,KCM_SUB);
  mat.l = pos;
  mat.l.y += 0.85f;
  HitCell.PrepareDynamic(mat);
}

void KKriegerBody::Add(KKriegerGame *game)
{
  game->AddPart(&Part[0]);
  game->AddCons(&Cons[0]);
  game->AddDCell(&HitCell);
}

/****************************************************************************/

static const sS8 PartBoxCorner[8][3] =
{
  { -1,-1,-1 },{ -1, 1,-1 },{  1, 1,-1 },{  1,-1,-1 },
  { -1,-1, 1 },{ -1, 1, 1 },{  1, 1, 1 },{  1,-1, 1 },
};

static const sU8 PartBoxCons[16][2] =
{
  { 0,1 },{ 1,2 },{ 2,3 },{ 3,0 },          // front ring
  { 4,5 },{ 5,6 },{ 6,7 },{ 7,4 },          // back ring
  { 0,4 },{ 1,5 },{ 2,6 },{ 3,7 },          // edges between
  { 0,6 },{ 1,7 },{ 2,4 },{ 3,5 },          // space diagonals
};

void KKriegerPartBox::Init(const sMatrix &mat,const sVector &scale,const sVector &speed,KKriegerCellAdd *cell)
{
  sInt i;
  sVector v;

  for(i=0;i<8;i++)
  {
    v.Init(PartBoxCorner[i][0],PartBoxCorner[i][1],PartBoxCorner[i][2],0);
    v.Scale3(0.5f);
    v.Mul3(scale);
    v.Rotate34(mat);
    Part[i].Init(v,speed,cell);
  }
  Part[7].EndMarker = 1;

  for(i=0;i<16;i++)
  {
    Cons[i].EndMarker = 0;
    Cons[i].Next = 0;
    Cons[i].PartA = &Part[PartBoxCons[i][0]];
    Cons[i].PartB = &Part[PartBoxCons[i][1]];
    v.Sub3(Cons[i].PartA->Pos,Cons[i].PartB->Pos);
    Cons[i].Dist = v.Abs3();
  }
  Cons[15].EndMarker = 1;
}

void KKriegerPartBox::Hit(const sVector &pos,const sVector &v,sF32 mass)
{
}

void KKriegerPartBox::Register(KKriegerGame *game)
{
  game->AddPart(&Part[0]);
  game->AddCons(&Cons[0]);
}

// axes from the parallel edges, origin in the middle of the corners

void KKriegerPartBox::GetMatrix(sMatrix &mat)
{
  sVector d;
  sInt i;
  static const sU8 edges[3][4][2] =
  {
    { { 3,0 },{ 2,1 },{ 7,4 },{ 6,5 } },
    { { 1,0 },{ 2,3 },{ 5,4 },{ 6,7 } },
    { { 4,0 },{ 5,1 },{ 6,2 },{ 7,3 } },
  };

  mat.i.Init(0,0,0,0);
  mat.j.Init(0,0,0,0);
  mat.k.Init(0,0,0,0);
  mat.l.Init(0,0,0,1);
  for(i=0;i<8;i++)
    mat.l.Add3(Part[i].Pos);
  for(i=0;i<4;i++)
  {
    d.Sub3(Part[edges[0][i][0]].Pos,Part[edges[0][i][1]].Pos); mat.i.Add3(d);
    d.Sub3(Part[edges[1][i][0]].Pos,Part[edges[1][i][1]].Pos); mat.j.Add3(d);
    d.Sub3(Part[edges[2][i][0]].Pos,Part[edges[2][i][1]].Pos); mat.k.Add3(d);
  }
  mat.i.Unit3();
  mat.j.Unit3();
  mat.k.Unit3();
  mat.l.Scale3(0.125f);
}

/****************************************************************************/

void KKriegerGame::FlushPhysic()
{
  PartUsed = 0;
  ConsUsed = 0;
  DCellUsed = 0;
  Monsters.Count = 0;
  Player.Body.Add(this);
}

void KKriegerGame::AddPart(KKriegerParticle *p)
{
  sVERIFY(PartUsed < KKRIEGER_MAXPARTICLE);
  PartPtr[PartUsed++] = p;
}

void KKriegerGame::AddCons(KKriegerConstraint *c)
{
  sVERIFY(ConsUsed < KKRIEGER_MAXCONSTRAINT);
  ConsPtr[ConsUsed++] = c;
}

void KKriegerGame::AddDCell(KKriegerCellDynamic *d)
{
  sVERIFY(DCellUsed < KKRIEGER_MAXDCELL);
  DCellPtr[DCellUsed++] = d;
}

/****************************************************************************/

void KKriegerGame::Physic(sF32 fade)
{
  sInt i;
  KKriegerConstraint *cons;
  KKriegerParticle *part;
  KKriegerCellDynamic *cell;

#if sPROFILE
  sZONE(GamePhysic);

  CallFindFirstIntersect = 0;
  CallFindSubIntersect = 0;
  CallMoveParticle = 0;
  CallOutsideMask = 0;
  CallIntersectOut = 0;
  CallIntersectIn = 0;
#endif

  for(i=0;i<ConsUsed;i++)
  {
    for(cons=ConsPtr[i];;cons++)
    {
      PhysicCons(cons);
      if(cons->EndMarker)
        break;
    }
  }

  for(i=0;i<PartUsed;i++)
  {
    for(part=PartPtr[i];;part++)
    {
      PhysicPart(part);
      if(part->EndMarker)
        break;
    }
  }

  for(i=0;i<DCellUsed;i++)
  {
    cell = DCellPtr[i];
    cell->CurrentMatrix.Lin4(cell->Matrix0,cell->Matrix1,fade);
    PhysicDCell(cell);
  }

  for(i=0;i<PartUsed;i++)
  {
    for(part=PartPtr[i];;part++)
    {
      part->Pos.Add3(part->Force);
      part->Force.Init(0,0,0,0);
      if(part->EndMarker)
        break;
    }
  }
}

// keep two particles at their distance (without square root)

void KKriegerGame::PhysicCons(KKriegerConstraint *cons)
{
  sVector d;
  sF32 dd,r;

  d.Sub3(cons->PartB->Pos,cons->PartA->Pos);
  dd = d.Dot3(d);
  r = cons->Dist*cons->Dist;
  d.Scale3(r/(r+dd)-0.5f);
  cons->PartB->Pos.Add3(d);
  cons->PartA->Pos.Sub3(d);
}

// verlet integration, then collision

void KKriegerGame::PhysicPart(KKriegerParticle *p)
{
  KKriegerCellAdd *cell;
  sF32 gravity,f;
  sVector np;
  sInt i;

  if(p->VerifyPos)
  {
    if(p->Cell==0 || p->Cell->OutsideMask(p->OldCollPos)!=0)
    {
      cell = FindCell(p->OldCollPos);
      if(!cell)
        return;
      p->Cell = cell;
    }
    p->VerifyPos = 0;
  }

  gravity = Gravity;
  switch(p->Kind)
  {
  case KPK_PLAYER:
  case KPK_MONSTER:                         // keep the body particles at one height
    f = 0;
    for(i=0;i<5;i++)
      f += p[i].Pos.y;
    f = f*0.2f*0.25f;
    for(i=0;i<5;i++)
      p[i].Pos.y = p[i].Pos.y*0.75f+f;
    // fall through
  case KPK_PLAYERAUX:
  case KPK_MONSTERAUX:
    gravity = FlyMode ? 0 : GravityPlayer;
    p->Damp = OnGround ? DampPlayerGround : DampPlayerAir;
    break;
  }

  f = 1.0f-p->Damp;
  p->Damp = 0;
  np.x = p->Pos.x-f*p->OldPos.x;
  np.y = p->Pos.y-f*p->OldPos.y;
  np.z = p->Pos.z-f*p->OldPos.z;
  p->OldPos = p->Pos;
  np.x = f*p->Pos.x+np.x;
  np.y = f*p->Pos.y-gravity+np.y;
  np.z = f*p->Pos.z+np.z;
  np.w = 1.0f;
  p->Pos = np;

  if(MoveParticle(p,0))
  {
    p->Damp = 0.1f;
    p->OldPos.y += gravity;
  }
}

// move the cell to CurrentMatrix, pushing the particles out of its way.
// a particle that can't be pushed blocks the cell for this frame.

void KKriegerGame::PhysicDCell(KKriegerCellDynamic *cell)
{
  KKriegerCellDynamic temp;
  KKriegerParticle *p;
  sMatrix inv,delta;
  sVector v,w,d,plane,save;
  sF32 dist,len2;
  sBool blocked;
  sInt i;

  if(cell->PartBox)
    cell->PartBox->GetMatrix(cell->CurrentMatrix);

  temp = *cell;
  temp.ApplyMatrix(temp.CurrentMatrix);
  inv = temp.ConfirmedMatrix;
  inv.InvCheap4();
  delta.MulA(inv,temp.CurrentMatrix);

  blocked = 0;
  dist = 0;
  for(i=0;i<PartUsed;i++)
  {
    for(p=PartPtr[i];;p++)
    {
      if(!(cell->Monster && (p->Kind<KPK_WEAPON0 || p->Kind>=KPK_WEAPON0+KKRIEGER_WEAPONKIND))  // monsters only push shots
        && p->SkipCell!=cell && temp.OutsideMask(p->Pos)==0)
      {
        v = p->Pos;
        w.Rotate4(delta,v);
        d.Sub3(v,w);                        // against the motion of the cell
        len2 = d.Dot3(d);
        if(len2<1e-16)
        {
          blocked = 1;
        }
        else
        {
          d.Scale3(1.0f/sFSqrt(len2));
          if(!temp.IntersectIn(v,d,plane,dist))
            blocked = 1;
          v.AddScale3(d,dist);
          v.AddScale3(plane,-EPSILON);
          v.w = 1.0f;

          save = p->Pos;
          p->Pos = v;
          if(MoveParticle(p,1))
            blocked = 1;
          p->Pos = save;
        }
      }
      if(p->EndMarker)
        break;
    }
  }

  if(!blocked)
  {
    cell->Mode &= ~KCM_UNCONFIRMED;
    cell->ConfirmedMatrix = cell->CurrentMatrix;
    cell->ApplyMatrix(cell->ConfirmedMatrix);
  }
  else
  {
    cell->Blocked = 1;
  }
}

/****************************************************************************/

// sweep the particle from OldCollPos to Pos. on a collision it slides along
// the plane, then along a second plane or the edge of both.
// returns 0 when free, 1 when it slid, 2 when it had to stay where it was.

sInt KKriegerGame::MoveParticle(KKriegerParticle *part,sBool onlyoldcoll)
{
  sVector p0,p1,d,v;
  sVector plane0,plane1,plane2;
  sVector pl0,pl1,e,eb,q,t;
  sF32 dist,invlen,c,s,invdet;
  KKriegerCellAdd *cadd;
  sInt result;

  GAMEPERF(CallMoveParticle);

  p0 = part->OldCollPos;
  p1 = part->Pos;
  cadd = part->Cell;
  result = 0;
  if(part->SkipCell)
    part->SkipCell->Mode |= KCM_DISABLED;

  d.Sub3(p1,p0);
  dist = d.Dot3(d);
  if(dist<1e-16)
    goto stuck;
  invlen = 1.0f/sFSqrt(dist);
  d.Scale3(invlen);

  // first contact: move there and slide on the plane

  SomethingBadHasHappened = 0;
  if(!FindFirstIntersect(d,p1,p0,cadd,plane0,dist,part,part))
    goto done;
  result = 1;
  if(SomethingBadHasHappened)
    goto stuck;
  p0 = p1;
  p0.AddScale3(d,dist);
  if(part->ShotEvent)
    part->Hit(this,p0,plane0,0,0);
  p0.AddScale3(plane0,EPSILON);
  p1.AddScale3(plane0,EPSILON-(plane0.Dot3(p1)+plane0.w));
  v.Sub3(p1,p0);
  if(v.Dot3(v)>1e-16)
    d.Scale3(v,invlen);

  // second contact

  if(!FindFirstIntersect(d,p1,p0,cadd,plane1,dist,part,0))
    goto done;
  if(SomethingBadHasHappened)
    goto stuck;
  c = plane0.Dot3(plane1);
  p0 = p1;
  p0.AddScale3(d,dist);
  if(c<-0.9f)                               // between opposite walls: stay
  {
    part->OldPos = part->OldCollPos;
    part->Pos = part->OldCollPos;
    p1 = part->OldCollPos;
  }
  else if(c>EPSILON)                        // slide on the second plane
  {
    p0.AddScale3(plane1,EPSILON);
    p1.AddScale3(plane1,EPSILON-(plane1.Dot3(p1)+plane1.w));
  }
  else                                      // slide along the edge of both planes
  {
    pl0 = plane0;
    pl0.w -= EPSILON;
    pl1 = plane1;
    pl1.w -= EPSILON;
    e.Cross3(plane0,plane1);
    q.Init(0,0,0,0);
    t.Cross3(pl1,e);
    q.AddScale3(t,-pl0.w);
    t.Cross3(e,pl0);
    q.AddScale3(t,-pl1.w);
    eb.Cross3(pl0,pl1);
    invdet = 1.0f/(e.y*pl1.x*pl0.z + e.x*pl1.z*pl0.y + e.z*pl1.y*pl0.x
                 - e.y*pl1.z*pl0.x - pl1.x*pl0.y*e.z - pl0.z*e.x*pl1.y);
    s = e.Dot3(p1);
    p1 = q;
    p1.AddScale3(eb,s);
    p1.Scale3(invdet);
    s = e.Dot3(p0);
    p0 = q;
    p0.AddScale3(eb,s);
    p0.Scale3(invdet);
  }

  // third contact: give up

  v.Sub3(p1,p0);
  if(v.Dot3(v)>1e-16)
    d.Scale3(v,invlen);
  d.Scale3(invlen);
  if(FindFirstIntersect(d,p1,p0,cadd,plane2,dist,part,0))
    goto stuck;

done:
  if(part->Kind)
    CheckZones(part->OldCollPos,part->Cell,p1,cadd,part);
  part->OldCollPos = p1;
  part->Cell = cadd;
  if(!onlyoldcoll)
    part->Pos = p1;
  if(part->SkipCell)
    part->SkipCell->Mode &= ~KCM_DISABLED;
  return result;

stuck:
  if(part->Kind)
    CheckZones(part->OldCollPos,part->Cell,p1,cadd,part);
  if(!onlyoldcoll)
  {
    part->OldPos = part->OldCollPos;
    part->Pos = part->OldCollPos;
  }
  if(part->SkipCell)
    part->SkipCell->Mode &= ~KCM_DISABLED;
  return 2;
}

/****************************************************************************/

// a ray from p0 to p1. moves p1 to the first hit.

sBool KKriegerGame::FindFirstIntersect(const sVector &p0,sVector &p1,KKriegerCellAdd *&cadd,sVector *plane)
{
  sVector d,pl;
  sF32 dist;

  d.Sub3(p1,p0);
  if(FindFirstIntersect(d,p1,p0,cadd,pl,dist,0,0))
  {
    p1.AddScale3(d,dist);
    if(plane)
      *plane = pl;
    return sTRUE;
  }
  return sFALSE;
}

// follow the ray p0->p1 (direction d) through the connected adds and find the
// first wall or sub. the hit is at p1+d*dist (dist<=0). part is the moving
// particle (0 for probes), zonepart gets the impacts.

sBool KKriegerGame::FindFirstIntersect(const sVector &d,const sVector &p1,const sVector &p0,KKriegerCellAdd *&cadd,sVector &plane,sF32 &dist,KKriegerParticle *part,KKriegerParticle *zonepart)
{
  KKriegerCellAdd *list[16];
  KKriegerCellAdd *next;
  sInt count;
  sInt i,j;
  sBool isplayer;
  sVector v,plane2;
  sF32 dist2;

  GAMEPERF(CallFindFirstIntersect);
  isplayer = part && (part->Kind==KPK_PLAYER || part->Kind==KPK_PLAYERAUX);
  list[0] = cadd;
  count = 1;

again:

// see if we leave this cadd

  if(cadd->IntersectOut(p1,d,plane,dist))
  {
    v = p1;
    v.AddScale3(d,dist);
    if(count<16)
    {
      for(i=0;i<cadd->Adds.Count;i++)
      {
        next = cadd->Adds[i];
        if(next->OutsideMask(v)!=0)
          continue;
        if(isplayer && next->OnlyForShot)
          continue;
        for(j=0;j<count;j++)
          if(list[j]==next)
            goto retry;

        list[count++] = cadd;               // the list holds the adds left behind
        cadd = next;
        goto again;
retry:;
      }
    }

    // we collided with the outside of an add, even after following all
    // connected adds. a sub may be hit before that wall.

    if(FindSubIntersect(d,p1,p0,list,count,plane2,dist2,part,zonepart) && dist2<dist)
    {
      dist = dist2;
      plane = plane2;
    }
    return sTRUE;
  }

  // we are well in add, check subs

  return FindSubIntersect(d,p1,p0,list,count,plane,dist,part,zonepart);
}

// find nearest intersection with subcell.

sBool KKriegerGame::FindSubIntersect(const sVector &d,const sVector &p1,const sVector &p0,KKriegerCellAdd **list,sInt count,sVector &plane,sF32 &dist,KKriegerParticle *part,KKriegerParticle *zonepart)
{
  sInt i,j;
  sBool collided;
  sBool isplayer;
  KKriegerCell *cell;
  KKriegerCellDynamic *dcell;
  sVector v,dd;

  GAMEPERF(CallFindSubIntersect);
  isplayer = part && (part->Kind==KPK_PLAYER || part->Kind==KPK_PLAYERAUX);
  dist = EPSILON;
  collided = 0;
  for(j=0;j<count;j++)
  {
    for(i=0;i<list[j]->Subs.Count;i++)
    {
      cell = list[j]->Subs[i];
      if(cell->Mode==KCM_SUB || (cell->Mode==(KCM_SUB|KCM_ONLYMONSTER) && CollisionForMonsterMode))
      {
        if(!isplayer && cell->OnlyForShot)
          continue;
        if(FindSubIntersect2(d,p1,p0,cell,plane,dist))
        {
          collided = 1;
          if(SomethingBadHasHappened)
            return sTRUE;
        }
      }
    }
  }

  for(i=0;i<DCellUsed;i++)
  {
    dcell = DCellPtr[i];
    if(dcell->Mode!=KCM_SUB)
      continue;
    if(!isplayer && dcell->OnlyForShot)
      continue;
    if((dcell->Monster || dcell==&Player.Body.HitCell) && !part)   // probes don't see bodies
      continue;
    if(FindSubIntersect2(d,p1,p0,dcell,plane,dist))
    {
      collided = 1;
      if(SomethingBadHasHappened)
        return sTRUE;
      if(zonepart && !(zonepart->ShotEvent && zonepart->ShotEvent->Select))
      {
        dcell->Collided |= 1<<zonepart->Kind;
        v = p1;
        v.AddScale3(d,dist);
        zonepart->Hit(this,v,plane,0,dcell->Monster!=0);
      }
      if(part && part->Mass>0 && dcell->PartBox)
      {
        dd.Sub3(p1,p0);
        v = p1;
        v.AddScale3(d,dist);
        dcell->PartBox->Hit(v,dd,part->Mass);
      }
    }
  }

  return collided;
}

sBool KKriegerGame::FindSubIntersect2(const sVector &d,const sVector &p1,const sVector &p0,KKriegerCell *cell,sVector &plane,sF32 &distresult)
{
  sVector planei,v;
  sF32 besti;
  sInt m0,m1;

  if(cell->Mode==KCM_SUB || (cell->Mode==(KCM_SUB|KCM_ONLYMONSTER) && CollisionForMonsterMode))
  {
    m0 = cell->OutsideMask(p0);
    m1 = cell->OutsideMask(p1);
    if(m0==0)                               // started inside: collision panic
    {
      plane.Init(1,0,0,0);
      SomethingBadHasHappened = 1;
      distresult = 0;
      return sTRUE;
    }
    if(m0&m1)                               // both on the outside of one plane
      return sFALSE;
    if(cell->IntersectIn(p1,d,planei,besti) && besti<distresult)
    {
      v = p1;
      v.AddScale3(d,besti);
      if(cell->OutsideMask(v,EPSILON)==0)
      {
        distresult = besti;
        plane = planei;
        plane.Neg4();
        return sTRUE;
      }
    }
  }
  return sFALSE;
}

// mark the zones the particle entered, left or is inside

void KKriegerGame::CheckZones(const sVector &p0,KKriegerCellAdd *c0,const sVector &p1,KKriegerCellAdd *c1,KKriegerParticle *part)
{
  sInt i;
  sU32 mask;
  sBool hit0,hit1;
  KKriegerCell *cell;
  KKriegerCellAdd *cadd;

  if(part->ShotEvent && part->ShotEvent->Select)
    return;
  mask = 1<<part->Kind;
  cadd = c0;
  for(;;)
  {
    for(i=0;i<cadd->Zones.Count;i++)
    {
      cell = cadd->Zones[i];
      hit0 = (cell->OutsideMask(p0)==0);
      hit1 = (cell->OutsideMask(p1)==0);

      if(hit0 && !hit1) cell->Leave |= mask;
      if(!hit0 && hit1) cell->Enter |= mask;
      if(hit1) cell->Collided |= mask;
    }
    if(cadd==c1)
      break;
    cadd = c1;
  }
}

/****************************************************************************/
/***                                                                      ***/
/***   Math                                                               ***/
/***                                                                      ***/
/****************************************************************************/

sInt KKriegerCell::OutsideMask(const sVector &v,sF32 radius)    // normal points inside!
{
  sInt i;
  sInt mask;

  GAMEPERF(KKriegerGame::CallOutsideMask);

  mask = 0;
  for(i=0;i<PlaneCount;i++)
  {
    if(Planes[i].x*v.x+Planes[i].y*v.y+Planes[i].z*v.z+Planes[i].w+radius<0)
      mask |= 1<<i;
  }
  return mask;
}

// calculate intersection point ray -> cell when casting from inside to outside

sBool KKriegerCell::IntersectOut(const sVector &p,const sVector &d,sVector &plane,sF32 &distresult)
{
  sInt bestplane;
  sInt i;
  sF32 dist;
  sF32 best;

  GAMEPERF(KKriegerGame::CallIntersectOut);

  best = 0;
  bestplane = -1;
  for(i=0;i<PlaneCount;i++)
  {
    dist = Planes[i].Dot3(p)+Planes[i].w;
    if(dist<0)
    {
      dist = (-1.0f/Planes[i].Dot3(d))*dist;
      if(dist<best)
      {
        best = dist;
        bestplane = i;
      }
    }
  }
  if(bestplane>=0)
  {
    plane = Planes[bestplane];
    distresult = best;
    return sTRUE;
  }
  return sFALSE;
}

// calculate intersection point ray -> cell when casting from the outside

sBool KKriegerCell::IntersectIn(const sVector &p,const sVector &d,sVector &plane,sF32 &distresult)
{
  sInt bestplane;
  sInt i;
  sF32 dist,det;
  sF32 best;

  GAMEPERF(KKriegerGame::CallIntersectIn);

  best = -1024*1024;
  bestplane = -1;
  for(i=0;i<PlaneCount;i++)
  {
    dist = Planes[i].Dot3(p)+Planes[i].w;
    det = Planes[i].Dot3(d);
    if(det>1.0e-16)
    {
      dist = -dist/det;
      if(dist>best)
      {
        best = dist;
        bestplane = i;
      }
    }
  }
  if(bestplane>=0)
  {
    plane = Planes[bestplane];
    distresult = best;
    return sTRUE;
  }
  return sFALSE;
}

/****************************************************************************/

// find an ADD cell.

KKriegerCellAdd *KKriegerGame::FindCell(const sVector &v)
{
  sInt i;
  for(i=0;i<CellAdd.Count;i++)
  {
    if(CellAdd[i]->OutsideMask(v)==0)
      return CellAdd[i];
  }
  return 0;
}

/****************************************************************************/
/****************************************************************************/
/***                                                                      ***/
/***   Operators                                                          ***/
/***                                                                      ***/
/****************************************************************************/

#pragma lekktor(on)

void __stdcall Exec_KKrieger_Para(KOp *op,KEnvironment *kenv)
{
  sU32 *du;
  sF32 *df;
  sInt i;

  du = op->GetAnimPtrU(0);
  df = op->GetAnimPtrF(0);

  kenv->Game->Player.LifeMax    = du[1];
  kenv->Game->Player.ArmorMax  = du[3];
  kenv->Game->Player.AlphaMax   = du[5];
  kenv->Game->Player.BetaMax    = du[7];
  kenv->Game->Player.AmmoMax[0] = du[9];
  kenv->Game->Player.AmmoMax[1] = du[11];
  kenv->Game->Player.AmmoMax[2] = du[13];
  kenv->Game->Player.AmmoMax[3] = du[15];
  if(kenv->Game->ResetByOp)
  {
    kenv->Game->ResetByOp = 0;
    kenv->Game->Player.Life    = du[0];
    kenv->Game->Player.Armor  = du[2];
    kenv->Game->Player.Alpha   = du[4];
    kenv->Game->Player.Beta    = du[6];
    kenv->Game->Player.Ammo[0] = du[8];
    kenv->Game->Player.Ammo[1] = du[10];
    kenv->Game->Player.Ammo[2] = du[12];
    kenv->Game->Player.Ammo[3] = du[14];
    for(i=0;i<8;i++)
      kenv->Game->Player.Weapon[i] = (du[16]>>i)&1;;
  }

  kenv->Game->GravityPlayer     = df[17];
  kenv->Game->DampPlayerGround  = df[18];
  kenv->Game->DampPlayerAir     = df[19];
  kenv->Game->DampPlayerHeight  = df[20];
  kenv->Game->StairHeight       = df[21];
  kenv->Game->EyeHeight         = df[22];
  kenv->Game->MouseTurnSpeed    = df[23];
  kenv->Game->MouseLookSpeed    = df[24];
  kenv->Game->AccelSideFactor   = df[25];
  kenv->Game->AccelForwFactor   = df[26];
  kenv->Game->AccelBackFactor   = df[27];
  kenv->Game->JumpForce         = df[28];
  kenv->Game->PlayerStartPos.Init(df[29],df[30],df[31]);
  kenv->Game->DampPlayerCam     = df[56];


  sCopyMem(kenv->Game->WeaponHits,du+32,24*4);

  kenv->Game->CamOffset = kenv->Game->EyeHeight-kenv->Game->StairHeight;

  op->ExecInputs(kenv);
}

void __stdcall Exec_KKrieger_Events(KOp *op,KEnvironment *kenv)
{
  KOp **ptr;
  sInt i;

  switch(*op->GetAnimPtrU(0))
  {
  case 0:
  default:
    ptr = kenv->Game->WeaponShot;
    break;
  case 1:
    ptr = kenv->Game->WeaponOptics;
    break;
  case 2:
    ptr = kenv->Game->WeaponExplode[0];
    break;
  case 3:
    ptr = kenv->Game->WeaponExplode[1];
    break;
  }

  for(i=0;i<8;i++)
    ptr[i] = op->GetLink(i);

  op->ExecInputs(kenv);
}

class KObject * __stdcall Init_KKrieger_Para(KOp *op,KEnvironment *kenv)
{
  sF32 *df;

  df = op->GetAnimPtrF(0);
  kenv->Game->PlayerStartPos.Init(df[29],df[30],df[31]);

  if(op->GetInput(0))
    return op->GetInput(0)->Cache;
  else
    return new GenScene;
}

class KObject * __stdcall Init_KKrieger_Events(KOp *op)
{
  if(op->GetInput(0))
    return op->GetInput(0)->Cache;
  else
    return new GenScene;
}

/****************************************************************************/

/****************************************************************************/

struct MonsterMem : public KInstanceMem
{
  KKriegerMonster Monster;
};

void __stdcall Exec_KKrieger_Monster(KOp *op,KEnvironment *kenv)
{
  KKriegerMonster *mon;
  sF32 *fp;
  sMatrix mat;
  sVector scale,offset,pos;

  MonsterMem *mem;

  mem = kenv->GetInst<MonsterMem>(op);
  mon = &mem->Monster;
  if(mem->Reset)
  {
    sSetMem(mon,0,sizeof(KKriegerMonster));
    mem->DeleteChild = &mon->Event.FirstInstance;
    mem->DeleteChild2 = &mon->DeathEvent.FirstInstance;
  }

  if(mem->Reset || kenv->Game->MonsterResetTicks)
  {
    fp = op->GetAnimPtrF(0);

    mon->Event.Init();
    mon->Event.Rotate.Init(fp[0],fp[1],fp[2],0);
    mon->Event.Translate.Init(fp[3],fp[4],fp[5],0);
    sCopyMem(&mon->Speed,fp+6,14*4);
    mon->Speed = mon->Speed*0.001f;
    mon->ShotOp = op->GetLink(0);
    mon->Life = mon->LifeMax;
    mon->State = KMS_ASLEEP;
    mon->Matrix = kenv->ExecStack.Top();
    mon->Event.Translate.Add3(mon->Matrix.l);
    mon->Event.Op = 0;
    mon->Event.ReturnMatrix.Init();
    mon->Event.ReturnMatrix.l = mon->Event.Translate;
    mon->Event.ReturnMatrix.l.w = 1.0f;
    mon->Event.Monster = mon;
    mon->DeathEvent.Init();

    // the hit box turns with the monster's rotation parameter

    mat.InitEuler(mon->Event.Rotate.x,mon->Event.Rotate.y,mon->Event.Rotate.z);
    scale.Init(fp[20],fp[21],fp[22],0);
    offset.Init(fp[23],fp[24],fp[25],0);
    mat.l.Rotate4(mat,offset);

    pos = mon->Matrix.l;
    pos.y += 0.5f;
    mon->Body.Init(pos,kenv->Game->FindCell(pos),KPK_MONSTER);
    mon->Body.HitCell.Init(mat,scale,KCM_SUB);
    mon->Body.HitCell.PrepareDynamic(mon->Matrix);
    mon->Body.HitCell.Monster = mon;
  }

  sVERIFY(op->GetInput(0));
  if(mon->Life>0)
  {
    mon->Body.HitCell.Matrix1 = mon->Matrix;
    *kenv->Game->Monsters.Add() = mon;
    if(mon->State!=KMS_ASLEEP)
      mon->Body.Add(kenv->Game);
    kenv->ExecStack.PushIdentity();
    op->GetInput(0)->ExecEvent(kenv,&mon->Event,1);
    kenv->ExecStack.Pop();
  }
}

class KObject * __stdcall Init_KKrieger_Monster(KOp *op)
{
  sInt i;

  for(i=0;i<op->GetInputCount();i++)
  {
    sVERIFY(op->GetInput(i));
    sVERIFY(op->GetInput(i)->Cache);
    op->GetInput(i)->Cache->Release();
  }
  return new GenScene();
}

/****************************************************************************/

void __stdcall Exec_KKrieger_MonsterFlags(KOp *op,KEnvironment *kenv,sInt kill,sInt spawn,sInt flags)
{
  kenv->NextMonsterOverride = 1;
  kenv->NextMonsterKillSwitch = kill;
  kenv->NextMonsterSpawnSwitch = spawn;
  kenv->NextMonsterFlags = flags;

  op->ExecInputs(kenv);

  kenv->NextMonsterOverride = 0;
}

class KObject * __stdcall Init_KKrieger_MonsterFlags(KOp *op)
{
  sVERIFY(op->GetInput(0));
  sVERIFY(op->GetInput(0)->Cache);
  return op->GetInput(0)->Cache;
//  op->GetInput(0)->Cache->Release();
//  return new GenScene();
}

/****************************************************************************/

class KObject * __stdcall Init_KKrieger_Respawn(sF323 pos,sInt zone,sF32 dir)
{
  return new GenScene;
}

void __stdcall Exec_KKrieger_Respawn(KOp *op,KEnvironment *kenv,sF323 pos,sInt zone,sF32 dir)
{
  kenv->Game->Respawn[zone].Valid = 1;
  kenv->Game->Respawn[zone].Pos.Init(pos.x,pos.y,pos.z);
  kenv->Game->Respawn[zone].Dir = dir*sPI2F;
}

/****************************************************************************/

class KObject * __stdcall Init_KKrieger_SplashDamage(sInt hits,sF32 range,sF32 enable)
{
  return new GenScene;
}

struct SplashDamageMem : public KInstanceMem
{
  sInt old;
};

void __stdcall Exec_KKrieger_SplashDamage(KOp *op,KEnvironment *kenv,sInt hits,sF32 range,sF32 enable)
{
  SplashDamageMem *mem;
  sBool ok;

  mem = kenv->GetInst<SplashDamageMem>(op);
  
  ok = (enable>0);
  if(ok && !mem->old)
  {
    kenv->Game->SplashEnable = 1;
    kenv->Game->SplashPos = kenv->ExecStack.Top().l;
    kenv->Game->SplashRange = range;
    kenv->Game->SplashHits = hits;
  }
  mem->old = ok;
}

/****************************************************************************/
/***                                                                      ***/
/***   Helpers                                                            ***/
/***                                                                      ***/
/****************************************************************************/

static sBool sMakePlane(sVector &p,const sVector &v0,const sVector &v1,const sVector &v2)
{
  sVector d0,d1,v;
  sF32 e;

  d0.Sub3(v0,v1);
  d1.Sub3(v1,v2);
  v.Cross3(d0,d1);

  if(v.Dot3(v)<1e-10) return sFALSE;

  e = 1.0f/sFSqrt(v.Dot3(v));
  v.x = v.x*e;
  v.y = v.y*e;
  v.z = v.z*e;
  v.w = v.Dot3(v1);

  p = v;
  return sTRUE;
}

void KKriegerCell::Init(const sMatrix &mat,const sVector &scale,sInt mode)
{
  sVector va[8];
  sInt i;

  for(i=0;i<8;i++)
  {
    va[i].x = scale.x * -(((i>>0)&1)-0.5f);
    va[i].y = scale.y * -(((i>>1)&1)-0.5f);
    va[i].z = scale.z * -(((i>>2)&1)-0.5f);
    va[i].w = 1;
    va[i].Rotate34(mat);
  }
  Init(va,mode);
}

void KKriegerCell::Init(const sVector *va,sInt mode)
{
  sInt k;
  sF32 d;
  sVector p;
  static sInt order[6][4] =
  {
    { 0,2,3,1 },
    { 2,6,7,3 },
    { 3,7,5,1 },
    { 4,0,1,5 },
    { 2,0,4,6 },
    { 7,6,4,5 },
  };

  sSetMem(this,0,sizeof(*this));

  Mode = mode&(3|KCM_ONLYMONSTER);
  OnlyForShot = (mode&KCM_NOTFORPLAYER)?1:0;

  for(k=0;k<6;k++)
  {
    if(sMakePlane(p,va[order[k][0]],va[order[k][1]],va[order[k][2]]))
    {
      // it's a quad. lets check if it's a plane one!
      d = p.Dot3(va[order[k][3]])-p.w;
      if(d<-0.005f)    // it's a bad one, and even inconvex
      {
        sMakePlane(p,va[order[k][1]],va[order[k][2]],va[order[k][3]]);
        sVERIFY(PlaneCount<KKRIEGER_MAXPLANES);
        Planes[PlaneCount++] = p;
        sMakePlane(p,va[order[k][3]],va[order[k][0]],va[order[k][1]]);
      }
      else if(d>0.005f)   // it's a bad one, but convex
      {
        sVERIFY(PlaneCount<KKRIEGER_MAXPLANES);
        Planes[PlaneCount++] = p;
        sMakePlane(p,va[order[k][2]],va[order[k][3]],va[order[k][0]]);
      }
      sVERIFY(PlaneCount<KKRIEGER_MAXPLANES);
      Planes[PlaneCount++] = p;
    }
    else    // a triangle: only the corners 2,3,0 make a plane here
    {
      if(sMakePlane(p,va[order[k][2]],va[order[k][3]],va[order[k][0]]))
      {
        sVERIFY(PlaneCount<KKRIEGER_MAXPLANES);
        Planes[PlaneCount++] = p;
      }
    }
  }

  for(k=0;k<PlaneCount;k++)
  {
    Planes[k].w = -Planes[k].w+OVERLAP;
    if((Mode&3)==KCM_SUB)         // extrude subs twice, so that subs directly on add's still overlap
      Planes[k].w += OVERLAP;
  }
  Center = BBMin = BBMax = va[0];
  for(k=1;k<8;k++)
  {
    Center.Add3(va[k]);
    BBMin.x = sMin(BBMin.x,va[k].x);
    BBMin.y = sMin(BBMin.y,va[k].y);
    BBMin.z = sMin(BBMin.z,va[k].z);
    BBMax.x = sMax(BBMax.x,va[k].x);
    BBMax.y = sMax(BBMax.y,va[k].y);
    BBMax.z = sMax(BBMax.z,va[k].z);
  }
  BBMin.x -= OVERLAP;
  BBMin.y -= OVERLAP;
  BBMin.z -= OVERLAP;
  BBMax.x += OVERLAP;
  BBMax.y += OVERLAP;
  BBMax.z += OVERLAP;
  Center.Scale3(1.0f/8);
  Center.w = 1;
}

/****************************************************************************/

void KKriegerCellDynamic::PrepareDynamic(sMatrix &mat)
{
  sInt i;
  for(i=0;i<PlaneCount;i++)
    OrigPlanes[i] = Planes[i];

  Mode |= KCM_UNCONFIRMED;
  Blocked = 0;
  ConfirmedMatrix = mat;
  CurrentMatrix = mat;
  Matrix0 = mat;
  Matrix1 = mat;
  PartBox = 0;
  Monster = 0;
}

void KKriegerCellDynamic::ApplyMatrix(sMatrix &mat)
{
  sInt i;
  sVector n,p;

  for(i=0;i<PlaneCount;i++)
  {
    n = OrigPlanes[i];
    p.Scale3(n,-n.w);                       // a point on the plane
    p.w = 1;
    n.Rotate3(mat);
    p.Rotate34(mat);
    n.Unit3();
    n.w = -n.Dot3(p);
    Planes[i] = n;
  }

  Center = mat.l;
  CurrentMatrix = mat;
  Blocked = 0;
}

/****************************************************************************/
