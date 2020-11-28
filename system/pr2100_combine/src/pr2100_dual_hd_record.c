#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <utils/ThreadDefs.h>
#include <sys/statfs.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <linux/fb.h>

#include "pr2100_combine.h"
#include "wwc2_pr2100_capture.h"
#include "wwc2_pr2100_record.h"
#include "wwc2_pr2100_record_four.h"
#include "wwc2_h264_stream.h"

static pthread_mutex_t recordStatusMutex;
extern struct WWC2_FOUR_RECORD_THREAD *frontThread;
extern struct WWC2_FOUR_RECORD_THREAD *backThread;

static void dual_hd_sem_wait_rt(sem_t *pSem, int64_t reltime)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
		ALOGE("bbl--record error in clock_gettime! Please check");

	ts.tv_sec  += reltime/1000000000;
	ts.tv_nsec += reltime%1000000000;
	if (ts.tv_nsec >= 1000000000)
	{
		ts.tv_nsec -= 1000000000;
		ts.tv_sec += 1;
	}

	int s = sem_timedwait(pSem, &ts);
	if (s == -1)
	{
		if (errno == ETIMEDOUT)
		{
			ALOGD("bbl--record read time out");
		}
		else
		{
			ALOGE("bbl--record [%s]sem_timedwait() errno = %d", __FUNCTION__, errno);
		}
    }
}

static unsigned char *dualHdGetShareMem(char* shareDev, const unsigned int size)
{
	int fd = 0;
	unsigned char *shm;

	unsigned int memSize = (size + 4096 - 1) & 0xFFFFF000;

	fd = open(shareDev, O_CREAT | O_RDWR, 0666);
	if(fd <= 0)
	{
		ALOGE("bbl open shareDev fail");
		return NULL;
	}

	if(ftruncate(fd, memSize) < 0)
	{
		ALOGE("bbl ftruncate fail");
		close(fd);
		return NULL;
	}

	shm = (unsigned char *)mmap(NULL, memSize, (PROT_READ|PROT_WRITE), MAP_SHARED, fd, 0);
	close(fd);
	if(shm == (void *) -1)
	{
		ALOGE("bbl mmap fail  %d, %s", errno, strerror(errno));
		return NULL;
	}

	memset(shm,0,memSize);

	return shm;
}

static int pr2100_dual_hd_read_camera_node(const char* node)
{
	char data[8] = {0};
	int val = -1;
	FILE *fd = NULL;

	fd = fopen(node, "r");
	if(fd)
	{
		fread(data, 8, 1, fd);
		fclose(fd);
		data[strlen(data) - 1] = '\0';
		val = atoi(data);
	}
	return val;
}

static int pr2100_dual_hd_write_camera_node(const char* node, const char* data)
{
	int ret = -1;
	FILE* fd = NULL; 
	fd = fopen(node, "w");

	if(fd)
	{
		fwrite(data, strlen(data), 1, fd);
		fclose(fd);
		ret = 0;
	}

	return ret;
}

static void dual_hd_param_data_get(char *node, char dd[][8])
{
	FILE *fd = NULL;
	char data[256] = {0};
	unsigned int len = 0;
	unsigned int i = 0;
	unsigned int separateCount = 0;
	unsigned int elementCount = 0;

	fd = fopen(node,"r");
	if(fd)
	{
		fread(&data, sizeof(data), 1, fd);
		fclose(fd);
		len = strlen(data);
		data[len - 1] = '\0';
	}
	ALOGD("bbl--param_data_get %s:%s",node, data);

	for(i = 0; i < len; i++)
	{
		if(data[i] == '-')
		{
			dd[separateCount][elementCount] = '\0';
			separateCount++;
			elementCount = 0;
		}
		else
			dd[separateCount][elementCount++] = data[i];
	}
}

static void dual_hd_get_camera_action_type(struct WWC2_CAMERA_ACTION *action)
{
	char dd[2][8] = {{0},{0}};

	dual_hd_param_data_get("/sys/bus/platform/devices/wwc2_camera_combine/camera_action", dd);
	action->mode = atoi(dd[0]);
	action->act = atoi(dd[1]);
}

