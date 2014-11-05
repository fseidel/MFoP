#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <portaudio.h>
#include <samplerate.h>

/*typedef struct{

} pattern;*/

uint8_t* audiobuf;

static double const PAL_CLOCK = 7093789.2;

typedef struct{
  uint8_t sample;
  double rate;
  //uint8_t volume;
  uint32_t index;
} channel;

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
  uint8_t name[20];
  uint8_t* patterns;
  uint8_t songlength;
  sample* samples[31];
  uint8_t patternlist[128];
  uint32_t speed;
  uint16_t tempo;
  double secsperframe;
  uint8_t magicstring[5];
  //uint8_t* sampledata;
} modfile;



channel* initsound()
{
  channel* channels = malloc(4*sizeof(channel));
  //Driver init
  if(Pa_Initialize() != paNoError) exit(1);
  //TODO: set up buffer, open a stream
  return channels;
}
void processnote(modfile* m, channel* c, uint8_t* data)
{
  printf("%x\n", *data);
  printf("%x\n", *(data+1));
  printf("%x\n", *(data+2));
  printf("%x\n", *(data+3));
  if(data[0] != 0x00)
  {
    c->index = 0;
    c->sample = (((*data))&0xF0) | ((*(data+2)>>4)&0x0F);
    uint16_t period = (((uint16_t)((*data)&0x0F))<<8) | *(data+1); 
    //THIS NEEDS TO BE CHANGED WHEN EFFECTS ARE IMPLEMENTED
    c->rate = PAL_CLOCK/(2*period);
  }
  //write to audio buffer
  for(int i = 0; i < (m->secsperframe)*c->rate; i++)
  {
    //LOL, save hard stuff for later
  }
  
}

void play(modfile* m, channel* cp)
{
  uint8_t* curdata;
  for(int i = 0; i < m->songlength; i++)
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
      usleep(20000*(m->speed)); //20000 microseconds = 1/50 second
    }
  }
  return;
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
    //requires a little endian machine!!!!!
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
  m->secsperframe = ((double)1.0/(((double)2.5/(m->tempo))*(m->speed)));
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
  modfile* m = modparse(f);
  printf("finished parsing modfile\n");
  if(m->songlength < 0)
  {
    printf("Invalid modfile. Error %d\n", m->songlength);
    return 1;
  }
  fclose(f);
  printf("%s\n", m->name);
  channel* cp =  initsound();
  play(m, cp);
  return 0;
}