#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <samplerate.h>
#include <math.h>
#include <portaudio.h>

static double const PAL_CLOCK = 7093789.2;
static double const SAMPLE_RATE = 44100;
static double const FINETUNE_BASE = 1.0072382087;

uint16_t periods[] = {
  856,808,762,720,678,640,604,570,538,508,480,453,
  428,404,381,360,339,320,302,285,269,254,240,226,
  214,202,190,180,170,160,151,143,135,127,120,113};

float* audiobuf;
float* buffs[2];
int pattern;
int row;
uint8_t* curdata;
bool done;
int libsrc_error;
uint8_t curbuf;
PaStream* stream;
PaError pa_error;
uint8_t globaltick;
bool patternset;
uint8_t delcount;
bool delset;
bool inrepeat;

static int (*callback)(const void*, void*, unsigned long,
                      const PaStreamCallbackTimeInfo*,
                      PaStreamCallbackFlags, void*);

typedef struct {
  char name[23];
  uint16_t length;
  int8_t finetune;
  uint8_t volume;
  uint16_t repeatpoint;
  uint16_t repeatlength;
  bool inverted;
  float* sampledata;
} sample;

typedef struct{
  sample* sample;
  double prevrate;
  uint32_t rate;
  double volume;
  double offset;
  uint32_t index;
  float* buffer;
  float* resampled;
  uint32_t increment;
  bool repeat;
  bool stop;
  bool doarp;
  bool doport;
  bool targetedport;
  bool fineeffect;
  int8_t effect_timer;
  uint16_t period;
  uint16_t prevperiod;
  int8_t volstep;
  uint16_t* arp;
  uint16_t portdest;
  int8_t portstep;
  uint8_t targstep;
  uint8_t prevport;
  SRC_STATE* converter;
  SRC_DATA* cdata;
  int8_t finetune;
  int8_t retrig;
} channel;

typedef struct{
  char name[21];
  uint8_t* patterns;
  uint8_t songlength;
  sample* samples[31];
  uint8_t patternlist[128];
  uint32_t speed;
  uint16_t tempo;
  double secsperrow;
  char magicstring[5];
} modfile;

modfile* gm;
channel* gcp;

static int standardcallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void* userdata);

static int headphonecallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void* userdata);

int findperiod(uint16_t period)
{
  for(int i = 0; i < 36; i++) 
    if(periods[i] == period) return i;
  printf("PERIOD TABLE LOOKUP ERROR: %d\n", period);
  return -1;
}

static inline double calcrate(uint16_t period, int8_t finetune)
{
  return PAL_CLOCK/
    (2.0*round((double)period*pow(FINETUNE_BASE, -(double)finetune)));
}

void floatncpy(float* dest, int8_t* src, int n)
{
  for(int i = 0; i < n; i++)
    dest[i] = ((float)src[i])/128.0f;
}

void error(int err)
{
  printf("Error: %s\n", src_strerror(err));
  abort();
}

