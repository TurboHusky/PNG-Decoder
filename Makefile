INCLUDE_PATH := ../include

CC := x86_64-w64-mingw32-cc
CCFLAGS := -I ${INCLUDE_PATH} -Wall -Wextra -Werror
LDFLAGS := -L./
LDLIBS := -lglfw3

SRC := png_utils.h crc.h adler32.h decompress.h decompress.c filter.h filter.c png.c

all:
	$(CC) $(SRC) -o png.exe $(CCFLAGS)
	mv png.exe ../../../../Pictures/png.exe

.PHONY : clean
clean :
	@echo Cleaning up...
	rm -f png.exe