// Typed call wrappers for every operator handler of the player (see tools/genthunks_web.py).

#include "_types.hpp"
#include "kdoc.hpp"
#include "genbitmap.hpp"
#include "genmesh.hpp"
#include "genmaterial.hpp"
#include "genscene.hpp"
#include "geneffect.hpp"
#include "genoverlay.hpp"
#include "kkriegergame.hpp"
#include "kkload.hpp"

template <class T> static T W(const sU32 *a,sInt k)
{
  T r;
  sCopyMem(&r,a+k,sizeof(T));
  return r;
}

#include "thunks.inc"

const PortThunk *PortFindThunk(sInt id)
{
  for(const PortThunk *t=PortThunks;t->Init;t++)
    if(t->Id == id)
      return t;
  return 0;
}
