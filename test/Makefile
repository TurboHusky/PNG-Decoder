INCLUDE_PATH := include/
PNG_INCLUDE_PATH := ../include/
MUNIT_PATH := munit/
SRC_PATH := src/
TEST_PATH := munit/

BUILD_PATH := build/
BIN_PATH := $(BUILD_PATH)bin/
LIB_PATH := ../build/lib/

CC := cc
AR := ar
CCFLAGS := -O3 -iquote $(INCLUDE_PATH) -iquote $(MUNIT_PATH) -iquote $(PNG_INCLUDE_PATH) -Wall -Wextra -Werror -Wpedantic -Winline -std=c17

LIB_NAMES := png
LDFLAGS := -L$(LIB_PATH) $(addprefix -l, $(LIB_NAMES))

LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .a, $(LIB_NAMES)))
ifeq ($(CC), x86_64-w64-mingw32-cc)
BIN_SUFFIX := .exe
DYNAMIC_LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .dll, $(LIB_NAMES)))
else
BIN_SUFFIX := 
DYNAMIC_LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .so, $(LIB_NAMES)))
endif

SRC := main.c $(SRC_PATH)zlib_tests.c $(MUNIT_PATH)munit.c

test: $(LIBS)
	@mkdir -p $(BIN_PATH)
	@$(CC) $(CCFLAGS) $(SRC) -o $(BIN_PATH)test $(LDFLAGS)

$(LIBS):
	@cd ../ && $(MAKE) static

.PHONY : clean
clean:
	@echo Cleaning up...
	@rm -r build