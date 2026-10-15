// This file is distributed under a BSD license. See LICENSE.txt for details.
// Browser port: the V2 music player interface used by the kkrieger player,
// implemented on the C++ version of the V2 synth (web/kkv2.cpp).

#ifndef ___VIRUZ2_HPP__
#define ___VIRUZ2_HPP__

#include "_types.hpp"

class CV2MPlayer
{
  struct Private;
  Private *P;
public:
  CV2MPlayer();
  ~CV2MPlayer();
  sBool Open(const void *a_v2mptr,sU32 a_samplerate=44100);
  void Close();
  void Play(sU32 a_time=0);
  void Stop(sU32 a_fadetime=0);
  sBool Render(sF32 *a_buffer,sU32 a_len);
};

#endif
