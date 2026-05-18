CC = x86_64-w64-mingw32-gcc
CFLAGS = -O1 -Os -fno-stack-protector -fno-asynchronous-unwind-tables \
         -fmerge-all-constants -ffunction-sections -fdata-sections -Wall -mwindows
LDFLAGS = -static -Wl,--gc-sections -s
LIBS = -lole32 -lwindowscodecs -luuid -lgdi32 -luser32 -lkernel32 -lshell32 -lmsimg32 -lm

codex-pet.exe: src/main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

upx: codex-pet.exe
	-upx --best $@

clean:
	rm -f codex-pet.exe

.PHONY: clean upx
