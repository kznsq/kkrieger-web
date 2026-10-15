// Loading .kkrieger from the original executable (see kkload.cpp).

#ifndef __KKLOAD_HPP__
#define __KKLOAD_HPP__

#include "_types.hpp"
#include "kdoc.hpp"

struct PortThunk                  // one operator class of the port (web/thunks.inc)
{
  sInt Id;
  sU32 Convention;                // calling convention of the published code
  const sChar *Packing;
  KObject *(*Init)(sU32 *);
  void (*Exec)(sU32 *);
  void *InitFn;
  void *ExecFn;
};

const PortThunk *PortFindThunk(sInt id);

// decompress the kkrunchy image of pno0001.exe; out is malloc'ed
sBool KKUnpack(const sU8 *exe,sInt size,sU8 *&out,sInt &outSize,sU32 &outVA);

// read the 2004 export inside the unpacked image into doc (image must stay alive)
sBool KKLoad2004(KDoc *doc,const sU8 *image,sInt size);

#endif
