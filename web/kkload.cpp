// Loading .kkrieger from the original executable (pno0001.exe, Breakpoint 2004).
//
// 1. Unpack: the exe is compressed with an early kkrunchy (LZ + adaptive
//    multi-symbol range coder). KKUnpack is a port of the decompressor stub at
//    the exe's entry point; it yields the memory image the stub writes.
// 2. Read: the content is an early werkkzeug3 player export inside that image.
//    KKLoad2004 reads it into a KDoc. The format differs from the published
//    loader (werkkzeug3_kkrieger/kdoc.cpp KDoc::Init): one-byte class ids, no
//    BPM/length/root table, 16 extra bytes per Text operator, events without
//    the start/end interval. tools/kkdata.py documents the same format.
// 3. Convert: a few operator classes changed parameters between the release
//    and the published source. Their 2004 parameters are mapped onto the
//    layout and calling convention the source expects.

#include "_types.hpp"
#include "_start.hpp"
#include "kdoc.hpp"
#include "kkload.hpp"

sBool DistributeAnimR(KOp *start);        // kdoc.cpp
extern sBool KKriegerRelease2004;         // kkriegergame.cpp

/****************************************************************************/
/***                                                                      ***/
/***   kkrunchy decompressor                                              ***/
/***                                                                      ***/
/****************************************************************************/

namespace
{
  struct RangeModel
  {
    sU32 Limit;
    sU32 Total;
    sU32 Count;
    sU32 Freq[257];               // 1-based like the stub
  };

  struct RangeDecoder
  {
    const sU8 *Src;
    sU32 Low,Range;

    sInt Decode(RangeModel &m)
    {
      sU32 r = Range / m.Total;
      sU32 code = ((sU32)Src[0]<<24 | (sU32)Src[1]<<16 | (sU32)Src[2]<<8 | Src[3]) - Low;
      sU32 value = code / r;
      sS32 acc = (sS32) value;
      sInt sym = 0;
      do
      {
        sym++;
        acc -= m.Freq[sym];
      }
      while(acc >= 0);
      sU32 f = m.Freq[sym];
      m.Freq[sym] = f + 2;
      sU32 cumlow = value - (sU32)(acc + (sS32)f);
      sU32 low = Low + cumlow * r;
      sU32 rng = f * r;
      for(;;)
      {
        if(((low ^ (low + rng))) < 0x1000000u)
          ;
        else if(rng < 0x10000u)
          rng = (0u - low) & 0xffff;
        else
          break;
        Src++;
        low <<= 8;
        rng <<= 8;
      }
      Low = low;
      Range = rng;

      // adapt
      sU32 total = m.Total + 2;
      if(total >= m.Limit)
      {
        for(sU32 i=1;i<=m.Count;i++)
        {
          sU32 h = m.Freq[i] >> 1;
          m.Freq[i] -= h;
          total -= h;
        }
      }
      m.Total = total;
      return sym - 1;
    }

    sU32 Gamma(RangeModel &m)
    {
      sU32 v = 1;
      for(;;)
      {
        sInt s = Decode(m);
        v = v*2 + (s & 1);                  // 32-bit wrap marks the end of the stream
        if((s >> 1) == 0)
          return v;
      }
    }
  };

  sU32 Read32(const sU8 *p) { return p[0] | p[1]<<8 | p[2]<<16 | (sU32)p[3]<<24; }
}

