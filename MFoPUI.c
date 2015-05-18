#ifndef M_PI
  #define M_PI 3.1415926535897932384
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <samplerate.h>
#include <math.h>
#include <portaudio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ncurses.h>

static uint32_t const PAL_CLOCK = 3546895;
static double const SAMPLE_RATE = 48000;
static double const FINETUNE_BASE = 1.0072382087;

uint16_t periods[] = {
  856,808,762,720,678,640,604,570,538,508,480,453,
  428,404,381,360,339,320,302,285,269,254,240,226,
  214,202,190,180,170,160,151,143,135,127,120,113};

char* notes[] = {
  "C-1", "C#1", "D-1", "D#1", "E-1", "F-1",
  "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
  "C-2", "C#2", "D-2", "D#2", "E-2", "F-2",
  "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
  "C-3", "C#3", "D-3", "D#3", "E-3", "F-3",
  "F#3", "G-3", "G#3", "A-3", "A#3", "B-3"};

uint8_t funktable[] = {
  0,5,6,7,8,10,11,13,16,19,22,26,32,43,64,128};

WINDOW* patternwin;
//WINDOW* instrwin;

int16_t* waves[3];
int16_t sine[64];
int16_t saw[64];
int16_t square[64];

char* displaypatterns;
//int16_t randwave[64];

off_t filelength;
bool loop;
bool headphones;
float* audiobuf;
float* mixbuf;

int pattern;
int row;
int currow;
int curpattern;
bool addflag; //used for emualting obscure Dxx bug
uint8_t* curdata;
bool done;
int libsrc_error;
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
uint8_t numsamples;
uint8_t type;

typedef struct {
  char name[23];
  uint16_t length;
  int8_t finetune;
  uint8_t volume;
  uint16_t repeatpoint;
  uint16_t repeatlength;
  int8_t* sampledata;
} sample;

typedef struct{
  sample* sample;
  int8_t volume;
  int8_t tempvolume;
  uint32_t index;
  float* buffer;
  float* resampled;
  uint32_t increment;
  bool repeat;
  bool stop;
  uint8_t deltick;
  uint16_t period;
  uint16_t arp[3];
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
  int8_t looppoint;
  int8_t loopcount;
  uint16_t offset;
  uint16_t offsetmem;
  float error;
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
  if(period > 856 || period < 113) return -1;
  uint8_t upper = 35;
  uint8_t lower = 0;
  uint8_t mid;
  while(upper >= lower)
  {
    mid = (upper+lower)/2;
    if(periods[mid] == period) return mid;
    else if(periods[mid] > period) lower = mid+1;
    else upper = mid-1;
  }
  return -1;
  /*for(int i = 0; i < 36; i++)
    if(periods[i] == period) return i;
  //printw("PERIOD TABLE LOOKUP ERROR: %d\n", period);
  return -1;*/
}

static inline double calcrate(uint16_t period, int8_t finetune)
{
  return PAL_CLOCK/
    (round((double)period*pow(FINETUNE_BASE, -(double)finetune)));
}

//Currently unused because of funkrepeat
/*void floatncpy(float* dest, int8_t* src, int n)
{
  for(int i = 0; i < n; i++)
    dest[i] = ((float)src[i])/128.0f;
}*/

void libsrcerror(int err)
{
  printw("Lib SRC Error: %s\n", src_strerror(err));
  abort();
}

void portaudioerror(int err)
{
  printw("PortAudio error: %s\n", Pa_GetErrorText(err));
  abort();
}

void precalculatetables()
{
  double cursin;
  //srand(time(NULL));
  waves[0] = sine;
  waves[1] = saw;
  waves[2] = square;
  //waves[3] = randwave;
  for(int i = 0; i < 64; i++)
  {
    cursin = sin(i*2*M_PI/64.0);
    sine[i] = 255*cursin;
    saw[i] = 255-(8*i);
    square[i] = (i<32)?255:-256;
    //randwave[i] = (int8_t)(rand()%256);
  }
}

