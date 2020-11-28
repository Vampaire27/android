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


#include "pr2100_combine.h"
#include "wwc2_pr2100_capture.h"
#include "wwc2_pr2100_record.h"
#include "wwc2_pr2100_record_four.h"
#include "wwc2_h264_stream.h"


struct PR2100_RECORD *pr2100Obj = NULL;

int signalFd = 0;

static pthread_mutex_t recordStatusMutex;
struct WWC2_FOUR_RECORD_THREAD *frontThread = NULL;
struct WWC2_FOUR_RECORD_THREAD *backThread = NULL;
struct WWC2_FOUR_RECORD_THREAD *leftThread = NULL;
struct WWC2_FOUR_RECORD_THREAD *rightThread = NULL;

struct WWC2_H264_STREAM_THREAD *frontH264Stream = NULL;
struct WWC2_H264_STREAM_THREAD *backH264Stream = NULL;
struct WWC2_H264_STREAM_THREAD *leftH264Stream = NULL;
struct WWC2_H264_STREAM_THREAD *rightH264Stream = NULL;

struct WWC2_PR2100_AVM_DATA *avmData = NULL;


static void sem_wait_rt(sem_t *pSem, int64_t reltime)
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

static void* fourRecordThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct WWC2_FOUR_RECORD_THREAD* obj = (struct WWC2_FOUR_RECORD_THREAD*)arg;
	struct PR2100_THREAD* recordThread = &obj->pThread;
	
	prctl(PR_SET_NAME,obj->threadName, 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	ALOGD("bbl--recordThreadFunc in mode:%d", obj->mode);

	while(1)
	{
		sem_wait(&recordThread->sem);
		if(recordThread->threadLoopFlag == false)
			break;
		if(obj->data == NULL)
		{
			obj->data = (unsigned char*)malloc(FRAME_YUV420_SIZE);
			memset(obj->data, 0x80, FRAME_YUV420_SIZE);
		}
		while(obj->recordStopFlag == 0)
		{
			wwc2_record_four((void*)obj, &recordStatusMutex, (void*)pr2100Obj);
		}

		if(obj->data != NULL && obj->recordStopFlag == 1)
		{
			sem_wait_rt(&obj->sem.recordReadSem, 200000000L);
			free(obj->data);
			obj->data = NULL;
		}
	}

	if(obj->data != NULL)
	{
		free(obj->data);
		obj->data = NULL;
	}

	ALOGD("bbl--recordThreadFunc exit mode:%d",obj->mode);
	return NULL;
}

static struct WWC2_FOUR_RECORD_THREAD* pr2100_four_record_init(RECORD_MODE mode)
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
		case LEFT_RECORD:
			recordThread->threadName = "wwc2RecordLeft";
			break;
		case RIGHT_RECORD:
			recordThread->threadName = "wwc2RecordRight";
			break;
		default:
			break;
	}
	pthread_create(&recordThread->pThread.thread, &attr, fourRecordThreadFunc, (void *)recordThread);

	return recordThread;
}

static void pr2100_four_record_uninit(struct WWC2_FOUR_RECORD_THREAD *thread)
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


DISPLAY_MODE get_camera_display_mode(void)
{
	if(pr2100Obj == NULL)
		return UNKNOW_DISPLAY;

	if(pr2100Obj->display == NULL)
		return DISABLE_DISPLAY;

	return pr2100Obj->displayMode;
}

RECORD_MODE get_camera_record_mode(void)
{
	if(pr2100Obj == NULL)
		return QUART_RECORD;

	return pr2100Obj->recordMode;
}

CAPTURE_MODE get_camera_capture_mode(void)
{
	if(pr2100Obj == NULL)
		return FOUR_CAPTURE;

	return pr2100Obj->captureMode;
}

H264_MODE get_camera_h264_mode(void)
{
	if(pr2100Obj == NULL)
		return QUART_H264;

	return pr2100Obj->h264Mode;
}