sBool KKUnpack(const sU8 *exe,sInt size,sU8 *&out,sInt &outSize,sU32 &outVA)
{
  out = 0;
  outSize = 0;
  if(size < 0x400 || exe[0]!='M' || exe[1]!='Z')
    return sFALSE;
  sU32 pe = Read32(exe+0x3c);
  if(pe+0x100 > (sU32)size || Read32(exe+pe)!=0x00004550)
    return sFALSE;
  sU32 opt = pe + 24;
  sU32 base = Read32(exe+opt+28);
  sU32 entry = Read32(exe+opt+16);
  // kkrunchy files are laid out like their memory image (file offset == rva)
  if(entry+0x40 > (sU32)size || exe[entry]!=0xbd)
    return sFALSE;

  sU32 src = Read32(exe+entry+8) - base;    // mov dword [ebp],imm32
  sU32 table = Read32(exe+entry+0x16) - base;  // mov esi,imm32
  outVA = Read32(exe+entry+0x37);           // mov edi,imm32
  if(src >= (sU32)size || table+16 > (sU32)size)
    return sFALSE;

  static RangeModel models[8];
  for(sInt k=0;k<8;k++)
  {
    RangeModel &m = models[7-k];            // the stub stores them as 7,6,...,0
    m.Limit = exe[table+2*k] << 3;
    m.Count = exe[table+2*k+1] + 1;
    m.Total = m.Count;
    for(sU32 i=1;i<=m.Count;i++)
      m.Freq[i] = 1;
  }

  RangeDecoder rd;
  rd.Src = exe + src;
  rd.Low = 0;
  rd.Range = 0xffffffff;

  sInt alloc = 1<<20;
  sU8 *dst = (sU8 *) malloc(alloc);
  sInt n = 0;
  sU32 r0 = 0;
  sInt state = -1;                          // -1 after a literal, 0 after a match
  sBool literal = sTRUE;
  const sU8 *srcEnd = exe + size;

  for(;;)
  {
    if(rd.Src+4 > srcEnd)
    {
      free(dst);
      return sFALSE;
    }
    if(n + 0x10000 > alloc)
    {
      alloc *= 2;
      dst = (sU8 *) realloc(dst,alloc);
    }
    if(literal)
    {
      dst[n++] = rd.Decode(models[0]);
      state = -1;
    }
    if(rd.Decode(models[4+state]) == 0)
    {
      literal = sTRUE;
      continue;
    }
    literal = sFALSE;
    sU32 len,offs;
    if(state!=0 && rd.Decode(models[7])!=0)
    {
      len = rd.Gamma(models[2]);
      offs = r0;
    }
    else
    {
      sS32 hi = (sS32)(rd.Gamma(models[1]) - 2);
      if(hi < 0)
        break;
      offs = (hi<<4) + rd.Decode(hi ? models[6] : models[5]) + 1;
      len = rd.Gamma(models[2]);
      if(offs >= 0x800) len++;
      if(offs >= 0x60) len++;
    }
    r0 = offs;
    if(offs > (sU32)n || len > 0x1000000)
    {
      free(dst);
      return sFALSE;
    }
    while(n + (sInt)len + 16 > alloc)
    {
      alloc *= 2;
      dst = (sU8 *) realloc(dst,alloc);
    }
    for(sU32 i=0;i<len;i++,n++)
      dst[n] = dst[n-offs];
    state = 0;
  }

  out = dst;
  outSize = n;
  return sTRUE;
}

/****************************************************************************/
/***                                                                      ***/
/***   2004 export reader                                                 ***/
/***                                                                      ***/
/****************************************************************************/

namespace
{
  const sInt MaxClasses = 128;

  struct Class2004
  {
    sU32 Convention;                // as stored (calling convention of the 2004 code)
    sInt Id;
    const sChar *Packing;
    const PortThunk *Thunk;         // handler in the port
  };

  struct Reader
  {
    const sU8 *P;
    sU8 U8() { return *P++; }
    sU16 U16() { sU16 v = P[0] | P[1]<<8; P+=2; return v; }
    sS16 S16() { return (sS16) U16(); }
    sU32 U32() { sU32 v = Read32(P); P+=4; return v; }
    sInt Short() { sInt v = *P++; if(v&128) v = (v&127) | (*P++ << 7); return v; }
    sF32 F16() { return sReadF16(P); }
    sF32 F24() { return sReadF24(P); }
    sF32 X16() { return sReadX16(P); }
    const sChar *CStr() { const sChar *s = (const sChar *) P; while(*P) P++; P++; return s; }
  };

  sU32 FloatBits(sF32 f) { sU32 u; sCopyMem(&u,&f,4); return u; }

  // find the export: flags word, optional 32 bytes, song size, V2M header (0x60)
  sInt FindExport(const sU8 *image,sInt size)
  {
    for(sInt p=0;p+48<size;p++)
    {
      sU32 flags = Read32(image+p);
      if(flags & ~0x47u) continue;
      sInt q = p + 4 + ((flags&1) ? 32 : 0);
      sU32 song = Read32(image+q);
      if(song < 1000 || song > 200000) continue;
      if(Read32(image+q+4) != 0x60) continue;
      return p;
    }
    return -1;
  }