channel* initsound()
{
  pa_error = Pa_Initialize();
  if(pa_error != paNoError)
  {
    printf("PortAudio error: %s\n", Pa_GetErrorText(pa_error));
    abort();
  }
  channel* channels = malloc(4*sizeof(channel));
  buffs[0] = malloc(0.02*2*SAMPLE_RATE*sizeof(float));
  buffs[1] = malloc(0.02*2*SAMPLE_RATE*sizeof(float));
  curbuf = 0;
  audiobuf = buffs[curbuf];
  for(int i = 0; i < 4; i++)
  {
    channels[i].increment = 0.0f;
    channels[i].buffer = malloc((PAL_CLOCK/2.0)*sizeof(float));
    //channels[i].output = malloc(1024*sizeof(float));
    channels[i].resampled = malloc(0.02*SAMPLE_RATE*sizeof(float));
    channels[i].stop = true;
    channels[i].repeat = false;
    channels[i].arp = malloc(3*sizeof(uint16_t));
    channels[i].doarp = false;
    channels[i].doport = false;
    channels[i].targetedport = false;
    channels[i].volstep = 0;
    channels[i].prevrate = 0.0;
    channels[i].period = 0;
    channels[i].prevperiod = 0;
    channels[i].portdest = 0;
    channels[i].portstep = 0;
    channels[i].targstep = 0;
    channels[i].prevport = 0;
    channels[i].offset = 0.0;
    channels[i].finetune = 0;
    channels[i].fineeffect = false;
    channels[i].retrig = 0;
    //channels[i].converter = src_new(SRC_ZERO_ORDER_HOLD, 1, &libsrc_error);
    channels[i].converter = src_new(SRC_LINEAR, 1, &libsrc_error);
    //channels[i].converter = src_new(SRC_SINC_BEST_QUALITY, 1, &libsrc_error);
    //channels[i].converter = src_new(SRC_SINC_FASTEST, 1, &libsrc_error);
    channels[i].cdata = malloc(sizeof(SRC_DATA));
    channels[i].cdata->data_in = channels[i].buffer;
    channels[i].cdata->data_out = channels[i].resampled;
    channels[i].cdata->output_frames = SAMPLE_RATE*0.02;
    channels[i].cdata->end_of_input = 0;
    row = 0;
    pattern = 0;
    delset = false;
    inrepeat = false;
    delcount = 0;
    globaltick = 0;
  }         
  //open the audio stream
  pa_error = Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, SAMPLE_RATE,
                                  SAMPLE_RATE*0.02, callback,
                                  audiobuf);
  if(pa_error != paNoError)
  {
    printf("PortAudio error: %s\n", Pa_GetErrorText(pa_error));
    abort();
  }

  return channels;
}

void preprocesseffects(uint8_t* data)
{
  uint8_t tempeffect = *(data+2)&0x0F;
  uint8_t effectdata = *(data+3);
  switch(tempeffect)
  {
    case 0x0B: //position jump
      pattern = effectdata;
      patternset = true;
      break;

    case 0x0D: //row jump
      if(!patternset) pattern++;
      row = (effectdata>>4)*10+(effectdata&0x0F) - 1;
      break;

    case 0x0F:
      if(effectdata == 0) break;
      if(effectdata >= 0x10) break;
      else
      {
        gm->speed = effectdata;
        //printf("Speed: %d\n", gm->speed);
        gm->secsperrow = 0.02*gm->speed;
        gm->tempo = 125/(gm->speed);
      break;
      }

    default:
      break;
  }
}

