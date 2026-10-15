// Instruction-for-instruction C translations of the MMX assembly in genbitmap.cpp
// (werkkzeug3_kkrieger version: older bilinear filter, no min/max blend modes).
// Register names follow the original code so the two can be compared side by side.

using namespace mmx;

void Fade64(sU64 &r,sU64 &c0,sU64 &c1,sInt fade)
{
  static const sU64 xor0 = 0xffffffff00000000ULL;
  static const sU64 add0 = 0x0000800100000000ULL;
  mmreg mm0,mm1,mm2,mm3;

  mm2 = c0;
  mm3 = c1;
  mm0 = movd(fade);
  mm0 = punpckldq(mm0,mm0);
  mm0 = psrad(mm0,1);
  mm0 ^= xor0;
  mm0 = paddd(mm0,add0);
  mm0 = packssdw(mm0,mm0);
  mm0 = punpcklwd(mm0,mm0);
  mm1 = mm0;
  mm0 = punpcklwd(mm0,mm0);
  mm1 = punpckhwd(mm1,mm1);
  mm3 = pmulhw(mm3,mm0);
  mm2 = pmulhw(mm2,mm1);
  mm2 = paddw(mm2,mm3);
  mm2 = psllw(mm2,1);
  r = mm2;
}

void AddScale64(sU64 &r,sU64 &c0,sU64 &c1,sInt fade)
{
  mmreg mm0,mm2,mm3;

  mm2 = c0;
  mm3 = c1;
  mm0 = movd(fade);
  mm0 = psrad(mm0,1);
  mm0 = packssdw(mm0,mm0);
  mm0 = punpcklwd(mm0,mm0);
  mm0 = punpcklwd(mm0,mm0);
  mm3 = pmulhw(mm3,mm0);
  mm3 = psllw(mm3,1);
  mm2 = paddsw(mm2,mm3);
  r = mm2;
}

void BilinearFilter(BilinearContext *ctx,sU64 *r,sInt u,sInt v)
{
  static const sU64 adjust = 0x8000800080008000ULL;
  mmreg mm0,mm1,mm2,mm3,mm4,mm5,mm6;
  sU32 eax,ebx,edx;
  const sU8 *edi = (const sU8 *) ctx->Src;
  const sU8 *rowB;
  sInt shift = ctx->XShift;

  mm1 = movd(v);

  // calc s0: row v>>16, clamped (or wrapped through YAm)
  eax = (sU32)((sS32)v >> 16);
  ebx = (sU32)((sS32)((sU32)v - 0x80000000u) >> 31);
  edx = eax;
  eax &= ctx->YAm;
  ebx &= ctx->YSize1;
  if(eax > (sU32)ctx->YSize1) eax = ebx;
  edi += eax << shift;

  mm0 = movd(u);

  // calc s1
  eax = edx + 1;
  ebx = (sU32)((sS32)(edx + 0x7fffffffu) >> 31);
  eax &= ctx->YAm;
  ebx &= ctx->YSize1;
  if(eax > (sU32)ctx->YSize1) eax = ebx;
  rowB = (const sU8 *) ctx->Src + (eax << shift);

  // u+0 (byte offsets, 8 bytes per pixel)
  eax = (sU32)((sS32)u >> 13);
  ebx = (sU32)((sS32)((sU32)u - 0x80000000u) >> 31);
  edx = eax;
  eax &= ctx->XAm;
  ebx &= ctx->XSize1;
  if(eax > (sU32)ctx->XSize1) eax = ebx;
  mm2 = *(const sU64 *)(edi+eax);
  mm4 = *(const sU64 *)(rowB+eax);

  // u+1
  eax = edx + 8;
  ebx = (sU32)((sS32)(edx + 0x7ffffff8u) >> 31);
  eax &= ctx->XAm;
  ebx &= ctx->XSize1;
  if(eax > (sU32)ctx->XSize1) eax = ebx;
  mm3 = *(const sU64 *)(edi+eax);
  mm5 = *(const sU64 *)(rowB+eax);

  // actual sample
  mm6 = adjust;
  mm0 = punpcklwd(mm0,mm0);
  mm1 = punpcklwd(mm1,mm1);
  mm0 = punpcklwd(mm0,mm0);
  mm1 = punpcklwd(mm1,mm1);
  mm0 = psrlw(mm0,1);
  mm1 = psrlw(mm1,1);

  // horiz
  mm3 = psubw(mm3,mm2);
  mm5 = psubw(mm5,mm4);
  mm3 = pmulhw(mm3,mm0);
  mm5 = pmulhw(mm5,mm0);
  mm3 = psllw(mm3,1);
  mm5 = psllw(mm5,1);
  mm2 = paddw(mm2,mm3);
  mm4 = paddw(mm4,mm5);

  // vert+write
  mm4 = psubw(mm4,mm2);
  mm2 = paddw(mm2,mm6);
  mm4 = pmulhw(mm4,mm1);
  mm4 = psllw(mm4,1);
  mm2 = paddw(mm2,mm4);
  mm2 = psubusw(mm2,mm6);
  *r = mm2;
}

