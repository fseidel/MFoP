MFoP
=====
Welcome!

MFoP (Mod Files on Pizza) is a portable Amiga Mod player written in C. It is designed to be fast, small (Mac binary is under 24KiB!), lightweight, and portable.
Currently, MFoP should support all 4 channel 15/31 instrument Amiga Mod files, as well as all effects except E3x and E0x. Give it a try!

You'll need to build MFoP with PortAudio and libsamplerate.

To build: 
```
gcc [portaudio path] [libsrc path] -std=c99 -pedantic -Wall -Werror -Wextra MFoP.c -o MFoP -O3
```

To use:

normal mode
```
MFoP [modfile]
```
options
```
-h = headphones mode (does a bit of mixing to make the panning less severe)
-l = looping (restarts song at end)
```