static int pr2100_read_camera_node(const char* node)
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

static int pr2100_write_camera_node(const char* node, const char* data)
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

static unsigned char *getShareMem(char* shareDev, const unsigned int size)
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

static void param_data_get(char *node, char dd[][8])
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

static void gps_data_get(char gps[32])
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

static void init_camera_param(struct PR2100_RECORD *param)
{
	int i = 0;
	char dd[22][8] = {{0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
	int avmEnable = property_get_int32("wwc2.avm360.enable", 0);

	param_data_get("/sys/devices/platform/wwc2_camera_combine/camera_param", dd);

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
	param->ch2Filp = atoi(dd[16]);
	param->ch3Filp = atoi(dd[17]);

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

	param_data_get("/sys/devices/platform/wwc2_camera_combine/card_data", dd);

	for(i = 0; i < 10; i++)
		param->cardData[i] = atoi(dd[i]);

	gps_data_get(param->gpsData);

	param->capture = NULL;
	param->record = NULL;
	param->h264Yuv = NULL;

	param->h264YuvSize = (FRAME_H264_SIZE + 4096 - 1) & 0xFFFFF000;
	param->displaySize = FRAME_YUV420_SIZE;
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
	pr2100_flip_set(2, param->ch2Filp);
	pr2100_flip_set(3, param->ch3Filp);

	param->cameraFormat = FOUR_HD;
	param->displayType = 0;
}

static void get_camera_action_type(struct WWC2_CAMERA_ACTION *action)
{
	char dd[2][8] = {{0},{0}};

	param_data_get("/sys/bus/platform/devices/wwc2_camera_combine/camera_action", dd);
	action->mode = atoi(dd[0]);
	action->act = atoi(dd[1]);
}

static bool is_disk_free_size_enough(SAVE_FILE_DIR record_dir)
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
		ALOGD("bbl--is_disk_free_size_enough %s is not exist",dir);
		return 0;
	}

	statfs(dir, &diskInfo);
	size = (unsigned long long)diskInfo.f_bfree * (unsigned long long)diskInfo.f_bsize;

	ALOGD("bbl--is_disk_free_size_enough size:%lld = %ld x %ld", size, (long)diskInfo.f_bfree, (long)diskInfo.f_bsize);

	if(size < 1073741824)// 1024*1024*1024
		return 0;

	return 1;
}

