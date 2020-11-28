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

#undef xlog
#define xlog(...) printf(__VA_ARGS__)

#define TO_CEIL(x,a) ( (((unsigned int)x) + (((unsigned int)a)-1)) & ~(((unsigned int)a)-1) )
#define TO_CEIL64(x,a) ( (((unsigned long)x) + (((unsigned long)a)-1)) & ~(((unsigned long)a)-1) )

enum CAM_360_TYPE{
	AHD_25FPS = 1,
	AHD_30FPS,
	CVBS_NTSC,
	CVBS_PAL,
	FHD_25FPS,
	FHD_30FPS,
	TVI_INPUT,
	CVI_INPUT,
	PVI_INPUT,

	MAIN_NTSC_SUB_NTSC = 11,
	MAIN_NTSC_SUB_PAL,
	MAIN_NTSC_SUB_HD,
	MAIN_NTSC_SUB_FHD,

	MAIN_PAL_SUB_NTSC = 21,
	MAIN_PAL_SUB_PAL,
	MAIN_PAL_SUB_HD,
	MAIN_PAL_SUB_FHD,

	MAIN_HD_SUB_NTSC = 31,
	MAIN_HD_SUB_PAL,
	MAIN_HD_SUB_HD,
	MAIN_HD_SUB_FHD,

	MAIN_FHD_SUB_NTSC = 41,
	MAIN_FHD_SUB_PAL,
	MAIN_FHD_SUB_HD,
	MAIN_FHD_SUB_FHD,
};

enum INPUT_SIZE{
	SIZE_720X240 = 0,
	SIZE_1280X720,
	SIZE_720X480,
	SIZE_720X288,
	SIZE_720X576,
	SIZE_960X480,
	SIZE_960X576,
	SIZE_1280X960,
	SIZE_1920X1080
};

enum CAPTURE_ACTION{
	CAPTURE_START = 11,
	CAPTURE_RUNNING,
	CAPTURE_STOP,

	CAPTURE_FAIL = 99,
	CAPTURE_UNKONW = 100
};

static enum CAPTURE_ACTION getCaptureAction(int sensorId)
{
	enum CAPTURE_ACTION action = CAPTURE_UNKONW;

	if(sensorId == 0)
		action = (enum CAPTURE_ACTION)property_get_int32("wwc2.main.capture.action", 100);
	else if(sensorId == 1)
		action = (enum CAPTURE_ACTION)property_get_int32("wwc2.sub.capture.action", 100);

	return action;
}

static void setCaptureAction(enum CAPTURE_ACTION CaptureAction, int sensorId)
{
	int action = (int)CaptureAction;

	char value[PROPERTY_VALUE_MAX] = {'\0'};
	sprintf(value, "%d", action);

	if(sensorId == 0)
		property_set("wwc2.main.capture.action",value);
	else if(sensorId == 1)
		property_set("wwc2.sub.capture.action",value);
}

static void getCaptureFileName(char *name, int sensorId)
{
	if(sensorId == 0)
		property_get("wwc2.captrue.file.name", name, "/sdcard/Pictures/main.jpg");
	else if(sensorId == 1)
		property_get("wwc2.captrue.file.name", name, "/sdcard/Pictures/sub.jpg");
}

static int getCameraFeatureFlag(const char* node)
{
	char flag[8] = {'\0'};
	FILE *fd = NULL;
	int value = -1;

	fd = fopen(node,"r");
	if(fd)
	{
		fread(&flag,1,8,fd);
		fclose(fd);
		value = atoi(flag);
	}
	return value;
}

static enum INPUT_SIZE getVideoSize(unsigned int sensorId)
{
	enum CAM_360_TYPE cameraType = (enum CAM_360_TYPE)getCameraFeatureFlag("/sys/class/gpiodrv/gpio_ctrl/360_camtype");
	enum INPUT_SIZE size = SIZE_1280X720;