channel* initsound()
{
  precalculatetables();
  pa_error = Pa_Initialize();
  if(pa_error != paNoError) portaudioerror(pa_error);
  channel* channels = malloc(4*sizeof(channel));
  mixbuf = malloc(0.08*2*SAMPLE_RATE*sizeof(float));
  //buffs[1] = malloc(0.02*2*SAMPLE_RATE*sizeof(float));
  //curbuf = 0;
  audiobuf = malloc(0.08*2*SAMPLE_RATE*sizeof(float));
  for(int i = 0; i < 4; i++)
  {
    channels[i].error = 0;
    channels[i].volume = 0;
    channels[i].tempvolume = 0;
    channels[i].deltick = 0;
    channels[i].increment = 0.0f;
    channels[i].buffer = malloc(PAL_CLOCK*sizeof(float));
    //channels[i].output = malloc(1024*sizeof(float));
    channels[i].resampled = malloc(0.08*SAMPLE_RATE*sizeof(float));
    channels[i].stop = true;
    channels[i].repeat = false;
    //channels[i].arp = malloc(3*sizeof(uint16_t));
    channels[i].period = 0;
    channels[i].portdest = 0;
    channels[i].tempperiod = 0;
    channels[i].portstep = 0;
    channels[i].offset = 0;
    channels[i].offsetmem = 0;
    channels[i].retrig = 0;
    channels[i].vibwave = 0;
    channels[i].tremwave = 0;
    channels[i].vibpos = 0;
    channels[i].trempos = 0;
    channels[i].funkcounter = 0;
    channels[i].funkpos = 0;
    channels[i].funkspeed = 0;
    channels[i].looppoint = 0;
    channels[i].loopcount = -1;
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
    currow = 0;
    pattern = 0;
    delset = false;
    inrepeat = false;
    delcount = 0;
    globaltick = 0;
    addflag = false;
  }
  //open the audio stream
  pa_error = Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, SAMPLE_RATE,
                                  paFramesPerBufferUnspecified, NULL,
                                  audiobuf);

  if(pa_error != paNoError) portaudioerror(pa_error);
  return channels;
}

void renderpattern(uint8_t* patterndata)
{
  uint16_t period;
  uint8_t tempsam;
  uint8_t* data;
  uint8_t tempeffect;
  uint8_t effectdata;

  for(int line = 0; line < 64; line++)
  {
    for(int chan = 0; chan < 4; chan++)
    {
      data = patterndata+16*line+chan*4;
      period = (((uint16_t)((*data)&0x0F))<<8) | (uint16_t)(*(data+1));
      tempsam = ((((*data))&0xF0) | ((*(data+2)>>4)&0x0F));
      tempeffect = *(data+2)&0x0F;
      effectdata = *(data+3);

      //tempsam = tempsam;
      //if(period) sprintf((displaypatterns+49*line+chan*12), "%03x ", period);
      if(period)
      {
        char* notestr;
        int8_t noteid = findperiod(period);
        if(noteid == -1) notestr = "???";
        else notestr = notes[noteid];
        sprintf((displaypatterns+48*line+chan*12), "%s ", 
          notestr);
      }
      else sprintf((displaypatterns+48*line+chan*12), "    ");
      if(tempsam) sprintf((displaypatterns+48*line+chan*12+4),
        "%2X ", tempsam);
      else sprintf((displaypatterns+48*line+chan*12+4), "   ");
      if(tempeffect || effectdata)
        sprintf((displaypatterns+48*line+chan*12+7), "%03X| ",
          ((uint16_t)tempeffect<<8)|effectdata);
      else sprintf((displaypatterns+48*line+chan*12+7), "   | ");
    }
    //wprintw(patternwin, "%s\n", displaypatterns+line*49);
    //wrefresh(patternwin);
    //sprintf(displaypatterns, "");
  }

  return;
}

