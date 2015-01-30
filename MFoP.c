#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <samplerate.h>
#include <math.h>
#include <portaudio.h>
#include <time.h>

static uint32_t const PAL_CLOCK = 3546895;
static double const SAMPLE_RATE = 44100;
static double const FINETUNE_BASE = 1.0072382087;

uint16_t periods[] = {
  856,808,762,720,678,640,604,570,538,508,480,453,
  428,404,381,360,339,320,302,285,269,254,240,226,
  214,202,190,180,170,160,151,143,135,127,120,113};

uint8_t funktable[] = {
  0,5,6,7,8,10,11,13,16,19,22,26,32,43,64,128};

int8_t* waves[4];
int8_t sine[64];
int8_t saw[64];
int8_t square[64];
int8_t randwave[64];

bool loop;
bool headphones;
float* audiobuf;
float* mixbuf;
//float* buffs[2];
int pattern;
int row;
uint8_t* curdata;
bool done;
int libsrc_error;
//uint8_t curbuf;
PaStream* stream;
PaError pa_error;
uint8_t globaltick;
bool patternset;
uint8_t delcount;
bool delset;
bool inrepeat;
double ticktime;
double nextticktime;
uint8_t nexttempo;
uint8_t nextspeed;
uint8_t looppoint;
uint8_t loopcount;
uint8_t type;
bool inloop;

typedef struct {
  char name[23];
  uint16_t length;
  int8_t finetune;
  uint8_t volume;
  uint16_t repeatpoint;
  uint16_t repeatlength;
  bool inverted;
  //float* sampledata;
  int8_t* sampledata;
} sample;

typedef struct{
  sample* sample;
  double volume;
  double storedvolume;
  double offset;
  uint32_t index;
  float* buffer;
  float* resampled;
  uint32_t increment;
  bool repeat;
  bool stop;
  uint8_t deltick;
  uint16_t period;
  uint16_t* arp;
  uint16_t portdest;
  uint16_t tempperiod;
  uint8_t portstep;
  uint8_t cut;
  SRC_STATE* converter;
  SRC_DATA* cdata;
  int8_t retrig;
  uint8_t vibspeed;
  uint8_t vibwave;
  uint8_t vibpos;
  uint8_t vibdepth;
  uint8_t tremspeed;
  uint8_t tremwave;
  uint8_t trempos;
  uint8_t tremdepth;
  uint8_t funkcounter;
  uint8_t funkpos;
  uint8_t funkspeed;
} channel;

typedef struct{
  char name[21];
  uint8_t* patterns;
  uint8_t songlength;
  sample* samples[31];
  uint8_t patternlist[128];
  uint32_t speed;
  uint16_t tempo;
  char magicstring[5];
} modfile;

modfile* gm;
channel* gcp;

int findperiod(uint16_t period)
{
  for(int i = 0; i < 36; i++) 
    if(periods[i] == period) return i;
  //printf("PERIOD TABLE LOOKUP ERROR: %d\n", period);
  return -1;
}

static inline double calcrate(uint16_t period, int8_t finetune)
{
  return PAL_CLOCK/
    (round((double)period*pow(FINETUNE_BASE, -(double)finetune)));
}

//Currently unused because of funkrepeat
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

void precalculatetables()
{
  //formulas from http://stackoverflow.com/questions/3387159/actionscript-creating-square-triangle-sawtooth-waves-from-math-sin
  double cursin;
  srand(time(NULL));
  waves[0] = sine;
  waves[1] = saw;
  waves[2] = square;
  waves[3] = randwave;
  for(int i = 0; i < 64; i++)
  {
    cursin = sin(i*2*M_PI/64.0);
    sine[i] = 255*cursin;
    saw[i] = -255*((i/64.0) - floor((i/64.0) + 0.5));
    cursin = sin((i*2*M_PI+(M_PI/2)/64));
    square[i] = 255*((cursin == 0)?0:cursin/abs(cursin));
    randwave[i] = (int8_t)(rand()%256);
  }
}

