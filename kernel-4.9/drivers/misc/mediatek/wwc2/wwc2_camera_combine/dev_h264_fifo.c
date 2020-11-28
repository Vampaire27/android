#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>


#define H264_STREAM_FIFO_SIZE	(64*1024)
static struct kfifo h264_stream;
static struct kfifo h264_stream_front;
static struct kfifo h264_stream_back;
static struct kfifo h264_stream_left;
static struct kfifo h264_stream_right;


static int h264_stream_open(struct inode *inode, struct file *filp)
{
	if(kfifo_initialized(&h264_stream))
		return 0;

	if(kfifo_alloc(&h264_stream, H264_STREAM_FIFO_SIZE, GFP_KERNEL))
	{
		printk("bbl--kfifo_alloc h264_stream fail\n");
		return -1;
	}
	return 0;
}

static int h264_stream_release(struct inode *inode, struct file *filp)
{
	kfifo_free(&h264_stream);

	return 0;
}
static ssize_t h264_stream_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_to_user(&h264_stream, buf, count, &copied);

	return ret?ret:copied;
}

static ssize_t h264_stream_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_from_user(&h264_stream, buf, count, &copied);

	return ret?ret:copied;
}


static struct file_operations stream_fops = {
	.owner = THIS_MODULE,
	.open = h264_stream_open,
	.release = h264_stream_release,
	.read = h264_stream_read,
	.write = h264_stream_write,
};


static int h264_stream_front_open(struct inode *inode, struct file *filp)
{
	if(kfifo_initialized(&h264_stream_front))
		return 0;

	if(kfifo_alloc(&h264_stream_front, H264_STREAM_FIFO_SIZE, GFP_KERNEL))
	{
		printk("bbl--kfifo_alloc h264_stream_front fail\n");
		return -1;
	}
	return 0;
}

static int h264_stream_front_release(struct inode *inode, struct file *filp)
{
	kfifo_free(&h264_stream_front);

	return 0;
}
static ssize_t h264_stream_front_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_to_user(&h264_stream_front, buf, count, &copied);

	return ret?ret:copied;
}

static ssize_t h264_stream_front_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_from_user(&h264_stream_front, buf, count, &copied);

	return ret?ret:copied;
}


static struct file_operations stream_front_fops = {
	.owner = THIS_MODULE,
	.open = h264_stream_front_open,
	.release = h264_stream_front_release,
	.read = h264_stream_front_read,
	.write = h264_stream_front_write,
};

static int h264_stream_back_open(struct inode *inode, struct file *filp)
{
	if(kfifo_initialized(&h264_stream_back))
		return 0;

	if(kfifo_alloc(&h264_stream_back, H264_STREAM_FIFO_SIZE, GFP_KERNEL))
	{
		printk("bbl--kfifo_alloc h264_stream_back fail\n");
		return -1;
	}
	return 0;
}

static int h264_stream_back_release(struct inode *inode, struct file *filp)
{
	kfifo_free(&h264_stream_back);

	return 0;
}
static ssize_t h264_stream_back_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_to_user(&h264_stream_back, buf, count, &copied);

	return ret?ret:copied;
}

static ssize_t h264_stream_back_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_from_user(&h264_stream_back, buf, count, &copied);

	return ret?ret:copied;
}


static struct file_operations stream_back_fops = {
	.owner = THIS_MODULE,
	.open = h264_stream_back_open,
	.release = h264_stream_back_release,
	.read = h264_stream_back_read,
	.write = h264_stream_back_write,
};

static int h264_stream_left_open(struct inode *inode, struct file *filp)
{
	if(kfifo_initialized(&h264_stream_left))
		return 0;

	if(kfifo_alloc(&h264_stream_left, H264_STREAM_FIFO_SIZE, GFP_KERNEL))
	{
		printk("bbl--kfifo_alloc h264_stream_left fail\n");
		return -1;
	}
	return 0;
}

static int h264_stream_left_release(struct inode *inode, struct file *filp)
{
	kfifo_free(&h264_stream_left);

	return 0;
}
static ssize_t h264_stream_left_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_to_user(&h264_stream_left, buf, count, &copied);

	return ret?ret:copied;
}

static ssize_t h264_stream_left_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_from_user(&h264_stream_left, buf, count, &copied);

	return ret?ret:copied;
}

static struct file_operations stream_left_fops = {
	.owner = THIS_MODULE,
	.open = h264_stream_left_open,
	.release = h264_stream_left_release,
	.read = h264_stream_left_read,
	.write = h264_stream_left_write,
};

static int h264_stream_right_open(struct inode *inode, struct file *filp)
{
	if(kfifo_initialized(&h264_stream_right))
		return 0;

	if(kfifo_alloc(&h264_stream_right, H264_STREAM_FIFO_SIZE, GFP_KERNEL))
	{
		printk("bbl--kfifo_alloc h264_stream_right fail\n");
		return -1;
	}
	return 0;
}

static int h264_stream_right_release(struct inode *inode, struct file *filp)
{
	kfifo_free(&h264_stream_right);

	return 0;
}
static ssize_t h264_stream_right_read(struct file *file, char __user *buf,
						size_t count, loff_t *ptr)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_to_user(&h264_stream_right, buf, count, &copied);

	return ret?ret:copied;
}

static ssize_t h264_stream_right_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	ret = kfifo_from_user(&h264_stream_right, buf, count, &copied);

	return ret?ret:copied;
}


static struct file_operations stream_right_fops = {
	.owner = THIS_MODULE,
	.open = h264_stream_right_open,
	.release = h264_stream_right_release,
	.read = h264_stream_right_read,
	.write = h264_stream_right_write,
};

static  int __init wwc2_h264_fifo_init(void)
{
	struct proc_dir_entry *h264_dir = NULL;
	struct proc_dir_entry *h264_entry = NULL;

	h264_dir = proc_mkdir("h264", NULL);
	h264_entry = proc_create("stream", 0666, h264_dir, &stream_fops);
	h264_entry = proc_create("stream_front", 0666, h264_dir, &stream_front_fops);
	h264_entry = proc_create("stream_back", 0666, h264_dir, &stream_back_fops);
	h264_entry = proc_create("stream_left", 0666, h264_dir, &stream_left_fops);
	h264_entry = proc_create("stream_right", 0666, h264_dir, &stream_right_fops);

	return 0;
}

static void __exit wwc2_h264_fifo_exit(void)
{
	remove_proc_entry("h264/stream", NULL);
	remove_proc_entry("h264/stream_front", NULL);
	remove_proc_entry("h264/stream_back", NULL);
	remove_proc_entry("h264/stream_left", NULL);
	remove_proc_entry("h264/stream_right", NULL);
}

module_init(wwc2_h264_fifo_init);
module_exit(wwc2_h264_fifo_exit);

MODULE_AUTHOR("bbl");
MODULE_DESCRIPTION("wwc2 h264 fifo driver");
MODULE_LICENSE("GPL");