void preprocesseffects(uint8_t* data)
{
  if ((*(data+2)&0x0F) == 0x0F) //set speed/tempo
  {
    uint8_t effectdata = *(data+3);
    //if(effectdata == 0) return;
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
      if(effectdata) c->tempperiod = c->arp[globaltick%3];
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
      if(effectdata&0xF0) c->volume += ((effectdata>>4) & 0x0F);
      //slide down
      else c->volume -= effectdata;
      c->tempvolume = c->volume;
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
      if(effectdata&0xF0) c->volume += ((effectdata>>4) & 0x0F);
      //slide down
      else c->volume -= effectdata;
      c->tempvolume = c->volume;
      //exploit fallthrough

    case 0x04: //vibrato
      c->tempperiod = c->period +
        ((c->vibdepth*waves[c->vibwave&3][c->vibpos])>>7);
      c->vibpos += c->vibspeed;
      c->vibpos %= 64;
      //printw("%d %d\n", c->period, c->tempperiod);
      break;

    case 0x07: //tremolo
      c->tempvolume = c->volume +
        ((c->tremdepth*waves[c->tremwave&3][c->trempos])>>6);
      c->trempos += c->tremspeed;
        //c->trempos += 1;
      c->trempos %= 64;
      break;

    case 0x0A: //volume slide
      //slide up
      if(effectdata&0xF0) c->volume += ((effectdata>>4) & 0x0F);
      //slide down
      else c->volume -= effectdata;
      c->tempvolume = c->volume;
      break;

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
          if(((effectdata&0x0F) == 0) ||
            (globaltick % (effectdata&0x0F)) == 0) c->index = c->offset;
          break;

        case 0xC0: //cut from note + x vblanks
          if(globaltick == (effectdata&0x0F)) c->volume = 0;
          break;
      }
      break;
    }
    case 0x0F:
      if(effectdata == 0)
      {
        done = true;
        break;
      }
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
  if(globaltick == 0 && tempeffect == 0x0E && (effectdata&0xF0) == 0xD0)
      c->deltick = (effectdata&0x0F)%gm->speed;
  if(globaltick == c->deltick)
  {
    uint16_t period = (((uint16_t)((*data)&0x0F))<<8) | (uint16_t)(*(data+1));
    uint8_t tempsam = ((((*data))&0xF0) | ((*(data+2)>>4)&0x0F));
    //if(period) printw("%03x", period);
    //else printw("   ");
    if((period || tempsam) && !inrepeat)
    {
      if(tempsam)
      {
        /*if (c->sample != gm->samples[tempsam])
        {
          c->funkpos = 0;
        }*/
        //printw("%2x ", tempsam);
        c->error = 0;
        c->stop = false;
        tempsam--;
        if(tempeffect != 0x03 && tempeffect != 0x05) c->offset = 0;
        //sample* prevsam = c->sample;
        c->sample = gm->samples[tempsam];
        c->volume = c->sample->volume;
        c->tempvolume = c->volume;
      }
      //else printw("00 ");
      //printw("FINETUNE: %f\n", (double)(c->sample->finetune));
      if(period)
      {
        if(tempeffect != 0x03 && tempeffect != 0x05)
        {
          if(tempeffect == 0x09)
          {
            if(effectdata) c->offsetmem = effectdata * 0x100;
            c->offset += c->offsetmem;
          }
          c->period = period;
          c->tempperiod = period;
          c->index = c->offset;
          c->stop = false;
          c->repeat = false;
          c->vibpos = 0;
          c->trempos = 0;
          c->error = 0;
        }
        c->portdest = period;
        //c->arp[0] = c->period;
      }
    }
    //if(tempeffect || effectdata) printw("%1x %02x ", tempeffect, effectdata);
    //else printw("    ");
    switch(tempeffect)
    {
      case 0x00:
        if(effectdata)
        {
          c->period = c->portdest;
          int base = findperiod(c->period);
          if(base == -1)
          {
            c->arp[0] = c->period;
            c->arp[1] = c->period;
            c->arp[2] = c->period;
            break;
          }
          uint8_t step1 = base+((effectdata>>4)&0x0F);
          uint8_t step2 = base+(effectdata&0x0F);
          c->arp[0] = c->period;
          if(step1 > 35)
          {
            if(step1 == 36) c->arp[1] = 0;
            else c->arp[1] = periods[(step1-1)%36];
          }
          else c->arp[1] = periods[step1];
          if(step2 > 35)
          {
            if(step2 == 36) c->arp[2] = 0;
            else c->arp[2] = periods[(step2-1)%36];
          }
          else c->arp[2] = periods[step2];
        }
        break;

      case 0x03:
        if(effectdata) c->portstep = effectdata;
        break;

      case 0x04: //vibrato
        if(effectdata & 0x0F) c->vibdepth = effectdata & 0x0F;
        if(effectdata & 0xF0) c->vibspeed = (effectdata >> 4) & 0x0F;
        break;

      case 0x07: //tremolo
        if(effectdata)
        {
          c->tremdepth = effectdata & 0x0F;
          c->tremspeed = (effectdata >> 4) & 0x0F;
        }
        break;

      case 0x0B: //position jump
        if(currow == row) row = 0;
        pattern = effectdata;
        patternset = true;
        break;

      case 0x0C: //set volume
        if(effectdata > 64) c->volume = 64;
        else c->volume = effectdata;
        c->tempvolume = c->volume;
        break;

      case 0x0D: //row jump
        if(delcount) break;
        if(!patternset)
          pattern++;
        if(pattern >= gm->songlength) pattern = 0;
        row = (effectdata>>4)*10+(effectdata&0x0F);
        patternset = true;
        if(addflag) row++; //emulate protracker EEx + Dxx bug
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
            if(!(effectdata & 0x0F)) c->looppoint = row;
            else if(effectdata & 0x0F)
            {
              if(c->loopcount == -1)
              {
                c->loopcount = (effectdata & 0x0F);
                row = c->looppoint;
              }
              else if(c->loopcount) row = c->looppoint;
              c->loopcount--;
            }
            break;

          case 0xA0:
            c->volume += (effectdata&0x0F);
            c->tempvolume = c->volume;
            break;

          case 0xB0:
            c->volume -= (effectdata&0x0F);
            c->tempvolume = c->volume;
            break;

          case 0xC0:
            c->cut = effectdata&0x0F;
            break;

          case 0xE0: //delay pattern x notes
            if(!delset) delcount = effectdata&0x0F;
            delset = true;
            /*emulate bug that causes protracker to cause Dxx to jump
            too far when used in conjunction with EEx*/
            addflag = true;
            break;

          case 0xF0:
            c->funkspeed = funktable[effectdata&0x0F];
            break;

          default:
            break;
        }
      }

    default:
      break;
    }

    if(c->tempperiod == 0 || c->sample == NULL || c->sample->length == 0)
      c->stop = true;
  }
  else if (c->deltick == 0) processnoteeffects(c, data);
  if(c->retrig && globaltick == c->retrig-1)
  {
    c->index = 0;
    c->stop = false;
    c->repeat = false;
  }

  double conv_ratio;

  //RESAMPLE PER TICK

  int writesize = SAMPLE_RATE*ticktime;
  if(c->volume < 0) c->volume = 0;
  else if(c->volume > 64) c->volume = 64;

  if(c->tempvolume < 0) c->tempvolume = 0;
  else if(c->tempvolume > 64) c->tempvolume = 64;
  if(c->tempperiod > 856) c->tempperiod = 856;
  else if(c->tempperiod < 113) c->tempperiod = 113;

  //write empty frame
  c->cdata->output_frames = ticktime*SAMPLE_RATE;
  if(c->stop)
  {
    conv_ratio = 1.0;
    c->cdata->src_ratio = conv_ratio;
    libsrc_error = src_set_ratio(c->converter, conv_ratio);
    if(libsrc_error) libsrcerror(libsrc_error);
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
    if(libsrc_error) libsrcerror(libsrc_error);
    c->cdata->input_frames = ticktime*rate;

    c->funkcounter += c->funkspeed;
    if(c->funkcounter >= 128)
    {
      c->funkcounter = 0;
      c->sample->sampledata[c->sample->repeatpoint*2+c->funkpos] ^= 0xFF;
      c->funkpos = (c->funkpos+1) % (c->sample->repeatlength*2);
    }

    for(int i = 0; i < ticktime*rate-1; i++)
    {
      c->buffer[i] = (float)c->sample->sampledata[c->index++]/128.0f
        * c->tempvolume/64.0 * 0.4f;

      if(c->repeat && (c->index >= (c->sample->repeatlength)*2
        + (c->sample->repeatpoint)*2))
      {
        c->index = c->sample->repeatpoint*2;
      }
      else if(c->index >= (c->sample->length)*2)
      {
        if(c->sample->repeatlength > 1)
        {
          c->index = c->sample->repeatpoint*2;
          c->repeat = true;
        }
        else
        {
          //float last = c->buffer[i];
          for(int j = i+1; j < ticktime*rate-1; j++)
            c->buffer[j] = 0;
          c->stop = true;
          break;
        }
        //c->index = restore;
      }
    }
    //add fractional part of rate calculation to account for error
    /*c->error += rate*ticktime - (uint32_t)(rate*ticktime);
    c->index += c->error;
    c->error -= (uint32_t)(c->error);*/
  }
  libsrc_error = src_process(c->converter, c->cdata);
  if(libsrc_error) libsrcerror(libsrc_error);

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

  if(globaltick == gm->speed - 1)
  {
    c->tempperiod = c->period;
    //c->tempvolume = c->volume;
    c->deltick = 0;
  }
}

