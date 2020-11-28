/*
 * Gadget Driver for zjinnova iAP
 *
 * Copyright (C) 2016 zjinnova, Inc.
 * Author: guwenbiao <guwenbiao@zjinnova.com>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/configfs.h>
#include <linux/usb/composite.h>

#include "configfs.h"

#define IAP_BULK_BUFFER_SIZE           4096

/* number of tx requests to allocate */
#define TX_REQ_MAX 4

#define IAP_GET_DEVICE_STATE               _IOW('z', 1, int)

static const char iap_shortname[] = "zjinnova_iap";

struct iap_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	spinlock_t lock;

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;

	int online;
	int error;

	atomic_t read_excl;
	atomic_t write_excl;
	atomic_t open_excl;

	struct list_head tx_idle;

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	struct usb_request *rx_req;
	int rx_done;
	
	struct work_struct work;
	struct miscdevice *misc_device;
	int sw_online;
};

static struct usb_interface_descriptor iap_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 2,
	.bInterfaceClass        = 0xFF,
	.bInterfaceSubClass     = 0xF0,
	.bInterfaceProtocol     = 0,
};

static struct usb_endpoint_descriptor iap_highspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor iap_highspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor iap_fullspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor iap_fullspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_iap_descs[] = {
	(struct usb_descriptor_header *) &iap_interface_desc,
	(struct usb_descriptor_header *) &iap_fullspeed_in_desc,
	(struct usb_descriptor_header *) &iap_fullspeed_out_desc,
	NULL,
};

static struct usb_descriptor_header *hs_iap_descs[] = {
	(struct usb_descriptor_header *) &iap_interface_desc,
	(struct usb_descriptor_header *) &iap_highspeed_in_desc,
	(struct usb_descriptor_header *) &iap_highspeed_out_desc,
	NULL,
};

static struct usb_string iap_string_defs[] = {
	[0].s = "iAP Interface",
	{  } /* end of list */
};

static struct usb_gadget_strings iap_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		iap_string_defs,
};

static struct usb_gadget_strings *iap_strings[] = {
	&iap_string_table,
	NULL,
};

//#define DRIVER_NAME "iap"
#define MAX_INST_NAME_LEN          40

struct iap_instance {
	struct usb_function_instance func_inst;
	const char *name;
	struct iap_dev *dev;
	char iap_ext_compat_id[16];
	struct usb_os_desc iap_os_desc;
};

/* temporary variable used between iap_open() and iap_gadget_bind() */
static struct iap_dev *_iap_dev;

static inline struct iap_dev *func_to_iap(struct usb_function *f)
{
	return container_of(f, struct iap_dev, function);
}

static struct usb_request *iap_request_new(struct usb_ep *ep, int buffer_size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req)
		return NULL;

	/* now allocate buffers for the requests */
	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void iap_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static inline int iap_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1) {
		return 0;
	} else {
		atomic_dec(excl);
		return -1;
	}
}

static inline void iap_unlock(atomic_t *excl)
{
	atomic_dec(excl);
}

/* add a request to the tail of a list */
static void iap_req_put(struct iap_dev *dev, struct list_head *head, struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* remove a request from the head of a list */
static struct usb_request *iap_req_get(struct iap_dev *dev, struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req;

	spin_lock_irqsave(&dev->lock, flags);
	if (list_empty(head)) {
		req = 0;
	} else {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return req;
}

static void iap_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct iap_dev *dev = _iap_dev;

	if (req->status != 0)
		dev->error = 1;

	iap_req_put(dev, &dev->tx_idle, req);

	wake_up(&dev->write_wq);
}

static void iap_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct iap_dev *dev = _iap_dev;

	dev->rx_done = 1;
	if (req->status != 0)
		dev->error = 1;

	wake_up(&dev->read_wq);
}

static int iap_create_bulk_endpoints(struct iap_dev *dev,
				struct usb_endpoint_descriptor *in_desc,
				struct usb_endpoint_descriptor *out_desc)
{
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req;
	struct usb_ep *ep;
	int i;

