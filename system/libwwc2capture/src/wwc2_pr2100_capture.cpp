/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#include <stdio.h>
#include <cutils/log.h>
#include <linux/fs.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <cutils/properties.h>  // For property_get().

#include <linux/ion.h>
#include <ion/ion.h>
#include <ion.h>
#include <linux/mtk_ion.h>

#include "mhal/MediaHal.h"
#include "mhal/MediaTypes.h"                 // For MHAL_ERROR_ENUM
#include "mhal/mhal_jpeg.h"                  // For MHAL JPEG
#include "wwc2_pr2100_capture.h"
#include "pr2100_combine.h"

typedef enum {
	YUYV,
	YVYU,
	NV12,
	NV21
} JpegDrvEncYUVFormat;

#define BUFFER_SIZE (2*1024*1024)
#define HW_ALIGN (64)

#define TO_CEIL(x,a) ( (((unsigned int)x) + (((unsigned int)a)-1)) & ~(((unsigned int)a)-1) )
#define TO_CEIL64(x,a) ( (((unsigned long)x) + (((unsigned long)a)-1)) & ~(((unsigned long)a)-1) )

sem_t captureWriteSem;
sem_t captureReadSem;

static int mainRun(char* filename, unsigned int w, unsigned int h, unsigned int srcFormat, struct PR2100_RECORD *obj)
{
	int src_fd = 0, dst_fd = 0;
	int dst_size;
	unsigned int enc_size;
	unsigned char *src_va = NULL;
	unsigned char *dst_va = NULL;
	unsigned int dst_offset =0;

	unsigned int imgStride = 0 ;
	unsigned int imgUVStride = 0 ;
	unsigned int memStride = 0 ;
	unsigned int heightStride = TO_CEIL((h), (srcFormat == YUYV)? 8 : 16);
	unsigned int     srcBufferSize , chromaBufferSize ;
	unsigned char* chromaAddrVA = NULL;
	unsigned int height = h;

	unsigned int width = w;
	unsigned int width2;
	int mainRet = 0;
	MHAL_JPEG_ENC_START_IN encStartInParam;

	ALOGD("bbl-capture-mainRun file(width , height) = %s (%d , %d)", filename,w, h);

	encStartInParam.imgWidth = w;
	encStartInParam.imgHeight = h;
	if(srcFormat == YUYV || srcFormat == YVYU)
	{
		encStartInParam.encFormat = JPEG_ENC_FORMAT_YUY2;
		encStartInParam.quality = 50;
	}
	else
	{
		encStartInParam.encFormat = JPEG_ENC_FORMAT_NV21;
		encStartInParam.quality = 90;//70
	}

	width2 = ((width+1)>>1) << 1;

	if (srcFormat == YUYV || srcFormat == YVYU)
	{
		imgStride = TO_CEIL((width2 << 1), 16);
		heightStride = TO_CEIL((height), 8);
	}
	else if (srcFormat == NV12 || srcFormat == NV21)
	{
		imgStride = TO_CEIL((width2), 16);
		imgUVStride = imgStride >> 1;
		heightStride = TO_CEIL((height), 16);
	}

	memStride        = imgStride;
	srcBufferSize    = ((imgStride * heightStride) + HW_ALIGN);
	chromaBufferSize = (imgUVStride * heightStride) + HW_ALIGN;

	ALOGD("bbl-capture-mainRun memStride = %d, srcBufferSize = %d, chromaBufferSize = %d",memStride,srcBufferSize,chromaBufferSize);

	int ion_client = 0;
	ion_user_handle_t src_hnd = 0;
	int ret;

	ion_client = mt_ion_open("JpgUnitTest");
	if (ion_client < 0)
	{
		ALOGD("bbl-capture-mainRun mt_ion_open failed!");
		return -1;
	}
	int bufferSize = srcBufferSize + chromaBufferSize;

	ret = ion_alloc(ion_client, bufferSize, 0, ION_HEAP_MULTIMEDIA_MASK, (ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC), &src_hnd);
	if (src_hnd > 0)
	{
		ret = ion_share( ion_client, src_hnd, &src_fd);
		if (ret != 0)
		{
			ALOGD("bbl-capture-mainRun 1-ion_share(%d) failed", ion_client);
			if (0 != ion_free(ion_client, src_hnd))
				ALOGD("bbl-capture-mainRun 2-ion_free(%d) failed", ion_client);
			else
				src_hnd = 0;

			close(ion_client);
			ion_client = 0;
			return -2;
		}

		src_va = (unsigned char*)mmap( NULL, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, src_fd, 0);
		if (MAP_FAILED == src_va || NULL == src_va)
		{
			ALOGD("bbl-capture-mainRun ion_map(%d) failed", ion_client);
			if (0 != ion_free( ion_client, src_hnd))
				ALOGD("bbl-capture-mainRun 3-ion_free(%d) failed", ion_client);
			else
				src_hnd = 0;

			close(src_fd);
			src_fd = 0;
			close(ion_client);
			ion_client = 0;
			return -3;
		}

		ALOGD("bbl-capture-mainRun src fd %d , handle: 0x%x, L:%d!", src_fd , src_hnd, __LINE__);
		encStartInParam.srcFD = src_fd;
		encStartInParam.srcChromaFD = src_fd;
	}
	else
	{
		ALOGD("bbl-capture-mainRun src ion_alloc failed! ion_client(%d), srcBufferSize(%d)", ion_client, srcBufferSize);
		close(ion_client);
		ion_client = 0;
		return -4;
	}

	obj->capture = src_va;
	sem_post(&captureWriteSem);
	sem_wait(&captureReadSem);
	chromaAddrVA = src_va + w*h;

	ALOGD("bbl-capture-mainRun %d", __LINE__);

	dst_size = BUFFER_SIZE;
	ion_user_handle_t dst_hnd = 0;

	ret = ion_alloc(ion_client, dst_size, 0, ION_HEAP_MULTIMEDIA_MASK, (ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC), &dst_hnd);
	if (dst_hnd > 0)
	{
		ret = ion_share( ion_client, dst_hnd, &dst_fd);
		if ( ret != 0 )
		{
			ALOGD("bbl-capture-mainRun 4-ion_share(%d) failed", ion_client);
			if (0 != ion_free( ion_client, dst_hnd))
				ALOGD("bbl-capture-mainRun 5-ion_free(%d) failed", ion_client);
			else
				dst_hnd = 0;
			mainRet = -7;
		}

		dst_va = (unsigned char*)mmap( NULL, dst_size, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
		if ( MAP_FAILED == dst_va || NULL == dst_va )
		{
			ALOGD("bbl-capture-mainRun 6-ion_map(%d) failed", ion_client );
			if (0 != ion_free( ion_client, dst_hnd))
				ALOGD("bbl-capture-mainRun 7-ion_free(%d) failed", ion_client);
			else
				dst_hnd = 0;
			mainRet = -8;
		}
		ALOGD("bbl-capture-mainRun dst fd %d , handle: 0x%x, L:%d!", dst_fd , dst_hnd, __LINE__);
		encStartInParam.dstFD = dst_fd;
	}
	else
	{
		ALOGD("bbl-capture-mainRun dst ion_alloc failed! ion_client(%d), dst_size(%d)", ion_client, dst_size);
		mainRet = -9;
	}

	if(mainRet >= 0)
	{
		memset(dst_va, 0, sizeof(char) * dst_size);
		dst_offset = 0 ;
		encStartInParam.srcAddr = (unsigned char *)src_va;
		encStartInParam.srcChromaAddr = (unsigned char *)chromaAddrVA;
		encStartInParam.srcBufferSize = srcBufferSize/3*2;
		encStartInParam.imgStride = memStride;
		encStartInParam.srcChromaBufferSize = chromaBufferSize;
		encStartInParam.dstAddr = (unsigned char *)((unsigned long)dst_va + dst_offset);
		encStartInParam.dstBufferSize = dst_size;
		encStartInParam.enableSOI = 1;
		encStartInParam.encSize = &enc_size;

		if(!mHalJpeg(MHAL_IOCTL_JPEG_ENC_START, (void*)&encStartInParam, sizeof(encStartInParam), NULL, 0, NULL))
		{
			int fd = -1;
			fd = open(filename, O_CREAT | O_LARGEFILE | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
			ALOGD("bbl-capture-mainRun encode SUCCESS, size %x~~~ fd = %d", enc_size, fd);
			if(fd > 0)
			{
				write(fd, dst_va,enc_size);
				close(fd);
			}
			else
				mainRet = -10;
		}
		else
		{
			ALOGD("bbl-capture-mainRunencode FAIL, size %x~~~", enc_size);
		}
	}
out:
	if(src_va)
		munmap((void*)src_va, bufferSize);

	if(src_fd)
		close(src_fd);

	if(src_hnd > 0)
		ion_free( ion_client, src_hnd);

	if(dst_va)
		munmap((void*)dst_va, dst_size);

	if(dst_fd)
		close(dst_fd);

	if(dst_hnd > 0)
		ion_free(ion_client, dst_hnd);

	if(ion_client)
		close(ion_client);

    return mainRet;
}

static void mkdirs(const char *muldir) 
{
	char cmd[256] = {0};
	FILE *fd = NULL;

	sprintf(cmd, "mkdir -p %s", muldir);
	ALOGD("bbl--%s", cmd);
	fd = popen(cmd, "r");
	if(fd == NULL)
	{
		ALOGE("bbl--mkdirs popen fail");
		return;
	}

	pclose(fd);
}

static int get_abs_file_name(enum WWC2_SAVE_FILE_DIR dir, char *name)
{
	char fileDir[64] = {0};
	char fileName[64] = {0};
	struct tm *p = NULL;
	int i = 0;

	time_t timer = time(NULL);
	p = localtime(&timer);

	switch(dir)
	{
		case DIR_LOCAL:
			sprintf(fileDir, "/storage/%s/dvr/pictures","sdcard0");
			break;
		case DIR_TFCARD:
			sprintf(fileDir, "/storage/%s/dvr/pictures","sdcard1");
			break;
		case DIR_USB0:
			sprintf(fileDir, "/storage/%s/dvr/pictures","usbotg");
			break;
		case DIR_USB1:
			sprintf(fileDir, "/storage/%s/dvr/pictures","usbotg1");
			break;
		case DIR_USB2:
			sprintf(fileDir, "/storage/%s/dvr/pictures","usbotg2");
			break;
		case DIR_USB3:
			sprintf(fileDir, "/storage/%s/dvr/pictures","usbotg3");
			break;
		case DIR_LOCAL_USB0:
			sprintf(fileDir, "/storage/sdcard0/%s/pictures","dvr_usbotg");
			break;
		case DIR_LOCAL_USB1:
			sprintf(fileDir, "/storage/sdcard0/%s/pictures","dvr_usbotg1");
			break;
		case DIR_LOCAL_USB2:
			sprintf(fileDir, "/storage/sdcard0/%s/pictures","dvr_usbotg2");
			break;
		case DIR_LOCAL_USB3:
			sprintf(fileDir, "/storage/sdcard0/%s/pictures","dvr_usbotg3");
			break;
		case DIR_LOCAL_TFCARD:
			sprintf(fileDir, "/storage/sdcard0/%s/pictures","dvr_sdcard");
			break;
		default:
			break;
	}

	if(access(fileDir,F_OK) != 0)
		mkdirs(fileDir);

	if(access(fileDir,F_OK) != 0)
	{
		ALOGD("bbl--capture get_abs_file_name fileDir = %s not exist",fileDir);
		return -1;
	}

	strftime(fileName, 64, "IMG_%F_%T.jpg",p);
	for(i = 0; i < 64; i++)
	{
		if(fileName[i] == ':')
			fileName[i] = '-';
	}

	sprintf(name, "%s/%s", fileDir, fileName);

	return 0;
}

static void set_capture_file_name(char fileName[128])
{
	FILE *fd = NULL;
	int len = strlen(fileName);

	ALOGD("bbl--len = %d, write %s to node", len, fileName);

	fd = fopen("/sys/devices/platform/wwc2_camera_combine/capture_file", "w");
	if(fd)
	{
		fwrite(fileName, len, 1, fd);
		fclose(fd);
	}
}

int wwc2_capture(void *p)
{
	struct PR2100_RECORD *obj = (struct PR2100_RECORD *)p;
	CAPTURE_MODE mode = obj->captureMode;
	enum CAMERA_FORMAT cameraFormat = obj->cameraFormat;
	char saveFile[128] = {0};
	unsigned int width = 0;
	unsigned int height = 0;
	int ret = -100;

	if(cameraFormat == CH0FHD_CH1FHD)
	{
		switch(mode)
		{
			case FRONT_CAPTURE:
			case BACK_CAPTURE:
			case DUAL_CAPTURE:
				width = 1920;
				height = 1080;	
				break;
			case TWO_CAPTURE:
				width = 1920*2;
				height = 1080;			
				break;
			default:
				break;
		}
	}
	else
	{
		switch(mode)
		{
			case FRONT_CAPTURE:
			case BACK_CAPTURE:
			case LEFT_CAPTURE:
			case RIGHT_CAPTURE:
			case QUART_CAPTURE:
			case DUAL_CAPTURE:
				width = 1280;
				height = 720;
				break;
			case FOUR_CAPTURE:
				width = 1280*2;
				height = 720*2;
				break;
			case TWO_CAPTURE:
				width = 1280*2;
				height = 720;
				break;
			default:
				break;
		}
	}

	if(width == 0 || height == 0)
	{
		ALOGD("bbl-capture-main capture mode is %d, stop it",mode);
		return ret;
	}

	ret = get_abs_file_name(obj->capture_dir, saveFile);
	if(ret < 0)
	{
		saveFile[0] = ' ';
		saveFile[1] = '\0';
		set_capture_file_name(saveFile);
		ALOGD("bbl-capture-main get_abs_file_name result => fail %d",ret);
		return ret;
	}

	ret = -100;
	if(obj->capture == NULL)
		ret = mainRun(saveFile,width,height,NV21,obj);

	if(ret == 0)
	{
		set_capture_file_name(saveFile);
		ALOGD("bbl-capture-main result => pass\n");
	}
	else
	{
		saveFile[0] = ' ';
		saveFile[1] = '\0';
		set_capture_file_name(saveFile);
		ALOGD("bbl-capture-main result => fail %d",ret);
	}
	obj->capture = NULL;
	return ret;
}
