MFoP
=====
Welcome!

MFoP (Mod Files on Pizza) is a portable Amiga Mod player written in C. It is designed to be fast, small, lightweight, and portable.
Currently, MFoP should support all 4 channel 15/31 instrument Amiga Mod files, as well as most effects. Give it a try!

You'll need to build MFoP with PortAudio and libsamplerate.

To build: 
```
gcc [portaudio path] [libsrc path] -std=c99 -pedantic -Wall -Werror -Wextra MFoP3.c -o MFoP3 -O3
```

To use:

normal mode
```
MFoP3 [modfile]
```
headphones mode (currently disabled)
```
MFoP3 [modfile] -h
```