	DBG(cdev, "create_bulk_endpoints dev: %p\n", dev);

	ep = usb_ep_autoconfig(cdev->gadget, in_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_in failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_in got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for iap ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	/* now allocate requests for our endpoints */
	req = iap_request_new(dev->ep_out, IAP_BULK_BUFFER_SIZE);
	if (!req)
		goto fail;
	req->complete = iap_complete_out;
	dev->rx_req = req;

	for (i = 0; i < TX_REQ_MAX; i++) {
		req = iap_request_new(dev->ep_in, IAP_BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = iap_complete_in;
		iap_req_put(dev, &dev->tx_idle, req);
	}

	return 0;

fail:
	printk(KERN_ERR "iap_bind() could not allocate requests\n");
	return -1;
}

static ssize_t iap_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos)
{
	struct iap_dev *dev = fp->private_data;
	struct usb_request *req;
	int r = count, xfer;
	int ret;

//	pr_debug("iap_read(%d)\n", count);
	if (!_iap_dev)
		return -ENODEV;

	if (count > IAP_BULK_BUFFER_SIZE)
		return -EINVAL;

	if (iap_lock(&dev->read_excl))
		return -EBUSY;

	/* we will block until we're online */
	while (!(dev->online || dev->error)) {
		pr_debug("iap_read: waiting for online state\n");
		ret = wait_event_interruptible(dev->read_wq,
				(dev->online || dev->error));
		if (ret <= 0) {
			iap_unlock(&dev->read_excl);
			return ret;
		}
	}
	if (dev->error) {
		r = -EIO;
		goto done;
	}

requeue_req:
	/* queue a request */
	req = dev->rx_req;
	req->length = count;
	dev->rx_done = 0;
	ret = usb_ep_queue(dev->ep_out, req, GFP_ATOMIC);
	if (ret < 0) {
		pr_debug("iap_read: failed to queue req %p (%d)\n", req, ret);
		r = -EIO;
		dev->error = 1;
		goto done;
	} else {
		pr_debug("rx %p queue\n", req);
	}

	/* wait for a request to complete */
	//ret = wait_event_interruptible(dev->read_wq, dev->rx_done);
	ret = wait_event_interruptible_timeout(dev->read_wq, dev->rx_done, msecs_to_jiffies(1000));
	if (ret < 0) {
		dev->error = 1;
		r = ret;
		usb_ep_dequeue(dev->ep_out, req);
		goto done;
	}
	else if(ret == 0)
		{
			r = 0;
			usb_ep_dequeue(dev->ep_out, req);
			goto done;
		}
	
	if (!dev->error) {
		/* If we got a 0-len packet, throw it back and try again. */
		if (req->actual == 0)
			goto requeue_req;

		pr_debug("rx %p %d\n", req, req->actual);
		xfer = (req->actual < count) ? req->actual : count;
		if (copy_to_user(buf, req->buf, xfer))
			r = -EFAULT;

	} else
		r = -EIO;


done:
	iap_unlock(&dev->read_excl);
	return r;
}

static ssize_t iap_write(struct file *fp, const char __user *buf,
				 size_t count, loff_t *pos)
{
	struct iap_dev *dev = fp->private_data;
	struct usb_request *req = 0;
	int r = count, xfer;
	int ret;

	if (!_iap_dev)
		return -ENODEV;


	if (iap_lock(&dev->write_excl))
		return -EBUSY;

	/* we will block until we're online */
	while (!(dev->online || dev->error)) {
		pr_debug("iap_read: waiting for online state\n");
		ret = wait_event_interruptible(dev->write_wq,
				(dev->online || dev->error));
		if (ret <= 0) {
			iap_unlock(&dev->write_excl);
			return ret;
		}
	}
	if (dev->error) {
		r = -EIO;
		goto done;
	}

	while (count > 0) {
		if (dev->error) {
			printk("iap_write dev->error\n");
			
			r = -EIO;
			break;
		}

		/* get an idle tx request to use */
		req = 0;
		ret = wait_event_interruptible(dev->write_wq,
			(req = iap_req_get(dev, &dev->tx_idle)) || dev->error);
//		ret = wait_event_interruptible_timeout(dev->write_wq,
//			(req = iap_req_get(dev, &dev->tx_idle)) || dev->error,  HZ*5);

		if (ret < 0) {
			r = ret;
			break;
		}

		if (req != 0) {
			if (count > IAP_BULK_BUFFER_SIZE)
				xfer = IAP_BULK_BUFFER_SIZE;
			else
				xfer = count;
			if (copy_from_user(req->buf, buf, xfer)) {
				r = -EFAULT;
				break;
			}

			req->length = xfer;
			ret = usb_ep_queue(dev->ep_in, req, GFP_ATOMIC);
			if (ret < 0) {
				dev->error = 1;
				r = -EIO;
				break;
			}

			buf += xfer;
			count -= xfer;

			/* zero this so we don't try to free it on error exit */
			req = 0;
		}
	}

	if (req)
		iap_req_put(dev, &dev->tx_idle, req);
done:
	iap_unlock(&dev->write_excl);
	return r;
}

static int iap_open(struct inode *ip, struct file *fp)
{
	if (!_iap_dev)
		return -ENODEV;

	if (iap_lock(&_iap_dev->open_excl))
		return -EBUSY;

	fp->private_data = _iap_dev;

	/* clear the error latch */
	_iap_dev->error = 0;

	return 0;
}

static int iap_release(struct inode *ip, struct file *fp)
{
	iap_unlock(&_iap_dev->open_excl);
	return 0;
}

static long iap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iap_dev *dev = file->private_data;
	int online;
	
