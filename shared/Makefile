CFLAGS += -I../include -I$(QUBES_INCLUDES) -DUNICODE -D_UNICODE -DWINVER=0x0600 -D_WIN32_WINNT=0x600 -Wall
LDFLAGS += -L$(QUBES_LIBS) -lshlwapi
CC = gcc
AR = ar

all: libwindows-utils.a
	mkdir -p libs
	cp libwindows-utils.a libs/
	cp *.o libs/

libwindows-utils.a: exec.o getopt.o log.o utf8-conv.o buffer.o
	$(AR) rcs $@ $^

clean:
	rm -fr *.o *.a libs/
