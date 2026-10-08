LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

SDL_PATH := ../SDL

# phone_remote.cpp includes headers as <SDL2/SDL.h> (matching Cxxdroid's
# layout), but the vendored SDL2 source exposes them as plain SDL.h. The
# SDL2inc/SDL2 symlink gives us an "SDL2/" prefix without touching SDL's
# own source tree.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include $(LOCAL_PATH)/../SDL2inc

LOCAL_SRC_FILES := phone_remote.cpp

LOCAL_SHARED_LIBRARIES := SDL2

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid

LOCAL_CPP_FEATURES := exceptions

include $(BUILD_SHARED_LIBRARY)
