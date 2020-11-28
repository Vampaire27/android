#ifdef __cplusplus
extern "C" {
#endif


void wwc2WaterMask(unsigned char *src, const unsigned int w, const unsigned int h, const int sensorId);//YUV420
void wwc2RecordInit(void);
void wwc2RecordUninit(void);
int getCameraFeatureFlag(const char* node);
void doRecordMemCopy(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, const int sensorId, const unsigned int width, const unsigned int height);
void doH264MemCopy(unsigned char *src, unsigned char *srcU, unsigned char *srcV, const int sensorId, const unsigned int width, const unsigned int height);
void doMirrorYUV420(unsigned char *srcY, unsigned char *srcU, unsigned char *srcV, const int sensorId, const unsigned int width, const unsigned int height);
void getStereoCameraSize(int* width, int* height, const int sensorId);

void wwc2CaptureInit(const int sensorId);
void wwc2CaptureUninit(const int sensorId);
void doCaptureMemCopy(unsigned char *src, const unsigned int width, const unsigned int height, const int sensorId);

#ifdef __cplusplus
}
#endif
