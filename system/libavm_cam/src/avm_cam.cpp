#include "DpBlitStream.h"

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

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <semaphore.h>

#include "avm_cam.h"

#define YUV420_FRAME_SIZE		(1280*720/2*3)
static avm_cam_event_callback avm_cb = NULL;
static int ch0_status = 0;
static int ch1_status = 0;
static int ch2_status = 0;
static int ch3_status = 0;

static int avm_open_flag = 0;
static int avm_start_flag = 0;

static unsigned char *ch0_buff = NULL;
static unsigned char *ch1_buff = NULL;
static unsigned char *ch2_buff = NULL;
static unsigned char *ch3_buff = NULL;
static int avm_fd = -1;

struct AVM_CAM_THREAD
{
	bool threadLoopFlag;
	pthread_t thread;
	sem_t sem;
};

static struct AVM_CAM_THREAD avmCbThread;

static void avm_cam_do_cb(void)
{
	char data = 0;
	unsigned char *ch0Flag = ch0_buff + YUV420_FRAME_SIZE;
	unsigned char *ch1Flag = ch1_buff + YUV420_FRAME_SIZE;
	unsigned char *ch2Flag = ch2_buff + YUV420_FRAME_SIZE;
	unsigned char *ch3Flag = ch3_buff + YUV420_FRAME_SIZE;

	property_set("wwc2.avm360cb.running", "1");
	avm_cb(AVM_CAM_EVENT_SIGNAL, 0, 1280, 720);
	avm_cb(AVM_CAM_EVENT_SIGNAL, 1, 1280, 720);
	avm_cb(AVM_CAM_EVENT_SIGNAL, 2, 1280, 720);
	avm_cb(AVM_CAM_EVENT_SIGNAL, 3, 1280, 720);

	while(1)
	{
		read(avm_fd, &data, 1);

		//ALOGD("bbl--avm_cam_do_cb (%d %d %d %d)", *ch0Flag, *ch1Flag, *ch2Flag, *ch3Flag);

		if(ch0_status == 0)
			break;

		if(data == 0)
			continue;

		if(*ch0Flag == 1)
		{
			avm_cb(AVM_CAM_EVENT_BUF_DONE, 0, 1280, 720);
		}

		if(*ch1Flag == 1)
		{
			avm_cb(AVM_CAM_EVENT_BUF_DONE, 1, 1280, 720);
		}

		if(*ch2Flag == 1)
		{
			avm_cb(AVM_CAM_EVENT_BUF_DONE, 2, 1280, 720);
		}

		if(*ch3Flag == 1)
		{
			avm_cb(AVM_CAM_EVENT_BUF_DONE, 3, 1280, 720);
		}
	}

	property_set("wwc2.avm360cb.running", "0");
}

static void* avmCbThreadFunc(void* arg)
{
	struct sched_param sched_p;

	prctl(PR_SET_NAME,"avm_cam_cb", 0, 0, 0);
	sched_getparam(0, &sched_p);
	sched_p.sched_priority = ANDROID_PRIORITY_FOREGROUND;  //  Note: "priority" is nice value.
	sched_setscheduler(0, SCHED_OTHER, &sched_p);

	while(1)
	{
		sem_wait(&avmCbThread.sem);
		if(avmCbThread.threadLoopFlag== false)
			break;
		
		avm_cam_do_cb();
	}
	ALOGD("bbl--avmCbThreadFunc exit");
	return NULL;
}
static unsigned char *getShareMem(char* shareDev, const unsigned int size)
{
	int fd = 0;
	unsigned char *shm;

	unsigned int memSize = (size + 4096 - 1) & 0xFFFFF000;

	fd = open(shareDev, O_RDWR, 0666);
	if(fd <= 0)
	{
		ALOGE("bbl open shareDev fail");
		return NULL;
	}

	shm = (unsigned char *)mmap(NULL, memSize, (PROT_READ|PROT_WRITE), MAP_SHARED, fd, 0);
	close(fd);
	if(shm == (void *) -1)
	{
		ALOGE("bbl mmap fail  %d, %s", errno, strerror(errno));
		return NULL;
	}

	return shm;
}