	if(sensorId == 0)//main
	{
		switch(cameraType)
		{
			case MAIN_NTSC_SUB_NTSC:
			case MAIN_NTSC_SUB_PAL:
			case MAIN_NTSC_SUB_HD:
			case MAIN_NTSC_SUB_FHD:
			case CVBS_NTSC:
				size = SIZE_720X480;
				break;
			case MAIN_PAL_SUB_NTSC:
			case MAIN_PAL_SUB_PAL:
			case MAIN_PAL_SUB_HD:
			case MAIN_PAL_SUB_FHD:
			case CVBS_PAL:
				size = SIZE_720X576;
				break;
			case MAIN_HD_SUB_NTSC:
			case MAIN_HD_SUB_PAL:
			case MAIN_HD_SUB_HD:
			case MAIN_HD_SUB_FHD:
			case AHD_25FPS:
			case AHD_30FPS:
				size = SIZE_1280X720;
				break;
			case MAIN_FHD_SUB_NTSC:
			case MAIN_FHD_SUB_PAL:
			case MAIN_FHD_SUB_HD:
			case MAIN_FHD_SUB_FHD:
			case FHD_25FPS:
			case FHD_30FPS:
				size = SIZE_1920X1080;
				break;
			default:
				size = SIZE_1280X720;
				break;
		}
	}
	else if(sensorId == 1)//sub
	{
		switch(cameraType)
		{
			case MAIN_NTSC_SUB_NTSC:
			case MAIN_PAL_SUB_NTSC:
			case MAIN_HD_SUB_NTSC:
			case MAIN_FHD_SUB_NTSC:
				size = SIZE_720X480;
				break;
			case MAIN_NTSC_SUB_PAL:
			case MAIN_PAL_SUB_PAL:
			case MAIN_HD_SUB_PAL:
			case MAIN_FHD_SUB_PAL:
				size = SIZE_720X576;
				break;
			case MAIN_NTSC_SUB_HD:
			case MAIN_PAL_SUB_HD:
			case MAIN_HD_SUB_HD:
			case MAIN_FHD_SUB_HD:
				size = SIZE_1280X720;
				break;
			case MAIN_NTSC_SUB_FHD:
			case MAIN_PAL_SUB_FHD:
			case MAIN_HD_SUB_FHD:
			case MAIN_FHD_SUB_FHD:
				size = SIZE_1920X1080;
				break;
			default:
				size = SIZE_1280X720;
				break;
		}
	}
	return size;
}

static void setPictureParam(unsigned int *width, unsigned int *height, unsigned int *shareMemSize, unsigned int sensorId)
{
	enum INPUT_SIZE videoSize = getVideoSize(sensorId);
	switch(videoSize)
	{
		case SIZE_720X240:
			*width = 720;
			*height = 240;
			*shareMemSize = 64*0x1000;
			break;
		case SIZE_720X288:
			*width = 720;
			*height = 288;
			*shareMemSize = 76*0x1000;
			break;
		case SIZE_720X480:
			*width = 720;
			*height = 480;
			*shareMemSize = 127*0x1000;
			break;
		case SIZE_720X576:
			*width = 720;
			*height = 576;
			*shareMemSize = 152*0x1000;
			break;
		case SIZE_1280X720:
			*width = 1280;
			*height = 720;
			*shareMemSize = 338*0x1000;
			break;
		case SIZE_1920X1080:
			*width = 1920;
			*height = 1080;
			*shareMemSize = 760*0x1000;
			break;
		default:
			break;
	}
}