void processnoteeffects(channel* c, uint8_t* data)
{
  uint8_t tempeffect = *(data+2)&0x0F;
  uint8_t effectdata = *(data+3);
  switch(tempeffect)
  {
    case 0x00: //normal/arpeggio
      if(effectdata)
      {
        c->period = c->portdest;
        c->doarp = true;
        c->doport = 0;
        c->volstep = 0;
        int base = findperiod(c->period);
        if(base == -1)
        {
          c->doarp = false;
          break;
        }
        c->arp[0] = c->period;
        //printf("ARP0: %d\n", (int)c->arp[0]);
        c->arp[1] = periods[base+((effectdata>>4)&0x0F)];
        //printf("ARP1: %d\n", (int)c->arp[1]);
        c->arp[2] = periods[base+(effectdata&0x0F)];
        //printf("ARP2: %d\n", (int)c->arp[2]);
        c->effect_timer = 1;
      }
      break;

    case 0x01: //slide up
      c->targetedport = false;
      c->doarp = false;
      c->doport = true;
      c->portstep = -effectdata;
      c->volstep = 0;
      c->fineeffect = false;
      c->effect_timer = 1;
      break;

    case 0x02: //slide down
      c->targetedport = false;
      c->doarp = false;
      c->doport = true;
      c->portstep = effectdata;
      c->volstep = 0;
      c->fineeffect = false;
      c->effect_timer = 1;
      break;

    case 0x03: //tone portamento
      if(effectdata)
      {
        //c->portdest = c->period;
        c->period = c->prevperiod;
        c->targstep = effectdata;
        c->prevport = c->targstep;
      }
      else 
      {
        c->period = c->prevperiod;
        //c->targstep = c->prevport;
      }
      c->targetedport = true;
      c->volstep = 0;
      c->doarp = false;
      c->doport = true;
      c->effect_timer = 1;
      c->fineeffect = false;
      c->stop = false;
      break;

    case 0x04: //vibrato
      break;

    case 0x05: //tone portamento + volume slide
      if(!effectdata) c->volstep = 0;
      //slide up
      else if(effectdata&0xF0) c->volstep = (effectdata>>4) & 0x0F;
      //slide down
      else c->volstep = -(int8_t)effectdata;
      c->effect_timer = 1;
      c->targetedport = true;
      c->period = c->prevperiod;
      c->doport = true;
      //c->targstep = c->prevport;
      c->fineeffect = false;
      c->stop = false;
      break;

    case 0x06: //vibrato + volume slide
      break;

    case 0x07: //tremolo
      break;

    //Set panning doesn't do anything in a standard Amiga MOD
    case 0x08: //set panning
      break;

    case 0x09: //set sample offset
      c->index = (uint32_t)effectdata * 0x100;
      break;

    case 0x0A: //volume slide
      //cancel slide
      if(!effectdata) c->volstep = 0;
      //slide up
      else if(effectdata&0xF0) c->volstep = (effectdata>>4) & 0x0F;
      //slide down
      else c->volstep = -(int8_t)effectdata;
      c->fineeffect = false;
      c->effect_timer = 1;
      break;

    //0x0B already taken care of by preprocesseffects()

    case 0x0C: //set volume
      c->volstep = 0;
      if(effectdata > 64) c->volume = 1.0;
      else c->volume = (double)effectdata / 64.0;
      break;

    //0x0D already taken care of by preprocesseffects()

    case 0x0E: //E commands
    {
      switch(effectdata & 0xF0)
      {
        case 0x00: //set filter (0 on, 1 off)
          break;

        case 0x10: //fine slide up
          c->targetedport = false;
          c->doarp = false;
          c->doport = true;
          c->portstep = -(int8_t)(effectdata & 0x0F);
          c->volstep = 0;
          c->effect_timer = 2;
          c->fineeffect = true;
          break;

        case 0x20: //fine slide down
          c->targetedport = false;
          c->doarp = false;
          c->doport = true;
          c->portstep = (effectdata & 0x0F);
          c->volstep = 0;
          c->effect_timer = 2;
          c->fineeffect = true;
          break;

        case 0x30: //glissando control (0 off, 1 on, use with tone portamento)
          break;

        case 0x40: //set vibrato waveform (0 sine, 1 ramp down, 2 square)
          break;

        case 0x50: //set finetune
        {
          int8_t tempfinetune = effectdata & 0x0F;
          if(tempfinetune > 0x07) tempfinetune |= 0xF0;
          c->finetune = tempfinetune;
          break;
        }

        case 0x60: //jump to loop, play x times
          break;

        case 0x70: //set tremolo waveform (0 sine, 1 ramp down, 2 square)
          break;

        //There's no effect 0xE8 (is 8 evil or something?)

        case 0x90: //retrigger note + x vblanks (ticks)
          c->retrig = effectdata&0x0F;
          break;

        case 0xA0: //fine volume slide up (add x to volume)
          c->targetedport = false;
          c->doarp = false;
          c->doport = false;
          c->volstep = (effectdata & 0x0F);
          c->effect_timer = 2;
          c->fineeffect = true;
          break;

        case 0xB0: //fine volume slide down (subtract x from volume)
          c->targetedport = false;
          c->doarp = false;
          c->doport = false;
          c->volstep = -(effectdata & 0x0F);
          c->effect_timer = 2;
          c->fineeffect = true;
          break;

        case 0xC0: //cut from note + x vblanks
          break;

        case 0xD0: //delay note x vblanks
          break;

        case 0xE0: //delay pattern x notes
          if(!delset) delcount = effectdata&0x0F;
          delset = true;
          break;

        case 0xF0: //invert loop (x = speed)
          break;
      }
      break;
    }

    //0x0F already taken care of by preprocesseffects()

    default:
      c->doport = false;
      break;
  }
}

