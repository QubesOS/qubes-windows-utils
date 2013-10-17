CFLAGS += -I../include -DUNICODE -D_UNICODE -DWINVER=0x0600 -D_WIN32_WINNT=0x600 -Wall
LDFLAGS += -lshlwapi
CC = gcc
AR = ar

all: libwindows-utils.a
	mkdir -p libs
	cp libwindows-utils.a libs/
	cp log.o libs/

libwindows-utils.a: exec.o getopt.o log.o utf8-conv.o
	$(AR) rcs $@ $^

clean:
	rm -fr *.o *.a libs/
