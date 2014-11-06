#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <portaudio.h>
#include <samplerate.h>
#include "/Users/ford/Development/portaudio/qa/loopback/src/write_wav.h"
//#include <pa_ringbuffer.h>
//#include <pa_util.h>

/*typedef struct{

} pattern;*/
uint8_t* audiobuf;
int pattern;
int row;
uint8_t* curdata;
bool done;
PaError err;
PaStream *stream;

static double const PAL_CLOCK = 7093789.2;
static int const SAMPLE_RATE = 44100;

typedef struct {
  uint8_t name[22];
  uint16_t length;
  uint8_t finetune;
  uint8_t volume;
  uint16_t repeatpoint;
  uint16_t repeatlength;
  uint8_t* sampledata;
} sample;

typedef struct{
  sample* sample;
  double rate;
  //uint8_t volume;
  uint32_t index;
  uint8_t* buffer;
  bool repeat;
  bool stop;
  SRC_STATE* converter;
  SRC_DATA* cdata;
  int nsamples;
} channel;

typedef struct{
  uint8_t name[20];
  uint8_t* patterns;
  uint8_t songlength;
  sample* samples[31];
  uint8_t patternlist[128];
  uint32_t speed;
  uint16_t tempo;
  double secsperrow;
  uint8_t magicstring[4];
  //uint8_t* sampledata;
} modfile;

modfile* gm;
channel* gcp;

void stepframe(modfile* m, channel* cp);

static int patestCallback(const void *input, void *output,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData)
{
  for(int i = 0; i < framesPerBuffer; i++)
  {
    stepframe(gm, gcp);
    printf("called!\n");
  }
  //if(done) return 1;
  return 0;
}

channel* initsound()
{
  done = false;
  //src_error = 0;
  channel* channels = malloc(4*sizeof(channel));
  for(int i = 0; i < 4; i++)
  {
    channels[i].buffer = malloc(4096*sizeof(uint8_t)); //big enough?
    printf("buffer address: %d\n", channels[i].buffer);
    channels[i].stop = true;
    //Do hacky conversion for now
    //channels[i]->converter = src_new (0, 1, &src_error);
    //channels[i]->cdata = malloc(sizeof(SRC_DATA));
  }
  audiobuf = (channels[0].buffer);
  //Driver init
  if(Pa_Initialize() != paNoError) exit(1);
  //TODO: set up buffer, open a stream
  int numDevices = Pa_GetDeviceCount();
  printf("Number of audio devices: %d\n", numDevices);
  //data.rBufToRTData = PaUtil_AllocateMemory(sizeof(OceanWave*) * 256);
  //PaUtilRingBuffer* rbuf;
  uint8_t* buf = malloc(16384*sizeof(uint8_t));
  //if(!PaUtilRingBuffer(rbuf, 1, 16384, buf)) exit(1);
  //paStreamParamaters outputParameters;
  //outputParameters.device = Pa_GetDefaultOutputDevice();
  //outputParameters.channelCount = 1;
  //outputParameters.sampleFormat = paUInt8;
  PaError e = Pa_OpenDefaultStream(&stream, 0, 1, paUInt8, SAMPLE_RATE,
                              paFramesPerBufferUnspecified,
                              patestCallback, NULL);
  if(e != paNoError)
  { 
    printf("Failed to Open Stream. Error code: %d\n", e);
    exit(1);
  }
                                                   
  return channels;
}
void processnote(modfile* m, channel* c, uint8_t* data)
{
  //printf("%x\n", *data);
  //printf("%x\n", *(data+1));
  //printf("%x\n", *(data+2));
  //printf("%x\n", *(data+3));
  uint8_t tempsam = (((*data))&0xF0) | ((*(data+2)>>4)&0x0F);
  //printf("tempsam: %d\n", tempsam);
  if(tempsam)
  {
    c->stop = false;
    c->repeat = false;
    c->index = 0;
    c->sample = m->samples[tempsam];
    uint16_t period = (((uint16_t)((*data)&0x0F))<<8) | *(data+1); 
    //THIS NEEDS TO BE CHANGED WHEN EFFECTS ARE IMPLEMENTED
    c->rate = PAL_CLOCK/(2*period);
    c->nsamples = SAMPLE_RATE/c->rate; //48000 = default output rate
  }
  //printf("nsamples: %d\n", c->nsamples);
  //printf("secsperrow: %f\n", m->secsperrow);
  //write to audio buffer
  //printf("chunk size: %d\n", (int)((m->secsperrow)*c->rate));
  for(int i = 0; i < (int)((m->secsperrow)*c->rate); i++)
  {
    //LOL, save hard stuff for later
    //resample in a stupid and hacky way
    //uint8_t* tempbuffer = c->buffer;
    for(int j = 0; j < c->nsamples; j++)
    {
      *(c->buffer+j) = *(c->sample->sampledata + c->index);
    }
    c->index++;
    //repeating sample
    if(c->repeat && c->index >= (c->sample->repeatlength)*2)
    {
      c->index = c->sample->repeatpoint;
    }
    //nonrepeating sample
    else if(c->index >= (c->sample->length)*2)
    {
      //printf("here\n");
      if(c->sample->repeatlength > 1 )
      {
        c->index = c->sample->repeatpoint;
        c->repeat = true;
      }
      else c->stop = true;
    }
  }
}