void processnote(modfile* m, channel* c, uint8_t* data, uint8_t offset, 
                 bool overwrite)
{
  if(globaltick == 0)
  {
    uint16_t period = (((uint16_t)((*data)&0x0F))<<8) | (uint16_t)(*(data+1));
    uint8_t tempsam = ((((*data))&0xF0) | ((*(data+2)>>4)&0x0F));
    if((period || tempsam) && !inrepeat)
    {
      if(tempsam)
      {
        tempsam--;
        //sample* prevsam = c->sample;
        c->sample = m->samples[tempsam];
        c->volume = (double)c->sample->volume / 64.0;
      }
      //printf("FINETUNE: %f\n", (double)(c->sample->finetune));
      if(period)
      {
        c->period = period;
        c->portdest = c->period;
        c->stop = false;
        c->repeat = false;
        c->index = 0;
      }
      
      c->finetune = c->sample->finetune;
    }

    if(c->period == 0 || c->sample == NULL || !c->sample->length)
      c->stop = true;

    c->volstep = 0;
    c->effect_timer = 0;
    processnoteeffects(c, data);
  }
  if(c->retrig && globaltick == c->retrig-1)
  {
    c->index = 0;
    c->stop = false;
    c->repeat = false;
  }

  double conv_ratio;

  //RESAMPLE PER TICK

  int writesize = SAMPLE_RATE*0.02;

  //c->cdata->output_frames = (int)(SAMPLE_RATE*0.02);

  if(c->effect_timer == 2)
  {
    if(c->doarp) c->period = c->arp[globaltick%3];
    else if(c->doport)
    {   
      if(c->targetedport)
      {
        if(abs(c->portdest - c->period) < c->targstep)
        {
          //printf("no portstep\n");
          //c->portstep = c->portstep/2;
          c->period = c->portdest;
          c->doport = false;
        }
        else if(c->portdest > c->period)
        {
          c->period += c->targstep;
          if(c->period > 856) c->period = 856;
        }
        else
        {
          c->period -= c->targstep;
          if(c->period < 113) c->period = 113;
        }
      }
      else
      {
        c->period += c->portstep;
        if(c->period > 856) c->period = 856;
        else if(c->period < 113) c->period = 113;
      }
    }
    
    c->volume += (double)c->volstep/64.0;
    if(c->volume < 0.0) c->volume = 0.0;
    else if(c->volume > 1.0) c->volume = 1.0;
    if(c->fineeffect) c->effect_timer = 0;
  }
  else if(c->effect_timer == 1) c->effect_timer++;
  //if(c->stop) c->rate = SAMPLE_RATE;
  
  
  //c->cdata->input_frames = writesize;

  //printf("writesize: %d\n", writesize);

  //write empty frame
  if(c->stop)
  {
    conv_ratio = 1.0;
    c->cdata->src_ratio = conv_ratio;
    libsrc_error = src_set_ratio(c->converter, conv_ratio);
    if(libsrc_error) error(libsrc_error);
    c->cdata->input_frames = 0.02*SAMPLE_RATE;
    //c->rate = SAMPLE_RATE;
    for(int i = 0; i < 0.02*SAMPLE_RATE; i++)
      c->buffer[i] = 0.0f;
  }
  //write non-empty frame to buffer to be interpolated
  else
  {
    double rate = calcrate(c->period, c->finetune);
    conv_ratio = SAMPLE_RATE/rate;
    libsrc_error = src_set_ratio(c->converter, conv_ratio);
    if(libsrc_error) error(libsrc_error);
    c->cdata->src_ratio = conv_ratio;
    c->cdata->input_frames = 0.02*rate;
    //add fractional part of rate calculation to account for error
    c->offset += rate*0.02 - (double)(uint32_t)(rate*0.02);

    for(int i = 0; i < 0.02*rate; i++)
    {
      //uint32_t rate = calcrate(c->period, c->sample->finetune);
      //c->increment = (rate/(uint32_t)SAMPLE_RATE);
      c->buffer[i] = c->sample->sampledata[c->index++] * c->volume * 0.3f;

      if(c->repeat && (c->index >= (c->sample->repeatlength)*2
        + (c->sample->repeatpoint)*2))
      {
        c->index = c->sample->repeatpoint*2;
      }
      else if(c->index >= (c->sample->length)*2)
      {
        if(c->sample->repeatlength > 2)
        {
          c->index = c->sample->repeatpoint * 2;
          c->repeat = true;
        }
        else
        {
          //float last = c->buffer[i];
          for(int j = i+1; j < 0.02*rate; j++)
            c->buffer[j] = 0.0f;
          c->stop = true;
          break;
        }
        //c->index = restore;
      }
    }
    c->index += offset;
  }

  libsrc_error = src_process(c->converter, c->cdata);
  if(libsrc_error) 
  {
    printf("RATIO: %f\n", c->cdata->src_ratio);
    printf("FAILED ON ATTEMPT TO PROCESS\n");
    error(libsrc_error);
  }
  
  if(c->cdata->output_frames_gen != c->cdata->output_frames)
  {
    for(int k = c->cdata->output_frames_gen; k < c->cdata->output_frames; k++)
    {
      c->resampled[k] =
        c->resampled[c->cdata->output_frames_gen-1];
    }
    //exit(1);
  }

  //WRITE TO MIXING BUFFER
  if(overwrite)
  {
    for(int i = 0; i < writesize; i++)
    {
      audiobuf[i*2+offset] = c->resampled[i];
      if(audiobuf[i*2+offset] >= 0.75f)
        audiobuf[i*2+offset] = 0.0f;
    }
  }
  else
  {
    for(int i = 0; i < writesize; i++)
    {
      audiobuf[i*2+offset] += c->resampled[i];
      if(audiobuf[i*2+offset] >= 0.75f)
        audiobuf[i*2+offset] = 0.0f;
    }
  }

  c->prevperiod = c->period;
  /*printf("period: %d\n", c->period);
  printf("SPEED: %d\n", m->speed);
  printf("ROW: %d\n", row);
  printf("TICK: %u\n", globaltick);
  printf("length of buffer: %d\n", count);
  printf("normal size: %f\n", SAMPLE_RATE*m->secsperrow);*/

  //if(outer == 5) exit(0);
  //PAY ATTENTION TO THIS

  if(globaltick == m->speed - 1)
  {
    c->doport = false;
    c->effect_timer = 0;
    c->retrig = 0;
  }
}

