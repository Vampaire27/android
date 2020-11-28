# Copyright 2018 by WWC2 Incorporated.
ifneq ($(TARGET_BUILD_MMITEST),true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	wwc2_camera_reset.c

LOCAL_MODULE:= wwc2_camera_reset

LOCAL_STATIC_LIBRARIES := libcutils liblog

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
endif
