FmodP
=====
Welcome!

MFoP (Mod Files on Pizza) is a portable Amiga Mod player written in C. It is designed to be fast, small, lightweight, and portable.
Currently, MFoP should support all 4 channel 31 instrument Amiga Mod files, as well as most effects. Give it a try!

You'll need to build MFoP with PortAudio and libsamplerate.

To build: 
```
gcc [portaudio path] [libsrc path] -std=c99 -pedantic MFoP3.c -o MFoP3 -w -O3
```
