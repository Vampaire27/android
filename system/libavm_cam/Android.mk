# Copyright 2018 by WWC2 Incorporated.
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/inc \
	$(TOP)/vendor/mediatek/libs/libdpframework/$(PLATFORM)/include

LOCAL_SRC_FILES := \
		src/avm_cam.cpp

LOCAL_SHARED_LIBRARIES += liblog
LOCAL_SHARED_LIBRARIES += libcutils
LOCAL_SHARED_LIBRARIES += libdpframework

LOCAL_MODULE:= libavm_cam

LOCAL_MODULE_TAGS := optional

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
include $(MTK_SHARED_LIBRARY)
