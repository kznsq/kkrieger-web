// CV2MPlayer for the browser port: Farbrausch's V2 synthesizer (web/v2),
// reached through the C interface in web/v2/v2glue.cpp.

#include "_viruz2.hpp"

extern "C" void *v2_open(const void *song,int size,int rate);
extern "C" void v2_close(void *h);
extern "C" void v2_play(void *h,unsigned int time);
extern "C" void v2_stop(void *h,unsigned int fade);
extern "C" void v2_render(void *h,float *buf,unsigned int len);

struct CV2MPlayer::Private
{
  void *Handle;
};

CV2MPlayer::CV2MPlayer()                  { P = new Private; P->Handle = 0; }
CV2MPlayer::~CV2MPlayer()                 { Close(); delete P; }

// The V2M size is not passed in; the glue measures it from the song structure.
sBool CV2MPlayer::Open(const void *v2m,sU32 rate)
{
  Close();
  P->Handle = v2_open(v2m,0,rate);
  return P->Handle!=0;
}

void CV2MPlayer::Close()                  { if(P->Handle) v2_close(P->Handle); P->Handle = 0; }
void CV2MPlayer::Play(sU32 time)          { v2_play(P->Handle,time); }
void CV2MPlayer::Stop(sU32 fade)          { v2_stop(P->Handle,fade); }
sBool CV2MPlayer::Render(sF32 *buf,sU32 len)
{
  v2_render(P->Handle,buf,len);
  return sTRUE;
}