  // map a 2004 class id onto the port's handler table
  const PortThunk *ThunkFor2004(sInt id)
  {
    if(id == 0xe0) id = 0x03;       // 2004-only scene->viewport link without parameters: pass-through
    return PortFindThunk(id);
  }
}

static void Unpack2004Params(Reader &r,const sChar *pack,sU32 *words)
{
  for(sInt j=0;pack[j];j++)
  {
    sInt t = pack[j];
    if(t>='A' && t<='Z') t += 0x20;
    switch(t)
    {
    case 'g': words[j] = FloatBits(r.F16()); break;
    case 'f': words[j] = FloatBits(r.F24()); break;
    case 'e': words[j] = FloatBits(r.X16()); break;
    case 'i': words[j] = r.U32(); break;
    case 's': words[j] = (sU32)(sS32) r.S16(); break;
    case 'b': words[j] = r.U8(); break;
    case 'c': words[j] = r.U32(); break;
    case 'm': words[j] = r.U16(); words[j] |= r.U8() << 16; break;
    default:  words[j] = 0; break;
    }
  }
}

// Map 2004 parameter words (in) onto the published code's layout (out, zeroed).
static void Convert2004(sInt id,const sU32 *in,sInt inCount,sU32 *out,sInt outCount)
{
  const sU32 one = FloatBits(1.0f);
  switch(id)
  {
  case 0x31:  // Text: x,y,w,h,col,flags,extspace,inspace(byte) -> x,y,w,h,col,flags,extspace,inspace,lineskip
    for(sInt i=0;i<7;i++) out[i] = in[i];
    out[7] = FloatBits(in[7]/65536.0f);    // internal space of font pages (0 for ordinary text)
    out[8] = one;
    break;
  case 0x61:  // Print: flags(b),size2,space2 -> flags(i),size2,space2,radius
    for(sInt i=0;i<5;i++) out[i] = in[i];
    out[0] = (in[0] & ~1) | ((in[0] & 1) ? 0x100 : 0);   // bit 0 (centered) became the alignment field 0x300
    out[5] = 0;
    break;
  case 0xf0:  // Viewport: only the size in 2004
    out[0] = in[0];                 // size
    out[1] = 0x8003;                // clear color + z-buffer, camera set by the scene
    out[2] = 0x00000000;            // clear color (transparent: overlays are merged by alpha)
    out[9] = FloatBits(4096.0f);    // far clip
    out[10] = FloatBits(0.125f);    // near clip
    out[13] = one; out[14] = one;   // zoom
    out[15] = 0xff000000;           // fog color
    out[16] = FloatBits(4096.0f);   // far fog
    out[17] = FloatBits(16.0f);     // near fog
    out[22] = one; out[23] = one;   // window lower right
    break;
  default:    // same layout, possibly with parameters appended since 2004 (zero)
    for(sInt i=0;i<inCount && i<outCount;i++) out[i] = in[i];
    break;
  }
}

// The 2004 export does not store the tempo; the timeline runs in 16.16 beats
// at the song's tempo, which the V2M header holds as microseconds per beat
// (time division, max time, global event count, then the global events with
// each field stored byte-plane by byte-plane).
static sInt SongTempo(const sU8 *song,sInt size)
{
  if(size<16)
    return 120<<16;
  sU32 gdnum = song[8] | (song[9]<<8) | (song[10]<<16) | (song[11]<<24);
  if(gdnum<1 || 12+10*gdnum>(sU32)size)
    return 120<<16;
  const sU8 *usec = song+12+3*gdnum;     // after the 3 time-delta planes
  sU32 us = usec[0] | (usec[gdnum]<<8) | (usec[2*gdnum]<<16) | (usec[3*gdnum]<<24);
  if(us==0)
    return 120<<16;
  return (sInt)(60000000ULL*65536/us);
}

