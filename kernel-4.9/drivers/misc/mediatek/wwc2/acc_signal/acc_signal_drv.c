#include "acc_signal.h"

static struct platform_driver acc_signal_driver;


#define ACC_SIGNAL_DEBUG_DRV(format, args...) pr_warn(format, ##args)



static int acc_signal_probe(struct platform_device *dev)
{
	return mt_acc_signal_probe(dev);
}

static int acc_signal_remove(struct platform_device *dev)
{
	mt_acc_signal_remove();
	return 0;
}


/**********************************************************************
//add for IPO-H need update ACC_SIGNAL state when resume

***********************************************************************/
struct of_device_id acc_signal_of_match[] = {
	{ .compatible = "mediatek,acc_signal", },	
	{},
};


#ifdef CONFIG_PM
static int acc_signal_suspend(struct device *device)
{				/* wake up */
	mt_acc_signal_suspend();
	return 0;
}

static int acc_signal_resume(struct device *device)
{				/* wake up */
	mt_acc_signal_resume();
	return 0;
}

static int acc_signal_pm_restore_noirq(struct device *device)
{
	mt_acc_signal_pm_restore_noirq();
	return 0;
}


static const struct dev_pm_ops acc_signal_pm_ops = {
	.suspend = acc_signal_suspend,
	.resume = acc_signal_resume,
	.restore_noirq = acc_signal_pm_restore_noirq,
};
#endif

static struct platform_driver acc_signal_driver = {
	.probe = acc_signal_probe,
	/* .suspend = acc_signal_suspend, */
	/* .resume = acc_signal_resume, */
	.remove = acc_signal_remove,
	.driver = {
			.name = "acc_signal_Driver",
#ifdef CONFIG_PM //fangjie mask: defined 
			.pm = &acc_signal_pm_ops,
#endif
			.of_match_table = acc_signal_of_match,
		   },
};


static int acc_signal_mod_init(void)
{
	int ret = 0;

	//ACC_SIGNAL_DEBUG_DRV("[acc_signal]acc_signal_mod_init begin!\n");
	ret = platform_driver_register(&acc_signal_driver);
	if (ret)
		ACC_SIGNAL_DEBUG_DRV("[acc_signal]platform_driver_register error:(%d)\n", ret);
	else
		ACC_SIGNAL_DEBUG_DRV("[acc_signal]platform_driver_register done!\n");

	//ACC_SIGNAL_DEBUG_DRV("[acc_signal]acc_signal_mod_init done!\n");
	return ret;

}

static void acc_signal_mod_exit(void)
{
	ACC_SIGNAL_DEBUG_DRV("[acc_signal]acc_signal_mod_exit\n");
	platform_driver_unregister(&acc_signal_driver);

	ACC_SIGNAL_DEBUG_DRV("[acc_signal]acc_signal_mod_exit Done!\n");
}



module_init(acc_signal_mod_init);
module_exit(acc_signal_mod_exit);

//module_param(debug_enable_drv, int, 0644);

MODULE_DESCRIPTION("wwc2 ACC SIGNAL driver");
MODULE_AUTHOR("wwc2 <fjie@waterworld.com.cn>");
MODULE_LICENSE("GPL");