channel* initsound()
{
  precalculatetables();
  pa_error = Pa_Initialize();
  if(pa_error != paNoError)
  {
    printf("PortAudio error: %s\n", Pa_GetErrorText(pa_error));
    abort();
  }
  channel* channels = malloc(4*sizeof(channel));
  mixbuf = malloc(0.08*2*SAMPLE_RATE*sizeof(float));
  //buffs[1] = malloc(0.02*2*SAMPLE_RATE*sizeof(float));
  //curbuf = 0;
  audiobuf = malloc(0.08*2*SAMPLE_RATE*sizeof(float));
  for(int i = 0; i < 4; i++)
  {
    channels[i].volume = 0.0;
    channels[i].storedvolume = 0.0;
    channels[i].deltick = 0;
    channels[i].increment = 0.0f;
    channels[i].buffer = malloc(PAL_CLOCK*sizeof(float));
    //channels[i].output = malloc(1024*sizeof(float));
    channels[i].resampled = malloc(0.08*SAMPLE_RATE*sizeof(float));
    channels[i].stop = true;
    channels[i].repeat = false;
    channels[i].arp = malloc(3*sizeof(uint16_t));
    channels[i].period = 0;
    channels[i].portdest = 0;
    channels[i].tempperiod = 0;
    channels[i].portstep = 0;
    channels[i].offset = 0.0;
    channels[i].retrig = 0;
    channels[i].vibwave = 0;
    channels[i].tremwave = 0;
    channels[i].vibpos = 0;
    channels[i].trempos = 0;
    channels[i].funkcounter = 0;
    channels[i].funkpos = 0;
    channels[i].funkspeed = 0;
    channels[i].sample = NULL;
    //channels[i].converter = src_new(SRC_ZERO_ORDER_HOLD, 1, &libsrc_error);
    channels[i].converter = src_new(SRC_LINEAR, 1, &libsrc_error);
    //channels[i].converter = src_new(SRC_SINC_FASTEST, 1, &libsrc_error);
    //channels[i].converter = src_new(SRC_SINC_BEST_QUALITY, 1, &libsrc_error);
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
                                  SAMPLE_RATE*0.02, NULL,
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
  if ((*(data+2)&0x0F) == 0x0F) //set speed/tempo
  {
    uint8_t effectdata = *(data+3);
    if(effectdata == 0) return;
    if(effectdata > 0x1F)
    {
      gm->tempo = effectdata;
      nexttempo = effectdata;
      ticktime = 1/(0.4*effectdata);
      nextticktime = 1/(0.4*effectdata);
    }
    else
    {
      gm->speed = effectdata;
      nextspeed = effectdata;
    }
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
        c->tempperiod = c->arp[globaltick%3];
      }
      break;

    case 0x01: //slide up
      c->period -= effectdata;
      c->tempperiod = c->period;
      break;

    case 0x02: //slide down
      c->period += effectdata;
      c->tempperiod = c->period;
      break;

    case 0x05: //tone portamento + volume slide
      //slide up
      if(effectdata&0xF0) c->volume += ((effectdata>>4) & 0x0F)/64.0;
      //slide down
      else c->volume -= effectdata/64.0;
      //exploit fallthrough

    case 0x03: //tone portamento
      if(abs(c->portdest - c->period) < c->portstep)
          c->period = c->portdest;
      else if(c->portdest > c->period)
        c->period += c->portstep;
      else
        c->period -= c->portstep;
      c->tempperiod = c->period;
      break;

    case 0x06: //vibrato + volume slide
      //slide up
      if(effectdata&0xF0) c->volume += ((effectdata>>4) & 0x0F)/64.0;
      //slide down
      else c->volume -= effectdata/64.0;
      //exploit fallthrough

    case 0x04: //vibrato
      c->tempperiod = c->period + 
        (((int16_t)c->vibdepth*waves[c->vibwave&3][c->vibpos])>>7);
      c->vibpos += c->vibspeed;
      c->vibpos %= 64;
      break;

    case 0x07: //tremolo
      c->volume = c->storedvolume +
        (int16_t)c->tremdepth*waves[c->tremwave&3][c->trempos]/64.0;
      c->trempos += c->tremspeed;
      c->trempos %= 64;
      break;

    //Set panning doesn't do anything in a standard Amiga MOD
    case 0x08: //set panning
      break;

    case 0x0A: //volume slide
      //slide up
      if(effectdata&0xF0) c->volume += ((effectdata>>4) & 0x0F)/64.0;
      //slide down
      else c->volume -= effectdata/64.0;
      break;

    //0x0B already taken care of by preprocesseffects()


    //0x0D already taken care of by preprocesseffects()

    case 0x0E: //E commands
    {
      switch(effectdata & 0xF0)
      {
        case 0x00: //set filter (0 on, 1 off)
          break;

        case 0x30: //glissando control (0 off, 1 on, use with tone portamento)
          break;

        case 0x40: //set vibrato waveform (0 sine, 1 ramp down, 2 square)
          c->vibwave = effectdata & 0x0F;
          break;

        case 0x50: //set finetune
        {
          int8_t tempfinetune = effectdata & 0x0F;
          if(tempfinetune > 0x07) tempfinetune |= 0xF0;
          c->sample->finetune = tempfinetune;
          break;
        }

        case 0x70: //set tremolo waveform (0 sine, 1 ramp down, 2 square)
          c->tremwave = effectdata & 0x0F;
          break;

        //There's no effect 0xE8 (is 8 evil or something?)

        case 0x90: //retrigger note + x vblanks (ticks)
          if(globaltick % (effectdata&0x0F) == 0) c->index = 0;
          break;

        case 0xC0: //cut from note + x vblanks
          if(globaltick == (effectdata&0x0F)) c->volume = 0.0f;
          break;

        case 0xE0: //delay pattern x notes
          if(!delset) delcount = effectdata&0x0F;
          delset = true;
          break;
      }
      break;
    }
    case 0x0F:
      if(effectdata == 0) break;
      if(effectdata > 0x1F)
      {
        nexttempo = effectdata;
        nextticktime = 1/(0.4*effectdata);
      }
      else nextspeed = effectdata;
      break;

    default:
      break;
  }
}