void sampleparse(modfile* m, uint8_t* filearr, uint32_t start)
{

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

    if(i+5 < LINES)
    {
      if(s->name[0]) mvprintw(i+5, 52, "%02X %s", i+1, s->name);
      else mvprintw(i+5, 52, "%02X", i+1);
    }

    //printw("length: %d\n", s->length);
    if (s->length != 0)
    {
      int8_t tempfinetune = filearr[44 + 30 * i]&0x0F;
      if(tempfinetune > 0x07) tempfinetune |= 0xF0;
      s->finetune = tempfinetune;
      s->volume = filearr[45 + 30 * i];

      s->repeatpoint = (uint16_t)*(filearr+46+(30*i)) << 8;
      s->repeatpoint |= (uint16_t)*(filearr+47+(30*i));

      s->repeatlength = (uint16_t)*(filearr+48+(30*i)) << 8;
      s->repeatlength |= (uint16_t)*(filearr+49+(30*i));

      int copylen = (s->length)*2;
      //s->sampledata = malloc(copylen*sizeof(float));
      s->sampledata = malloc(copylen*sizeof(int8_t));

      if ((start + copylen) > filelength) abort();
      //floatncpy(s->sampledata, (int8_t*)(filearr+start), copylen);
      memcpy(s->sampledata, (int8_t*)(filearr+start), copylen);
      start += copylen;
    }
  }
  return;
}

