#ifdef __cplusplus
extern "C" {
#endif

void wwc2CvbsCombineInit(int sensorId);
void wwc2CvbsCombineUninit(void);
void wwc2CvbsCombine(unsigned char *src, const unsigned int width, const unsigned int height, const int sensorId);
bool getMirrorValue(void);
void doMirror(unsigned char *src, const unsigned int width, const unsigned int height);


#ifdef __cplusplus
}
#endif
