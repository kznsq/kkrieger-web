// Glue between the werkkzeug player and Farbrausch's V2 synthesizer (public
// domain, fr_public/v2): the version tables the V2M converter needs (normally
// set up by sounddef.cpp's sdInit in the editor) and a small C interface, so
// the V2 headers and the werkkzeug headers never meet in one file.

#include "types.h"
#include <string.h>
#include "sounddef.h"
#include "v2mconv.h"
#include "v2mplayer.h"

int  v2version;
int *v2vsizes;
int *v2gsizes;
int *v2topics2;
int *v2gtopics2;

static void VersionInit()
{
  static bool done = false;
  if(done)
    return;
  done = true;

  v2version = 0;
  for(int i=0;i<v2nparms;i++)
    if(v2parms[i].version>v2version) v2version = v2parms[i].version;
  for(int i=0;i<v2ngparms;i++)
    if(v2gparms[i].version>v2version) v2version = v2gparms[i].version;

  v2vsizes = new int[v2version+1];
  v2gsizes = new int[v2version+1];
  memset(v2vsizes,0,(v2version+1)*sizeof(int));
  memset(v2gsizes,0,(v2version+1)*sizeof(int));
  for(int i=0;i<v2nparms;i++)
    for(int j=v2version;j>=v2parms[i].version;j--)
      v2vsizes[j]++;
  for(int i=0;i<v2ngparms;i++)
    for(int j=v2version;j>=v2gparms[i].version;j--)
      v2gsizes[j]++;
  for(int i=0;i<=v2version;i++)
    v2vsizes[i] += 1+255*3;

  v2topics2 = new int[v2ntopics];
  for(int i=0,p=0;i<v2ntopics;i++) { v2topics2[i] = p; p += v2topics[i].no; }
  v2gtopics2 = new int[v2ngtopics];
  for(int i=0,p=0;i<v2ngtopics;i++) { v2gtopics2[i] = p; p += v2gtopics[i].no; }
}

struct V2Glue
{
  V2MPlayer Player;
  unsigned char *Song;
};

// Size of a V2M including its speech texts (the layout readfile() in
// v2mconv.cpp walks). The 2004 player always reads the speech size after the
// patch map and takes the texts when the size is between 1 and 8191.
static int SongSize(const unsigned char *song)
{
  const unsigned char *d = song;
  unsigned int gdnum = *(const unsigned int *)(d+8);
  d += 12 + 10*gdnum;
  for(int ch=0;ch<16;ch++)
  {
    unsigned int notes = *(const unsigned int *)d; d += 4;
    if(!notes) continue;
    d += 5*notes;
    unsigned int pc = *(const unsigned int *)d; d += 4 + 4*pc;
    unsigned int pb = *(const unsigned int *)d; d += 4 + 5*pb;
    for(int cn=0;cn<7;cn++)
    {
      unsigned int cc = *(const unsigned int *)d; d += 4 + 4*cc;
    }
  }
  unsigned int globsize = *(const unsigned int *)d; d += 4 + globsize;
  unsigned int patchsize = *(const unsigned int *)d; d += 4 + patchsize;
  unsigned int speechsize = *(const unsigned int *)d; d += 4;
  if(speechsize && speechsize<8192) d += speechsize;
  return (int)(d-song);
}

extern "C" void *v2_open(const void *song,int size,int rate)
{
  VersionInit();
  if(size<=0)
    size = SongSize((const unsigned char *)song);
  // the synthesizer expects its state (delay lines, filters) to start zeroed,
  // as fresh memory from the operating system is; reused heap memory is not
  V2Glue *g = new V2Glue;           // plain data: no constructor or virtual functions to preserve
  memset(g,0,sizeof(V2Glue));
  g->Song = 0;
  int outlen = 0;
  ConvertV2M((const unsigned char *)song,size,&g->Song,&outlen);   // old synth versions to the current one
  g->Player.Init(rate);             // positions are in samples (the sound effect bank's start positions), as the 2004 player takes them
  if(!g->Song || !g->Player.Open(g->Song,rate))
  {
    delete[] g->Song;
    delete g;
    return 0;
  }
  return g;
}

extern "C" void v2_close(void *h)
{
  V2Glue *g = (V2Glue *)h;
  if(!g) return;
  g->Player.Close();
  delete[] g->Song;
  delete g;
}

extern "C" void v2_play(void *h,unsigned int time)  { if(h) ((V2Glue *)h)->Player.Play(time); }
extern "C" void v2_stop(void *h,unsigned int fade)  { if(h) ((V2Glue *)h)->Player.Stop(fade); }
extern "C" void v2_render(void *h,float *buf,unsigned int len)
{
  if(h) ((V2Glue *)h)->Player.Render(buf,len);
  else memset(buf,0,len*8);
}
