LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libsamplerate

LOCAL_SRC_FILES := \
        samplerate.c \
        src_linear.c \
        src_sinc.c \
        src_zoh.c

include $(BUILD_SHARED_LIBRARY)