	switch (cmd){
		case IAP_GET_DEVICE_STATE:
			if(dev->online)
//				*(int *)arg = 1;
				online = 1;
			else
//				*(int *)arg = 0;
				online = 0;
		}

	if (copy_to_user((void __user *)arg, &online, sizeof(online)))
		return -1;
	
	printk("zjinnova: iap  iap_ioctl >>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	return 0;
}

/* file operations for iap device /dev/zjinnova_iap */
static struct file_operations iap_fops = {
	.owner = THIS_MODULE,
	.read = iap_read,
	.write = iap_write,
	.open = iap_open,
	.unlocked_ioctl = iap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = iap_ioctl,
#endif	
	.release = iap_release,
};

static struct miscdevice iap_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = iap_shortname,
	.fops = &iap_fops,
};


static void iap_work(struct work_struct *data)
{
	struct iap_dev *dev = container_of(data, struct iap_dev, work);
	
	char *disconnected[2] = { "IAP_STATE=DISCONNECTED", NULL };
	char *connected[2]    = { "IAP_STATE=CONNECTED", NULL };
	char **uevent_envp = NULL;

	if(dev->online != dev->sw_online){
			if(dev->online)
				uevent_envp = connected;
			else
				uevent_envp = disconnected;
		}
		dev->sw_online = dev->online;


	if (uevent_envp) {
		kobject_uevent_env(&dev->misc_device->this_device->kobj, KOBJ_CHANGE, uevent_envp);
		pr_info("%s: sent uevent %s\n", __func__, uevent_envp[0]);
	} else {
		pr_info("%s: did not send uevent (%d %p)\n", __func__,
		dev->sw_online, uevent_envp);
	}
}

static int iap_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct iap_dev	*dev = func_to_iap(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	printk("iap_function_set_alt\n");

	if (config_ep_by_speed(cdev->gadget, f,
				   dev->ep_in) ||
		config_ep_by_speed(cdev->gadget, f,
				   dev->ep_out)) {
	printk("iap_function_set_alt fail\n");
		return -EINVAL;
	}

	usb_ep_enable(dev->ep_in);
	usb_ep_enable(dev->ep_out);

	dev->online = 1;
	printk("iap_function_set_alt: online\n");

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);
	wake_up(&dev->write_wq);

	schedule_work(&dev->work);

	return 0;
}