static int mainRun(char* filename, unsigned int w, unsigned int h, unsigned int srcFormat , int sensorId, int shareMemSize)
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


	printf("JpgEncHal test file(width , height) = %s (%d , %d)\n", filename,w, h);

	MHAL_JPEG_ENC_START_IN encStartInParam;

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

	printf("memStride = %d, srcBufferSize = %d, chromaBufferSize = %d\n",memStride,srcBufferSize,chromaBufferSize);

	int ion_client = 0;
	ion_user_handle_t src_hnd = 0;
	int ret;

	ion_client = mt_ion_open("JpgUnitTest");
	if ( ion_client < 0 )
	{
		printf( "mt_ion_open failed!");
		return -1;
	}
	int bufferSize = srcBufferSize + chromaBufferSize;

	ret = ion_alloc(ion_client, bufferSize, 0, ION_HEAP_MULTIMEDIA_MASK, (ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC), &src_hnd);
	if (src_hnd > 0)
	{
		ret = ion_share( ion_client, src_hnd, &src_fd);
		if ( ret != 0 )
		{
			printf( "ion_share( %d ) failed", ion_client);
			if (0 != ion_free(ion_client, src_hnd))
				printf( "ion_free( %d ) failed", ion_client);
			else
				src_hnd = 0;

			close(ion_client);
			ion_client = 0;
			return -2;
		}

		src_va = (unsigned char*)mmap( NULL, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, src_fd, 0);
		if ( MAP_FAILED == src_va || NULL == src_va )
		{
			printf( "ion_map( %d ) failed", ion_client);
			if (0 != ion_free( ion_client, src_hnd ))
				printf( "ion_free( %d ) failed", ion_client);
			else
				src_hnd = 0;

			close(src_fd);
			src_fd = 0;
			close(ion_client);
			ion_client = 0;
			return -3;
		}

		printf("src fd %d , handle: 0x%x, L:%d!!\n", src_fd , src_hnd, __LINE__);
		encStartInParam.srcFD = src_fd;
		encStartInParam.srcChromaFD = src_fd;
	}
	else
	{
		printf( "src ion_alloc failed! ion_client(%d), srcBufferSize(%d)", ion_client, srcBufferSize);
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

		if(sensorId == 0)
		{
			Pfd = open("/sdcard/.cMainImg",O_RDWR,0666);
			stat("/sdcard/.cMainImg", &statbuf);
		}
		else if(sensorId == 1)
		{
			Pfd = open("/sdcard/.cSubImg",O_RDWR,0666);
			stat("/sdcard/.cSubImg", &statbuf);
		}
		if(Pfd <= 0)
		{
			printf("open /sdcard/cImg fail\n");
			mainRet = -5;
		}
		else
		{
			Pmem = (unsigned char*)mmap( NULL, shareMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, Pfd, 0 );
			if(MAP_FAILED == Pmem || Pmem == NULL || statbuf.st_size != shareMemSize)
			{
				printf("bbl mmap fail\n");
				mainRet = -6;
				close(Pfd);
			}
			else
			{
				setCaptureAction((enum CAPTURE_ACTION)(getCaptureAction(sensorId)+1),sensorId);
				*(Pmem + w*h/2*3) = 0;

				unsigned char readFlag = *(Pmem + w*h/2*3);
				while(!readFlag)
				{
					usleep(1000);
					readFlag = *(Pmem + w*h/2*3);

					if(count > 1000*30)
					{
						mainRet = -20;
						break;
					}
					count++;
				}
#if 0
				memcpy(src_va,Pmem,w*h);//Y
				for(index = 0; index < w*h/2; index+=2)
				{
					*(src_va+w*h+index) = *(Pmem+w*h+u_index++);
					*(src_va+w*h+index+1) =  *(Pmem+w*h/4*5+v_index++);
				}
#else
				memcpy(src_va,Pmem,w*h/2*3);
#endif
				chromaAddrVA = src_va + w*h;

				close(Pfd);
				munmap((void*)Pmem, shareMemSize);
			}
		}
	}
	printf("go %d !!\n", __LINE__);

	dst_size = BUFFER_SIZE;
	ion_user_handle_t dst_hnd = 0;

	ret = ion_alloc(ion_client, dst_size, 0, ION_HEAP_MULTIMEDIA_MASK, (ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC), &dst_hnd);
	if (dst_hnd > 0)
	{
		ret = ion_share( ion_client, dst_hnd, &dst_fd);
		if ( ret != 0 )
		{
			printf( "ion_share( %d ) failed", ion_client);
			if (0 != ion_free( ion_client, dst_hnd))
				printf( "ion_free( %d ) failed", ion_client);
			else
				dst_hnd = 0;
			mainRet = -7;
		}

		dst_va = (unsigned char*)mmap( NULL, dst_size, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
		if ( MAP_FAILED == dst_va || NULL == dst_va )
		{
			printf( "ion_map( %d ) failed", ion_client );
			if (0 != ion_free( ion_client, dst_hnd))
				printf( "ion_free( %d ) failed", ion_client);
			else
				dst_hnd = 0;
			mainRet = -8;
		}
		printf("dst fd %d , handle: 0x%x, L:%d!!\n", dst_fd , dst_hnd, __LINE__);
		encStartInParam.dstFD = dst_fd;
	}
	else
	{
		printf( "dst ion_alloc failed! ion_client(%d), dst_size(%d)", ion_client, dst_size);
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
			xlog("encode SUCCESS, size %x~~~\n", enc_size);
			fp = fopen(filename, "w");
			fwrite(dst_va,1,enc_size,fp);
			fflush(fp);
			fclose(fp);
		}
		else
		{
			xlog("encode FAIL, size %x~~~\n", enc_size);
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

int main(int argc, char *argv[])
{
	char saveFile[PROPERTY_VALUE_MAX] = {'\0'};
	unsigned int width = 1280;
	unsigned int height = 720;
	unsigned int shareMemSize = 338*0x1000;
	unsigned int sensorId = 0;
	int ret = 0;
	enum INPUT_SIZE videoSize = SIZE_1280X720;
	enum CAPTURE_ACTION action = CAPTURE_UNKONW;

	if(argc != 2)
		return -1;

	sensorId = atoi(argv[1]);
	if(sensorId != 0 && sensorId != 1)
		return -2;

	getCaptureFileName(saveFile, sensorId);
	setPictureParam(&width, &height, &shareMemSize, sensorId);

	ret = mainRun(saveFile,width,height,NV21,sensorId,shareMemSize);

	if(ret == 0)
	{
		printf("JpgEncHal test done! result => pass\n");
		setCaptureAction((enum CAPTURE_ACTION)(getCaptureAction(sensorId)+1), sensorId);
	}
	else
	{
		printf("JpgEncHal test done! result => fail %d\n",ret);
		setCaptureAction(CAPTURE_FAIL, sensorId);
	}
	return 0;
}
