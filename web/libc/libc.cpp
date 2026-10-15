// Minimal C runtime for the freestanding WebAssembly build: memory allocation,
// memory and string functions, a small printf, and the C++ allocation operators.

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef unsigned int u32;
typedef unsigned long long u64;

extern "C" __attribute__((import_module("env"),import_name("js_log"))) void js_log(const char *s,int len);
extern "C" __attribute__((import_module("env"),import_name("js_abort"))) void js_abort(const char *s,int len);

/****************************************************************************/
// Allocator: power-of-two size classes with per-class free lists. Blocks carry
// a 16-byte header holding their class, so free() knows where to put them.

extern unsigned char __heap_base;
static unsigned char *HeapTop;
static void *FreeLists[48];
static size_t HeapUsed,HeapPeak;

static size_t PageCount() { return __builtin_wasm_memory_size(0); }

static void *Sbrk(size_t n)
{
  if(!HeapTop) HeapTop = (unsigned char *)(((size_t)&__heap_base + 15) & ~(size_t)15);
  size_t end = (size_t)HeapTop + n;
  size_t have = PageCount() * 65536;
  if(end > have)
  {
    size_t pages = (end - have + 65535) / 65536;
    if(__builtin_wasm_memory_grow(0,pages) == (size_t)-1)
      return 0;
  }
  void *p = HeapTop;
  HeapTop += n;
  return p;
}

static int SizeClass(size_t n)
{
  int c = 4;                                  // 16 bytes minimum
  while(((size_t)1 << c) < n) c++;
  return c;
}

extern "C" void *malloc(size_t n)
{
  if(n == 0) n = 1;
  int c = SizeClass(n + 16);
  unsigned char *b;
  if(FreeLists[c])
  {
    b = (unsigned char *) FreeLists[c];
    FreeLists[c] = *(void **)(b + 16);
  }
  else
  {
    b = (unsigned char *) Sbrk((size_t)1 << c);
    if(!b)
    {
      static const char msg[] = "out of memory";
      js_abort(msg,sizeof(msg)-1);
      return 0;
    }
  }
  *(u32 *)b = c;
  HeapUsed += (size_t)1 << c;
  if(HeapUsed > HeapPeak) HeapPeak = HeapUsed;
  return b + 16;
}

extern "C" void free(void *p)
{
  if(!p) return;
  unsigned char *b = (unsigned char *)p - 16;
  int c = *(u32 *)b;
  *(void **)(b + 16) = FreeLists[c];
  FreeLists[c] = b;
  HeapUsed -= (size_t)1 << c;
}

extern "C" void *calloc(size_t a,size_t b)
{
  void *p = malloc(a*b);
  memset(p,0,a*b);
  return p;
}

extern "C" void *realloc(void *p,size_t n)
{
  if(!p) return malloc(n);
  unsigned char *b = (unsigned char *)p - 16;
  size_t have = ((size_t)1 << *(u32 *)b) - 16;
  if(n <= have) return p;
  void *q = malloc(n);
  memcpy(q,p,have);
  free(p);
  return q;
}

// rand() with the Microsoft C runtime's generator, as the Windows build used
static unsigned int RandState = 1;
extern "C" int rand(void)                { RandState = RandState*214013u+2531011u; return (RandState>>16)&0x7fff; }
extern "C" void srand(unsigned int seed) { RandState = seed; }

extern "C" void *aligned_alloc(size_t align,size_t n)
{
  return malloc(n);                           // blocks are 16-byte aligned
}

extern "C" size_t kk_heap_used() { return HeapUsed; }
extern "C" size_t kk_heap_peak() { return HeapPeak; }

void *operator new(size_t n)            { return malloc(n); }
void *operator new[](size_t n)          { return malloc(n); }
void operator delete(void *p) noexcept  { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p,size_t) noexcept  { free(p); }
void operator delete[](void *p,size_t) noexcept { free(p); }

extern "C" void __cxa_pure_virtual() { static const char m[] = "pure virtual call"; js_abort(m,sizeof(m)-1); }
extern "C" int __cxa_atexit(void (*)(void *),void *,void *) { return 0; }
void *__dso_handle = 0;

/****************************************************************************/

extern "C" void *memcpy(void *d,const void *s,size_t n)
{
  __builtin_memcpy(d,s,n);                    // bulk-memory instruction
  return d;
}

