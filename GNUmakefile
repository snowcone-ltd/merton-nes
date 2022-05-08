UNAME_S = $(shell uname -s)
ARCH = $(shell uname -m)
LIB_NAME = merton-nes

OBJS = \
	src/cart.o \
	src/apu.o \
	src/sys.o \
	src/cpu.o \
	src/ppu.o \
	src/retro.o

FLAGS = \
	-Wall \
	-Wextra \
	-Wno-unused-value \
	-std=c99

INCLUDES = \
	-I.

DEFS = \
	-D_POSIX_C_SOURCE=200112L

LD_FLAGS = \
	-nodefaultlibs \
	-shared


############
### WASM ###
############
ifdef WASM

SUFFIX = wasm

WASI_SDK = $(HOME)/wasi-sdk-12.0

LD_FLAGS := \
	-Wl,--allow-undefined \
	-Wl,--export-table \
	-Wl,-z,stack-size=$$((8 * 1024 * 1024))

CC = $(WASI_SDK)/bin/clang --sysroot=$(WASI_SDK)/share/wasi-sysroot

OS = web
ARCH := wasm32

else


#############
### LINUX ###
#############
ifeq ($(UNAME_S), Linux)

SUFFIX = so

LIBS = \
	-lc

OS = linux
endif


#############
### MACOS ###
#############
ifeq ($(UNAME_S), Darwin)

SUFFIX = dylib

LIBS = \
	-lc

OS = macosx
MIN_VER = 10.14
ARCH = x86_64

FLAGS := $(FLAGS) \
	-m$(OS)-version-min=$(MIN_VER) \
	-isysroot $(shell xcrun --sdk $(OS) --show-sdk-path) \
	-arch $(ARCH)

endif
endif


#############
### BUILD ###
#############
ifdef DEBUG
FLAGS := $(FLAGS) -O0 -g
else
FLAGS := $(FLAGS) -O3 -g0 -flto -fvisibility=hidden
LD_FLAGS := $(LD_FLAGS) -flto
endif

CFLAGS = $(INCLUDES) $(DEFS) $(FLAGS)

all: clean clear
	make objs -j4

objs: $(OBJS)
	$(CC) -o $(LIB_NAME).$(SUFFIX) $(LD_FLAGS) $(OBJS) $(LIBS)

merton: all
	cp $(LIB_NAME).$(SUFFIX) ../merton/merton-files/cores


###############
### ANDROID ###
###############

ANDROID_PROJECT = android
ANDROID_NDK = $(HOME)/android-ndk-r21d

android: clean clear
	@mkdir -p $(ANDROID_PROJECT)/app/libs
	@$(ANDROID_NDK)/ndk-build -j4 \
		NDK_PROJECT_PATH=. \
		APP_BUILD_SCRIPT=Android.mk \
		APP_OPTIM=release \
		APP_PLATFORM=android-26 \
		NDK_OUT=$(ANDROID_PROJECT)/build \
		NDK_LIBS_OUT=$(ANDROID_PROJECT)/app/libs \
		--no-print-directory \
		| grep -v 'fcntl(): Operation not supported'

clean:
	@rm -rf $(ANDROID_PROJECT)/build
	@rm -rf $(LIB_NAME).$(SUFFIX)
	@rm -rf $(OBJS)

clear:
	@clear
