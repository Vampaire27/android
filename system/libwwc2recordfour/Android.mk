# Copyright 2018 by WWC2 Incorporated.
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:=         \
        src/SineSource.cpp    \
        

ifneq (,$(filter $(strip $(TARGET_BOARD_PLATFORM)), mt6761))
	LOCAL_SRC_FILES	+= src/wwc2_pr2100_record_four_mt6761.cpp
else
	LOCAL_SRC_FILES	+= src/wwc2_pr2100_record_four.cpp
endif

LOCAL_SHARED_LIBRARIES := \
        libstagefright libmedia liblog libutils libbinder libstagefright_foundation libcutils

LOCAL_C_INCLUDES:= \
		$(LOCAL_PATH)/inc \
		$(TOP)/vendor/wwc2/system/pr2100_combine/inc \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/native/include/media/hardware

LOCAL_CFLAGS += -Wno-multichar -Werror -Wall

LOCAL_MODULE:= libwwc2recordfour

LOCAL_MODULE_TAGS := optional

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
include $(MTK_SHARED_LIBRARY)
