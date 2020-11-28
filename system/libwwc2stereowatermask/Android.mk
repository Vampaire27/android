# Copyright 2018 by WWC2 Incorporated.
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)/inc
LOCAL_C_INCLUDES += $(TOP)/vendor/wwc2/system/wwc2_include

LOCAL_SRC_FILES := \
		src/libwwc2StereoWaterMask.c

LOCAL_SHARED_LIBRARIES += liblog
LOCAL_SHARED_LIBRARIES += libcutils

LOCAL_MODULE:= libwwc2stereowatermask

LOCAL_MODULE_TAGS := optional

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
include $(MTK_SHARED_LIBRARY)