static int
iap_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct iap_dev	*dev = func_to_iap(f);
	int			id;
	int			ret;
	int 		status;
	
	printk("iap_function_bind\n");

	dev->cdev = cdev;
	DBG(cdev, "iap_function_bind dev: %p\n", dev);

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	iap_interface_desc.bInterfaceNumber = id;

	status = usb_string_id(cdev);
	if (status < 0)
		return status;
	iap_string_defs[0].id = status;
	iap_interface_desc.iInterface = status;

	/* copy descriptors, and track endpoint copies */
	dev->function.fs_descriptors = usb_copy_descriptors(fs_iap_descs);

	/* allocate endpoints */
	ret = iap_create_bulk_endpoints(dev, &iap_fullspeed_in_desc,
			&iap_fullspeed_out_desc);
	if (ret)
		return ret;

	/* support high speed hardware */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		iap_highspeed_in_desc.bEndpointAddress =
			iap_fullspeed_in_desc.bEndpointAddress;
		iap_highspeed_out_desc.bEndpointAddress =
			iap_fullspeed_out_desc.bEndpointAddress;

		dev->function.hs_descriptors = usb_copy_descriptors(hs_iap_descs);
	}

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			f->name, dev->ep_in->name, dev->ep_out->name);
	return 0;
}

static void
iap_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct iap_dev	*dev = func_to_iap(f);
	struct usb_request *req;

	printk("iap_function_unbind\n");

	dev->online = 0;
	dev->error = 1;

	wake_up(&dev->read_wq);
	wake_up(&dev->write_wq);
	
	schedule_work(&dev->work);

	iap_request_free(dev->rx_req, dev->ep_out);
	while ((req = iap_req_get(dev, &dev->tx_idle)))
		iap_request_free(req, dev->ep_in);
}

static void iap_function_disable(struct usb_function *f)
{
	struct iap_dev	*dev = func_to_iap(f);
	printk("iap_function_disable\n");

	dev->online = 0;
	dev->error = 1;
	usb_ep_disable(dev->ep_in);
	usb_ep_disable(dev->ep_out);

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);
	wake_up(&dev->write_wq);

	schedule_work(&dev->work);
}

