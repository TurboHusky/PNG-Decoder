INCLUDE_PATH := include/
SRC_PATH := src/

BUILD_PATH := build/
OBJ_PATH := $(BUILD_PATH)obj/
LIB_PATH := $(BUILD_PATH)lib/
BIN_PATH := $(BUILD_PATH)bin/

CC := cc
AR := ar
CCFLAGS := -O3 -I $(INCLUDE_PATH) -Wall -Wextra -Werror -Wpedantic -Winline -std=c17

LIB_NAMES := png
LDFLAGS := -L$(LIB_PATH) $(addprefix -l, $(LIB_NAMES))
DBG_LDFLAGS := -L$(LIB_PATH) $(addsuffix .dbg, $(addprefix -l, $(LIB_NAMES)))

STATIC_LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .a, $(LIB_NAMES)))
DEBUG_LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .dbg.a, $(LIB_NAMES)))
ifeq ($(CC), x86_64-w64-mingw32-cc)
BIN_SUFFIX := .exe
DYNAMIC_LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .dll, $(LIB_NAMES)))
else
BIN_SUFFIX := 
DYNAMIC_LIBS := $(LIB_PATH)$(addprefix lib, $(addsuffix .so, $(LIB_NAMES)))
endif

SRC := $(OBJ_PATH)main.o
DBG_SRC := $(SRC_PATH)main.c
OBJS := png zlib filter logger
LIB_SRC := $(addprefix $(OBJ_PATH), $(OBJS))

debug: $(SRC) $(DEBUG_LIBS)
	@mkdir -p $(BIN_PATH)
	$(info "GDB debug build")
	@$(CC) -ggdb $(DBG_SRC) -static $(DBG_LDFLAGS) -I$(INCLUDE_PATH) -o $(BIN_PATH)png2ppm_debug$(BIN_SUFFIX)

static: $(SRC) $(STATIC_LIBS)
	@mkdir -p $(BIN_PATH)
	$(info "Static build")
	@$(CC) $(SRC) -static $(LDFLAGS) -I$(INCLUDE_PATH) -o $(BIN_PATH)png2ppm_static$(BIN_SUFFIX)

shared: $(SRC) $(DYNAMIC_LIBS)
	@mkdir -p $(BIN_PATH)
	$(info "Shared build")
	@$(CC) $(SRC) $(LDFLAGS) -o $(BIN_PATH)png2ppm_shared$(BIN_SUFFIX)

runtime: $(DYNAMIC_LIBS)
	@mkdir -p $(BIN_PATH)
	@$(CC) main.c ./src/logger.c -I$(INCLUDE_PATH) -o $(BIN_PATH)png2ppm_runtime$(BIN_SUFFIX)

# $@ target file name
# $% target member name for archive member (.o)
# $< first prerequisite name
# $? names of prerequisites newer than the target
# $^ name of all prerequisites
# $+ $^ with repeats
# $(@D) path of target file name

$(STATIC_LIBS): $(addsuffix .o, $(LIB_SRC))
	@mkdir -p $(@D)
	$(info "Building static lib")
	@$(AR) -rcs $@ $^

$(DEBUG_LIBS): $(addsuffix .dbg.o, $(LIB_SRC))
	@mkdir -p $(@D)
	$(info "Building debug lib")
	@$(AR) -rcs $@ $^

$(DYNAMIC_LIBS): $(addsuffix .s.o, $(LIB_SRC))
	@mkdir -p $(@D)
	$(info "Building shared lib")
	@$(CC) -shared $^ -o $@

$(OBJ_PATH)%.o : $(SRC_PATH)%.c
	@mkdir -p $(@D)
	@$(CC) -c $< $(CCFLAGS) -o $@

$(OBJ_PATH)%.dbg.o : $(SRC_PATH)%.c
	@mkdir -p $(@D)
	@$(CC) -DLOG_DEBUG -ggdb -c $< $(CCFLAGS) -o $@

$(OBJ_PATH)%.s.o : $(SRC_PATH)%.c
	@mkdir -p $(@D)
	@$(CC) -fPIC -c $< $(CCFLAGS) -o $@

.PHONY : clean
clean :
	@echo Cleaning up...
	@rm -r build