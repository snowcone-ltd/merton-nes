LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

FLAGS = \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wno-unused-parameter \
	-std=c99 \
	-fPIC

ifdef DEBUG
LOCAL_CFLAGS := $(FLAGS) -O0 -g3
else
LOCAL_CFLAGS := $(FLAGS) -O3 -flto -fvisibility=hidden
LOCAL_LDFLAGS := -flto
endif

LOCAL_MODULE_FILENAME := libmerton-nes
LOCAL_MODULE := libmerton-nes

LOCAL_SRC_FILES := \
	src/cart.c \
	src/apu.c \
	src/sys.c \
	src/cpu.c \
	src/ppu.c \
	src/retro.c

include $(BUILD_SHARED_LIBRARY)