static int __iap_setup(struct iap_instance *fi_iap)
{
	struct iap_dev *dev;
	int ret;

	dev = kzalloc(sizeof(struct iap_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	if (fi_iap != NULL)
		fi_iap->dev = dev;

	spin_lock_init(&dev->lock);

	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);

	atomic_set(&dev->open_excl, 0);
	atomic_set(&dev->read_excl, 0);
	atomic_set(&dev->write_excl, 0);

	INIT_LIST_HEAD(&dev->tx_idle);
	INIT_WORK(&dev->work, iap_work);

	dev->misc_device = &iap_device;
	_iap_dev = dev;
	
	ret = misc_register(&iap_device);
	if (ret)
		goto err;

	return 0;

err:
	kfree(dev);
	printk(KERN_ERR "iap gadget driver failed to initialize\n");
	return ret;
}

static int iap_setup_configfs(struct iap_instance *fi_iap)
{
	return __iap_setup(fi_iap);
}

static void iap_cleanup(void)
{
	struct iap_dev *dev = _iap_dev;

	if (!dev)
		return;

	misc_deregister(&iap_device);
	_iap_dev = NULL;
	kfree(dev);
}
/*
static int iAP_setup(void)
{
	struct iap_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);

	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);

	atomic_set(&dev->open_excl, 0);
	atomic_set(&dev->read_excl, 0);
	atomic_set(&dev->write_excl, 0);

	INIT_LIST_HEAD(&dev->tx_idle);
	INIT_WORK(&dev->work, iap_work);

	dev->misc_device = &iap_device;
	_iap_dev = dev;
	
	ret = misc_register(&iap_device);
	if (ret)
		goto err;

	return 0;

err:
	kfree(dev);
	printk(KERN_ERR "iap gadget driver failed to initialize\n");
	return ret;
}

static void iAP_cleanup(void)
{
	misc_deregister(&iap_device);

	kfree(_iap_dev);
	_iap_dev = NULL;
}

static int iAP_bind_config(struct usb_configuration *c)
{
	struct iap_dev *dev = _iap_dev;
	int 	status;

	printk(KERN_INFO "iAP_bind_config\n");

	dev->cdev = c->cdev;
	dev->function.name = "iap";

	status = usb_string_id(c->cdev);
	if (status < 0)
		return status;
	iap_string_defs[0].id = status;
	iap_interface_desc.iInterface = status;

	dev->function.bind = iap_function_bind;
	dev->function.unbind = iap_function_unbind;
	dev->function.set_alt = iap_function_set_alt;
	dev->function.disable = iap_function_disable;
	dev->function.strings = iap_strings;

	return usb_add_function(c, &dev->function);
}
*/
static void iap_free(struct usb_function *f)
{
	
}

static struct iap_instance *to_iap_instance(struct config_item *item)
{
	return container_of(to_config_group(item), struct iap_instance,
		func_inst.group);
}

static void iap_attr_release(struct config_item *item)
{
	struct iap_instance *fi_iap = to_iap_instance(item);

	usb_put_function_instance(&fi_iap->func_inst);
}

static struct configfs_item_operations iap_item_ops = {
	.release        = iap_attr_release,
};

static struct config_item_type iap_func_type = {
	.ct_item_ops    = &iap_item_ops,
	.ct_owner       = THIS_MODULE,
};

static struct iap_instance *to_fi_iap(struct usb_function_instance *fi)
{
	return container_of(fi, struct iap_instance, func_inst);
}

static int iap_set_inst_name(struct usb_function_instance *fi, const char *name)
{
	struct iap_instance *fi_iap;
	char *ptr;
	int name_len;

	name_len = strlen(name) + 1;
	if (name_len > MAX_INST_NAME_LEN)
		return -ENAMETOOLONG;

	ptr = kstrndup(name, name_len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	fi_iap = to_fi_iap(fi);
	fi_iap->name = ptr;

	return 0;
}

static void iap_free_inst(struct usb_function_instance *fi)
{
	struct iap_instance *fi_iap;
	printk("zjinnova: iap free >>>>>>>>>>>>>>>>>>>>>>>\n");
	fi_iap = to_fi_iap(fi);
	kfree(fi_iap->name);
	iap_cleanup();
	//kfree(fi_iap->iap_os_desc.group.default_groups);
	kfree(fi_iap);
}

static struct usb_function_instance *iap_alloc_inst(void)
{
	struct iap_instance *fi_iap;
	int ret = 0;
//	struct usb_os_desc *descs[1];
//	char *names[1];
	printk("zjinnova: iap  alloc_inst >>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	fi_iap = kzalloc(sizeof(struct iap_instance), GFP_KERNEL);
	if (!fi_iap)
		return ERR_PTR(-ENOMEM);
	fi_iap->func_inst.set_inst_name = iap_set_inst_name;
	fi_iap->func_inst.free_func_inst = iap_free_inst;

	ret = iap_setup_configfs(fi_iap);
	if (ret) {
		kfree(fi_iap);
		pr_err("Error setting IAP\n");
		return ERR_PTR(ret);
	}

	config_group_init_type_name(&fi_iap->func_inst.group,
					"", &iap_func_type);

	return  &fi_iap->func_inst;
}

static struct usb_function *iap_alloc(struct usb_function_instance *fi)
{
	struct iap_instance *fi_iap = to_fi_iap(fi);
	struct iap_dev *dev;
	printk("zjinnova: iap alloc >>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	if (fi_iap->dev == NULL) {
		return ERR_PTR(-EINVAL);
	}

	dev = fi_iap->dev;
	dev->function.name = "iap_carplay"; 	//DRIVER_NAME;
	dev->function.strings = iap_strings;

	dev->function.bind = iap_function_bind;
	dev->function.unbind = iap_function_unbind;
	dev->function.set_alt = iap_function_set_alt;
	dev->function.disable = iap_function_disable;
	dev->function.free_func = iap_free;

	return &dev->function;
}

DECLARE_USB_FUNCTION_INIT(iap, iap_alloc_inst, iap_alloc);
MODULE_LICENSE("GPL");
