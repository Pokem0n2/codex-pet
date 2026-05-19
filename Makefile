CC = x86_64-w64-mingw32-gcc
CFLAGS = -Os -fno-stack-protector -fno-asynchronous-unwind-tables \
         -fno-ident -fmerge-all-constants -ffunction-sections -fdata-sections \
         -Wall -fno-unwind-tables -fno-common -fno-builtin
LDFLAGS = -nostartfiles -nodefaultlibs -Wl,--gc-sections -Wl,--build-id=none \
          -s -Wl,--file-alignment=256 -Wl,-e,entry -Wl,--disable-dynamicbase
LIBS = -lmsvcrt -lole32 -lwindowscodecs -luuid -lgdi32 -luser32 -lkernel32 -lmsimg32

# 单编译单元构建，以便编译器进行跨函数优化
SRCS = src/main.c
INCLUDES = src/pet_json.c src/pet_io.c src/pet_render.c src/pet_ai.c src/pet_wnd.c src/pet_data.c

codex-pet.exe: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(foreach f,$(INCLUDES),-include $f) $(LIBS)

upx: codex-pet.exe
	upx --ultra-brute $<

clean:
	rm -f codex-pet.exe

.PHONY: clean upx
