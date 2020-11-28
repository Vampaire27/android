#ifdef __cplusplus
extern "C" {
#endif


void wwc2WaterMask(unsigned char *src, const unsigned int w, const unsigned int h, const int sensorId);//UYVY
void wwc2RecordInit(int sensorId);
void wwc2RecordUninit(int sensorId);
int getCameraFeatureFlag(const char* node);
void doRecordMemCopyYuv420(unsigned char *src, const unsigned int stride0, const unsigned int stride1, const unsigned int stride2, const int sensorId, const unsigned int width, const unsigned int height);
void doH264MemCopyYuv420(unsigned char *src, const unsigned int stride0, const unsigned int stride1, const unsigned int stride2, const int sensorId, const unsigned int width, const unsigned int height);

void wwc2CvbsCombineInit(int sensorId);
void wwc2CvbsCombineUninit(void);
void wwc2CvbsCombine(unsigned char *src, const unsigned int width, const unsigned int height, const int sensorId);

#ifdef __cplusplus
}
#endif
