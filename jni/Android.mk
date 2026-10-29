LOCAL_PATH := $(call my-dir)

# RubberBand 4.0 (high-quality live pitch shifter, GPL-2.0-or-later) is OPTIONAL and not stored in this project.
# * present in jni/rubberband-src/ (single/RubberBandSingle.cpp)  -> built in, pitch engine "High"/"Auto" available
# * missing                                                        -> builds anyway with the built-in Lite pitch shifter only
RB_DIR := $(LOCAL_PATH)/rubberband-src
HAVE_RB := $(wildcard $(RB_DIR)/single/RubberBandSingle.cpp)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE        := HexedVoiceMod
LOCAL_SRC_FILES     := main.cpp mod/logger.cpp mod/config.cpp
LOCAL_C_INCLUDES    += $(LOCAL_PATH)
ifneq ($(HAVE_RB),)
$(info HexedVoiceMod: RubberBand found - building with the high-quality pitch engine)
LOCAL_SRC_FILES     += rubberband-src/single/RubberBandSingle.cpp
LOCAL_C_INCLUDES    += $(RB_DIR)
else
$(info HexedVoiceMod: RubberBand NOT found in jni/rubberband-src - building with the built-in Lite pitch shifter only)
LOCAL_CFLAGS        += -DVFX_NO_RUBBERBAND
endif
LOCAL_CPP_FEATURES  := exceptions rtti
LOCAL_ARM_MODE      := arm
LOCAL_ARM_NEON      := true
# RubberBand's R3 engine is heavy: release flags + NEON matter for stutter-free audio on phones
LOCAL_CFLAGS        += -O3 -DNDEBUG -ffast-math -fno-finite-math-only
LOCAL_CPPFLAGS      += -std=c++17
LOCAL_LDLIBS        += -llog -ldl
include $(BUILD_SHARED_LIBRARY)