static int query_camera_running(void)
{
	int running = 0;
	int count = 0;
	char data[4] = {'0', ' ', '8', '\0'};
	FILE* fd = fopen("/sys/bus/platform/devices/wwc2_camera_combine/camera_action", "w");

	property_set("wwc2.camera.running", "0");

	while(count < 10)
	{
		if(fd != NULL)
		{
			fwrite(data, strlen(data), 1, fd);
			fflush(fd);
		}
		usleep(100000);

		running = property_get_int32("wwc2.camera.running", 0);
		ALOGD("bbl--query_camera_running %d-%d",running,count);
		if(running == 1)
			break;
		else 
			sleep(1);
		count++;
	}

	if(fd != NULL)
	{
		fclose(fd);
		fd = NULL;
	}

	if(count == 10)
		return -1;
	else
		return 0;
}

int avm_cam_open (avm_cam_event_callback callback)
{
	int is_avm_server_ready = 0;
	int count = 0;
	int ret = -1;
	pthread_attr_t const attr = {0, NULL, 1024 * 1024, 4096, SCHED_OTHER, ANDROID_PRIORITY_FOREGROUND,
#ifdef __LP64__
            .__reserved = {0}
#endif
		};

	ALOGD("bbl------open avm_open_flag = %d",avm_open_flag);

	ret = query_camera_running();
	if(ret < 0)
		return ret;

	if(avm_open_flag == 0)
	{
		ch0_buff = getShareMem("/sdcard/.ch0Buffer", YUV420_FRAME_SIZE);
		ch1_buff = getShareMem("/sdcard/.ch1Buffer", YUV420_FRAME_SIZE);
		ch2_buff = getShareMem("/sdcard/.ch2Buffer", YUV420_FRAME_SIZE);
		ch3_buff = getShareMem("/sdcard/.ch3Buffer", YUV420_FRAME_SIZE);

		avm_cb = callback;
		avm_fd = open("/dev/wwc2_camera_combine", O_RDWR);

		sem_init(&avmCbThread.sem, 0, 0);
		avmCbThread.threadLoopFlag= true;
		pthread_create(&avmCbThread.thread, &attr, avmCbThreadFunc, NULL);

		avm_open_flag = 1;
	}
	return 0;
}

int avm_cam_enable_channel (const int enable[AVM_CAM_CHANNEL_MAX])
{
	ALOGD("bbl------enable channel %d-%d-%d-%d", enable[0], enable[1], enable[2], enable[3]);

	ch0_status = enable[0];
	ch1_status = enable[1];
	ch2_status = enable[2];
	ch3_status = enable[3];
	return 0;
}


int avm_cam_query_buf (int ch, int buf_id, avm_cam_buf* buf)
{
	switch(ch)
	{
		case 0:
			buf->ptr = (void *)ch0_buff;
			break;
		case 1:
			buf->ptr = (void *)ch1_buff;
			break;
		case 2:
			buf->ptr = (void *)ch2_buff;
			break;
		case 3:
			buf->ptr = (void *)ch3_buff;
			break;
		default:
			break;
	}

	buf->height = 720;
	buf->width = 1280;
	buf->stride = 1280;
	buf->format = 0x32315659;//NV12:0x11, yv12:0x32315659

	return 0;
}

int avm_cam_start (void)
{
	ALOGD("bbl------start avm_start_flag = %d",avm_start_flag);	

	if(avm_start_flag == 0)
	{
		sem_post(&avmCbThread.sem);
		avm_start_flag = 1;
	}
	return 0;
}

int avm_cam_queue_buf (int ch, int buf_id)
{
	volatile unsigned char *ch0Flag = ch0_buff + YUV420_FRAME_SIZE;
	volatile unsigned char *ch1Flag = ch1_buff + YUV420_FRAME_SIZE;
	volatile unsigned char *ch2Flag = ch2_buff + YUV420_FRAME_SIZE;
	volatile unsigned char *ch3Flag = ch3_buff + YUV420_FRAME_SIZE;

	switch(ch)
	{
		case 0:
			*ch0Flag = 0;
			break;
		case 1:
			*ch1Flag = 0;
			break;
		case 2:
			*ch2Flag = 0;
			break;
		case 3:
			*ch3Flag = 0;
			break;
		default:
			break;
	}
	return 0;
}

