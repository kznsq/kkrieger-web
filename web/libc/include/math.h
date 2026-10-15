#pragma once
// Math for the freestanding WebAssembly build: sqrt/floor/ceil/fabs/trunc/rint map to
// wasm instructions, the transcendental functions come from JavaScript's Math.
#ifdef __cplusplus
extern "C" {
#endif
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double,double);
double exp(double);
double log(double);
double log10(double);
double pow(double,double);
double fmod(double,double);
double sinh(double);
double cosh(double);
double tanh(double);
static inline double sqrt(double x)   { return __builtin_sqrt(x); }
static inline float sqrtf(float x)    { return __builtin_sqrtf(x); }
static inline double fabs(double x)   { return __builtin_fabs(x); }
static inline float fabsf(float x)    { return __builtin_fabsf(x); }
static inline double floor(double x)  { return __builtin_floor(x); }
static inline float floorf(float x)   { return __builtin_floorf(x); }
static inline double ceil(double x)   { return __builtin_ceil(x); }
static inline float ceilf(float x)    { return __builtin_ceilf(x); }
static inline double trunc(double x)  { return __builtin_trunc(x); }
static inline double rint(double x)   { return __builtin_rint(x); }   // f64.nearest: ties to even
static inline float rintf(float x)    { return __builtin_rintf(x); }
static inline long lrintf(float x)    { return (long) __builtin_rintf(x); }
static inline long lrint(double x)    { return (long) __builtin_rint(x); }
static inline float sinf(float x)     { return (float) sin(x); }
static inline float cosf(float x)     { return (float) cos(x); }
static inline float powf(float a,float b) { return (float) pow(a,b); }
static inline float expf(float x)     { return (float) exp(x); }
static inline float logf(float x)     { return (float) log(x); }
static inline float atan2f(float a,float b) { return (float) atan2(a,b); }
float fmodf(float a,float b);
#ifdef __cplusplus
}
#endif
