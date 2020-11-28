# Copyright 2018 by WWC2 Incorporated.
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/inc \
	$(TOP)/vendor/wwc2/system/wwc2_include \
	$(TOP)/vendor/wwc2/system/libwwc2capture/inc \
	$(TOP)/vendor/wwc2/system/libwwc2record/inc \
	$(TOP)/vendor/wwc2/system/libwwc2recordfour/inc \
	$(TOP)/vendor/wwc2/system/libwwc2h264/inc \
	$(TOP)/vendor/wwc2/system/libavm_cam/inc

LOCAL_SRC_FILES := \
		src/pr2100_water_mark.c \
		src/pr2100_dual_hd_combine.c \
		src/pr2100_dual_hd_record.c \
		src/pr2100_dual_hd_water_mark.c \
		src/pr2100_dual_fhd_combine.c \
		src/pr2100_dual_fhd_record.c \
		src/pr2100_dual_fhd_water_mark.c

ifneq (,$(filter $(strip $(TARGET_BOARD_PLATFORM)), mt6761))
	LOCAL_SRC_FILES	+= src/pr2100_combine_horizontal_mt6761.c
	LOCAL_SRC_FILES	+= src/pr2100_record_horizontal_mt6761.c
else
	LOCAL_SRC_FILES	+= src/pr2100_combine_horizontal.c
	LOCAL_SRC_FILES	+= src/pr2100_record_horizontal.c
endif

LOCAL_SHARED_LIBRARIES += liblog
LOCAL_SHARED_LIBRARIES += libcutils
LOCAL_SHARED_LIBRARIES += libwwc2capture
LOCAL_SHARED_LIBRARIES += libwwc2record
LOCAL_SHARED_LIBRARIES += libwwc2recordfour
LOCAL_SHARED_LIBRARIES += libwwc2h264
LOCAL_SHARED_LIBRARIES += libavm_cam

LOCAL_MODULE:= pr2100_combine

LOCAL_MODULE_TAGS := optional

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
include $(MTK_SHARED_LIBRARY)