int avm_cam_deque_buf (int ch)
{
	return 0;
}

int avm_cam_stop (void)
{
	ALOGD("bbl------stop avm_start_flag = %d",avm_start_flag);	
	if(avm_start_flag == 1)
	{
		ch0_status = 0;
		ch1_status = 0;
		ch2_status = 0;
		ch3_status = 0;
		avm_start_flag = 0;
	}
	return 0;
}

int avm_cam_close (void)
{
	ALOGD("bbl------close avm_open_flag = %d",avm_open_flag);

	if(avm_open_flag == 1)
	{
		avmCbThread.threadLoopFlag = false;
		sem_post(&avmCbThread.sem);
		if(avmCbThread.thread)
			pthread_join(avmCbThread.thread,NULL);
		avmCbThread.thread = 0;
		sem_destroy(&avmCbThread.sem);

		if(avm_fd > 0)
		{
			close(avm_fd);
			avm_fd = -1;
		}

		if(ch0_buff)
		{
			munmap(ch0_buff, 1384448);
			ch0_buff = NULL;
		}

		if(ch1_buff)
		{
			munmap(ch1_buff, 1384448);
			ch1_buff = NULL;
		}

		if(ch2_buff)
		{
			munmap(ch2_buff, 1384448);
			ch2_buff = NULL;
		}

		if(ch3_buff)
		{
			munmap(ch3_buff, 1384448);
			ch3_buff = NULL;
		}

		avm_cb = NULL;
		avm_open_flag = 0;
	}
	return 0;
}

static const avm_cam_ops ops = {
	avm_cam_open,
	avm_cam_enable_channel,
	avm_cam_query_buf,
	avm_cam_start,
	avm_cam_queue_buf,
	avm_cam_deque_buf,
	avm_cam_stop,
	avm_cam_close
};

const avm_cam_ops* avm_cam_get_ops (void)
{
	ALOGD("bbl------get ops");	

	return &ops;
}


mdp_handle mdplib_create (void)
{
	DpBlitStream *h = new DpBlitStream();

	return (mdp_handle)h;
}

int mdplib_set_src_buffer (mdp_handle h, void* plane_ptr[], uint32_t plane_siz[], uint32_t plane_num)
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl mdplib_set_src_buffer plane_siz[0] = %d, plane_siz[1] = %d, plane_siz[2] = %d, plane_num = %d", plane_siz[0], plane_siz[1], plane_siz[2], plane_num);

	plane_ptr[1] = (unsigned char *)plane_ptr[0] + 1280*720;
	plane_ptr[2] = (unsigned char *)plane_ptr[0] + 1280*720*5/4;
	plane_siz[1] = plane_siz[0]/4;
	plane_siz[2] = plane_siz[1];
	plane_num = 3;
	handle->setSrcBuffer((void**)plane_ptr, (unsigned int*)plane_siz, plane_num);
	return 0;
}

int mdplib_set_src_buffer2 (mdp_handle h, uint32_t fd, uint32_t plane_siz[], uint32_t plane_num)
{
	//ALOGD("bbl-----mdplib_set_src_buffer2");

	return 0;
}

int mdplib_set_dst_buffer (mdp_handle h, void* plane_ptr[], uint32_t plane_siz[], uint32_t plane_num)
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl mdplib_set_dst_buffer plane_siz[0] = %d, plane_siz[1] = %d, plane_siz[2] = %d, plane_num = %d", plane_siz[0], plane_siz[1], plane_siz[2], plane_num);

	handle->setDstBuffer((void**)plane_ptr, (unsigned int*)plane_siz, plane_num);
	return 0;
}
int mdplib_set_dst_buffer2 (mdp_handle h, uint32_t fd, uint32_t plane_siz[], uint32_t plane_num)
{
	//ALOGD("bbl-----mdplib_set_dst_buffer2");

	return 0;
}