static void dual_hd_gps_data_get(char gps[32])
{
	FILE *fd = NULL;
	unsigned int len = 0;

	fd = fopen("/sys/devices/platform/wwc2_camera_combine/gps_data","r");
	if(fd)
	{
		fread(gps, 32, 1, fd);
		fclose(fd);
		len = strlen(gps);
		gps[len - 1] = '\0';
	}
	ALOGD("bbl--gps_data_get %s", gps);
}

static bool dual_hd_is_disk_free_size_enough(SAVE_FILE_DIR record_dir)
{
	struct statfs diskInfo;
	unsigned long long size = 0;
	char dir[64] = "/storage/sdcard0";

	switch(record_dir)
	{
		case DIR_LOCAL:
		case DIR_LOCAL_USB0:
		case DIR_LOCAL_USB1:
		case DIR_LOCAL_USB2:
		case DIR_LOCAL_USB3:
		case DIR_LOCAL_TFCARD:
			sprintf(dir, "/storage/%s","sdcard0");
			break;
		case DIR_TFCARD:
			sprintf(dir, "/storage/%s","sdcard1");
			break;
		case DIR_USB0:
			sprintf(dir, "/storage/%s","usbotg");
			break;
		case DIR_USB1:
			sprintf(dir, "/storage/%s","usbotg1");
			break;
		case DIR_USB2:
			sprintf(dir, "/storage/%s","usbotg2");
			break;
		case DIR_USB3:
			sprintf(dir, "/storage/%s","usbotg3");
			break;
		default:
			break;
	}

	if(access(dir, F_OK) != 0)
	{
		ALOGD("bbl--dual_hd_is_disk_free_size_enough %s is not exist",dir);
		return 0;
	}

	statfs(dir, &diskInfo);
	size = (unsigned long long)diskInfo.f_bfree * (unsigned long long)diskInfo.f_bsize;

	ALOGD("bbl--dual_hd_is_disk_free_size_enough size:%lld = %ld x %ld", size, (long)diskInfo.f_bfree, (long)diskInfo.f_bsize);

	if(size < 1073741824)// 1024*1024*1024
		return 0;

	return 1;
}

