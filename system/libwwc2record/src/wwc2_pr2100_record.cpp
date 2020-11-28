/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SineSource.h"

#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include <binder/ProcessState.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/AudioPlayer.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaCodecSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MPEG2TSWriter.h>
#include <media/MediaPlayerInterface.h>
#include <media/mediarecorder.h>
#include <OMX_Video.h>
#include <binder/IPCThreadState.h>
#include <media/stagefright/AudioSource.h>
#include <media/openmax/OMX_Audio.h>
#include <sys/mman.h>
#include <cutils/properties.h>  // For property_get().
#include "wwc2_pr2100_record.h"
#include "pr2100_combine.h"

using namespace android;


sem_t recordWriteSem;
sem_t recordReadSem;
sem_t recordStopSem;

static output_format mOutputFormat = OUTPUT_FORMAT_MPEG_4;
static audio_encoder mAudioEncoder = AUDIO_ENCODER_AAC;
static audio_source_t mAudioSource = AUDIO_SOURCE_VOICE_CALL;
static int32_t mSampleRate = 48000;
static int32_t mAudioBitRate =127000;
static int32_t mAudioChannels =1;
static uid_t mClientUid;
static pid_t mClientPid;
static int32_t mAudioTimeScale =-1;
static String16 mOpPackageName = (String16)"recordVideo";
static sp<MediaCodecSource> mAudioCodecSource = NULL;
static sp<MediaCodecSource> mVideoCodecSource = NULL;
static int32_t mTotalBitRate = 0;
static sp<ALooper> looper;


