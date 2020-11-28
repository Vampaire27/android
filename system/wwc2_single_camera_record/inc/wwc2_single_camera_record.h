#ifdef __cplusplus
extern "C" {
#endif

void wwc2SingleCameraRecordInit(void);
void wwc2SingleCameraRecordUninit(void);
void doRecordMemCopy(unsigned char *src, const unsigned int width, const unsigned int height);
void doH264MemCopy(unsigned char *src, const unsigned int width, const unsigned int height);
void doCaptureMemCopy(unsigned char *src, const unsigned int width, const unsigned int height);



#ifdef __cplusplus
}
#endif