void processnote(channel* c, uint8_t* data, uint8_t offset, 
                 bool overwrite)
{
  uint8_t tempeffect = *(data+2)&0x0F;
  uint8_t effectdata = *(data+3);
  if(globaltick == 0)
  {
    if(tempeffect == 0x0E && (effectdata&0xF0) == 0xD0) 
      c->deltick = effectdata&0x0F;
    uint16_t period = (((uint16_t)((*data)&0x0F))<<8) | (uint16_t)(*(data+1));
    uint8_t tempsam = ((((*data))&0xF0) | ((*(data+2)>>4)&0x0F));
    if(c->deltick == 0)
    {
      if((period || tempsam) && !inrepeat)
      {
        if(tempsam)
        {
          c->funkpos = 0;
          c->stop = false;
          tempsam--;
          c->offset = 0;
          //sample* prevsam = c->sample;
          c->sample = gm->samples[tempsam];
          c->volume = (double)c->sample->volume / 64.0;
        }
        //printf("FINETUNE: %f\n", (double)(c->sample->finetune));
        if(period)
        {
          if(tempeffect != 0x03 && tempeffect != 0x05)
          {
            c->period = period;
            c->tempperiod = period;
            c->index = 0;
            c->stop = false;
            c->repeat = false;
            c->offset = 0;
          }
          c->portdest = period;
          //c->arp[0] = c->period;
        }
      }
      switch(tempeffect)
      {
        case 0x00:
          if(effectdata)
          {
            c->period = c->portdest;
            int base = findperiod(c->period);
            if(base == -1)
            {
              break;
            }
            c->arp[0] = c->period;
            //printf("ARP0: %d\n", (int)c->arp[0]);
            c->arp[1] = periods[base+((effectdata>>4)&0x0F)];
            //printf("ARP1: %d\n", (int)c->arp[1]);
            c->arp[2] = periods[base+(effectdata&0x0F)];
            //printf("ARP2: %d\n", (int)c->arp[2]);
          }
          break;

        case 0x03:
          if(effectdata) c->portstep = effectdata;
          break;

        case 0x04: //vibrato
          if(effectdata)
          {
            c->vibdepth = effectdata & 0x0F;
            c->vibspeed = (effectdata >> 4) & 0x0F;
          }
          if(!(c->vibwave&4)) c->vibpos = 0;
          break;
        
        case 0x07: //tremolo
          c->storedvolume = c->volume;
          if(effectdata)
          {
            c->tremdepth = effectdata & 0x0F;
            c->tremspeed = (effectdata >> 4) & 0x0F;
          }
          if(!(c->tremwave&4)) c->trempos = 0;
          break;

        case 0x09: //set sample offset
          c->index = (uint32_t)effectdata * 0x100;
          break;

        case 0x0B: //position jump
          pattern = effectdata;
          patternset = true;
          break;

        case 0x0C: //set volume
          if(effectdata > 64) c->volume = 1.0;
          else c->volume = effectdata / 64.0;
          break;

        case 0x0D: //row jump
          if(!patternset) pattern++;
          row = (effectdata>>4)*10+(effectdata&0x0F) - 1;
          if(!offset && !overwrite) patternset = false;
          break;
    
        case 0x0E:
        {
          switch(effectdata&0xF0)
          {
            case 0x10:
              c->period -= effectdata&0x0F;
              c->tempperiod = c->period;
              break;

            case 0x20:
              c->period += effectdata&0x0F;
              c->tempperiod = c->period;
              break;

            case 0x60: //jump to loop, play x times
              if(!(effectdata & 0x0F))
              {
                looppoint = ((row == -1)?0:row);
                //looppoint = row;
              }
              else if(effectdata & 0x0F) 
              {
                if(!inloop)
                {
                  loopcount = (effectdata & 0x0F) - 1;
                  row = looppoint-1;
                  inloop = true;
                }
                else if(inloop && loopcount)
                {
                  row = looppoint-1;
                  loopcount--;
                }
                else inloop = false;
              }
              break;

            case 0xA0:
              c->volume += (effectdata&0x0F)/64.0;
              break;

            case 0xB0:
              c->volume -= (effectdata&0x0F)/64.0;
              break;

            case 0xC0:
              c->cut = effectdata&0x0F;
              break;

            case 0xF0:
              c->funkspeed = funktable[effectdata&0x0F];
              break;

            default:
              break;
          }
        }

      /*case 0x0F: //set speed/tempo
        if(effectdata == 0) break;
        if(effectdata > 0x1F)
        {
          nexttempo = effectdata;
          nextticktime = 1/(0.4*effectdata);
        }
        else nextspeed = effectdata;
        break;*/

      default:
         break;
      }
    }

    if(c->tempperiod == 0 || c->sample == NULL || c->sample->length == 0)
      c->stop = true;
  }
  else if (c->deltick == 0) processnoteeffects(c, data);
  if(c->deltick) c->deltick--;
  if(c->retrig && globaltick == c->retrig-1)
  {
    c->index = 0;
    c->stop = false;
    c->repeat = false;
  }

  double conv_ratio;

  //RESAMPLE PER TICK

  int writesize = SAMPLE_RATE*ticktime;

  //c->cdata->output_frames = (int)(SAMPLE_RATE*0.02);

  /*if(c->effect_timer == 2)
  {
    
    else if(c->dovib)
    {
      c->period = c->portdest + 
        (int16_t)c->vibdepth*vibwaves[c->vibwave&3][c->vibpos]/64;
      c->vibpos += c->vibspeed;
      c->vibpos %= 64;
    }*/

  if(c->volume < 0.0) c->volume = 0.0;
  else if(c->volume > 1.0) c->volume = 1.0;
  if(c->tempperiod > 856) c->tempperiod = 856;
  else if(c->tempperiod < 113) c->tempperiod = 113;
  
  //write empty frame
  c->cdata->output_frames = ticktime*SAMPLE_RATE;
  if(c->stop)
  {
    conv_ratio = 1.0;
    c->cdata->src_ratio = conv_ratio;
    libsrc_error = src_set_ratio(c->converter, conv_ratio);
    if(libsrc_error) error(libsrc_error);
    c->cdata->input_frames = ticktime*SAMPLE_RATE;
    //c->rate = SAMPLE_RATE;
    for(int i = 0; i < ticktime*SAMPLE_RATE; i++)
      c->buffer[i] = 0.0f;
  }
  //write non-empty frame to buffer to be interpolated
  else
  {
    double rate = calcrate(c->tempperiod, c->sample->finetune);
    conv_ratio = SAMPLE_RATE/rate;
    c->cdata->src_ratio = conv_ratio;
    libsrc_error = src_set_ratio(c->converter, conv_ratio);
    if(libsrc_error) error(libsrc_error);
    c->cdata->input_frames = ticktime*rate;

    //add fractional part of rate calculation to account for error
    //c->offset += rate*ticktime - (uint32_t)(rate*ticktime);
    //c->index += c->offset;
    //c->offset -= (uint32_t)(c->offset);

    c->funkcounter += c->funkspeed;
    if(c->funkcounter >= 128)
    {
      c->funkcounter = 0;
      /*c->sample->sampledata[c->sample->repeatpoint*2+c->funkpos] =
        (((int8_t)(c->sample->sampledata[c->sample->repeatpoint*2
          +c->funkpos]*128)^0xFF)/128.0f);*/
      c->sample->sampledata[c->sample->repeatpoint*2+c->funkpos] ^= 0xFF;
      c->funkpos = (c->funkpos+1) % (c->sample->repeatlength*2);
    }

    for(int i = 0; i < ticktime*rate-1; i++)
    {
      c->buffer[i] = (float)c->sample->sampledata[c->index++]/128.0f 
        * c->volume * 0.4f;

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
          for(int j = i+1; j < ticktime*rate-1; j++)
            c->buffer[j] = 0.0f;
          c->stop = true;
          break;
        }
        //c->index = restore;
      }
    }
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
  }

  //WRITE TO MIXING BUFFER
  if(overwrite)
  {
    for(int i = 0; i < writesize; i++)
    {
      audiobuf[i*2+offset] = c->resampled[i];
    }
  }
  else
  {
    for(int i = 0; i < writesize; i++)
    {
      audiobuf[i*2+offset] += c->resampled[i];
    }
  }

  /*printf("period: %d\n", c->period);
  printf("SPEED: %d\n", m->speed);
  printf("ROW: %d\n", row);
  printf("TICK: %u\n", globaltick);
  printf("length of buffer: %d\n", count);
  printf("normal size: %f\n", SAMPLE_RATE*m->secsperrow);*/

  //if(outer == 5) exit(0);
  
  //PAY ATTENTION TO THIS

  if(globaltick == gm->speed - 1)
  {
    c->tempperiod = c->period;
    if(tempeffect == 0x07) c->volume = c->storedvolume;
  }
}