static void sigioHandler(int signum)
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

	get_camera_action_type(&action);
	ALOGD("bbl--sigioHandler action.mode = %d, action.act = %d", action.mode, action.act);

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
			if(action.act < FOUR_RECORD)
			{
				obj->recordMode= (RECORD_MODE)action.act;
				if(obj->record == NULL)
				{
					if(is_disk_free_size_enough(obj->record_dir) == 1)
					{
						obj->recordStopFlag = 0;
						obj->statusReportFlag = 1;
						sem_post(&obj->recordThread.sem);
					}
				}
			}
			else if(action.act == FOUR_RECORD)
			{
				obj->recordMode = FOUR_RECORD;
				if(frontThread->data == NULL && backThread->data == NULL && leftThread->data == NULL && rightThread->data == NULL)
				{
					if(is_disk_free_size_enough(obj->record_dir) == 1)
					{
						frontThread->recordStopFlag = 0;
						frontThread->statusReportFlag = 1;
						sem_post(&frontThread->pThread.sem);
						backThread->recordStopFlag = 0;
						backThread->statusReportFlag = 1;
						sem_post(&backThread->pThread.sem);
						leftThread->recordStopFlag = 0;
						leftThread->statusReportFlag = 1;
						sem_post(&leftThread->pThread.sem);
						rightThread->recordStopFlag = 0;
						rightThread->statusReportFlag = 1;
						sem_post(&rightThread->pThread.sem);
					}
				}
			}
			else if(action.act == STOP_RECORD)
			{
				if(obj->record)
				{
					status = (RECORD_STATUS)pr2100_read_camera_node("/sys/bus/platform/devices/wwc2_camera_combine/record_status");
					if(status == RUNNING_RECORD_STATUS)
					{
						obj->recordStopFlag = 1;
						obj->statusReportFlag = 1;
						sem_post(&recordStopSem);
					}
				}

				if(frontThread->data && backThread->data && leftThread->data && rightThread->data)
				{
					status = (RECORD_STATUS)pr2100_read_camera_node("/sys/bus/platform/devices/wwc2_camera_combine/record_four_status");
					if(status == RUNNING_RECORD_STATUS)
					{
						frontThread->recordStopFlag = 1;
						frontThread->statusReportFlag = 1;
						sem_post(&frontThread->sem.recordStopSem);

						backThread->recordStopFlag = 1;
						backThread->statusReportFlag = 1;
						sem_post(&backThread->sem.recordStopSem);

						leftThread->recordStopFlag = 1;
						leftThread->statusReportFlag = 1;
						sem_post(&leftThread->sem.recordStopSem);

						rightThread->recordStopFlag = 1;
						rightThread->statusReportFlag = 1;
						sem_post(&rightThread->sem.recordStopSem);
					}
				}
			}
			break;
		case WWC2_CAPTURE:
			if(action.act <= FOUR_CAPTURE)
			{
				obj->captureMode= (CAPTURE_MODE)action.act;
				if(obj->capture == NULL)
					sem_post(&obj->captureThread.sem);
			}
			break;
		case WWC2_H264:
			switch((H264_MODE)action.act)
			{
				case FRONT_H264:
				case BACK_H264:
				case LEFT_H264:
				case RIGHT_H264:
				case QUART_H264:
					obj->h264Mode = (H264_MODE)action.act;
					if(obj->h264Yuv == NULL)
						sem_post(&obj->h264YuvThread.sem);
					break;
				case STOP_H264:
					obj->h264Mode = STOP_H264;
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
					break;
				case FRONT_H264STREAM:
					if(frontH264Stream->yuvData == NULL)
						sem_post(&frontH264Stream->threadSem);
					break;
				case BACK_H264STREAM:
					if(backH264Stream->yuvData == NULL)
						sem_post(&backH264Stream->threadSem);
					break;
				case LEFT_H264STREAM:
					if(leftH264Stream->yuvData == NULL)
						sem_post(&leftH264Stream->threadSem);
					break;
				case RIGHT_H264STREAM:
					if(rightH264Stream->yuvData == NULL)
						sem_post(&rightH264Stream->threadSem);
					break;
				case STOP_FRONT_H264STREAM:
					if(frontH264Stream->yuvData != NULL)
						sem_post(&frontH264Stream->stopSem);
					break;
				case STOP_BACK_H264STREAM:
					if(backH264Stream->yuvData != NULL)
						sem_post(&backH264Stream->stopSem);
					break;
				case STOP_LEFT_H264STREAM:
					if(leftH264Stream->yuvData != NULL)
						sem_post(&leftH264Stream->stopSem);
					break;
				case STOP_RIGHT_H264STREAM:
					if(rightH264Stream->yuvData != NULL)
						sem_post(&rightH264Stream->stopSem);
					break;
				default:
					break;
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
			gps_data_get(obj->gpsData);
			break;
		case WWC2_CARDWATERMARK:
			obj->cardWaterMark = action.act;
			param_data_get("/sys/devices/platform/wwc2_camera_combine/card_data", dd);
			for(i = 0; i < 10; i++)
				obj->cardData[i] = atoi(dd[i]);

			break;
		case WWC2_AUDIOENABLE:
			obj->audioEnable = action.act;
			break;
		case WWC2_RECORD_TIMEOUT:
			if(obj->record)
			{
				if(is_disk_free_size_enough(obj->record_dir) == 1)
					obj->recordStopFlag = 0;
				else
					obj->recordStopFlag = 1;

				obj->statusReportFlag = 0;
				sem_post(&recordStopSem);
			}

			if(frontThread->data && backThread->data && leftThread->data && rightThread->data)
			{
				if(is_disk_free_size_enough(obj->record_dir) == 1)
				{
					frontThread->recordStopFlag = 0;
					backThread->recordStopFlag = 0;
					leftThread->recordStopFlag = 0;
					rightThread->recordStopFlag = 0;
				}
				else
				{
					frontThread->recordStopFlag = 1;
					backThread->recordStopFlag = 1;
					leftThread->recordStopFlag = 1;
					rightThread->recordStopFlag = 1;
				}

				frontThread->statusReportFlag = 0;
				backThread->statusReportFlag = 0;
				leftThread->statusReportFlag = 0;
				rightThread->statusReportFlag = 0;
				sem_post(&frontThread->sem.recordStopSem);
				sem_post(&backThread->sem.recordStopSem);
				sem_post(&leftThread->sem.recordStopSem);
				sem_post(&rightThread->sem.recordStopSem);
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
		case WWC2_CH2_FLIP:
			obj->ch2Filp = action.act;
			pr2100_flip_set(2, obj->ch2Filp);
			break;
		case WWC2_CH3_FLIP:
			obj->ch3Filp = action.act;
			pr2100_flip_set(3, obj->ch3Filp);
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

static void sigio_init(void)
{
	int flags = 0;
	char data[2] = {'0', '\0'};

	if(SIG_ERR == signal(SIGIO, sigioHandler))
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

	pr2100_write_camera_node("/sys/devices/platform/wwc2_camera_combine/record_latency", data);
}

static void sigio_uninit(void)
{
	if(signalFd > 0)
	{
		close(signalFd);
		signalFd = 0;
	}
}

static void* captureThreadFunc(void* arg)
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

static void* recordThreadFunc(void* arg)
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

	ALOGD("bbl--recordThreadFunc in");
	recordThread = &obj->recordThread;
	while(1)
	{
		sem_wait(&recordThread->sem);
		if(recordThread->threadLoopFlag== false)
			break;
		//do record

		if(obj->record == NULL)
		{
			obj->record = (unsigned char*)malloc(FRAME_YUV420_SIZE);
			memset(obj->record, 0x80, FRAME_YUV420_SIZE);
		}

		while(obj->recordStopFlag == 0)
		{
			wwc2_record((void*)obj);
		}

		if(obj->record != NULL && obj->recordStopFlag == 1)
		{
			sem_wait_rt(&recordReadSem, 200000000L);
			free(obj->record);
			obj->record = NULL;
		}
	}

	if(obj->record != NULL)
	{
		free(obj->record);
		obj->record = NULL;
	}
	ALOGD("bbl--recordThreadFunc exit");
	return NULL;
}

static void* h264YuvThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct PR2100_RECORD *obj = (struct PR2100_RECORD *)arg;
	struct PR2100_THREAD *h264YuvThread = NULL;
	H264_MODE mode = DISABLE_H264;
	unsigned char *h264YuvWriteFlag = NULL;
	int count = 0;

	prctl(PR_SET_NAME,"wwc2H264Yuv", 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	if(obj == NULL)
		return NULL;

	ALOGD("bbl--h264YuvThreadFunc in");
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
			obj->h264Yuv = getShareMem("/sdcard/.h264Img", obj->h264YuvSize);
			obj->h264Mode = mode;
		}
	}

	if(obj->h264Yuv != NULL)
	{
		h264YuvWriteFlag = obj->h264Yuv + FRAME_H264_SIZE;
		while(1)
		{
			if(count == 20)
				break;

			if(*h264YuvWriteFlag == 1)
			{
				munmap(obj->h264Yuv, obj->h264YuvSize);
				obj->h264Yuv = NULL;
				break;
			}
			count++;
			usleep(40000);
		}

		ALOGD("bbl--h264YuvThreadFunc count = %d", count);
	}
	if(!access("/sdcard/.h264Img", F_OK))
		remove("/sdcard/.h264Img");

	if(obj->h264YuvFd > 0)
	{
		close(obj->h264YuvFd);
		obj->h264YuvFd = -1;
	}

	ALOGD("bbl--h264YuvThreadFunc exit");
	return NULL;
}

