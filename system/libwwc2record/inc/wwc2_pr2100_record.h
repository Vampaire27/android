#ifdef __cplusplus
extern "C" {
#endif
#include <semaphore.h>

extern sem_t recordWriteSem;
extern sem_t recordReadSem;
extern sem_t recordStopSem;
extern int wwc2_record(void *p);
#ifdef __cplusplus
}
#endif
