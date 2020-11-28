LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	main.cpp \
	bmpdecoderhelper.cpp

LOCAL_MODULE:= gen_logo

LOCAL_STATIC_LIBRARIES := libcutils

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