static void wwc2_h264_stream(struct WWC2_H264_STREAM_THREAD *pThread)
{
	H264_MODE mode = pThread->mode;
	char fpsName[128] = {0};
	char bitRateName[128] = {0};
	char keyWord[64] = {0};
	unsigned int fps = 0;
	unsigned int bitRate = 0;

	switch(mode)
	{
		case FRONT_H264:
			sprintf(keyWord, "%s", "front");
			break;
		case BACK_H264:
			sprintf(keyWord, "%s", "back");
			break;
		case LEFT_H264:
			sprintf(keyWord, "%s", "left");
			break;
		case RIGHT_H264:
			sprintf(keyWord, "%s", "right");
			break;
		case QUART_H264:
			sprintf(keyWord, "%s", "quart");
			break;
		default:
			break;
	}

	sprintf(fpsName, "wwc2.h264.stream.%s.fps", keyWord);
	sprintf(bitRateName, "wwc2.h264.stream.%s.bitrate", keyWord);

	fps = (unsigned int)property_get_int32(fpsName, 100);
	bitRate = (unsigned int)property_get_int32(bitRateName, 160);

	wwc2_h264_stream_internal(pThread, fps, bitRate);
}

static void* h264StreamThreadFunc(void* arg)
{
	struct sched_param sched_p;
	struct WWC2_H264_STREAM_THREAD *thread= (struct WWC2_H264_STREAM_THREAD *)arg;

	prctl(PR_SET_NAME,thread->threadName, 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);
	int count = 0;

	ALOGD("bbl--h264StreamThreadFunc in mode = %d", thread->mode);

	while(1)
	{
		sem_wait(&thread->threadSem);
		if(thread->threadLoopFlag== false)
			break;
		if(thread->syncFd < 0)
			thread->syncFd = open(thread->syncDevName, O_RDWR);

		if(thread->yuvData == NULL)
		{
			thread->yuvData = (unsigned char*)malloc(FRAME_H264_SIZE);			
		}

		if(thread->streamData == NULL)
		{
			thread->streamData = getShareMem(thread->shareStreamFileName, H264_STREAM_SIZE);
		}
		//do h264 stream
		wwc2_h264_stream(thread);

		if(thread->streamData)
		{
			munmap(thread->streamData, H264_STREAM_SIZE);
			thread->streamData = NULL;
		}

		if(!access(thread->shareStreamFileName, F_OK))
			remove(thread->shareStreamFileName);

		if(thread->yuvData)
		{
			while(1)
			{
				if(count == 20)
					break;

				if(thread->yuvWriteFlag == false)
				{
					free(thread->yuvData);
					thread->yuvData = NULL;
					break;
				}
				count++;
				usleep(40000);
			}
			ALOGD("bbl--h264StreamThreadFunc exit count = %d", count);
		}

		if(thread->syncFd > 0)
		{
			close(thread->syncFd);
			thread->syncFd = -1;
		}
	}
	ALOGD("bbl--h264StreamThreadFunc exit mode = %d", thread->mode);
	return NULL;
}

