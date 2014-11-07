#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <portaudio.h>
#include <samplerate.h>
<<<<<<< Updated upstream:FmodP.c
#include <write_wav.h>
//#include <pa_ringbuffer.h>
//#include <pa_util.h>
=======
>>>>>>> Stashed changes:FmodPFile.c

/*typedef struct{

} pattern;*/
FILE* outfile;
//uint8_t* audiobuf;
int pattern;
int row;
uint8_t* curdata;
volatile bool done;
PaError err;
PaStream* stream;
//sample* test;
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
  int buffpointer;
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
    //printf("called!\n");
  }
  if(done) return paComplete;
  return 0;
}

channel* initsound()
{
  done = false;
  //src_error = 0;
  channel* channels = malloc(4*sizeof(channel));
  for(int i = 0; i < 4; i++)
  {
    channels[i].buffer = malloc(16384*sizeof(uint8_t)); //big enough?
    printf("buffer address: %d\n", channels[i].buffer);
    channels[i].stop = true;
    channels[i].buffpointer = 0;
    //Do hacky conversion for now
    //channels[i]->converter = src_new (0, 1, &src_error);
    //channels[i]->cdata = malloc(sizeof(SRC_DATA));
  }
  //audiobuf = (channels[1].buffer);
  //Driver init
  if(Pa_Initialize() != paNoError) exit(1);
  //TODO: set up buffer, open a stream
  int numDevices = Pa_GetDeviceCount();
  printf("Number of audio devices: %d\n", numDevices);
<<<<<<< Updated upstream:FmodP.c
  //ringbuffer stuff goes here
  uint8_t* buf = malloc(16384*sizeof(uint8_t));
=======
  //data.rBufToRTData = PaUtil_AllocateMemory(sizeof(OceanWave*) * 256);
  //PaUtilRingBuffer* rbuf;
  //uint8_t* buf = malloc(131072*sizeof(uint8_t));
>>>>>>> Stashed changes:FmodPFile.c
  //if(!PaUtilRingBuffer(rbuf, 1, 16384, buf)) exit(1);
  //paStreamParamaters outputParameters;
  //outputParameters.device = Pa_GetDefaultOutputDevice();
  //outputParameters.channelCount = 1;
  //outputParameters.sampleFormat = paUInt8;
  /*PaError e = Pa_OpenDefaultStream(&stream, 0, 1, paUInt8, SAMPLE_RATE, 8192,
                              NULL, NULL);*/
  PaError e = Pa_OpenDefaultStream(&stream, 0, 1, paUInt8, SAMPLE_RATE, 64,
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
  printf("data 1: %x\n", *data);
  printf("data 2: %x\n", *(data+1));
  printf("data 3: %x\n", *(data+2));
  printf("data 4: %x\n", *(data+3));
  uint8_t tempsam = ((((*data))&0xF0) | ((*(data+2)>>4)&0x0F))-1;
  printf("tempsam: %d\n", tempsam+1);
  if(tempsam != 0)
  {
    c->stop = false;
    c->repeat = false;
    c->index = 0;
    c->sample = m->samples[tempsam];
    uint16_t period = (((uint16_t)((*data)&0x0F))<<8) | *(data+1); 
    //period = 64;
    printf("period: %d\n", period);
    //THIS NEEDS TO BE CHANGED WHEN EFFECTS ARE IMPLEMENTED
    c->rate = PAL_CLOCK/(2*period);
    c->nsamples = SAMPLE_RATE/(c->rate); //48000 = default output rate
    //c->nsamples = 1;
  }
  //printf("nsamples: %d\n", c->nsamples);
  //printf("secsperrow: %f\n", m->secsperrow);
  //write to audio buffer
  //printf("chunk size: %d\n", (int)((m->secsperrow)*c->rate));
stop:
  if(c->stop)
  {
    for(int i = 0; i < (int)(SAMPLE_RATE*(m->secsperrow)); i++)
    {
      //printf("%d\n", (int)(SAMPLE_RATE*(m->secsperrow)));
      //*(c->buffer+i+c->buffpointer) = 0x00;
      putc(0x00, outfile);
      //fwrite(gcp[0].buffer, 1, 16384, outfile);
      //if(c->buffpointer >= 16384) c->buffpointer = 0;
    }
  }
  else
  {
    for(int i = 0; i < (int)((m->secsperrow)*c->rate); i++)
    {
      //LOL, save hard stuff for later
      //resample in a stupid and hacky way
      //uint8_t* tempbuffer = c->buffer;
      for(int j = 0; j < c->nsamples; j++)
      {
        //*(c->buffer+j+c->buffpointer) = *(c->sample->sampledata + c->index);
        fwrite((c->sample->sampledata + c->index), 1, 1, outfile);
        //if(c->buffpointer >= 16384) c->buffpointer = 0;
        //printf("%d", *(c->sample->sampledata + c->index));
        //*(out+j) = *(c->sample->sampledata + c->index);
      }
      c->index++;

      //repeating sample
      if(c->repeat && (c->index >= (c->sample->repeatlength)*2))
      {
        c->index = c->sample->repeatpoint;
      }
      //nonrepeating sample
      else if(c->index >= (c->sample->length)*2)
      {
        //printf("here\n");
        if(c->sample->repeatlength > 2 )
        {
          c->index = c->sample->repeatpoint;
          c->repeat = true;
        }
        else
        {
          c->stop = true;
          goto stop;
        }
      }
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
  //printf("channel 0\n");
  //processnote(m, &cp[0], curdata);
  //printf("channel 1\n");
  processnote(m, &cp[1], curdata + 4);
  //printf("channel 2\n");
  //processnote(m, &cp[2], curdata + 8);
  //printf("channel 3\n");
  //processnote(m, &cp[3], curdata + 12);
  row++;
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
    printf("length: %d\n", s->length);
    if (s->length != 0)
    {
      //printf("%s\n", );
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
      if(i == 4)
      {
        printf("sample data for sample %d:\n", i);
        FILE* smd = fopen("sample.raw", "wb");
        for(int j = 0; j < copylen; j++)
        {
          fwrite(s->sampledata+j, 1, 1, smd);
        }
        fclose(smd);
      }
    }
    //test = m->samples[0];
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
  printf("songlength: %d\n", m->songlength);
  //printf("about to copy patterns\n");
  memcpy(m->patternlist, filearr+952, 128);
  printf("patterns:\n");
  for(int i = 0; i < m->songlength; i++)
  {
    printf("%d\n", m->patternlist[i]);
  }
  //printf("copied pattern table\n");
  int max = 0;
  for(int i = 0; i < 128; i++)
  {
    if(m->patternlist[i] > max) max = m->patternlist[i];
  }
  printf("max: %d\n", max);
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
  printf("secsperrow: %f\n", m->secsperrow);
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
  //FILE* outfile;
  outfile = fopen("output.raw", "wb");
  if(outfile == NULL)
  {
    printf("Error opening file\n");
    exit(1);
  }
  for(int i = 0; i < 256; i++)
  {
    stepframe(gm, gcp);
  }
  /*while(!done)
  {
    stepframe(gm, gcp);
  }*/
  //err = Pa_StartStream(stream);
  //if(err != paNoError) printf("Failed to start stream. Error code: %d\n", err);
  //Pa_Sleep(1*1000);
  //Pa_StopStream(stream);
  fclose(outfile);
  //stepframe(gm, gcp);
  return 0;
}