void steptick(channel* cp)
{
  if(row == 64)
  {
    row = 0;
    if(pattern == curpattern) pattern++;
    /*if(pattern < gm->songlength)
      renderpattern(gm->patterns + 1024*gm->patternlist[pattern]);*/
  }
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
      /*renderpattern(gm->patterns + 1024*gm->patternlist[pattern]);*/
    }
    else
    {
      done = true;
      return;
    }
  }

  if(globaltick == 0)
  {
    attron(COLOR_PAIR(3));
    mvprintw(4, 0, "position: 0x%02X  pattern: 0x%02X  row: 0x%02X  speed: 0x%02X  tempo: %d\n",
      pattern, gm->patternlist[pattern], row, gm->speed, gm->tempo);
    if(pattern != curpattern)
      renderpattern(gm->patterns + 1024*gm->patternlist[pattern]);
    for(int line = -6; line < 12; line++)
    {
      if(line == 0)
      {
        wattron(patternwin, A_REVERSE);
        mvwprintw(patternwin, 7+line, 1, " %s", displaypatterns+(row+line)*48);
        wattroff(patternwin, A_REVERSE);
      }
      else if(row+line < 64 && row+line >= 0)
        mvwprintw(patternwin, 7+line, 1, " %s", displaypatterns+(row+line)*48);
      else
        mvwaddstr(patternwin, 7+line, 1, "           |           |           |           ");
    }
    box(patternwin, 0, 0);
    wrefresh(patternwin);
    refresh();

    patternset = false;
    curdata = gm->patterns + ((gm->patternlist[pattern])*1024) + (16*row);
    currow = row;
    curpattern = pattern;
    preprocesseffects(curdata);
    preprocesseffects(curdata + 4);
    preprocesseffects(curdata + 8);
    preprocesseffects(curdata + 12);
    gm->speed = nextspeed;
    gm->tempo = nexttempo;
    ticktime = nextticktime;
    attroff(COLOR_PAIR(3));
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
      addflag = false;
      inrepeat = false;
      if(currow == row) row++;
    }
    globaltick = 0;
  }
}