static struct WWC2_H264_STREAM_THREAD* h264_stream_thread_init(H264_MODE mode)
{
	pthread_attr_t const attr = {0, NULL, 1024 * 1024, 4096, SCHED_OTHER, ANDROID_PRIORITY_FOREGROUND,
#ifdef __LP64__
            .__reserved = {0}
#endif
		};

	struct WWC2_H264_STREAM_THREAD * h264StreamThread = (struct WWC2_H264_STREAM_THREAD *)malloc(sizeof(struct WWC2_H264_STREAM_THREAD));
	if(h264StreamThread == NULL)
	{
		return NULL;
	}
	memset(h264StreamThread, 0 , sizeof(struct WWC2_H264_STREAM_THREAD));
	h264StreamThread->mode = mode;
	sem_init(&h264StreamThread->yuvWriteSem, 0, 0);
	sem_init(&h264StreamThread->yuvReadSem, 0, 0);
	sem_init(&h264StreamThread->stopSem, 0, 0);

	sem_init(&h264StreamThread->threadSem, 0, 0);
	h264StreamThread->threadLoopFlag = true;
	h264StreamThread->syncFd = -1;
	h264StreamThread->streamData = NULL;
	h264StreamThread->yuvWriteFlag = false;
	switch(mode)
	{
		case FRONT_H264:
			h264StreamThread->threadName = "h264StreamFront";
			h264StreamThread->syncDevName = "/dev/wwc2_hsf_sync";
			h264StreamThread->shareStreamFileName = "/sdcard/.h264StreamFront";
			break;
		case BACK_H264:
			h264StreamThread->threadName = "h264StreamBack";
			h264StreamThread->syncDevName = "/dev/wwc2_hsb_sync";
			h264StreamThread->shareStreamFileName = "/sdcard/.h264StreamBack";
			break;
		case LEFT_H264:
			h264StreamThread->threadName = "h264StreamLeft";
			h264StreamThread->syncDevName = "/dev/wwc2_hsl_sync";
			h264StreamThread->shareStreamFileName = "/sdcard/.h264StreamLeft";
			break;
		case RIGHT_H264:
			h264StreamThread->threadName = "h264StreamRight";
			h264StreamThread->syncDevName = "/dev/wwc2_hsr_sync";
			h264StreamThread->shareStreamFileName = "/sdcard/.h264StreamRight";
			break;
		case QUART_H264:
			h264StreamThread->threadName = "h264StreamQuart";
			h264StreamThread->syncDevName = "/dev/wwc2_hsq_sync";
			h264StreamThread->shareStreamFileName = "/sdcard/.h264StreamQuart";
			break;			
		default:
			break;
	}
	pthread_create(&h264StreamThread->thread, &attr, h264StreamThreadFunc, (void *)h264StreamThread);