void stepframe(modfile* m, channel* cp)
{
  if(row == 64)
  { 
    row = 0;
    pattern++;
  }
  if(pattern == m->songlength)
  {
    done = true;
    return;
  }
  curdata = m->patterns + ((m->patternlist[pattern])*1024) + (16*row);
  printf("channel 0\n");
  processnote(m, &cp[0], curdata);
  printf("channel 1\n");
  processnote(m, &cp[1], curdata + 4);
  printf("channel 2\n");
  processnote(m, &cp[2], curdata + 8);
  printf("channel 3\n");
  processnote(m, &cp[3], curdata + 12);
  //usleep(20000*(m->speed)); //20000 microseconds = 1/50 second

  /*for(int i = 0; i < m->songlength; i++)
  {
    for(int j = 0; j < 64; j++)
    {
      curdata = m->patterns + ((m->patternlist[i])*1024) + (16*j);
      printf("channel 0\n");
      processnote(m, &cp[0], curdata);
      printf("channel 1\n");
      processnote(m, &cp[1], curdata + 4);
      printf("channel 2\n");
      processnote(m, &cp[2], curdata + 8);
      printf("channel 3\n");
      processnote(m, &cp[3], curdata + 12);
      //usleep(20000*(m->speed)); //20000 microseconds = 1/50 second
    }*/
}

void sampleparse(modfile* m, uint8_t* filearr, uint32_t start)
{
  for(int i = 0; i < 31; i++) //TODO: add support for non-31 instrument formats
  {
    sample* s = malloc(sizeof(sample));
    m->samples[i] = s;
    //printf("setting sample name\n");
    //memcpy(&(s->name), filearr+20, 22);
    memcpy(&(s->name), filearr+20+(30*i), 22);
    //printf("%s\n", s->name);
    //REQUIRES A LITTLE ENDIAN MACHINE!!!!!
    //TODO: preprocessor directives for checking endianness
    //alternative: do stuff with multiplication?????
    memcpy(&(s->length), filearr+43+(30*i), 1);
    memcpy((uint8_t*)&(s->length)+1, filearr+42+(30*i), 1);
    printf("%s\n", s->name);
    if (s->length != 0)
    {
      //printf("%d\n", 2*(int)(s->length));
      s->finetune = filearr[44 + 30 * i]&0x0F;
      //printf("copied finetune\n");
      s->volume = filearr[45 + 30 * i];
      //printf("copied volume\n");
      memcpy(&(s->repeatpoint), filearr+47+(30*i), 1);
      memcpy((uint8_t*)&(s->repeatpoint)+1, filearr+46+(30*i), 1);
      //printf("%d\n", 2*(int)(s->repeatpoint));
      memcpy(&(s->repeatlength), filearr+49+(30*i), 1);
      memcpy((uint8_t*)&(s->repeatlength)+1, filearr+48+(30*i), 1);
      //printf("%d\n", 2*(int)(s->repeatlength));
      //memcpy(&(s->repeatlength), filearr+48+(30*i), 2);
      int copylen = (s->length)*2;
      s->sampledata = malloc(copylen);
      memcpy(s->sampledata, filearr+start, copylen);
      s->sampledata = filearr+start;
      start += copylen;
    }
  }
  return;
}

modfile* modparse(FILE* f)
{
  fseek(f, 0L, SEEK_END);
  uint8_t* filearr = malloc(ftell(f)*sizeof(uint8_t));
  fseek(f, 0L,SEEK_SET);
  int seek = 0;
  int c;
  while((c = fgetc(f)) != EOF)
  {
    filearr[seek++] = (uint8_t)c;
  }
  modfile* m = malloc(sizeof(modfile));
  //printf("about to copy name\n");
  strncpy(&(m->name), filearr, 20);
  //printf("copied name\n");
  memcpy(&(m->magicstring), filearr+1080, 4);
  if(strcmp(&(m->magicstring), "M.K.") != 0) 
  { 
    printf("magic string check failed\n");
    m->songlength = -1;
    return m;
  }
  //printf("magic string checked\n");
  m->songlength = filearr[950];
  //printf("about to copy patterns\n");
  memcpy(&(m->patternlist), filearr+952, 128);
  //printf("copied pattern table\n");
  int max = 0;
  for(int i = 0; i < 128; i++)
  {
    if(m->patternlist[i] > max) max = m->patternlist[i];
  }
  uint32_t len = (uint32_t)(1024*(max+1)); //1024 = size of pattern
  m->patterns = malloc(len);
  memcpy(m->patterns, filearr+1084, len-1);
  //printf("copied pattern data\n");
  sampleparse(m, filearr, len+1084);
  m->speed = 6; //default speed = 6
  m->tempo = 125/(m->speed);
  //m->secsperrow = 3.0;
  //m->secsperrow = (1.0/((2.5/(m->tempo))*(m->speed)));
  m->secsperrow = 0.02*(m->speed);
  //printf("secsperrow calculation\n");
  //printf("secsperrow: %f\n", m->secsperrow);
  return m;
}

int main(int argc, char const *argv[])
{
  if(argc < 2)
  {
    printf("Please specify a valid mod file.\n");
    return 1;
  }
  FILE* f = fopen(argv[1], "r");
  gm = modparse(f);
  printf("finished parsing modfile\n");
  if(gm->songlength < 0)
  {
    printf("Invalid modfile. Error %d\n", gm->songlength);
    return 1;
  }
  fclose(f);
  printf("%s\n", gm->name);
  gcp = initsound();
  printf("Successfully initialized sound.\n");
  if(!Pa_StartStream(stream)) printf("failed to start stream\n");
  Pa_Sleep(50*1000);
  //stepframe(gm, gcp);
  return 0;
}