extern "C" void *memmove(void *d,const void *s,size_t n)
{
  __builtin_memmove(d,s,n);
  return d;
}

extern "C" void *memset(void *d,int c,size_t n)
{
  __builtin_memset(d,c,n);
  return d;
}

extern "C" int memcmp(const void *a,const void *b,size_t n)
{
  const unsigned char *x = (const unsigned char *)a,*y = (const unsigned char *)b;
  for(size_t i=0;i<n;i++)
    if(x[i]!=y[i]) return x[i]-y[i];
  return 0;
}

extern "C" size_t strlen(const char *s)
{
  size_t n = 0;
  while(s[n]) n++;
  return n;
}

extern "C" int strcmp(const char *a,const char *b)
{
  while(*a && *a==*b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}

extern "C" int strncmp(const char *a,const char *b,size_t n)
{
  for(size_t i=0;i<n;i++)
  {
    if(a[i]!=b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
  }
  return 0;
}

extern "C" char *strcpy(char *d,const char *s)  { char *r = d; while((*d++ = *s++)); return r; }
extern "C" char *strncpy(char *d,const char *s,size_t n)
{
  size_t i = 0;
  for(;i<n && s[i];i++) d[i] = s[i];
  for(;i<n;i++) d[i] = 0;
  return d;
}
extern "C" char *strcat(char *d,const char *s)  { strcpy(d+strlen(d),s); return d; }
extern "C" char *strchr(const char *s,int c)
{
  for(;*s;s++) if(*s==(char)c) return (char *)s;
  return c ? 0 : (char *)s;
}

extern "C" int abs(int v)   { return v<0 ? -v : v; }
extern "C" long labs(long v) { return v<0 ? -v : v; }
extern "C" void abort(void) { static const char m[] = "abort"; js_abort(m,sizeof(m)-1); __builtin_trap(); }
extern "C" void exit(int)   { static const char m[] = "exit"; js_abort(m,sizeof(m)-1); __builtin_trap(); }
extern "C" int atoi(const char *s)
{
  int sign = 1,v = 0;
  while(*s==' ') s++;
  if(*s=='-') { sign = -1; s++; }
  while(*s>='0' && *s<='9') v = v*10 + (*s++ - '0');
  return v*sign;
}
extern "C" double atof(const char *s) { return atoi(s); }

extern "C" void qsort(void *base,size_t n,size_t size,int (*cmp)(const void *,const void *))
{
  unsigned char *b = (unsigned char *)base;
  unsigned char tmp[256];
  for(size_t i=1;i<n;i++)                     // insertion sort, small arrays only
    for(size_t j=i;j>0 && cmp(b+(j-1)*size,b+j*size)>0;j--)
    {
      memcpy(tmp,b+j*size,size);
      memcpy(b+j*size,b+(j-1)*size,size);
      memcpy(b+(j-1)*size,tmp,size);
    }
}

/****************************************************************************/
// ecvt/fcvt (used by the werkkzeug formatter for %f) and a small vsnprintf.

static char CvtBuf[64];

static char *Cvt(double v,int ndigit,int *decpt,int *sign,int fixed)
{
  *sign = v < 0;
  if(v < 0) v = -v;
  int e = 0;
  if(v != 0)
  {
    while(v >= 10) { v /= 10; e++; }
    while(v < 1) { v *= 10; e--; }
  }
  *decpt = e + 1;
  int digits = fixed ? ndigit + e + 1 : ndigit;
  if(digits < 0) digits = 0;
  if(digits > 40) digits = 40;
  for(int i=0;i<digits;i++)
  {
    int d = (int) v;
    if(d > 9) d = 9;
    CvtBuf[i] = '0' + d;
    v = (v - d) * 10;
  }
  CvtBuf[digits] = 0;
  return CvtBuf;
}

extern "C" char *ecvt(double v,int ndigit,int *decpt,int *sign) { return Cvt(v,ndigit,decpt,sign,0); }
extern "C" char *fcvt(double v,int ndigit,int *decpt,int *sign) { return Cvt(v,ndigit,decpt,sign,1); }

extern "C" int vsnprintf(char *buf,size_t n,const char *fmt,va_list ap)
{
  size_t o = 0;
  auto put = [&](char c) { if(o + 1 < n) buf[o] = c; o++; };
  for(;*fmt;fmt++)
  {
    if(*fmt != '%') { put(*fmt); continue; }
    fmt++;
    int width = 0,prec = -1;
    bool zero = false;
    if(*fmt=='0') { zero = true; fmt++; }
    while(*fmt>='0' && *fmt<='9') width = width*10 + (*fmt++ - '0');
    if(*fmt=='.') { fmt++; prec = 0; while(*fmt>='0' && *fmt<='9') prec = prec*10 + (*fmt++ - '0'); }
    while(*fmt=='l' || *fmt=='h') fmt++;
    char tmp[64];
    int len = 0;
    switch(*fmt)
    {
    case 'd': case 'i': case 'u': case 'x': case 'X': case 'p':
      {
        u64 v;
        bool neg = false;
        int base = (*fmt=='x' || *fmt=='X' || *fmt=='p') ? 16 : 10;
        if(*fmt=='d' || *fmt=='i') { int s = va_arg(ap,int); neg = s<0; v = neg ? -(long long)s : s; }
        else if(*fmt=='p') v = (size_t) va_arg(ap,void *);
        else v = va_arg(ap,unsigned int);
        do { int d = v % base; tmp[len++] = d<10 ? '0'+d : 'a'+d-10; v /= base; } while(v);
        while(len < width - (neg?1:0)) tmp[len++] = zero ? '0' : ' ';
        if(neg) tmp[len++] = '-';
        while(len) put(tmp[--len]);
      }
      break;
    case 'f':
      {
        double v = va_arg(ap,double);
        if(v < 0) { put('-'); v = -v; }
        if(prec < 0) prec = 6;
        u64 ip = (u64) v;
        double fp = v - ip;
        do { tmp[len++] = '0' + ip % 10; ip /= 10; } while(ip);
        while(len) put(tmp[--len]);
        if(prec) put('.');
        for(int i=0;i<prec;i++) { fp *= 10; int d = (int) fp; put('0'+d); fp -= d; }
      }
      break;
    case 's':
      {
        const char *s = va_arg(ap,const char *);
        if(!s) s = "(null)";
        while(*s) put(*s++);
      }
      break;
    case 'c': put((char) va_arg(ap,int)); break;
    case '%': put('%'); break;
    default: put('?'); break;
    }
  }
  if(n) buf[o < n ? o : n-1] = 0;
  return (int) o;
}

extern "C" int snprintf(char *buf,size_t n,const char *fmt,...)
{
  va_list ap; va_start(ap,fmt);
  int r = vsnprintf(buf,n,fmt,ap);
  va_end(ap);
  return r;
}

extern "C" int sprintf(char *buf,const char *fmt,...)
{
  va_list ap; va_start(ap,fmt);
  int r = vsnprintf(buf,1<<20,fmt,ap);
  va_end(ap);
  return r;
}

extern "C" int printf(const char *fmt,...)
{
  char buf[1024];
  va_list ap; va_start(ap,fmt);
  int r = vsnprintf(buf,sizeof(buf),fmt,ap);
  va_end(ap);
  js_log(buf,(int) strlen(buf));
  return r;
}

/****************************************************************************/
// Math: transcendental functions come from JavaScript's Math object.

#define JSMATH1(name) extern "C" __attribute__((import_module("math"),import_name(#name))) double jsm_##name(double); \
  extern "C" double name(double x) { return jsm_##name(x); }
#define JSMATH2(name) extern "C" __attribute__((import_module("math"),import_name(#name))) double jsm_##name(double,double); \
  extern "C" double name(double x,double y) { return jsm_##name(x,y); }
JSMATH1(sin) JSMATH1(cos) JSMATH1(tan) JSMATH1(asin) JSMATH1(acos) JSMATH1(atan)
JSMATH1(exp) JSMATH1(log) JSMATH1(log10) JSMATH1(sinh) JSMATH1(cosh) JSMATH1(tanh)
JSMATH2(atan2) JSMATH2(pow) JSMATH2(fmod)

extern "C" float fmodf(float a,float b) { return (float) jsm_fmod(a,b); }
extern "C" double exp2(double x) { return jsm_pow(2.0,x); }
extern "C" float exp2f(float x) { return (float) jsm_pow(2.0,x); }

// used by the engine's release-build sFatal
extern "C" int wvsprintfA(char *buffer,const char *fmt,va_list ap)
{
  return vsnprintf(buffer,1024,fmt,ap);
}