	return h264StreamThread;
}

static void h264_stream_thread_uninit(struct WWC2_H264_STREAM_THREAD* thread)
{
	thread->threadLoopFlag = false;
	sem_post(&thread->threadSem);
	sem_post(&thread->stopSem);
	if(thread->thread)
		pthread_join(thread->thread, NULL);
	thread->thread = 0;
	sem_destroy(&thread->yuvWriteSem);
	sem_destroy(&thread->yuvReadSem);
	sem_destroy(&thread->stopSem);

	free(thread);
	thread = NULL;
}


static struct WWC2_PR2100_AVM_DATA *pr2100_avm_init(void)
{
	pthread_attr_t const attr = {0, NULL, 1024 * 1024, 4096, SCHED_OTHER, ANDROID_PRIORITY_FOREGROUND,
#ifdef __LP64__
            .__reserved = {0}
#endif
		};
	struct WWC2_PR2100_AVM_DATA *avm = NULL;

	avm = (struct WWC2_PR2100_AVM_DATA *)malloc(sizeof(struct WWC2_PR2100_AVM_DATA));
	if(avm == NULL)
	{
		return NULL;
	}
	memset(avm, 0 , sizeof(struct WWC2_PR2100_AVM_DATA));

	avm->ch0_avm_buff = getShareMem("/sdcard/.ch0Buffer", FRAME_YUV420_SIZE);
	avm->ch1_avm_buff = getShareMem("/sdcard/.ch1Buffer", FRAME_YUV420_SIZE);
	avm->ch2_avm_buff = getShareMem("/sdcard/.ch2Buffer", FRAME_YUV420_SIZE);
	avm->ch3_avm_buff = getShareMem("/sdcard/.ch3Buffer", FRAME_YUV420_SIZE);
	return avm;
}

