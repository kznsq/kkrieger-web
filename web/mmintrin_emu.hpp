// The MMX intrinsics used by the werkkzeug3 blur, implemented on the scalar
// MMX emulation in mmx.hpp (bit-exact with the instructions they stand for).

#ifndef __MMINTRIN_EMU_HPP__
#define __MMINTRIN_EMU_HPP__

#include "mmx.hpp"

typedef mmreg __m64;
typedef long long __int64;

static inline __m64 _mm_set_pi16(sInt e3,sInt e2,sInt e1,sInt e0) { return mmx::mkw(e0,e1,e2,e3); }
static inline __m64 _mm_set_pi32(sInt e1,sInt e0)                 { return mmx::mkd(e0,e1); }
static inline __m64 _mm_srli_pi16(__m64 a,sInt n)                  { return mmx::psrlw(a,n); }
static inline __m64 _mm_srli_pi32(__m64 a,sInt n)                  { return mmx::psrld(a,n); }
static inline __m64 _mm_srai_pi32(__m64 a,sInt n)                  { return mmx::psrad(a,n); }
static inline __m64 _mm_slli_pi32(__m64 a,sInt n)                  { return mmx::pslld(a,n); }
static inline __m64 _mm_madd_pi16(__m64 a,__m64 b)                 { return mmx::pmaddwd(a,b); }
static inline __m64 _mm_unpacklo_pi16(__m64 a,__m64 b)             { return mmx::punpcklwd(a,b); }
static inline __m64 _mm_unpackhi_pi16(__m64 a,__m64 b)             { return mmx::punpckhwd(a,b); }
static inline __m64 _mm_add_pi32(__m64 a,__m64 b)                  { return mmx::paddd(a,b); }
static inline __m64 _mm_sub_pi32(__m64 a,__m64 b)                  { return mmx::psubd(a,b); }
static inline __m64 _mm_packs_pi32(__m64 a,__m64 b)                { return mmx::packssdw(a,b); }
static inline __m64 _mm_cmpgt_pi32(__m64 a,__m64 b)                { return mmx::pcmpgtd(a,b); }
static inline __m64 _mm_adds_pu16(__m64 a,__m64 b)                 { return mmx::paddusw(a,b); }
static inline __m64 _mm_mullo_pi16(__m64 a,__m64 b)                { return mmx::pmullw(a,b); }
static inline __m64 _mm_mulhi_pu16(__m64 a,__m64 b)                { return mmx::pmulhuw(a,b); }
static inline __m64 _mm_add_pi16(__m64 a,__m64 b)                  { return mmx::paddw(a,b); }
static inline __m64 _mm_subs_pi16(__m64 a,__m64 b)                 { return mmx::psubsw(a,b); }

#endif
