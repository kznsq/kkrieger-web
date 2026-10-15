#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t n);
void *calloc(size_t a,size_t b);
void *realloc(void *p,size_t n);
void free(void *p);
void *aligned_alloc(size_t align,size_t n);
int abs(int v);
int rand(void);
void srand(unsigned int seed);
#define RAND_MAX 0x7fff
long labs(long v);
void abort(void);
void exit(int code);
int atoi(const char *s);
double atof(const char *s);
char *ecvt(double v,int ndigit,int *decpt,int *sign);
char *fcvt(double v,int ndigit,int *decpt,int *sign);
void qsort(void *base,size_t n,size_t size,int (*cmp)(const void *,const void *));
#ifdef __cplusplus
}
#endif
