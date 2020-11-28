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
#include <sys/types.h>
#include <sys/stat.h>
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
#include <media/stagefright/MPEG4H264Writer.h>
#include <media/MediaPlayerInterface.h>

#include <OMX_Video.h>
#include <cutils/properties.h>  // For property_get().
#include <semaphore.h>
#include <pthread.h>
#include "wwc2_h264_stream.h"
#include "pr2100_combine.h"

using namespace android;

class wwc2CameraSourceH264 : public MediaSource {

public:
	wwc2CameraSourceH264(int width, int height, void *data, int fps, int colorFormat, H264_MODE mode, struct WWC2_H264_STREAM_THREAD* thread)
		:mWidth(width),
		mHeight(height),
		mFrameRate(fps),
		mColorFormat(colorFormat),
		mH264Mode(mode),
		mThread(thread),
		mSize((width * height * 3) / 2),
		mData(data) {

	mGroup.add_buffer(new MediaBuffer(mData, mSize));}

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
        ALOGD("bbl--mode:%d h264 source read start", mH264Mode);
        mNumFramesOutput = 0;
        mStartTimeUs = 0;
        if (meta) {
            int64_t startTimeUs;
            if (meta->findInt64(kKeyTime, &startTimeUs)) {
                mStartTimeUs = startTimeUs;
            }
        }
        return OK;
    }

	virtual status_t stop() {
        return OK;
    }

	virtual status_t read(MediaBuffer **buffer, const MediaSource::ReadOptions *options __unused) {

        status_t err = mGroup.acquire_buffer(buffer);
        if (err != OK) {
            return err;
        }
		sem_post(&mThread->yuvWriteSem);
		usleep(1000000/mFrameRate);
		sem_wait_rt(&mThread->yuvReadSem, 200000000L, mH264Mode);

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
    virtual ~wwc2CameraSourceH264() {}

private:
    MediaBufferGroup mGroup;
    int mWidth, mHeight;
    int mFrameRate;
    int mColorFormat;
    H264_MODE mH264Mode;
	struct WWC2_H264_STREAM_THREAD* mThread;
    size_t mSize;
    int64_t mNumFramesOutput;
	 int64_t mStartTimeUs;
    void *mData;

	void sem_wait_rt(sem_t *pSem, nsecs_t reltime, H264_MODE mode);	
    wwc2CameraSourceH264(const wwc2CameraSourceH264 &);
    wwc2CameraSourceH264 &operator=(const wwc2CameraSourceH264 &);
};

void wwc2CameraSourceH264::sem_wait_rt(sem_t *pSem, nsecs_t reltime, H264_MODE mode)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
		ALOGE("bbl--h264 error in clock_gettime! Please check mode:%d",mode);

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
			ALOGD("bbl--h264 read time out mode:%d",mode);
		}
		else
		{
			ALOGE("bbl--h264 [%s]sem_timedwait() errno = %d mode:%d", __FUNCTION__, errno,mode);
		}
    }
}

static void wwc2_h264_set_file_directory(H264_MODE mode, char* directory)
{
	char saveDirectory[PROPERTY_VALUE_MAX] = {'\0'};

	property_get("wwc2.h264.save.directory", saveDirectory, "/sdcard/h264");
	switch(mode)
	{
		case FRONT_H264:
			sprintf(directory, "%s/%s", saveDirectory,"front");
			break;
		case BACK_H264:
			sprintf(directory, "%s/%s", saveDirectory,"back");
			break;
		case LEFT_H264:
			sprintf(directory, "%s/%s", saveDirectory,"left");
			break;
		case RIGHT_H264:
			sprintf(directory, "%s/%s", saveDirectory,"right");
			break;
		case QUART_H264:
			sprintf(directory, "%s/%s", saveDirectory,"quart");
			break;
		case FOUR_H264:
			sprintf(directory, "%s/%s", saveDirectory,"four");
			break;
		default:
			break;
	}
}

int wwc2_h264_stream_internal(void* pStream, unsigned int fps, unsigned int bitRate)
{
	int frameRateFps = fps/10;
	int width = 640;
	int height = 360;
	int bitRateBps = bitRate*1024;
	int iFramesIntervalSeconds = 1;
	int colorFormat = OMX_COLOR_FormatYUV420Planar;
	int level = OMX_VIDEO_AVCLevel11;        // Encoder specific default
	int profile = OMX_VIDEO_AVCProfileHigh;      // Encoder specific default
	const char *iFileDirectory = "/sdcard/h264/";
	char saveDirectory[PROPERTY_VALUE_MAX] = {'\0'};
	status_t err = OK;
	struct WWC2_H264_STREAM_THREAD* thread = (struct WWC2_H264_STREAM_THREAD*)pStream;
	H264_MODE h264Mode = thread->mode;

	android::ProcessState::self()->startThreadPool();
	wwc2_h264_set_file_directory(h264Mode, saveDirectory);
	iFileDirectory = saveDirectory;	
	sp<MediaSource> source = new wwc2CameraSourceH264(width, height, thread->yuvData, frameRateFps, colorFormat, h264Mode, thread);
	sp<AMessage> enc_meta = new AMessage;

	ALOGD("bbl--mode:%d h264 width = %d, height = %d, fps = %d, bitrate = %d", h264Mode, width, height, frameRateFps, bitRateBps);

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

	sp<ALooper> looper = new ALooper;
	looper->setName("h264stream");
	looper->start();

	{
		ALOGD("bbl--mode:%d h264 start1", h264Mode);
		sp<MediaCodecSource> encoder = MediaCodecSource::Create(looper, enc_meta, source, NULL ,0);
		sp<MPEG4H264Writer> writer = new MPEG4H264Writer();

		writer->addSource(encoder);
		writer->setH264StreamAddr(thread->streamData);
		writer->setH264StreamSyncFd(thread->syncFd);
		ALOGD("bbl--mode:%d h264 start2", h264Mode);
		CHECK_EQ((status_t)OK, writer->start());

		sem_wait(&thread->stopSem);
		ALOGD("bbl--h264 stop mode:%d",h264Mode);
		err = writer->stop();
	}

	ALOGD("bbl--mode:%d h264 end1",h264Mode);
	if (err != OK && err != ERROR_END_OF_STREAM)
	{
		ALOGD("bbl--mode:%d h264 failed: %d\n", h264Mode, err);
	}
	return 0;
}
