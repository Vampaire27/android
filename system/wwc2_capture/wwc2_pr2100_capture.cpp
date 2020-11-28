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
//#include <cutils/pmem.h>
//#include <cutils/memutil.h>

#include <linux/fs.h>
//#include <linux/delay.h>
//#include <linux/mm.h>
//#include <linux/slab.h>

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
#include "pr2100_combine.h"

#define xlog(...) \
        do { \
            LOGD(__VA_ARGS__); \
        } while (0)

#undef LOG_TAG
#define LOG_TAG "jpegUnitTest"

#include <linux/ion.h>
#include <ion/ion.h>
#include <ion.h>
#include <linux/mtk_ion.h>

#include "mhal/MediaHal.h"
#include "mhal/MediaTypes.h"                 // For MHAL_ERROR_ENUM
#include "mhal/mhal_jpeg.h"                  // For MHAL JPEG

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

static sem_t* getProcessSem(char* shareDev)
{
	int fd = 0;
	sem_t* sem = NULL;

	fd = open(shareDev, O_RDWR, 0666);
	if(fd <= 0)
	{
		ALOGE("bbl open shareDev fail");
		return NULL;
	}

	sem = (sem_t*)mmap(NULL, sizeof(sem_t), PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	close(fd);

	return sem;
}

static int mainRun(char* filename, unsigned int w, unsigned int h, unsigned int srcFormat, int shareMemSize)
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

	{
		int Pfd = 0;
		unsigned int index = 0;
		unsigned int u_index = 0;
		unsigned int v_index = 0;
		unsigned char *Pmem = NULL;
		int count = 0;
		struct stat statbuf;

		sem_t* captureWriteSem = getProcessSem("/sdcard/.captureWriteSem");
		sem_t* captureReadSem = getProcessSem("/sdcard/.captureReadSem");

		Pfd = open("/sdcard/.captureImg",O_RDWR,0666);
		stat("/sdcard/.captureImg", &statbuf);

		if(Pfd <= 0)
		{
			ALOGD("bbl-capture-mainRun open /sdcard/.captureImg fail");
			if(captureWriteSem)
					munmap(captureWriteSem, sizeof(sem_t));

			if(captureReadSem)
					munmap(captureReadSem, sizeof(sem_t));
			mainRet = -5;
		}
		else
		{
			Pmem = (unsigned char*)mmap( NULL, shareMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, Pfd, 0 );
			if(MAP_FAILED == Pmem || Pmem == NULL || statbuf.st_size < shareMemSize)
			{
				ALOGD("bbl-capture-mainRun mmap fail");
				if(captureWriteSem)
					munmap(captureWriteSem, sizeof(sem_t));

				if(captureReadSem)
					munmap(captureReadSem, sizeof(sem_t));

				mainRet = -6;
				close(Pfd);
			}
			else
			{
				sem_post(captureWriteSem);
				sem_wait(captureReadSem);
				memcpy(src_va,Pmem,w*h/2*3);

				chromaAddrVA = src_va + w*h;
				close(Pfd);
				munmap((void*)Pmem, shareMemSize);
				if(captureWriteSem)
					munmap(captureWriteSem, sizeof(sem_t));

				if(captureReadSem)
					munmap(captureReadSem, sizeof(sem_t));
			}
		}
	}
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
			FILE *fp;
			ALOGD("bbl-capture-mainRun encode SUCCESS, size %x~~~", enc_size);
			fp = fopen(filename, "w");
			fwrite(dst_va,1,enc_size,fp);
			fflush(fp);
			fclose(fp);
		}
		else
		{
			ALOGD("bbl-capture-mainRunencode FAIL, size %x~~~", enc_size);
		}
	}
out:
	if(src_va)
		munmap( (void*)src_va, srcBufferSize );

	if(src_fd)
		close(src_fd);

	if(src_hnd > 0)
		ion_free( ion_client, src_hnd);

	if(dst_va)
		munmap( (void*)dst_va, dst_size );

	if(dst_fd)
		close(dst_fd);

	if(dst_hnd > 0)
		ion_free(ion_client, dst_hnd);

	if(ion_client)
		close(ion_client);

    return mainRet;
}

static void setCaptureStatus(CAPTURE_STATUS status)
{
	int ret = -1;
	char data[2] = {'0', '\0'};

	data[0] += status;
	ret = pr2100_write_camera_node("/sys/devices/platform/wwc2_camera_combine/capture_status", data);
	if(ret < 0)
		ALOGD("bbl-capture-setCaptureStatus fail");		
}

static CAPTURE_MODE getCaptureMode(void)
{
	CAPTURE_MODE captureMode = DISABLE_CAPTURE;

	int mode = pr2100_read_camera_node("/sys/devices/platform/wwc2_camera_combine/capture_mode");

	if(mode >= 0 && mode <= 7)
			captureMode = (CAPTURE_MODE)mode;

	return captureMode;
}


int main(int argc, char *argv[])
{
	char saveFile[PROPERTY_VALUE_MAX] = {'\0'};
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int shareMemSize = 0;
	int ret = 0;
	CAPTURE_MODE mode = getCaptureMode();

	setCaptureStatus(CAPTURE_START);
	if(mode < FOUR_CAPTURE && mode > DISABLE_CAPTURE)
	{
		width = 1280;
		height = 720;
		shareMemSize = 338*0x1000;
	}
	else if(mode == FOUR_CAPTURE)
	{
		width = 1280*2;
		height = 720*2;
		shareMemSize = 1350*0x1000;
	}
	else
	{
		ALOGD("bbl-capture-main capture mode is %d, stop it",mode);
		setCaptureStatus(CAPTURE_STOP_FAIL);
		return 0;
	}

	property_get("wwc2.captrue.file.name", saveFile, "/sdcard/Pictures/capture.jpg");

	setCaptureStatus(CAPTURE_RUNNING);
	ret = mainRun(saveFile,width,height,NV21,shareMemSize);

	if(ret == 0)
	{
		ALOGD("bbl-capture-main result => pass\n");
		setCaptureStatus(CAPTURE_STOP_SUCCESS);
	}
	else
	{
		ALOGD("bbl-capture-main result => fail %d",ret);
		setCaptureStatus(CAPTURE_STOP_FAIL);
	}
	return 0;
}
