ifeq ($(shell uname), Darwin)
	CC=clang
else
	CC=gcc
endif

CFLAGS=-std=c99 -pedantic -Wall -Werror -Wextra -O3
INCLUDES=-I/opt/local/include
LIBS=-L/opt/local/lib
RM=/bin/rm -f

all: MFoP

#MFoP:
#	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -lsamplerate -lportaudio MFoP.c -o MFoP
MFoP:
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) -lncurses -lsamplerate -lportaudio MFoP.c -o MFoP

clean:
	$(RM) MFoP