int mdplib_set_src_config (mdp_handle h, int32_t width, int32_t height, int32_t ypitch, int32_t uvpitch, int32_t format, int32_t crop[4])
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl mdplib_set_src_buffer width = %d, height = %d, ypitch = %d, uvpitch = %d, format = 0x%x", width, height, ypitch, uvpitch,format);

	uvpitch = ypitch/2;
	handle->setSrcConfig(width, height, ypitch, uvpitch, eYV12, DP_PROFILE_BT601, eInterlace_None, NULL);

	return 0;
}

int mdplib_set_dst_config (mdp_handle h, int32_t width, int32_t height, int32_t ypitch, int32_t uvpitch, int32_t format)
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl mdplib_set_dst_config width = %d, height = %d, ypitch = %d, uvpitch = %d, format = 0x%x", width, height, ypitch, uvpitch,format);

	handle->setDstConfig(width, height, ypitch, uvpitch, eYV12, DP_PROFILE_BT601, eInterlace_None, NULL);
	return 0;
}

int mdplib_set_flip (mdp_handle h, int32_t flip)
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl-----mdplib_set_flip");

	handle->setFlip(flip);
	return 0;
}

int mdplib_invalidate (mdp_handle h)
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl-----mdplib_invalidate");

	handle->invalidate();
	return 0;
}

int mdplib_destroy (mdp_handle h)
{
	DpBlitStream *handle = (DpBlitStream *)h;
	//ALOGD("bbl-----mdplib_destroy");

	if(handle)
		delete handle;
	return 0;
}

static mdplib_ops mdp_ops = {
	mdplib_create,
	mdplib_set_src_buffer,
	mdplib_set_src_buffer2,
	mdplib_set_dst_buffer,
	mdplib_set_dst_buffer2,
	mdplib_set_src_config,
	mdplib_set_dst_config,
	mdplib_set_flip,
	mdplib_invalidate,
	mdplib_destroy
};

const mdplib_ops* mdplib_get_ops (void)
{
	ALOGD("bbl-----mdplib_get_ops");

	return &mdp_ops;
}

void mdp_resize_yv12(unsigned char*src, unsigned int src_width, unsigned int src_height, unsigned char*dst, unsigned int dst_width, unsigned int dst_height)
{
	DpBlitStream handle;
	unsigned int src_plane_siz[3] = {src_width*src_height, src_width*src_height/4, src_width*src_height/4};
	void *src_plane_ptr[3] = {(void*)src, (void*)(src+src_plane_siz[0]), (void*)(src+src_plane_siz[0]+src_plane_siz[1])};
	unsigned int src_plane_num = 3;
	unsigned int src_ypitch = src_width;
	unsigned int src_uvpitch = src_width/2;

	unsigned int dst_plane_siz[3] = {dst_width*dst_height, dst_width*dst_height/4, dst_width*dst_height/4};
	void *dst_plane_ptr[3] = {(void*)dst, (void*)(dst+dst_plane_siz[0]), (void*)(dst+dst_plane_siz[0]+dst_plane_siz[1])};
	unsigned int dst_plane_num = 3;
	unsigned int dst_ypitch = dst_width;
	unsigned int dst_uvpitch = dst_ypitch/2;
	int ret = 0;

	handle.setSrcBuffer((void**)src_plane_ptr, src_plane_siz, src_plane_num);
	handle.setSrcConfig(src_width, src_height, src_ypitch, src_uvpitch, eYV12, DP_PROFILE_BT601, eInterlace_None, NULL);

	handle.setDstBuffer((void**)dst_plane_ptr, dst_plane_siz, dst_plane_num);
	handle.setDstConfig(dst_width, dst_height, dst_ypitch, dst_uvpitch, eYV12, DP_PROFILE_BT601, eInterlace_None, NULL);

	ret = handle.invalidate();

}


#ifdef __cplusplus
}
#endif