/****************************************************************************/

void __stdcall Bitmap_Inner(sU64 *d,sU64 *s,sInt count,sInt mode,sU64 *x)
{
  mmreg mm0,mm1,mm2,mm3,mm4,mm5,mm6,mm7;

  mm3 = 0x7fff7fff7fff7fffULL;
  mm4 = psrlw(0x0000ffffffffffffULL,1);   // 0x00007fff7fff7fff
  mm5 = psrlw(0xffff000000000000ULL,1);   // 0x7fff000000000000
  mm2 = s[0];
  mm7 = 0x3fff3fff3fff3fffULL;
  mm6 = 0;
  mm1 = 0;

  if(mode>=BI_RANGE)
  {
    mm7 = psubsw(s[1],mm2);
    mm1 = 0;
  }

  // the source pointers walk along with the destination for per-pixel modes
  for(sInt i=0;i<count;i++)
  {
    const sU64 *ps = s+i;           // esi
    const sU64 *px = x+i;           // ebx
    sU64 *pd = d+i;                 // edi

    switch(mode)
    {
    case BI_ADD:
      mm0 = paddsw(*px,*ps);
      break;
    case BI_SUB:
      mm0 = psubusw(*px,*ps);
      break;
    case BI_MUL:
      mm0 = psllw(pmulhw(*px,*ps),1);
      break;
    case BI_DIFF:
      mm0 = psubw(*px,*ps);
      mm0 = paddw(mm0,mm3);
      mm0 = psrlw(mm0,1);
      break;
    case BI_ALPHA:
      mm0 = *ps;
      mm1 = *px;
      mm6 = mm0;
      mm7 = mm0;
      mm6 = psrlq(mm6,16);
      mm7 = psrlq(mm7,32);
      mm0 = paddw(mm0,mm6);
      mm7 = paddw(mm7,mm6);
      mm0 = psrlw(mm0,1);
      mm7 = psrlw(mm7,1);
      mm0 = paddw(mm0,mm7);
      mm1 &= mm4;
      mm0 = psllq(mm0,47);
      mm0 &= mm5;
      mm0 |= mm1;
      break;
    case BI_MULCOL:
      mm0 = psllw(pmulhw(*px,mm2),1);
      break;
    case BI_ADDCOL:
      mm0 = paddsw(*px,mm2);
      break;
    case BI_SUBCOL:
      mm0 = psubusw(*px,mm2);
      break;
    case BI_GRAY:
      mm0 = *px;
      mm6 = mm0;
      mm7 = mm0;
      mm6 = psrlq(mm6,16);
      mm7 = psrlq(mm7,32);
      mm0 = paddw(mm0,mm6);
      mm7 = paddw(mm7,mm6);
      mm0 = psrlw(mm0,1);
      mm7 = psrlw(mm7,1);
      mm0 = paddw(mm0,mm7);
      mm0 = psrlw(mm0,1);
      mm0 = punpcklwd(mm0,mm0);
      mm0 = punpcklwd(mm0,mm0);
      mm0 |= mm5;
      break;
    case BI_INVERT:
      mm0 = *px ^ mm3;
      break;
    case BI_SCALECOL:
      mm0 = *px;
      mm1 = mm0;
      mm0 = pmullw(mm0,mm2);
      mm1 = pmulhw(mm1,mm2);
      mm6 = mm0;
      mm0 = punpcklwd(mm0,mm1);
      mm6 = punpckhwd(mm6,mm1);
      mm0 = psrld(mm0,11);
      mm6 = psrld(mm6,11);
      mm0 = packssdw(mm0,mm6);
      break;
    case BI_MERGE:
      mm0 = *pd;
      mm4 = *ps;
      mm5 = *px;
      mm1 = mm3;
      mm1 = psubsw(mm1,mm0);
      mm0 = pmulhw(mm0,mm4);
      mm1 = pmulhw(mm1,mm5);
      mm0 = paddsw(mm0,mm1);
      mm0 = psllw(mm0,1);
      break;
    case BI_BRIGHTNESS:
      mm0 = *ps;
      mm1 = *px;
      mm2 = mm0;
      mm2 = pcmpgtw(mm2,mm7);
      mm2 = psrlw(mm2,1);
      mm0 ^= mm2;
      mm1 ^= mm2;
      mm0 = pmulhw(mm0,mm1);
      mm0 = psllw(mm0,2);
      mm0 ^= mm2;
      break;
    case BI_SUBR:
      mm0 = psubusw(*ps,*px);
      break;
    case BI_MULMERGE:
      mm0 = *pd;
      mm4 = *ps;
      mm5 = *px;
      mm1 = mm3;
      mm4 = pmulhw(mm4,mm5);
      mm4 = psllw(mm4,1);
      mm1 = psubsw(mm1,mm0);
      mm0 = pmulhw(mm0,mm4);
      mm1 = pmulhw(mm1,mm5);
      mm0 = paddsw(mm0,mm1);
      mm0 = psllw(mm0,1);
      break;
    case BI_SHARPEN:
      mm0 = *px;
      mm3 = mm0;
      mm0 = psubusw(mm0,*pd);
      mm1 = mm0;
      mm0 = pmullw(mm0,mm2);
      mm1 = pmulhw(mm1,mm2);
      mm7 = mm0;
      mm0 = punpcklwd(mm0,mm1);
      mm7 = punpckhwd(mm7,mm1);
      mm0 = psrad(mm0,11);
      mm7 = psrad(mm7,11);
      mm0 = packssdw(mm0,mm7);
      mm0 = paddsw(mm0,mm3);
      mm0 = pmaxsw(mm0,mm6);
      break;
    case BI_HARDLIGHT:
      mm1 = *ps;
      mm0 = *px;
      mm1 = psllw(mm1,1);
      mm2 = mm1;
      mm1 &= mm3;
      mm2 = psraw(mm2,15);
      mm4 = mm0;
      mm0 = pmulhw(mm0,mm1);
      mm0 = psllw(mm0,1);
      mm1 = paddw(mm1,mm4);
      mm0 ^= mm2;
      mm1 &= mm2;
      mm0 = paddw(mm0,mm1);
      break;
    case BI_OVER:
      mm0 = *px;
      mm1 = *ps;
      mm2 = mm1;
      mm1 = psubsw(mm1,mm0);
      mm2 = punpckhwd(mm2,mm2);
      mm2 = punpckhwd(mm2,mm2);
      mm1 = pmulhw(mm1,mm2);
      mm1 = psllw(mm1,1);
      mm0 = paddsw(mm0,mm1);
      mm0 = pmaxsw(mm0,mm6);
      break;
    case BI_ADDSMOOTH:
      mm0 = *px ^ mm3;
      mm1 = *ps ^ mm3;
      mm0 = pmulhw(mm0,mm1);
      mm0 = psllw(mm0,1);
      mm0 ^= mm3;
      break;
#ifdef BI_MIN
    case BI_MIN:
      mm0 = pminsw(*px,*ps);
      break;
    case BI_MAX:
      mm0 = pmaxsw(*px,*ps);
      break;
#endif
    default:                        // BI_RANGE (and anything above, as in the jump chain)
      mm0 = *px;
      mm0 = pmulhw(mm0,mm7);
      mm0 = psllw(mm0,1);
      mm0 = paddsw(mm0,mm2);
      mm0 = pmaxsw(mm0,mm1);
      break;
    }
    *pd = mm0;
  }
}

