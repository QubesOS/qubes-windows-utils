OUTDIR = bin/$(ARCH)
CFLAGS += -Iinclude -DUNICODE -std=c11 -fgnu89-inline
LDFLAGS += -lvchan -lshlwapi -lwtsapi32 -luserenv -lversion

all: $(OUTDIR) $(OUTDIR)/windows-utils.dll

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/windows-utils.dll: $(wildcard src/*.c)
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -DWINDOWSUTILS_EXPORTS -DNO_SHLWAPI_STRFCNS -shared -o $@

