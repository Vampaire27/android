
#ifndef __AVM_CAM_H__
#define __AVM_CAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#define AVM_CAM_CHANNEL_MAX     (4)
#define AVM_CAM_CHN_BUF_MAX     (4)
#define AVM_CB_SOCKET_NAME	"avmCamCbSocket"

enum EVENT_TYPE{
    AVM_CAM_EVENT_NONE = 0,
    AVM_CAM_EVENT_SIGNAL, /*param1:width param2:height  0x0:signal_lost  non-0x0:signal_ready*/
    AVM_CAM_EVENT_BUF_DONE, /*param1:buf_id*/
};


typedef void (*avm_cam_event_callback) (int event /*EVENT_TYPE*/, int ch /*camera index*/, int param1, int param2);

typedef struct {
    void* ptr;
    int fd;
    int format;
    int width;
    int height;
    int stride;
} avm_cam_buf;

int avm_cam_open (avm_cam_event_callback callback);
int avm_cam_enable_channel (const int enable[AVM_CAM_CHANNEL_MAX]);
int avm_cam_setup_buf (int ch, int buf_cnt, avm_cam_buf* bufs);
int avm_cam_query_buf (int ch, int buf_id, avm_cam_buf* buf);
int avm_cam_start (void);
int avm_cam_queue_buf (int ch, int buf_id);
int avm_cam_deque_buf (int ch);
int avm_cam_stop (void);
int avm_cam_close (void);

typedef struct {
    int (*open) (avm_cam_event_callback);
    int (*enable_channel) (const int[]);
    int (*query_buf) (int, int, avm_cam_buf*);
    int (*start) (void);
    int (*queue_buf) (int, int);
    int (*deque_buf) (int);
    int (*stop) (void);
    int (*close) (void);
} avm_cam_ops;
const avm_cam_ops* avm_cam_get_ops (void);


enum {
    MDP_PIXEL_FORMAT_NONE = 0,
    MDP_PIXEL_FORMAT_YUYV,
    MDP_PIXEL_FORMAT_YVYU,
    MDP_PIXEL_FORMAT_UYVY,
    MDP_PIXEL_FORMAT_VYUY,
    MDP_PIXEL_FORMAT_I420,
    MDP_PIXEL_FORMAT_YV12,
    MDP_PIXEL_FORMAT_NV21,
    MDP_PIXEL_FORMAT_NV12,
};

enum {
    MDP_FLIP_H = (1 << 0),
    MDP_FLIP_V = (1 << 1),
};

typedef void* mdp_handle;

mdp_handle mdplib_create (void);
int mdplib_set_src_buffer (mdp_handle h, void* plane_ptr[], uint32_t plane_siz[], uint32_t plane_num);
int mdplib_set_src_buffer2 (mdp_handle h, uint32_t fd, uint32_t plane_siz[], uint32_t plane_num);
int mdplib_set_dst_buffer (mdp_handle h, void* plane_ptr[], uint32_t plane_siz[], uint32_t plane_num);
int mdplib_set_dst_buffer2 (mdp_handle h, uint32_t fd, uint32_t plane_siz[], uint32_t plane_num);
int mdplib_set_src_config (mdp_handle h, int32_t width, int32_t height, int32_t ypitch, int32_t uvpitch, int32_t format, int32_t crop[4]);
int mdplib_set_dst_config (mdp_handle h, int32_t width, int32_t height, int32_t ypitch, int32_t uvpitch, int32_t format);
int mdplib_set_flip (mdp_handle h, int32_t flip);
int mdplib_invalidate (mdp_handle h);
int mdplib_destroy (mdp_handle h);

//for dlopen
typedef struct {
    mdp_handle (*create) (void);
    int (*set_src_buffer) (mdp_handle, void*[], uint32_t[], uint32_t);
    int (*set_src_buffer2) (mdp_handle, uint32_t, uint32_t[], uint32_t);
    int (*set_dst_buffer) (mdp_handle, void*[], uint32_t[], uint32_t);
    int (*set_dst_buffer2) (mdp_handle, uint32_t, uint32_t[], uint32_t);
    int (*set_src_config) (mdp_handle, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t[]);
    int (*set_dst_config) (mdp_handle, int32_t, int32_t, int32_t, int32_t, int32_t);
    int (*set_flip) (mdp_handle, int32_t);
    int (*invalidate) (mdp_handle);
    int (*destroy) (mdp_handle);
} mdplib_ops;
const mdplib_ops* mdplib_get_ops (void);

void mdp_resize_yv12(unsigned char*src, unsigned int src_width, unsigned int src_height, unsigned char*dst, unsigned int dst_width, unsigned int dst_height);

#ifdef __cplusplus
}
#endif
#endif
