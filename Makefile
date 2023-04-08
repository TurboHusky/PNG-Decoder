INCLUDE_PATH := ../include

CC := x86_64-w64-mingw32-cc
CCFLAGS := -I ${INCLUDE_PATH} -Wall -Wextra -Werror
LDFLAGS := -L ./
LDLIBS := -lglfw3

SRC := png.c decompress.h decompress.c

all:
	$(CC) $(SRC) -o png.exe $(CCFLAGS)
	mv png.exe ../../../../Pictures/png.exe

.PHONY : clean
clean :
	@echo Cleaning up...
	rm -f png.exe