static void set_record_status(H264_STATUS status, int isNeed)
{
	FILE* fd = NULL;
	char data[2] = {'0', '\0'};

	if(isNeed)
	{
		data[0] += (char)status;
		fd = fopen("/sys/devices/platform/wwc2_camera_combine/record_status", "w");
		if(fd)
		{
			fwrite(data, strlen(data), 1, fd);
			fclose(fd);
		}
	}
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

static int getNextFile(const char *fileDirectory, RECORD_MODE mode, enum CAMERA_FORMAT format)
{
	char name[64] = {'\0'};
	char saveFile[128] = {'\0'};
	int saveFileFd = 0;
	int i = 0;

	struct tm *p = NULL;
	time_t timer = time(NULL);
	p = localtime(&timer);

	if(format == CH0FHD_CH1FHD)
	{
		switch(mode)
		{
			case FRONT_RECORD:
				strftime(name, sizeof(name), "front_1080P_%F_%T.ts",p);
				break;
			case BACK_RECORD:
				strftime(name, sizeof(name),"back_1080P_%F_%T.ts",p);
				break;
			case LEFT_RECORD:
				strftime(name, sizeof(name),"left_1080P_%F_%T.ts",p);
				break;
			case RIGHT_RECORD:
				strftime(name, sizeof(name),"right_1080P_%F_%T.ts",p);
				break;
			case QUART_RECORD:
				strftime(name, sizeof(name),"quart_1080P_%F_%T.ts",p);
				break;
			case FOUR_RECORD:
				strftime(name, sizeof(name),"four_2160P_%F_%T.ts",p);
				break;
			case DUAL_RECORD:
				strftime(name, sizeof(name),"dual_1080P_%F_%T.ts",p);
				break;
			case TWO_RECORD:
				strftime(name, sizeof(name),"two_2160P_%F_%T.ts",p);
				break;
			default:
				break;
		}
	}
	else
	{
		switch(mode)
		{
			case FRONT_RECORD:
				strftime(name, sizeof(name), "front_720P_%F_%T.ts",p);
				break;
			case BACK_RECORD:
				strftime(name, sizeof(name),"back_720P_%F_%T.ts",p);
				break;
			case LEFT_RECORD:
				strftime(name, sizeof(name),"left_720P_%F_%T.ts",p);
				break;
			case RIGHT_RECORD:
				strftime(name, sizeof(name),"right_720P_%F_%T.ts",p);
				break;
			case QUART_RECORD:
				strftime(name, sizeof(name),"quart_720P_%F_%T.ts",p);
				break;
			case FOUR_RECORD:
				strftime(name, sizeof(name),"four_1440P_%F_%T.ts",p);
				break;
			case DUAL_RECORD:
				strftime(name, sizeof(name),"dual_720P_%F_%T.ts",p);
				break;
			case TWO_RECORD:
				strftime(name, sizeof(name),"two_1440P_%F_%T.ts",p);
				break;
			default:
				break;
		}
	}

	for(i = 0; i < 64; i++)
	{
		if(name[i] == ':')
			name[i] = '-';
	}

	sprintf(saveFile,"%s/%s",fileDirectory,name);
	if(access(fileDirectory,F_OK) != 0)
		mkdirs(fileDirectory);

	if(access(fileDirectory,F_OK) != 0)
	{
		ALOGE("bbl--record dir %s do not exist",fileDirectory);
		return -1;
	}
    //hzy write sdcard, maybe system iowait zhongyang .hu 60 -0  20200713
	saveFileFd = open(saveFile, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
	ALOGD("bbl--record fd = %d fname = %s\n",saveFileFd,saveFile);

	return saveFileFd;
}

class Wwc2CameraSource : public MediaSource {

public:
	Wwc2CameraSource(int width, int height, void *data, int fps, int colorFormat)
		:mWidth(width),
		mHeight(height),
		mFrameRate(fps),
		mColorFormat(colorFormat),
		mSize((width * height * 3) / 2),
		mData(data) {mGroup.add_buffer(new MediaBuffer(mData, mSize));}

	virtual sp<MetaData> getFormat() {
        sp<MetaData> meta = new MetaData;
        meta->setInt32(kKeyWidth, mWidth);
        meta->setInt32(kKeyHeight, mHeight);
        meta->setInt32(kKeyColorFormat, mColorFormat);
        meta->setInt32(kKeyFrameRate, mFrameRate);
        meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_RAW);

        return meta;
    }

	virtual status_t start(MetaData *meta) {
        ALOGD("bbl--record source read start");
        start_record_latency_timer();
        mNumFramesOutput = 0;
        mStartTimeUs = 0;
        if (meta) {
            int64_t startTimeUs;
            if (meta->findInt64(kKeyTime, &startTimeUs)) {
                mStartTimeUs = startTimeUs;
            }
        }
		 ALOGD("bbl--mStartTimeUs = %lld", (long long)mStartTimeUs);
		 if(mStartTimeUs == 0)
		 	mStartTimeUs = systemTime()/1000;
        return OK;
    }

	virtual status_t stop() {
        ALOGD("bbl--record source stop");
        return OK;
    }

	virtual status_t read(MediaBuffer **buffer, const MediaSource::ReadOptions *options __unused) {

        status_t err = mGroup.acquire_buffer(buffer);
        if (err != OK) {
            return err;
        }


		sem_post(&recordWriteSem);
		//sem_wait(&recordReadSem);
		sem_wait_rt(&recordReadSem, 200000000L);

		int64_t frame_time = 0;
		frame_time = systemTime()/1000;
		frame_time = frame_time - mStartTimeUs;

        (*buffer)->set_range(0, mSize);
        (*buffer)->meta_data()->clear();
        (*buffer)->meta_data()->setInt64(
                kKeyTime, frame_time);
        ++mNumFramesOutput;
        return OK;
    }

protected:
    virtual ~Wwc2CameraSource() {}

private:
    MediaBufferGroup mGroup;
    int mWidth, mHeight;
    int mFrameRate;
    int mColorFormat;
    size_t mSize;
    int64_t mNumFramesOutput;
	 int64_t mStartTimeUs;
    void *mData;

	void start_record_latency_timer(void);
	void sem_wait_rt(sem_t *pSem, nsecs_t reltime);

    Wwc2CameraSource(const Wwc2CameraSource &);
    Wwc2CameraSource &operator=(const Wwc2CameraSource &);
};

void Wwc2CameraSource::start_record_latency_timer(void)
{
	char data = 0;
	FILE *fd = NULL;

	fd = fopen("/sys/devices/platform/wwc2_camera_combine/record_latency", "r");
	if(fd)
	{
		fread(&data, 1, 1, fd);
		fclose(fd);
	}
}

static void stop_record_latency_timer(void)
{
	FILE* fd = NULL;
	char data[2] = {'0', '\0'};

	fd = fopen("/sys/devices/platform/wwc2_camera_combine/record_latency", "w");
	if(fd)
	{
		fwrite(data, strlen(data), 1, fd);
		fclose(fd);
	}
}

void Wwc2CameraSource::sem_wait_rt(sem_t *pSem, nsecs_t reltime)
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


static sp<MediaCodecSource> createAudioSource() {
    int32_t sourceSampleRate = mSampleRate;
    // Get UID and PID here for permission checking
    mClientUid = IPCThreadState::self()->getCallingUid();
    mClientPid = IPCThreadState::self()->getCallingPid();

        sp<AudioSource> audioSource =
            new AudioSource(
                    mAudioSource,
                    mOpPackageName,
                    sourceSampleRate,
                    mAudioChannels,
                    mSampleRate,
                    mClientUid,
                    mClientPid);


    status_t err = audioSource->initCheck();

    if (err != OK) {
        ALOGE("audio source is not initialized");
        return NULL;
    }

    sp<AMessage> format = new AMessage;
    switch (mAudioEncoder) {
        case AUDIO_ENCODER_AMR_NB:
        case AUDIO_ENCODER_DEFAULT:
            format->setString("mime", MEDIA_MIMETYPE_AUDIO_AMR_NB);
            break;
        case AUDIO_ENCODER_AMR_WB:
            format->setString("mime", MEDIA_MIMETYPE_AUDIO_AMR_WB);
            break;
        case AUDIO_ENCODER_AAC:
            format->setString("mime", MEDIA_MIMETYPE_AUDIO_AAC);
            format->setInt32("aac-profile", OMX_AUDIO_AACObjectLC);
            break;
        case AUDIO_ENCODER_HE_AAC:
            format->setString("mime", MEDIA_MIMETYPE_AUDIO_AAC);
            format->setInt32("aac-profile", OMX_AUDIO_AACObjectHE);
            break;
        case AUDIO_ENCODER_AAC_ELD:
            format->setString("mime", MEDIA_MIMETYPE_AUDIO_AAC);
            format->setInt32("aac-profile", OMX_AUDIO_AACObjectELD);
            break;

        default:
            ALOGE("Unknown audio encoder: %d", mAudioEncoder);
            return NULL;
    }

    int32_t maxInputSize;
    CHECK(audioSource->getFormat()->findInt32(
                kKeyMaxInputSize, &maxInputSize));

    format->setInt32("max-input-size", maxInputSize);
    format->setInt32("channel-count", mAudioChannels);
    format->setInt32("sample-rate", mSampleRate);
    format->setInt32("bitrate", mAudioBitRate);
     mTotalBitRate += mAudioBitRate;

    if (mAudioTimeScale > 0) {
        format->setInt32("time-scale", mAudioTimeScale);
    }
    format->setInt32("priority", 0 /* realtime */);

    sp<MediaCodecSource> audioEncoder =
            MediaCodecSource::Create(looper, format, audioSource);


    if (audioEncoder == NULL) {
        ALOGE("Failed to create audio encoder");
    }

    return audioEncoder;
}

static status_t setupAudioEncoder(const sp<MediaWriter>& writer) {
    sp<MediaCodecSource> audioEncoder = createAudioSource();
    if (audioEncoder == NULL) {
        return UNKNOWN_ERROR;
    }

    writer->addSource(audioEncoder);
    mAudioCodecSource = audioEncoder;
    return OK;
}

static void setupMPEGMetaData(sp<MetaData> *meta) {
    int64_t startTimeUs = 0;//systemTime() / 1000;
    (*meta)->setInt64(kKeyTime, startTimeUs);
    (*meta)->setInt32(kKeyFileType, mOutputFormat);
    (*meta)->setInt32(kKeyBitRate, mTotalBitRate);
}

static void wwc2_record_set_file_directory(struct PR2100_RECORD *obj, char* directory)
{
	char dir[64] = "/storage/sdcard0/dvr";

	switch(obj->record_dir)
	{
		case DIR_LOCAL:
			sprintf(dir, "/storage/%s/dvr","sdcard0");
			break;
		case DIR_TFCARD:
			sprintf(dir, "/storage/%s/dvr","sdcard1");
			break;
		case DIR_USB0:
			sprintf(dir, "/storage/%s/dvr","usbotg");
			break;
		case DIR_USB1:
			sprintf(dir, "/storage/%s/dvr","usbotg1");
			break;
		case DIR_USB2:
			sprintf(dir, "/storage/%s/dvr","usbotg2");
			break;
		case DIR_USB3:
			sprintf(dir, "/storage/%s/dvr","usbotg3");
			break;
		case DIR_LOCAL_USB0:
			sprintf(dir, "/storage/sdcard0/%s","dvr_usbotg");
			break;
		case DIR_LOCAL_USB1:
			sprintf(dir, "/storage/sdcard0/%s","dvr_usbotg1");
			break;
		case DIR_LOCAL_USB2:
			sprintf(dir, "/storage/sdcard0/%s","dvr_usbotg2");
			break;
		case DIR_LOCAL_USB3:
			sprintf(dir, "/storage/sdcard0/%s","dvr_usbotg3");
			break;
		case DIR_LOCAL_TFCARD:
			sprintf(dir, "/storage/sdcard0/%s","dvr_sdcard");
			break;
		default:
			break;
	}

	switch(obj->recordMode)
	{
		case FRONT_RECORD:
			sprintf(directory, "%s/%s", dir,"front");
			break;
		case BACK_RECORD:
			sprintf(directory, "%s/%s", dir,"back");
			break;
		case LEFT_RECORD:
			sprintf(directory, "%s/%s", dir,"left");
			break;
		case RIGHT_RECORD:
			sprintf(directory, "%s/%s", dir,"right");
			break;
		case QUART_RECORD:
			sprintf(directory, "%s/%s", dir,"quart");
			break;
		case FOUR_RECORD:
			sprintf(directory, "%s/%s", dir,"four");
			break;
		case DUAL_RECORD:
			sprintf(directory, "%s/%s", dir,"dual");
			break;
		case TWO_RECORD:
			sprintf(directory, "%s/%s", dir,"two");
			break;
		default:
			break;
	}
}

int wwc2_record(void *p)
{
	struct PR2100_RECORD *obj = (struct PR2100_RECORD *)p;
	enum CAMERA_FORMAT cameraFormat = obj->cameraFormat;
	int frameRateFps = 25;
	int width = (cameraFormat == CH0FHD_CH1FHD)?1920:1280;
	int height = (cameraFormat == CH0FHD_CH1FHD)?1080:720;
	int bitRateBps = obj->record_bps;
	int iFramesIntervalSeconds = 1;
	int colorFormat = OMX_COLOR_FormatYUV420Planar;
	int level = OMX_VIDEO_AVCLevel1;        // Encoder specific default
	int profile = OMX_VIDEO_AVCProfileHigh;      // Encoder specific default
	char saveDirectory[64] = {0};
	status_t err = OK;
	RECORD_MODE recordMode = obj->recordMode;

	android::ProcessState::self()->startThreadPool();
	wwc2_record_set_file_directory(obj, saveDirectory);
	mTotalBitRate = 0;
	mTotalBitRate += bitRateBps;
	sp<MediaSource> source = new Wwc2CameraSource(width, height, obj->record, frameRateFps, colorFormat);
	sp<AMessage> enc_meta = new AMessage;

	ALOGD("bbl--record width = %d, height = %d, bitrate = %d isRecordAudio = %d", width, height, bitRateBps, obj->audioEnable);

	enc_meta->setString("mime", MEDIA_MIMETYPE_VIDEO_AVC);
	enc_meta->setInt32("width", width);
	enc_meta->setInt32("height", height);
	enc_meta->setInt32("frame-rate", frameRateFps);
	enc_meta->setInt32("bitrate", bitRateBps);
	enc_meta->setInt32("stride", width);
	enc_meta->setInt32("slice-height", height);
	enc_meta->setInt32("i-frame-interval", iFramesIntervalSeconds);
	enc_meta->setInt32("color-format", colorFormat);
	enc_meta->setInt32("level", level);
	enc_meta->setInt32("profile", profile);

	looper = new ALooper;
	looper->setName("recordvideo");
	looper->start();

	{
		int saveFileFd = 0;

		saveFileFd =getNextFile(saveDirectory,recordMode, cameraFormat);
		if(saveFileFd < 0)
		{
			ALOGD("bbl--record couldn't open file %s",saveDirectory);
			set_record_status(STOP_FAIL_H264_STATUS, 1);
			return -1;
		}

		{
			ALOGD("bbl--record start1");
			sp<MediaCodecSource> encoder = MediaCodecSource::Create(looper, enc_meta, source, NULL ,0);
			mVideoCodecSource = encoder;

			sp<MPEG2TSWriter> writer = new MPEG2TSWriter(saveFileFd);
			close(saveFileFd);

			writer->addSource(encoder);
			if(obj->audioEnable)
				err = setupAudioEncoder(writer);
			if(obj->audioEnable)
			{
				sp<MetaData> meta = new MetaData;
				setupMPEGMetaData(&meta);
				CHECK_EQ((status_t)OK, writer->start(meta.get()));
				ALOGD("bbl--record-audio start2");
			}
			else
			{
				ALOGD("bbl--record start2");
				CHECK_EQ((status_t)OK, writer->start());
			}
			set_record_status(RUNNING_H264_STATUS, obj->statusReportFlag);

			sem_wait(&recordStopSem);
			ALOGD("bbl--record stop");
			stop_record_latency_timer();
			err = writer->stop();
		}

		ALOGD("bbl--record end1");
		if (err != OK && err != ERROR_END_OF_STREAM)
		{
			set_record_status(STOP_FAIL_H264_STATUS, 1);
			ALOGD("bbl--record failed: %d\n", err);
		}
		else
			set_record_status(STOP_SUCCESS_H264_STATUS, obj->statusReportFlag);
	}

	if (mAudioCodecSource != NULL)
		mAudioCodecSource.clear();

	if(mVideoCodecSource != NULL)
		mVideoCodecSource.clear();

	return 0;
}