static void dualHdSigioHandler(int signum)
{
	enum WWC2_CAMERA_MODE mode = WWC2_UNKNOW;
	struct WWC2_CAMERA_ACTION action = {.mode = 0xf, .act = 0xf};
	struct PR2100_RECORD * obj = pr2100Obj;
	RECORD_STATUS status = UNKNOW_RECORD_STATUS;
	int i = 0;
	char dd[10][8] = {{0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
	unsigned char *h264YuvWriteFlag = NULL;

	(void)signum;
	if(obj == NULL)
		return;

	dual_hd_get_camera_action_type(&action);
	ALOGD("bbl--dualHdSigioHandler action.mode = %d, action.act = %d", action.mode, action.act);

	mode = (enum WWC2_CAMERA_MODE)action.mode;
	switch(mode)
	{
		case WWC2_DISPLAY:
			if(action.act < UNKNOW_DISPLAY)
				obj->displayMode = (DISPLAY_MODE)action.act;
			else if(action.act == UNKNOW_DISPLAY)
			{
				obj->displayMode == UNKNOW_DISPLAY;
				property_set("wwc2.camera.running", "1");
			}
			break;
		case WWC2_RECORD:
			if(action.act == FRONT_RECORD || action.act == BACK_RECORD || action.act == DUAL_RECORD)
			{
				obj->recordMode= (RECORD_MODE)action.act;
				if(obj->record == NULL)
				{
					if(dual_hd_is_disk_free_size_enough(obj->record_dir) == 1)
					{
						obj->recordStopFlag = 0;
						obj->statusReportFlag = 1;
						sem_post(&obj->recordThread.sem);
					}
				}
			}
			else if(action.act == TWO_RECORD)
			{
				obj->recordMode = TWO_RECORD;
				if(frontThread->data == NULL && backThread->data == NULL)
				{
					if(dual_hd_is_disk_free_size_enough(obj->record_dir) == 1)
					{
						frontThread->recordStopFlag = 0;
						frontThread->statusReportFlag = 1;
						sem_post(&frontThread->pThread.sem);
						backThread->recordStopFlag = 0;
						backThread->statusReportFlag = 1;
						sem_post(&backThread->pThread.sem);
					}
				}
			}
			else if(action.act == STOP_RECORD)
			{
				if(obj->record)
				{
					status = (RECORD_STATUS)pr2100_dual_hd_read_camera_node("/sys/bus/platform/devices/wwc2_camera_combine/record_status");
					if(status == RUNNING_RECORD_STATUS)
					{
						obj->recordStopFlag = 1;
						obj->statusReportFlag = 1;
						sem_post(&recordStopSem);
					}
				}

				if(frontThread->data && backThread->data)
				{
					status = (RECORD_STATUS)pr2100_dual_hd_read_camera_node("/sys/bus/platform/devices/wwc2_camera_combine/record_four_status");
					if(status == RUNNING_RECORD_STATUS)
					{
						frontThread->recordStopFlag = 1;
						frontThread->statusReportFlag = 1;
						sem_post(&frontThread->sem.recordStopSem);

						backThread->recordStopFlag = 1;
						backThread->statusReportFlag = 1;
						sem_post(&backThread->sem.recordStopSem);
					}
				}
			}
			break;
		case WWC2_CAPTURE:
			if(action.act == FRONT_CAPTURE || action.act == BACK_CAPTURE || action.act == DUAL_CAPTURE || action.act == TWO_CAPTURE)
			{
				obj->captureMode= (CAPTURE_MODE)action.act;
				if(obj->capture == NULL)
					sem_post(&obj->captureThread.sem);
			}
			break;
		case WWC2_H264:
			if(action.act == FRONT_H264 || action.act == BACK_H264 || action.act == DUAL_H264)
			{
				obj->h264Mode= (H264_MODE)action.act;
				if(obj->h264Yuv == NULL)
				{
					sem_post(&obj->h264YuvThread.sem);
				}
			}
			else if(action.act == STOP_H264)
			{
				if(obj->h264Yuv != NULL)
				{
					h264YuvWriteFlag = obj->h264Yuv + FRAME_H264_SIZE;
					i = 0;
					while(1)
					{
						if(*h264YuvWriteFlag == 1)
						{
							munmap(obj->h264Yuv, obj->h264YuvSize);
							obj->h264Yuv = NULL;

							if(!access("/sdcard/.h264Img", F_OK))
								remove("/sdcard/.h264Img");

							if(obj->h264YuvFd > 0)
							{
								close(obj->h264YuvFd);
								obj->h264YuvFd = -1;
							}
							break;
						}

						if(i == 20)
							break;
						usleep(40000);
						i++;
					}
					ALOGD("bbl--h264 stop cost %d ms", i*40);
				}
			}
			break;
		case WWC2_CHANNELWATERMARK:
			obj->channelWaterMark = action.act;
			break;
		case WWC2_TIMEWATERMARK:
			obj->timeWaterMark = action.act;
			break;
		case WWC2_GPSWATERMARK:
			obj->gpsWaterMark = action.act;
			dual_hd_gps_data_get(obj->gpsData);
			break;
		case WWC2_CARDWATERMARK:
			obj->cardWaterMark = action.act;
			dual_hd_param_data_get("/sys/devices/platform/wwc2_camera_combine/card_data", dd);
			for(i = 0; i < 10; i++)
				obj->cardData[i] = atoi(dd[i]);

			break;
		case WWC2_AUDIOENABLE:
			obj->audioEnable = action.act;
			break;
		case WWC2_RECORD_TIMEOUT:
			if(obj->record)
			{
				if(dual_hd_is_disk_free_size_enough(obj->record_dir) == 1)
					obj->recordStopFlag = 0;
				else
					obj->recordStopFlag = 1;

				obj->statusReportFlag = 0;
				sem_post(&recordStopSem);
			}

			if(frontThread->data && backThread->data)
			{
				if(dual_hd_is_disk_free_size_enough(obj->record_dir) == 1)
				{
					frontThread->recordStopFlag = 0;
					backThread->recordStopFlag = 0;
				}
				else
				{
					frontThread->recordStopFlag = 1;
					backThread->recordStopFlag = 1;
				}

				frontThread->statusReportFlag = 0;
				backThread->statusReportFlag = 0;
				sem_post(&frontThread->sem.recordStopSem);
				sem_post(&backThread->sem.recordStopSem);
			}
			break;
		case WWC2_CH0_FLIP:
			obj->ch0Filp = action.act;
			pr2100_flip_set(0, obj->ch0Filp);
			break;
		case WWC2_CH1_FLIP:
			obj->ch1Filp = action.act;
			pr2100_flip_set(1, obj->ch1Filp);
			break;
		case WWC2_RECORD_BPS:
			obj->record_bps = action.act;
			break;
		case WWC2_RECORD_DIR:
			obj->record_dir = (SAVE_FILE_DIR)action.act;
			break;
		case WWC2_CAPTURE_DIR:
			obj->capture_dir = (SAVE_FILE_DIR)action.act;
			break;
		case WWC2_CHANNEL_ORDER:
			obj->chOrder = (CH_ORDER)action.act;
			break;
		default:
			break;
	}

	ioctl(signalFd, WWC2_SIGIO_ACK, NULL);
}
static void dual_hd_sigio_init(void)
{
	int flags = 0;
	char data[2] = {'0', '\0'};

	if(SIG_ERR == signal(SIGIO, dualHdSigioHandler))
	{
		ALOGE("setupSignal fail");
		return;
	}

	signalFd = open("/dev/wwc2_camera_combine", O_RDWR);
	if(signalFd <= 0)
	{
		ALOGE("setupSignal open '/dev/wwc2_camera_combine' fail");
		return;
	}

	fcntl(signalFd, F_SETOWN, getpid());
	flags = fcntl(signalFd, F_GETFL);
	fcntl(signalFd, F_SETFL, flags | O_ASYNC);

	pr2100_dual_hd_write_camera_node("/sys/devices/platform/wwc2_camera_combine/record_latency", data);
}

static void dual_hd_sigio_uninit(void)
{
	if(signalFd > 0)
	{
		close(signalFd);
		signalFd = 0;
	}
}


static void dual_hd_init_camera_param(struct PR2100_RECORD *param)
{
	int i = 0;
	char dd[22][8] = {{0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
	int avmEnable = property_get_int32("wwc2.avm360.enable", 0);
	int fd = 0;
	struct fb_var_screeninfo screen_info;

	dual_hd_param_data_get("/sys/devices/platform/wwc2_camera_combine/camera_param", dd);

	param->displayMode = (DISPLAY_MODE)atoi(dd[0]);
	param->recordMode = (RECORD_MODE)atoi(dd[1]);
	param->captureMode = (CAPTURE_MODE)atoi(dd[2]);
	param->h264Mode = (H264_MODE)atoi(dd[3]);

	param->channelWaterMark = atoi(dd[8]);
	param->timeWaterMark = atoi(dd[9]);
	param->gpsWaterMark = atoi(dd[10]);
	param->cardWaterMark = atoi(dd[11]);
	param->audioEnable = atoi(dd[12]);

	param->ch0Filp = atoi(dd[14]);
	param->ch1Filp = atoi(dd[15]);

	param->record_bps = atoi(dd[18]);
	param->record_dir = (SAVE_FILE_DIR)atoi(dd[19]);
	param->capture_dir = (SAVE_FILE_DIR)atoi(dd[20]);
	param->chOrder = (CH_ORDER)atoi(dd[21]);

	ALOGD("bbl--init_camera_param1:displayMode = %d recordMode = %d captureMode = %d h264Mode = %d",
		(int)param->displayMode, (int)param->recordMode, (int)param->captureMode, (int)param->h264Mode);

	ALOGD("bbl--init_camera_param2:channelWaterMark = %d timeWaterMark = %d gpsWaterMark = %d cardWaterMark = %d audioEnable = %d",
		param->channelWaterMark, param->timeWaterMark, param->gpsWaterMark, param->cardWaterMark, param->audioEnable);

	ALOGD("bbl--init_camera_param3:ch0Filp = %d ch1Filp = %d ch2Filp = %d ch3Filp = %d",
		param->ch0Filp, param->ch1Filp, param->ch2Filp, param->ch3Filp);

	ALOGD("bbl--init_camera_param4:record_bps = %d record_dir = %d capture_dir = %d, chOrder = %d",
		param->record_bps, param->record_dir, param->capture_dir, (int)param->chOrder);

	dual_hd_param_data_get("/sys/devices/platform/wwc2_camera_combine/card_data", dd);

	for(i = 0; i < 10; i++)
		param->cardData[i] = atoi(dd[i]);

	dual_hd_gps_data_get(param->gpsData);

	param->capture = NULL;
	param->record = NULL;
	param->h264Yuv = NULL;

	param->h264YuvSize = (FRAME_H264_SIZE + 4096 - 1) & 0xFFFFF000;
	param->displaySize = DUAL_HD_FRAME_YUV420_SIZE;
	if(avmEnable)
		param->display = NULL;
	else
	{
		param->display = (unsigned char*)malloc(param->displaySize);
		memset(param->display, 0x80, param->displaySize);
	}

	pthread_mutex_init(&param->displayMutex, NULL);

	pr2100_flip_set(0, param->ch0Filp);
	pr2100_flip_set(1, param->ch1Filp);

	param->cameraFormat = CH0HD_CH1HD;

	fd = open("/dev/graphics/fb0", O_RDWR);
	if(fd > 0)
	{
		ioctl(fd, FBIOGET_VSCREENINFO, &screen_info);
		ALOGD("bbl--screen %dx%d", screen_info.xres, screen_info.yres);

		if(screen_info.yres * 2 > screen_info.xres)
			param->displayType = 1;
		else
			param->displayType = 0;

		close(fd);
	}
	else
		param->displayType = 1;
}

static void* dualHdCaptureThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct PR2100_RECORD *obj = (struct PR2100_RECORD *)arg;
	struct PR2100_THREAD *captureThread = NULL;

	prctl(PR_SET_NAME,"wwc2Capture", 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	if(obj == NULL)
		return NULL;

	ALOGD("bbl--captureThreadFunc in");
	captureThread = &obj->captureThread;

	while(1)
	{
		sem_wait(&captureThread->sem);
		if(captureThread->threadLoopFlag== false)
			break;
		//do capture
		wwc2_capture((void*)obj);
	}
	ALOGD("bbl--captureThreadFunc exit");
	return NULL;
}

static void* dualHdRecordThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct PR2100_RECORD *obj = (struct PR2100_RECORD *)arg;
	struct PR2100_THREAD *recordThread = NULL;

	prctl(PR_SET_NAME,"wwc2Record", 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	if(obj == NULL)
		return NULL;

	ALOGD("bbl--dualHdRecordThreadFunc in");
	recordThread = &obj->recordThread;
	while(1)
	{
		sem_wait(&recordThread->sem);
		if(recordThread->threadLoopFlag== false)
			break;
		//do record

		if(obj->record == NULL)
		{
			obj->record = (unsigned char*)malloc(DUAL_HD_FRAME_YUV420_SIZE);
			memset(obj->record, 0x80, DUAL_HD_FRAME_YUV420_SIZE);
		}

		while(obj->recordStopFlag == 0)
		{
			wwc2_record((void*)obj);
		}

		if(obj->record != NULL && obj->recordStopFlag == 1)
		{
			dual_hd_sem_wait_rt(&recordReadSem, 200000000L);
			free(obj->record);
			obj->record = NULL;
		}
	}

	if(obj->record != NULL)
	{
		free(obj->record);
		obj->record = NULL;
	}
	ALOGD("bbl--dualHdRecordThreadFunc exit");
	return NULL;
}

static void* dualHdH264YuvThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct PR2100_RECORD *obj = (struct PR2100_RECORD *)arg;
	struct PR2100_THREAD *h264YuvThread = NULL;
	H264_MODE mode = DISABLE_H264;

	prctl(PR_SET_NAME,"wwc2H264Yuv", 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	if(obj == NULL)
		return NULL;

	ALOGD("bbl--dualHdH264YuvThreadFunc in");
	h264YuvThread = &obj->h264YuvThread;
	while(1)
	{
		sem_wait(&h264YuvThread->sem);
		if(h264YuvThread->threadLoopFlag== false)
			break;

		if(obj->h264YuvFd < 0)
			obj->h264YuvFd = open("/dev/wwc2_yuv_sync", O_RDWR);

		//do h264Yuv
		if(obj->h264Yuv == NULL)
		{
			mode = obj->h264Mode;
			obj->h264Mode = STOP_H264;
			obj->h264Yuv = dualHdGetShareMem("/sdcard/.h264Img", obj->h264YuvSize);
			obj->h264Mode = mode;
		}
	}

	if(obj->h264Yuv != NULL)
	{
		munmap(obj->h264Yuv, obj->h264YuvSize);
		obj->h264Yuv = NULL;
	}
	if(!access("/sdcard/.h264Img", F_OK))
		remove("/sdcard/.h264Img");

	if(obj->h264YuvFd > 0)
	{
		close(obj->h264YuvFd);
		obj->h264YuvFd = -1;
	}

	ALOGD("bbl--dualHdH264YuvThreadFunc exit");
	return NULL;
}

static void* dualHdTwoRecordThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct WWC2_FOUR_RECORD_THREAD* obj = (struct WWC2_FOUR_RECORD_THREAD*)arg;
	struct PR2100_THREAD* recordThread = &obj->pThread;
	
	prctl(PR_SET_NAME,obj->threadName, 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	ALOGD("bbl--dualHdTwoRecordThreadFunc in mode:%d", obj->mode);

	while(1)
	{
		sem_wait(&recordThread->sem);
		if(recordThread->threadLoopFlag == false)
			break;
		if(obj->data == NULL)
		{
			obj->data = (unsigned char*)malloc(DUAL_HD_FRAME_YUV420_SIZE);
			memset(obj->data, 0x80, DUAL_HD_FRAME_YUV420_SIZE);
		}

		while(obj->recordStopFlag == 0)
		{
			wwc2_record_four((void*)obj, &recordStatusMutex, (void*)pr2100Obj);
		}

		if(obj->data != NULL && obj->recordStopFlag == 1)
		{
			dual_hd_sem_wait_rt(&obj->sem.recordReadSem, 200000000L);
			free(obj->data);
			obj->data = NULL;
		}
	}

	if(obj->data != NULL)
	{
		free(obj->data);
		obj->data = NULL;
	}

	ALOGD("bbl--dualHdTwoRecordThreadFunc exit mode:%d",obj->mode);
	return NULL;
}

static struct WWC2_FOUR_RECORD_THREAD* pr2100_dual_hd_two_record_init(RECORD_MODE mode)
{
	pthread_attr_t const attr = {0, NULL, 1024 * 1024, 4096, SCHED_OTHER, ANDROID_PRIORITY_FOREGROUND,
#ifdef __LP64__
            .__reserved = {0}
#endif
		};

	struct WWC2_FOUR_RECORD_THREAD * recordThread = (struct WWC2_FOUR_RECORD_THREAD *)malloc(sizeof(struct WWC2_FOUR_RECORD_THREAD));
	if(recordThread == NULL)
	{
		return NULL;
	}
	memset(recordThread, 0 , sizeof(struct WWC2_FOUR_RECORD_THREAD));
	recordThread->mode = mode;
	sem_init(&recordThread->sem.recordWriteSem, 0, 0);
	sem_init(&recordThread->sem.recordReadSem, 0, 0);
	sem_init(&recordThread->sem.recordStopSem, 0, 0);

	sem_init(&recordThread->pThread.sem, 0, 0);
	recordThread->pThread.threadLoopFlag = true;
	recordThread->recordStopFlag = 1;
	switch(mode)
	{
		case FRONT_RECORD:
			recordThread->threadName = "wwc2RecordFront";
			break;
		case BACK_RECORD:
			recordThread->threadName = "wwc2RecordBack";
			break;
		default:
			break;
	}
	pthread_create(&recordThread->pThread.thread, &attr, dualHdTwoRecordThreadFunc, (void *)recordThread);

	return recordThread;
}

void pr2100_dual_hd_record_init(void)
{
	struct PR2100_RECORD *pr2100Data = NULL;
	pthread_attr_t const attr = {0, NULL, 1024 * 1024, 4096, SCHED_OTHER, ANDROID_PRIORITY_FOREGROUND,
#ifdef __LP64__
            .__reserved = {0}
#endif
		};
	int recordEnable = property_get_int32("wwc2_camera_record_enable", 1);

	if(recordEnable)
	{
		pr2100Data = (struct PR2100_RECORD *)malloc(sizeof(struct PR2100_RECORD));
		if(pr2100Data == NULL)
		{
			pr2100Obj = NULL;
			return;
		}
		memset(pr2100Data, 0 , sizeof(struct PR2100_RECORD));
		dual_hd_init_camera_param(pr2100Data);
		pr2100Obj = pr2100Data;

		sem_init(&pr2100Data->captureThread.sem, 0, 0);
		pr2100Data->captureThread.threadLoopFlag= true;
		pthread_create(&pr2100Data->captureThread.thread, &attr, dualHdCaptureThreadFunc, (void *)pr2100Obj);
		sem_init(&captureWriteSem, 0, 0);
		sem_init(&captureReadSem, 0, 0);

		sem_init(&pr2100Data->recordThread.sem, 0, 0);
		pr2100Data->recordStopFlag = 1;
		pr2100Data->statusReportFlag = 1;
		pr2100Data->recordThread.threadLoopFlag = true;
		pthread_create(&pr2100Data->recordThread.thread, &attr, dualHdRecordThreadFunc, (void *)pr2100Obj);
		sem_init(&recordWriteSem, 0, 0);
		sem_init(&recordReadSem, 0, 0);
		sem_init(&recordStopSem, 0, 0);

		sem_init(&pr2100Data->h264YuvThread.sem, 0, 0);
		pr2100Data->h264YuvFd = -1;
		pr2100Data->h264YuvThread.threadLoopFlag = true;
		pthread_create(&pr2100Data->h264YuvThread.thread, &attr, dualHdH264YuvThreadFunc, (void *)pr2100Obj);

		frontThread = pr2100_dual_hd_two_record_init(FRONT_RECORD);
		backThread = pr2100_dual_hd_two_record_init(BACK_RECORD);
		pthread_mutex_init(&recordStatusMutex, NULL);
	}
	dual_hd_sigio_init();
}

static void pr2100_dual_hd_two_record_uninit(struct WWC2_FOUR_RECORD_THREAD *thread)
{
	struct PR2100_THREAD *th = &thread->pThread;
	struct WWC2_RECORD_SEM *sem = &thread->sem;

	th->threadLoopFlag = false;
	sem_post(&th->sem);
	thread->recordStopFlag = 1;
	sem_post(&sem->recordStopSem);
	if(th->thread)
		pthread_join(th->thread,NULL);
	th->thread = 0;
	sem_destroy(&th->sem);

	sem_destroy(&sem->recordWriteSem);
	sem_destroy(&sem->recordReadSem);
	sem_destroy(&sem->recordStopSem);

	if(thread->data)
	{
		free(thread->data);
		thread->data = NULL;
	}

	free(thread);
	thread = NULL;
}

void pr2100_dual_hd_record_uninit(void)
{
	struct PR2100_RECORD * obj = pr2100Obj;

	if(obj != NULL)
	{
		obj->captureThread.threadLoopFlag = false;
		sem_post(&obj->captureThread.sem);
		if(obj->captureThread.thread)
			pthread_join(obj->captureThread.thread,NULL);
		obj->captureThread.thread = 0;
		sem_destroy(&obj->captureThread.sem);
		sem_destroy(&captureWriteSem);
		sem_destroy(&captureReadSem);

		obj->recordThread.threadLoopFlag = false;
		sem_post(&obj->recordThread.sem);
		obj->recordStopFlag = 1;
		sem_post(&recordStopSem);
		if(obj->recordThread.thread)
			pthread_join(obj->recordThread.thread,NULL);
		obj->recordThread.thread = 0;
		sem_destroy(&obj->recordThread.sem);
		sem_destroy(&recordWriteSem);
		sem_destroy(&recordReadSem);
		sem_destroy(&recordStopSem);

		obj->h264YuvThread.threadLoopFlag = false;
		sem_post(&obj->h264YuvThread.sem);
		if(obj->h264YuvThread.thread)
			pthread_join(obj->h264YuvThread.thread, NULL);
		obj->h264YuvThread.thread = 0;
		sem_destroy(&obj->h264YuvThread.sem);

		pr2100_dual_hd_two_record_uninit(frontThread);
		pr2100_dual_hd_two_record_uninit(backThread);
		pthread_mutex_destroy(&recordStatusMutex);

		if(obj->record)
		{
			free(obj->record);
			obj->record = NULL;
		}

		if(obj->display)
		{
			free(obj->display);
			obj->display = NULL;
		}

		pthread_mutex_destroy(&obj->displayMutex);

		free(obj);
		pr2100Obj = NULL;
	}

	dual_hd_sigio_uninit();
}

#ifdef __cplusplus
}
#endif
