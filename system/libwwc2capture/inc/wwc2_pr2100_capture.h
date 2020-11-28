#ifdef __cplusplus
extern "C" {
#endif
#include <semaphore.h>

extern sem_t captureWriteSem;
extern sem_t captureReadSem;
extern int wwc2_capture(void *p);
#ifdef __cplusplus
}
#endif