void sampleparse(modfile* m, uint8_t* filearr, uint32_t start)
{
  for(int i = 0; i < 31; i++) //TODO: add support for non-31 instrument formats
  {
    sample* s = malloc(sizeof(sample));
    m->samples[i] = s;
    memcpy(&(s->name), filearr+20+(30*i), 22);
    s->name[22] = '\x00';

    s->length = (uint16_t)*(filearr+42+(30*i)) << 8;
    s->length |= (uint16_t)*(filearr+43+(30*i));

    //if(s->name[0]) printf("%s\n", s->name);
    //printf("length: %d\n", s->length);
    if (s->length != 0)
    {
      int8_t tempfinetune = filearr[44 + 30 * i]&0x0F;
      if(tempfinetune > 0x07)
      {
        tempfinetune |= 0xF0;
      }
      s->finetune = tempfinetune;
      s->volume = filearr[45 + 30 * i];

      s->repeatpoint = (uint16_t)*(filearr+46+(30*i)) << 8;
      s->repeatpoint |= (uint16_t)*(filearr+47+(30*i));

      s->repeatlength = (uint16_t)*(filearr+48+(30*i)) << 8;
      s->repeatlength |= (uint16_t)*(filearr+49+(30*i));

      int copylen = (s->length)*2;
      s->inverted = false;
      s->sampledata = malloc(copylen*sizeof(float));
      floatncpy(s->sampledata, (int8_t*)(filearr+start), copylen);
      start += copylen;
    }
  }
  return;
}