modfile* modparse(FILE* f)
{
  uint8_t* filearr = malloc(filelength*sizeof(uint8_t));
  int seek = 0;
  int c;
  while((c = fgetc(f)) != EOF)
    filearr[seek++] = (uint8_t)c;
  modfile* m = malloc(sizeof(modfile));
  strncpy((char*)m->name, (char*)filearr, 20);
  m->name[20] = '\x00';
  //printw("%s\n", m->name);
  memcpy(m->magicstring, filearr+1080, 4);
  m->magicstring[4] = '\x00';
  if(strcmp(m->magicstring, "M.K.") && strcmp(m->magicstring, "4CHN"))
  {
    printw("Warning: Not a 31 instrument 4 channel MOD file. May not be playable.\n");
    type = 1;
  }
  else type = 0;

  numsamples = type?15:31;
  //printw("magic string%s\n", m->magicstring);
  if (type == 0) m->songlength = filearr[950];
  else m->songlength = filearr[470];
  //printw("songlength: %d\n", m->songlength);
  if (type == 0) memcpy(m->patternlist, filearr+952, 128);
  else memcpy(m->patternlist, filearr+472, 128);
  /*printw("patterns:\n");
  for(int i = 0; i < m->songlength; i++)
  {
    printw("%d\n", m->patternlist[i]);
  }*/
  int max = 0;
  for(int i = 0; i < 128; i++)
  {
    if(m->patternlist[i] > max) max = m->patternlist[i];
  }
  //printw("max: %d\n", max);
  uint32_t len = (uint32_t)(1024*(max+1)); //1024 = size of pattern
  m->patterns = malloc(len);
  uint16_t size;
  if(type == 0) size = 1084;
  else size = 600;
  memcpy(m->patterns, filearr+size, len);
  sampleparse(m, filearr, len+size);
  m->speed = 6; //default speed = 6
  nextspeed = 6;
  m->tempo = 125;
  nexttempo = 125;
  ticktime = 0.02;
  nextticktime = 0.02;
  //printw("secsperrow: %f\n", m->secsperrow);
  free(filearr);
  return m;
}
char* filename;
int main(int argc, char *argv[])
{
  if(argc < 2) goto fileerror;
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
  struct stat s;
  if(stat(filename, &s) == 0 && !S_ISREG(s.st_mode)) goto fileerror;
  FILE* f = fopen(filename, "rb");
  if(f == NULL) goto fileerror;
  fseek(f, 0L, SEEK_END);
  filelength = ftell(f);
  if(filelength <= 0) goto fileerror;
  fseek(f, 0L,SEEK_SET);
  initscr();
  start_color();
  curs_set(0);
  init_pair(1, COLOR_YELLOW, COLOR_BLACK);
  attron(COLOR_PAIR(1));
  printw("MFoP 1.1.4: A tiny ProTracker MOD player\nBaked with love\n");
  attroff(COLOR_PAIR(1));
  init_pair(2, COLOR_BLUE, COLOR_BLACK);
  attron(COLOR_PAIR(2));
  refresh();
  patternwin = newwin(20, 49, 5, 0);
  //instrwin = newwin()
  box(patternwin, 0, 0);
  init_pair(5, COLOR_BLACK, COLOR_WHITE);
  wcolor_set(patternwin, COLOR_PAIR(2), NULL);
  wattron(patternwin, COLOR_PAIR(5));
  //allocate buffer for pattern viewer (includes null byte at end of line)
  displaypatterns = malloc(3136*sizeof(char));
  gm = modparse(f);

  fclose(f);
  gcp = initsound();
  patternset = false;
  //printw("Successfully initialized sound.\n");

  pa_error = Pa_StartStream(stream);
  if(pa_error != paNoError) portaudioerror(pa_error);

  curdata = gm->patterns + ((gm->patternlist[pattern])*1024) + (16*row);
  renderpattern(gm->patterns + gm->patternlist[pattern]*1024);
  init_pair(3, COLOR_WHITE, COLOR_BLACK);
  attroff(COLOR_PAIR(2));
  attron(COLOR_PAIR(3));
  mvprintw(3, 0, "Title: %s", gm->name);
  attroff(COLOR_PAIR(3));
  attron(COLOR_PAIR(2));

  noecho();
  bool pause = false;
  nodelay(stdscr, true);
  char c;
  while(!done)
  {
    input_loop:
    c = getch();
    switch (c)
    {
      case 'q':
        done = true;
        pause = false;
        break;
      case 'h':
        headphones = !headphones;
        break;
      case 'p':
        pause = !pause;
        nodelay(stdscr, !pause);
        break;
      }
      if(pause) goto input_loop;

    //break;
    steptick(gcp);
    if(headphones)
    {
      float* in = audiobuf;
      float* out = mixbuf;
      for(unsigned int i = 0; i < ticktime*SAMPLE_RATE; i++)
      {
        float l = *in++;
        float r = *in++;
        *out++ = l+0.5*r;
        *out++ = r+0.5*l;
      }
      pa_error = Pa_WriteStream(stream, mixbuf, ticktime*SAMPLE_RATE);
    }
    else
      pa_error = Pa_WriteStream(stream, audiobuf, ticktime*SAMPLE_RATE);

    if(pa_error != paNoError && pa_error != paOutputUnderflowed)
      portaudioerror(pa_error);
  }

  for(int i = 0; i < 4; i++)
  {
    src_delete(gcp[i].converter);
    free(gcp[i].buffer);
    free(gcp[i].resampled);
    free(gcp[i].cdata);
  }
  free(gcp);
  free(audiobuf);
  free(mixbuf);
  for(int i = 0; i < numsamples; i++)
  {
    if(gm->samples[i]->length) free(gm->samples[i]->sampledata);
    free(gm->samples[i]);
  }
  free(gm->patterns);
  free(gm);
  free(displaypatterns);
  pa_error = Pa_StopStream(stream);
  if(pa_error != paNoError) portaudioerror(pa_error);
  pa_error = Pa_CloseStream(stream);
  if(pa_error != paNoError) portaudioerror(pa_error);
  pa_error = Pa_Terminate();
  if(pa_error != paNoError) portaudioerror(pa_error);
  attroff(COLOR_PAIR(1));
  attroff(COLOR_PAIR(2));
  attroff(COLOR_PAIR(5));
  endwin();
  return 0;

  fileerror:
    printf("Please specify a valid mod file.\n");
    return 1;
}
