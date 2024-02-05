UNAME_S = $(shell uname -s)
ARCH = $(shell uname -m)
CORE_ARCH = x86_64
NAME = merton-nes

OBJS = \
	src/core.o \
	src/cart.o \
	src/apu.o \
	src/sys.o \
	src/cpu.o \
	src/ppu.o

INCLUDES = \
	-I.

FLAGS = \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wno-unused-parameter \
	-std=c99 \
	-fPIC

DEFS = \
	-D_POSIX_C_SOURCE=200809L \
	-DCORE_EXPORT

LD_FLAGS = \
	-nodefaultlibs

LIBS = \
	-lc

ifdef DEBUG
FLAGS := $(FLAGS) -O0 -g3
else
FLAGS := $(FLAGS) -O3 -g0 -flto -fvisibility=hidden
LD_FLAGS := $(LD_FLAGS) -flto=auto
endif

############
### WASM ###
############
ifdef WASM

WASI_SDK = $(HOME)/wasi-sdk

CC = $(WASI_SDK)/bin/clang

TARGET = web
ARCH := wasm32
SUFFIX = wasm

LD_FLAGS := $(LD_FLAGS) \
	-Wl,--allow-undefined \
	-Wl,--export-table \
	-Wl,--import-memory,--export-memory,--max-memory=1073741824 \
	-Wl,-z,stack-size=$$((8 * 1024 * 1024))

FLAGS := $(FLAGS) \
	--sysroot=$(WASI_SDK)/share/wasi-sysroot \
	--target=wasm32-wasi-threads \
	-pthread

else

#############
### LINUX ###
#############
ifeq ($(UNAME_S), Linux)

TARGET = linux
SUFFIX = so

LD_FLAGS := $(LD_FLAGS) \
	-shared

endif

#############
### APPLE ###
#############
ifeq ($(UNAME_S), Darwin)

ifndef TARGET
TARGET = macosx
endif

ifndef ARCH
ARCH = x86_64
endif

ifeq ($(ARCH), arm64)
CORE_ARCH := arm64
endif

SUFFIX = dylib

ifeq ($(TARGET), macosx)
MIN_VER = 10.15
else
MIN_VER = 13.0
FLAGS := $(FLAGS) -fembed-bitcode
endif

FLAGS := $(FLAGS) \
	-m$(TARGET)-version-min=$(MIN_VER) \
	-isysroot $(shell xcrun --sdk $(TARGET) --show-sdk-path) \
	-arch $(ARCH)

LD_FLAGS := $(LD_FLAGS) \
	-arch $(ARCH) \
	-shared

endif
endif

CFLAGS = $(INCLUDES) $(FLAGS) $(DEFS)

all: clean clear
	make objs -j4

objs: $(OBJS)
	$(CC) -o $(NAME).$(SUFFIX) $(LD_FLAGS) $(OBJS) $(LIBS)

merton: all
	cp $(NAME).$(SUFFIX) ../merton/merton-files/cores

upload: all
	python3 ../merton/assets/upload-core.py upload $(NAME) $(TARGET) $(CORE_ARCH) $(NAME).$(SUFFIX)

###############
### ANDROID ###
###############

# developer.android.com/ndk/downloads -> ~/android-ndk

ifndef ANDROID_NDK_ROOT
ANDROID_NDK_ROOT = $(HOME)/android-ndk
endif

ifndef ABI
ABI = all
endif

android: clean clear $(SHADERS)
	@$(ANDROID_NDK_ROOT)/ndk-build -j4 \
		APP_BUILD_SCRIPT=Android.mk \
		APP_PLATFORM=android-28 \
		APP_ABI=$(ABI) \
		NDK_PROJECT_PATH=. \
		--no-print-directory

clean:
	@rm -rf obj
	@rm -rf libs
	@rm -rf $(NAME).*
	@rm -rf $(OBJS)

clear:
	@clear
