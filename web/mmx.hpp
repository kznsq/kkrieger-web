// Scalar emulation of the MMX instructions used by the werkkzeug3 texture code.
// Each function mirrors one instruction on a 64-bit register value, so the
// original assembly can be transliterated line by line with identical results.

#ifndef __MMX_HPP__
#define __MMX_HPP__

#include "_types.hpp"

typedef sU64 mmreg;

namespace mmx
{
  inline sS16 w(mmreg a,sInt i)              { return (sS16)(a>>(16*i)); }
  inline sU16 uw(mmreg a,sInt i)             { return (sU16)(a>>(16*i)); }
  inline sS32 d(mmreg a,sInt i)              { return (sS32)(a>>(32*i)); }
  inline mmreg mkw(sInt w0,sInt w1,sInt w2,sInt w3)
  {
    return (mmreg)(sU16)w0 | ((mmreg)(sU16)w1<<16) | ((mmreg)(sU16)w2<<32) | ((mmreg)(sU16)w3<<48);
  }
  inline mmreg mkd(sInt d0,sInt d1)          { return (mmreg)(sU32)d0 | ((mmreg)(sU32)d1<<32); }
  inline sInt ssat16(sInt v)                 { return v<-32768 ? -32768 : v>32767 ? 32767 : v; }
  inline sInt usat16(sInt v)                 { return v<0 ? 0 : v>65535 ? 65535 : v; }

  template <class F> inline mmreg perw(mmreg a,mmreg b,F f)
  {
    return mkw(f(w(a,0),w(b,0),uw(a,0),uw(b,0)),f(w(a,1),w(b,1),uw(a,1),uw(b,1)),
               f(w(a,2),w(b,2),uw(a,2),uw(b,2)),f(w(a,3),w(b,3),uw(a,3),uw(b,3)));
  }

  inline mmreg paddw(mmreg a,mmreg b)    { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x+y; }); }
  inline mmreg psubw(mmreg a,mmreg b)    { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x-y; }); }
  inline mmreg paddsw(mmreg a,mmreg b)   { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return ssat16(x+y); }); }
  inline mmreg psubsw(mmreg a,mmreg b)   { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return ssat16(x-y); }); }
  inline mmreg paddusw(mmreg a,mmreg b)  { return perw(a,b,[](sInt,sInt,sInt x,sInt y){ return usat16(x+y); }); }
  inline mmreg psubusw(mmreg a,mmreg b)  { return perw(a,b,[](sInt,sInt,sInt x,sInt y){ return usat16(x-y); }); }
  inline mmreg pmulhw(mmreg a,mmreg b)   { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return (x*y)>>16; }); }
  inline mmreg pmulhuw(mmreg a,mmreg b)  { return perw(a,b,[](sInt,sInt,sInt x,sInt y){ return (sInt)(((sU32)x*(sU32)y)>>16); }); }
  inline mmreg pmullw(mmreg a,mmreg b)   { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x*y; }); }
  inline mmreg pmaxsw(mmreg a,mmreg b)   { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x>y?x:y; }); }
  inline mmreg pminsw(mmreg a,mmreg b)   { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x<y?x:y; }); }
  inline mmreg pcmpgtw(mmreg a,mmreg b)  { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x>y?-1:0; }); }
  inline mmreg pcmpeqw(mmreg a,mmreg b)  { return perw(a,b,[](sInt x,sInt y,sInt,sInt){ return x==y?-1:0; }); }

  inline mmreg psllw(mmreg a,sInt n)     { return n>15 ? 0 : perw(a,0,[n](sInt,sInt,sInt x,sInt){ return x<<n; }); }
  inline mmreg psrlw(mmreg a,sInt n)     { return n>15 ? 0 : perw(a,0,[n](sInt,sInt,sInt x,sInt){ return x>>n; }); }
  inline mmreg psraw(mmreg a,sInt n)     { if(n>15) n=15; return perw(a,0,[n](sInt x,sInt,sInt,sInt){ return x>>n; }); }
  inline mmreg psrld(mmreg a,sInt n)     { return n>31 ? 0 : mkd((sU32)d(a,0)>>n,(sU32)d(a,1)>>n); }
  inline mmreg psrad(mmreg a,sInt n)     { if(n>31) n=31; return mkd(d(a,0)>>n,d(a,1)>>n); }
  inline mmreg pslld(mmreg a,sInt n)     { return n>31 ? 0 : mkd((sU32)d(a,0)<<n,(sU32)d(a,1)<<n); }
  inline mmreg psrlq(mmreg a,sInt n)     { return n>63 ? 0 : a>>n; }
  inline mmreg psllq(mmreg a,sInt n)     { return n>63 ? 0 : a<<n; }

  inline mmreg paddd(mmreg a,mmreg b)    { return mkd(d(a,0)+d(b,0),d(a,1)+d(b,1)); }
  inline mmreg psubd(mmreg a,mmreg b)    { return mkd(d(a,0)-d(b,0),d(a,1)-d(b,1)); }
  inline mmreg pcmpgtd(mmreg a,mmreg b)  { return mkd(d(a,0)>d(b,0)?-1:0,d(a,1)>d(b,1)?-1:0); }
  inline mmreg pmaddwd(mmreg a,mmreg b)
  {
    return mkd(w(a,0)*w(b,0)+w(a,1)*w(b,1),w(a,2)*w(b,2)+w(a,3)*w(b,3));
  }

  inline mmreg punpcklwd(mmreg a,mmreg b){ return mkw(w(a,0),w(b,0),w(a,1),w(b,1)); }
  inline mmreg punpckhwd(mmreg a,mmreg b){ return mkw(w(a,2),w(b,2),w(a,3),w(b,3)); }
  inline mmreg punpckldq(mmreg a,mmreg b){ return mkd(d(a,0),d(b,0)); }
  inline mmreg punpckhdq(mmreg a,mmreg b){ return mkd(d(a,1),d(b,1)); }
  inline mmreg packssdw(mmreg a,mmreg b)
  {
    return mkw(ssat16(d(a,0)),ssat16(d(a,1)),ssat16(d(b,0)),ssat16(d(b,1)));
  }
  inline mmreg movd(sU32 v)              { return (mmreg)v; }
}

#endif