static void pr2100_avm_uninit(struct WWC2_PR2100_AVM_DATA *avm)
{
	if(avm == NULL)
		return;

	if(avm->ch0_avm_buff)
	{
		munmap(avm->ch0_avm_buff, 1384448);
		avm->ch0_avm_buff = NULL;
		if(!access("/sdcard/.ch0Buffer", F_OK))
			remove("/sdcard/.ch0Buffer");
	}

	if(avm->ch1_avm_buff)
	{
		munmap(avm->ch1_avm_buff, 1384448);
		avm->ch1_avm_buff = NULL;
		if(!access("/sdcard/.ch1Buffer", F_OK))
			remove("/sdcard/.ch1Buffer");
	}

	if(avm->ch2_avm_buff)
	{
		munmap(avm->ch2_avm_buff, 1384448);
		avm->ch2_avm_buff = NULL;
		if(!access("/sdcard/.ch2Buffer", F_OK))
			remove("/sdcard/.ch2Buffer");
	}

	if(avm->ch3_avm_buff)
	{
		munmap(avm->ch3_avm_buff, 1384448);
		avm->ch3_avm_buff = NULL;
		if(!access("/sdcard/.ch3Buffer", F_OK))
			remove("/sdcard/.ch3Buffer");
	}	

	free(avm);
	avm = NULL;
}

void pr2100_record_init(void)
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
		init_camera_param(pr2100Data);
		pr2100Obj = pr2100Data;

		sem_init(&pr2100Data->captureThread.sem, 0, 0);
		pr2100Data->captureThread.threadLoopFlag= true;
		pthread_create(&pr2100Data->captureThread.thread, &attr, captureThreadFunc, (void *)pr2100Obj);
		sem_init(&captureWriteSem, 0, 0);
		sem_init(&captureReadSem, 0, 0);

		sem_init(&pr2100Data->recordThread.sem, 0, 0);
		pr2100Data->recordStopFlag = 1;
		pr2100Data->statusReportFlag = 1;
		pr2100Data->recordThread.threadLoopFlag = true;
		pthread_create(&pr2100Data->recordThread.thread, &attr, recordThreadFunc, (void *)pr2100Obj);
		sem_init(&recordWriteSem, 0, 0);
		sem_init(&recordReadSem, 0, 0);
		sem_init(&recordStopSem, 0, 0);

		sem_init(&pr2100Data->h264YuvThread.sem, 0, 0);
		pr2100Data->h264YuvFd = -1;
		pr2100Data->h264YuvThread.threadLoopFlag = true;
		pthread_create(&pr2100Data->h264YuvThread.thread, &attr, h264YuvThreadFunc, (void *)pr2100Obj);

		frontH264Stream = h264_stream_thread_init(FRONT_H264);
		backH264Stream = h264_stream_thread_init(BACK_H264);
		leftH264Stream = h264_stream_thread_init(LEFT_H264);
		rightH264Stream = h264_stream_thread_init(RIGHT_H264);

		frontThread = pr2100_four_record_init(FRONT_RECORD);
		backThread = pr2100_four_record_init(BACK_RECORD);
		leftThread = pr2100_four_record_init(LEFT_RECORD);
		rightThread = pr2100_four_record_init(RIGHT_RECORD);
		pthread_mutex_init(&recordStatusMutex, NULL);

		if(pr2100Data->display == NULL)
			avmData = pr2100_avm_init();
	}
	sigio_init();
}

void pr2100_record_uninit(void)
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

		h264_stream_thread_uninit(frontH264Stream);
		h264_stream_thread_uninit(backH264Stream);
		h264_stream_thread_uninit(leftH264Stream);
		h264_stream_thread_uninit(rightH264Stream);

		pr2100_four_record_uninit(frontThread);
		pr2100_four_record_uninit(backThread);
		pr2100_four_record_uninit(leftThread);
		pr2100_four_record_uninit(rightThread);
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

		pr2100_avm_uninit(avmData);
	}

	sigio_uninit();
}

#ifdef __cplusplus
}
#endif