sBool KKLoad2004(KDoc *doc,const sU8 *image,sInt size)
{
  sInt start = FindExport(image,size);
  if(start < 0)
    return sFALSE;

  Reader r;
  r.P = image + start;
  sU32 flags = r.U32();
  if(flags & 1) r.P += 32;
  doc->BuzzTiming = flags & 4;
  doc->SongSize = r.U32();
  doc->SongData = (sU8 *) r.P;
  r.P += sAlign(doc->SongSize,4);
  doc->SampleSize = 0;
  doc->SampleData = 0;
  if(flags & 2)
  {
    doc->SampleSize = r.U32();
    doc->SampleData = (sU8 *) r.P;
    r.P += sAlign(doc->SampleSize,4);
  }
  doc->SongBPM = SongTempo(doc->SongData,doc->SongSize);
  doc->SongLength = 0;
  sInt nOps = r.Short();
  sInt nSplines = r.Short();

  static Class2004 classes[MaxClasses];
  sInt nClasses = 0;
  for(;;)
  {
    sU32 conv = r.U32();
    if(conv == 0) break;
    if(nClasses >= MaxClasses) return sFALSE;
    Class2004 &c = classes[nClasses++];
    c.Convention = conv;
    c.Id = r.U8();
    c.Packing = r.CStr();
    c.Thunk = ThunkFor2004(c.Id);
    if(!c.Thunk)
    {
      { sChar buf[64]; sSPrintF(buf,sizeof(buf),"kkload: no handler for class %02x",c.Id); sSystem->Log(buf); }
      return sFALSE;
    }
  }

  // one extra op at the end: a Demo root over the last op, as the player expects
  sInt total = nOps + 1;
  doc->Ops.Init(total);
  doc->Ops.Count = total;
  doc->Splines.Init(nSplines);
  doc->Splines.Count = nSplines;

  static sInt opClass[16384];
  static sInt extraInput[16384];          // 2004 links that the source takes as inputs
  if(total > 16384) return sFALSE;

  for(sInt i=0;i<nOps;i++)
  {
    sInt typeByte = r.U8();
    sInt ci = typeByte & 0x7f;
    if(ci >= nClasses) return sFALSE;
    Class2004 &c = classes[ci];
    const PortThunk *t = c.Thunk;
    KOp *op = &doc->Ops[i];
    sSetMem(op,0,sizeof(KOp));
    op->Init(t->Convention);
    op->Command = ci;
    op->Convention = t->Convention;
    op->InitHandler = t->InitFn;
    op->ExecHandler = t->ExecFn;
    op->InitThunk = t->Init;
    op->ExecThunk = t->Exec;
    op->OpId = i;
    op->AnimFlag = sTRUE;
    opClass[i] = ci;
    extraInput[i] = -1;

    sInt max;
    if(typeByte & 0x80)
      max = 1;
    else
      max = (c.Convention & 0x80000000) ? r.U8() : OPC_GETINPUT(c.Convention);
    for(sInt in=0;in<max;in++)
    {
      sInt delta = (typeByte & 0x80) ? 0 : r.Short();
      KOp *dest = &doc->Ops[i-1-delta];
      op->AddInput(dest);
      dest->AddOutput(op);
    }
  }

  for(sInt i=0;i<nOps;i++)
  {
    Class2004 &c = classes[opClass[i]];
    KOp *op = &doc->Ops[i];
    for(sInt in=0;in<OPC_GETLINK(c.Convention);in++)
    {
      sInt k = r.Short();
      KOp *dest = k ? &doc->Ops[k-1] : 0;
      if(c.Id == 0x11 && in == 0)           // Monster: the 2004 link became the second input
      {
        if(dest) extraInput[i] = k-1;
        continue;
      }
      if(in < OPC_GETLINK(op->Convention))
      {
        op->SetLink(in,dest);
        if(dest) dest->AddOutput(op);
      }
    }
  }
  for(sInt i=0;i<nOps;i++)
  {
    if(extraInput[i] >= 0)
    {
      doc->Ops[i].AddInput(&doc->Ops[extraInput[i]]);
      doc->Ops[extraInput[i]].AddOutput(&doc->Ops[i]);
    }
  }

  // parameters, sorted by class
  sU32 in[256],out[256];
  for(sInt ci=0;ci<nClasses;ci++)
  {
    Class2004 &c = classes[ci];
    for(sInt i=0;i<nOps;i++)
    {
      if(opClass[i] != ci) continue;
      KOp *op = &doc->Ops[i];
      sInt inCount = sGetStringLen(c.Packing);
      sSetMem(in,0,sizeof(in));
      sSetMem(out,0,sizeof(out));
      Unpack2004Params(r,c.Packing,in);
      sInt outCount = OPC_GETDATA(op->Convention);
      Convert2004(c.Id,in,inCount,out,outCount);
      for(sInt j=0;j<outCount;j++)
        *op->GetEditPtrU(j) = out[j];

      for(sInt j=0;j<OPC_GETSTRING(c.Convention);j++)
      {
        const sChar *s = r.CStr();
        if(j < OPC_GETSTRING(op->Convention))
          op->SetString(j,(sChar *)s);
      }
      for(sInt j=0;j<OPC_GETSPLINE(c.Convention);j++)
      {
        sInt k = r.U16();
        if(j < OPC_GETSPLINE(op->Convention))
          op->SetSpline(j,k ? &doc->Splines[k-1] : 0);
      }
      if(c.Convention & OPC_BLOB)
        op->SetBlob(0,r.U32());
      if(c.Id == 0x31 && c.Convention == 0x06820108)
        r.P += 16;                          // unused bytes after the strings (always zero)
    }
  }

  // animation bytecode
  for(sInt i=0;i<nOps;i++)
  {
    sInt j = CalcCodeSize((sU8 *)r.P);
    doc->Ops[i].SetAnimCode((sU8 *)r.P,j);
    r.P += j;
  }

  // events
  doc->Events.Init();
  sInt eventCount = r.Short();
  doc->Events.AtLeast(eventCount);
  while(eventCount--)
  {
    KEvent *event = doc->Events.Add();
    event->Init();
    event->Op = &doc->Ops[r.Short()];
    event->Start = (sInt) r.U32();
    event->End = (sInt) r.U32();
    event->Velocity = r.F24();
    event->Modulation = r.F24();
    event->Select = r.U8();
    event->Scale.x = r.F24();
    event->Scale.y = r.F24();
    event->Scale.z = r.F24();
    event->Rotate.x = r.F24();
    event->Rotate.y = r.F24();
    event->Rotate.z = r.F24();
    event->Translate.x = r.F24();
    event->Translate.y = r.F24();
    event->Translate.z = r.F24();
    event->Color = r.U32();
    sInt k = r.Short();
    event->Spline = k ? &doc->Splines[k-1] : 0;
  }

  // splines
  for(sInt i=0;i<nSplines;i++)
  {
    KSpline *spline = &doc->Splines[i];
    sInt j = r.U8();
    spline->Count = j & 7;
    spline->Interpolation = j >> 3;
    for(j=0;j<spline->Count;j++)
    {
      sInt k = r.Short();
      spline->Channel[j].KeyCount = k;
      spline->Channel[j].Keys = k ? new KSplineKey[k] : 0;
      for(k=0;k<spline->Channel[j].KeyCount;k++)
      {
        spline->Channel[j].Keys[k].Time = r.U8() / 255.0f;
        spline->Channel[j].Keys[k].Value = r.F16();
      }
    }
  }

  // blobs
  for(sInt i=0;i<nOps;i++)
  {
    sInt j = doc->Ops[i].GetBlobSize();
    if(j)
    {
      doc->Ops[i].SetBlobData(r.P);
      r.P += j;
    }
  }

  // the Demo root
  {
    const PortThunk *t = PortFindThunk(0x0d);
    KOp *op = &doc->Ops[nOps];
    sSetMem(op,0,sizeof(KOp));
    op->Init(t->Convention);
    op->Command = nClasses;
    op->Convention = t->Convention;
    op->InitHandler = t->InitFn;
    op->ExecHandler = t->ExecFn;
    op->InitThunk = t->Init;
    op->ExecThunk = t->Exec;
    op->OpId = nOps;
    op->AnimFlag = sTRUE;
    op->AddInput(&doc->Ops[nOps-1]);
    doc->Ops[nOps-1].AddOutput(op);
    static sU8 endCode = KA_END;
    op->SetAnimCode(&endCode,1);
  }

  DistributeAnimR(&doc->Ops[nOps]);
  KKriegerRelease2004 = sTRUE;
  extern sBool Material11InvertAlpha;
  Material11InvertAlpha = sTRUE;
  for(sInt i=0;i<MAX_OP_ROOT;i++)
    doc->RootOps[i] = &doc->Ops[nOps];
  doc->CurrentRoot = 0;

  {
    sChar buf[160];
    sSPrintF(buf,sizeof(buf),"kkload: %d ops, %d classes, %d events, %d splines, %d bytes of export",
      nOps,nClasses,doc->Events.Count,nSplines,(sInt)(r.P-(image+start)));
    sSystem->Log(buf);
  }
  return sTRUE;
}