void stepframe(modfile* m, channel* cp)
{
  if(row == 64)
  { 
    row = 0;
    pattern++;
  }
  //printf("Pattern: %d\n", pattern);
  if(pattern >= m->songlength)
  {
    done = true;
    return;
  }

  if(globaltick == 0)
  {
    curdata = m->patterns + ((m->patternlist[pattern])*1024) + (16*row);
    preprocesseffects(curdata);
    preprocesseffects(curdata + 4);
    preprocesseffects(curdata + 8);
    preprocesseffects(curdata + 12);
    patternset = false;
  }
  //curdata = m->patterns + ((m->patternlist[pattern])*1024) + (16*row);
  processnote(m, &cp[0], curdata, 0, true);
  //printf("channel 1\n");
  processnote(m, &cp[1], curdata + 4, 1, true);
  //printf("channel 2\n");
  processnote(m, &cp[2], curdata + 8, 1, false);
  //printf("channel 3\n");
  processnote(m, &cp[3], curdata + 12, 0, false);
  //fwrite(audiobuf, sizeof(float), SAMPLE_RATE*gm->secsperrow*2, outfile);
  globaltick++;
  if(globaltick == m->speed)
  {
    if(delcount)
    {
      inrepeat = true;
      delcount--;
    }
    else
    {
      delset = false;
      inrepeat = false;
      row++;
    }
    globaltick = 0;
  }
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
  strncpy((char*)m->name, (char*)filearr, 20);
  m->name[20] = '\x00';
  printf("%s\n", m->name);
  memcpy(m->magicstring, filearr+1080, 4);
  m->magicstring[4] = '\x00';
  if(!strcmp(m->magicstring, "M.K.") && !strcmp(m->magicstring, "4CHN")) 
  { 
    printf("magic string check failed\n");
    m->songlength = 0;
    return m;
  }
  m->songlength = filearr[950];
  //printf("songlength: %d\n", m->songlength);
  memcpy(m->patternlist, filearr+952, 128);
  /*printf("patterns:\n");
  for(int i = 0; i < m->songlength; i++)
  {
    printf("%d\n", m->patternlist[i]);
  }*/
  int max = 0;
  for(int i = 0; i < 128; i++)
  {
    if(m->patternlist[i] > max) max = m->patternlist[i];
  }
  //printf("max: %d\n", max);
  uint32_t len = (uint32_t)(1024*(max+1)); //1024 = size of pattern
  m->patterns = malloc(len);
  memcpy(m->patterns, filearr+1084, len);
  sampleparse(m, filearr, len+1084);
  m->speed = 6; //default speed = 6
  m->tempo = 125/(m->speed);
  m->secsperrow = 0.02*(m->speed);
  //printf("secsperrow: %f\n", m->secsperrow);
  return m;
}

static int standardcallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void* userdata)
{
  (void) inputBuffer;
  (void) timeInfo;
  (void) statusFlags;
  (void) userdata;
  stepframe(gm, gcp);
  float* out = (float*) outputBuffer;
  float* in = audiobuf;

  for(unsigned int i = 0; i < framesPerBuffer; i++)
  {
    *out++ = *in++;
    *out++ = *in++;
  }

  (curbuf == 1)?(curbuf = 0):(curbuf = 1);
  audiobuf = buffs[curbuf];
  if(!done) return 0;
  return 1;
}

static int headphonecallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void* userdata)
{
  (void) inputBuffer;
  (void) timeInfo;
  (void) statusFlags;
  (void) userdata;
  stepframe(gm, gcp);
  float* out = (float*) outputBuffer;
  float* in = audiobuf;

  for(unsigned int i = 0; i < framesPerBuffer; i++)
  {
    float l = *in++;
    float r = *in++;
    *out++ = l+0.30f*r;
    *out++ = r+0.30f*l;
  }

  (curbuf == 1)?(curbuf = 0):(curbuf = 1);
  audiobuf = buffs[curbuf];
  if(!done) return 0;
  return 1;
}

int main(int argc, char const *argv[])
{
  if(argc < 2)
  {
    printf("Please specify a valid mod file.\n");
    return 1;
  }
  if(argc > 2 && strcmp(argv[2], "-h") == 0) callback = headphonecallback;
  else callback = standardcallback;
  FILE* f = fopen(argv[1], "r");
  if(f == NULL)
  {
    printf("Please specify a valid mod file.\n");
    return 1;
  }
  gm = modparse(f);
  //printf("finished parsing modfile\n");
  if(gm->songlength == 0)
  {
    printf("Invalid modfile. Error %d\n", gm->songlength);
    return 1;
  }
  fclose(f);
  gcp = initsound();
  patternset = false;
  //printf("Successfully initialized sound.\n");

  pa_error = Pa_StartStream(stream);
  if(pa_error != paNoError)
  {
    printf("PortAudio error: %s\n", Pa_GetErrorText(pa_error));
    abort();
  }


  while(!done)
  {
    Pa_Sleep(20); //play until done
  }

  pa_error = Pa_StopStream(stream);
  for(int i = 0; i < 4; i++)
  {
    src_delete(gcp[i].converter);
  }

  if(pa_error != paNoError)
  {
    printf("PortAudio error: %s\n", Pa_GetErrorText(pa_error));
    abort();
  }

  return 0;
}