void sampleparse(modfile* m, uint8_t* filearr, uint32_t start)
{
  uint8_t numsamples = type?15:31;
  for(int i = 0; i < numsamples; i++)
  {
    sample* s = malloc(sizeof(sample));
    m->samples[i] = s;
    strncpy(s->name, (char*)filearr+20+(30*i), 22);
    s->name[22] = '\x00';

    s->length = (uint16_t)*(filearr+42+(30*i)) << 8;
    s->length |= (uint16_t)*(filearr+43+(30*i));

    for(int j = 0; j < 22; j++)
    {
      if(!s->name[j]) break;
      if(s->name[j] < 32) s->name[j] = 32;
    }
    if(s->name[0]) printf("%s\n", s->name);
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
      //s->sampledata = malloc(copylen*sizeof(float));
      s->sampledata = malloc(copylen*sizeof(int8_t));
      //floatncpy(s->sampledata, (int8_t*)(filearr+start), copylen);
      memcpy(s->sampledata, (int8_t*)(filearr+start), copylen);
      start += copylen;
    }
  }
  return;
}

void steptick(channel* cp)
{
  //gm->speed = nextspeed;
  //gm->tempo = nexttempo;
  //ticktime = nextticktime;

  if(row == 64)
  { 
    row = 0;
    pattern++;
  }
  //printf("Pattern: %d\n", pattern);
  if(pattern >= gm->songlength)
  {
    if(loop)
    {
      pattern = 0;
      row = 0;
      globaltick = 0;
      gm->speed = 6;
      nextspeed = 6;
      gm->tempo = 125;
      nexttempo = 125;
      ticktime = 0.02;
      nextticktime = 0.02;
    }
    else
    {
      done = true;
      return;
    }
  }

  if(globaltick == 0)
  {
    curdata = gm->patterns + ((gm->patternlist[pattern])*1024) + (16*row);
    preprocesseffects(curdata);
    preprocesseffects(curdata + 4);
    preprocesseffects(curdata + 8);
    preprocesseffects(curdata + 12);
    gm->speed = nextspeed;
    gm->tempo = nexttempo;
    ticktime = nextticktime;
  }


  processnote(&cp[0], curdata, 0, true);
  processnote(&cp[1], curdata + 4, 1, true);
  processnote(&cp[2], curdata + 8, 1, false);
  processnote(&cp[3], curdata + 12, 0, false);

  globaltick++;
  if(globaltick == gm->speed)
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
  if(strcmp(m->magicstring, "M.K.") && strcmp(m->magicstring, "4CHN")) 
  { 
    printf("Warning: Not a 31 instrument 4 channel MOD file. May not be playable.\n");
    type = 1;
    //m->songlength = 0;
    //return m;
  }
  else type = 0;
  //printf("magic string%s\n", m->magicstring);
  if (type == 0) m->songlength = filearr[950];
  else m->songlength = filearr[470];
  //printf("songlength: %d\n", m->songlength);
  if (type == 0) memcpy(m->patternlist, filearr+952, 128);
  else memcpy(m->patternlist, filearr+472, 128);
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
  if(type == 0)
  {
    memcpy(m->patterns, filearr+1084, len);
    sampleparse(m, filearr, len+1084);
  }
  else
  {
    memcpy(m->patterns, filearr+600, len);
    sampleparse(m, filearr, len+600);
  }
  m->speed = 6; //default speed = 6
  nextspeed = 6;
  m->tempo = 125;
  nexttempo = 125;
  ticktime = 0.02;
  nextticktime = 0.02;
  //printf("secsperrow: %f\n", m->secsperrow);
  return m;
}
char* filename;
int main(int argc, char *argv[])
{
  if(argc < 2)
  {
    printf("Please specify a valid mod file.\n");
    return 1;
  }
  headphones = false;
  for(int i = 1; i < argc; i++)
  {
    switch(*argv[i])
    {
      case '-':
        switch(*(argv[i]+1))
        {
          case 'h':
            headphones = true;
            break;
          case 'l':
            loop = true;
            break;
        }
        break;
      default:
        filename = argv[i];
    }
  }
  //if(argc > 2 && strcmp(argv[2], "-h") == 0) headphones = true;
  //else headphones = false;

  FILE* f = fopen(filename, "r");
  if(f == NULL)
  {
    printf("Please specify a valid mod file.\n");
    return 1;
  }
  gm = modparse(f);
  inloop = false;
  //ticktime = 0.02;
  //nextticktime = 0.02;
  //nexttempo = 125;
  //nextspeed = 6;

  //printf("finished parsing modfile\n");
  /*if(gm->songlength == 0)
  {
    printf("Invalid modfile. Error %d\n", gm->songlength);
    return 1;
  }*/

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

  curdata = gm->patterns + ((gm->patternlist[pattern])*1024) + (16*row);

  while(!done)
  {
    steptick(gcp);
    if(headphones)
    {
      float* in = audiobuf;
      float* out = mixbuf;
      for(unsigned int i = 0; i < ticktime*SAMPLE_RATE; i++)
      {
        float l = *in++;
        float r = *in++;
        *out++ = l+0.30f*r;
        *out++ = r+0.30f*l;
      }
      pa_error = Pa_WriteStream(stream, mixbuf, ticktime*SAMPLE_RATE);
    }
    else
      pa_error = Pa_WriteStream(stream, audiobuf, ticktime*SAMPLE_RATE);
    if(pa_error != paNoError)
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(pa_error));
      abort();
    }
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
