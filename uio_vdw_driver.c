// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/uio/uio_vdw.c
 *
 * Userspace IO for Vandewiele
 *
 * Base Functions
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/stringify.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#define DRV_NAME "uio_vdw"
#define DRV_DEVICE_NAME "uio_vdw_device"
#define USE_PROBE 0

#if defined(CONFIG_OF)
static const struct of_device_id vdw_dt_ids[] = {
    { .compatible = "vandewiele,"DRV_NAME }
};
#endif

// forward declaration
typedef struct _vdw_uio_dev_priv *vdw_uio_dev_priv_ptr;

typedef struct _vdw_uio_dev_priv {
	struct uio_info info;
	struct device dev;
	void *memalloc;
	int irq;
	ulong regstart;
	uint regsize;
	vdw_uio_dev_priv_ptr pnext;
} vdw_uio_dev_priv, *vdw_uio_dev_priv_ptr;

typedef struct _vdw_uio_module {
	int instancecount;
	vdw_uio_dev_priv *uioinst;
} vdw_uio_module;

static vdw_uio_module module = { 0, 0 };

static char *devregions = "-1,0,4096"; // default
static char devregionsstorage[4096];
static char *devadd = ""; // default
static int devrm = -1; // default

// forward declarations
static int simpledriver_instance_remove(int instance);
static int simpledriver_instance_add(const char* params);
static int builddevregionsstring(void);

/* module parameters are visible in /sys/module/uio_vdw/parameters
 * and can be manipulated either at
 * 1. insmod time: devregions
 * 2. runtime: devregadd, devrm
 */

/*! "devregions" can be manipulated at module load
 * @param devregions
 * interruptnr,regaddress,regsize[,interruptnr,regaddress,regsize]
 */
module_param( devregions, charp, S_IRUGO);

static int builddevregionsstring()
{
	vdw_uio_dev_priv_ptr uioinst = module.uioinst;
	memset(devregionsstorage, 0, sizeof(devregionsstorage));
	while (uioinst) {
		sprintf(&devregionsstorage[strlen(devregionsstorage)], "%d,%lx,%u",
				uioinst->irq, uioinst->regstart, uioinst->regsize);
		uioinst = uioinst->pnext;
		if (uioinst) {
			strcat(devregionsstorage, ",");
		}
	}
	devregions = devregionsstorage;
	return 0;
}

/*! "devadd" can be manipulated at runtime
 * @param devadd
 * interruptnr,regaddress,regsize[,interruptnr,regaddress,regsize]
 */
static int param_set_devadd(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	printk(KERN_INFO "param_set_devadd = %s\n", val?val:"NULL");
	sscanf(val, "%d", &devrm);
	ret = simpledriver_instance_add(val);
	builddevregionsstring();
	return ret;
}

static int param_get_devadd(char *buffer, const struct kernel_param *kp)
{
	int result = 0;
	sprintf(buffer, "%d", module.instancecount);
	printk(KERN_INFO "param_get_devadd = %s\n", buffer?buffer:"NULL");
	result = strlen(buffer);
	return result;
}

static struct kernel_param_ops param_ops_devadd = {
 .set = param_set_devadd,
 .get = param_get_devadd,
};

/*! "devrm" can be manipulated at runtime
 * @param devrm
 * index to remove
 */
module_param_cb(devadd, &param_ops_devadd, &devadd, (S_IRUSR|S_IWUSR));

static int param_set_devrm(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	printk(KERN_INFO "param_set_devrm = %s\n", val?val:"NULL");
	sscanf(val, "%d", &devrm);
	ret = simpledriver_instance_remove(devrm);
	builddevregionsstring();
	return ret;
}

static int param_get_devrm(char *buffer, const struct kernel_param *kp)
{
	int result = 0;
	sprintf(buffer, "%d", module.instancecount);
	printk(KERN_INFO "param_get_devrm = %s\n", buffer?buffer:"NULL");
	result = strlen(buffer);
	return result;
}

static struct kernel_param_ops param_ops_devrm = {
 .set = param_set_devrm,
 .get = param_get_devrm,
};
/*! "devrm" can be manipulated at runtime
 * @param devrm
 * index to remove
 */
module_param_cb(devrm, &param_ops_devrm, &devrm, (S_IRUSR|S_IWUSR));

/* if one wants to do work in kernel space (interrupt), this is the place
 * to put the code...
 */
static irqreturn_t vdw_uio_handler(int irq, struct uio_info *info) {
	return IRQ_HANDLED; // user space will be notified (and unblocked)
}

static int uio_vdw_runtime_nop(struct device *dev) {
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * In this driver pm_runtime_get_sync() and pm_runtime_put_sync()
	 * are used at open() and release() time. This allows the
	 * Runtime PM code to turn off power to the device while the
	 * device is unused, ie before open() and after release().
	 *
	 * This Runtime PM callback does not need to save or restore
	 * any registers since user space is responsbile for hardware
	 * register reinitialization after open().
	 */
	return 0;
}

static const struct dev_pm_ops uio_vdw_dev_pm_ops = { .runtime_suspend =
		uio_vdw_runtime_nop, .runtime_resume = uio_vdw_runtime_nop, };

#if defined(USE_PROBE) && (USE_PROBE!=0)
/* Forward declaration of a probe routine */
static int simpledriver_probe(struct platform_device *pdev);

static struct platform_driver vdw_driver = {
    .probe = simpledriver_probe,
    .driver = {
        .name = DRV_NAME,
        .pm = &uio_vdw_dev_pm_ops,
        .of_match_table = of_match_ptr(vdw_dt_ids),
        .owner = THIS_MODULE,
    }
};

module_platform_driver(vdw_driver);

#if 0
/* devicetree example */
user_io@80000000 {
    compatible = "vandewiele,uio_vdw";
    regs = <0x20 0x80000000 0x0 0x1000>;
    clock = <&xclk 0>;
    interrupt-parent = <&L4>;
    interrupts = <32>;
    status = "okay";
};
#endif

/* Implement a probe routine */
static int simpledriver_probe(struct platform_device *pdev)
{
    int ret;
    struct resource * reg_base;
    uint32_t len;
    struct uio_mem *uiomem;
    struct uio_info *linfo;
    void __iomem * reg_vaddr;
    int irq;
    printk( KERN_NOTICE ""DRV_NAME" probe\n");

    ret = -1;
    reg_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    len = resource_size(reg_base);
    printk( KERN_NOTICE "vdw-driver resource=%p, len=%u\n", reg_base, len);

    linfo = dev_get_platdata(&pdev->dev);
    printk( KERN_NOTICE "vdw-driver Running Probe, uioinfo=%p\n", linfo);

    // kzalloc memory for the driver itself 

    // Generate a virtual address for the hardware's base addr
    reg_vaddr = ioremap(reg_base->start, len);

    // Generate an IRQ entry for the hardware's IRQ
    irq = platform_get_irq(pdev, 0);
    
    uiomem = &linfo->mem[0];

    // Connect this info to the UIO subsystem
    uiomem->addr = reg_base->start & PAGE_MASK;
    uiomem->offs = reg_base->start & ~PAGE_MASK;
    uiomem->size = len + uiomem->offs;
    uiomem->memtype = UIO_MEM_PHYS;

    linfo->mem[1].size = 0; // sentinel

    ++module.instancecount;
    linfo->name = kasprintf(GFP_KERNEL, "%s_%lx", DRV_DEVICE_NAME,
        		(uintptr_t) (reg_base->start?reg_base->start:module.instancecount));
    linfo->version = "1.0";
    linfo->irq = irq;
    linfo->irq_flags = IRQF_SHARED;
    linfo->handler = vdw_uio_handler;  // We're going to provide our own handler

    // Disable interrupts, e.g.?

    // Register this driver with UIO subsystem,
    ret = uio_register_device(&pdev->dev, linfo);
    
    if (0 == ret) {
        printk( KERN_NOTICE "vdw-driver created vdw UIO device\n");
    }
    else {
        printk( KERN_WARNING "vdw-driver failed to create vdw UIO device!\n");
    }
    
    return ret;
}

#else

static void simpledriver_release(struct device *dev) {
	printk(KERN_INFO "releasing vdw uio device\n");
}

static int simpledriver_instance_remove(int instance) {
	int ret = -ENODEV;
	int instanceiter = 0;
	vdw_uio_dev_priv_ptr uioinst = module.uioinst;
	vdw_uio_dev_priv_ptr uioinstnext = 0;
	vdw_uio_dev_priv_ptr uioinstprev = 0;
	printk( KERN_NOTICE "simpledriver_instance_remove begin, %d instances, remove %d\n",
			module.instancecount, instance);
	if (instance >= module.instancecount) {
		printk( KERN_ERR "simpledriver_instance_remove, index %d out of bound!\n", instance);
		goto exit_func;
	}
	while (uioinst && instanceiter < instance) {
		uioinstnext = uioinst->pnext;
		uioinstprev = uioinst;
		uioinst = uioinstnext;
		++instanceiter;
	}
	if (uioinst) {
		printk(KERN_INFO "UnRegister UIO handler for IRQ=%d name=%s\n",
				(int) uioinst->info.irq,
				uioinst->info.name);
		uioinstnext = uioinst->pnext;
		uio_unregister_device(&uioinst->info);
		device_unregister(&uioinst->dev);
		kfree(uioinst->memalloc);
		kfree(uioinst);
		--module.instancecount;
		if (uioinstprev) {
			uioinstprev->pnext = uioinstnext;
		}
		else {
			module.uioinst = uioinstnext;
		}
		ret = 0;
	}
	exit_func:
	printk( KERN_NOTICE "simpledriver_instance_remove, %d instances left\n", module.instancecount);
	return ret;
}

static int simpledriver_instance_init(int irq, uintptr_t regstart,
	uint32_t regsize) {
	int error = -1;
	bool devregistered = false;
	struct uio_mem *uiomem = 0;
	vdw_uio_dev_priv_ptr uioinst = 0;

	printk(KERN_INFO "instance_init irq=%d start=%lx size=%u\n",
			irq, regstart, regsize);

	if (regstart % PAGE_SIZE) {
		printk(KERN_WARNING "Reg space start must be page-aligned\n");
		error = -EFAULT;
		goto exit_func;
	}

	uioinst = kzalloc(sizeof(vdw_uio_dev_priv), GFP_KERNEL);
	if (!uioinst) {
		printk(KERN_WARNING "Failing to allocate module struct\n");
		error = -ENOMEM;
		goto exit_func;
	}

	printk(KERN_INFO "uioinst %px allocated\n", uioinst);

	if (!module.uioinst) {
		module.uioinst = uioinst; // first instance
		module.instancecount = 1;
	} else {
		vdw_uio_dev_priv_ptr uioinstnext = module.uioinst;
		while (uioinstnext->pnext) {
			uioinstnext = uioinstnext->pnext;
		}
		uioinstnext->pnext = uioinst;
		++module.instancecount;
	}

	printk(KERN_INFO "instance count = %d\n", module.instancecount);

	dev_set_name(&uioinst->dev, "%s_%d", DRV_DEVICE_NAME, module.instancecount);
	uioinst->dev.release = simpledriver_release;

	if (device_register(&uioinst->dev)) {
		printk(KERN_WARNING "Failing to register dev device\n");
		error = -ENODEV;
		goto exit_func;
	}

	devregistered = true;

	uioinst->info.name = kasprintf(GFP_KERNEL, "%s_%lx", DRV_DEVICE_NAME,
			(uintptr_t) (regstart ? regstart : module.instancecount));
	printk(KERN_INFO "uioinst->info.name = %s\n", uioinst->info.name);
	uioinst->info.version = "1.0.0";
	uioinst->info.irq = irq;
	uioinst->info.irq_flags = IRQF_SHARED;
	uioinst->info.handler = vdw_uio_handler;

	// round to page
	regsize = ((regsize + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

	uiomem = &uioinst->info.mem[0];
	// Connect this info to the UIO subsystem
	uiomem->size = regsize;
	uiomem->offs = 0;
	uiomem->name = kasprintf(GFP_KERNEL, "%s%s", uioinst->info.name, "_map0");
	printk(KERN_INFO "uiomem->name = %s\n", uiomem->name);

	uioinst->irq = irq;
	uioinst->regstart = regstart;
	uioinst->regsize = regsize;

	if (!regstart) {
		uioinst->memalloc = kzalloc(regsize, GFP_KERNEL | GFP_DMA);
		printk(KERN_INFO "memalloc %px, pa=%px, size=%u bytes\n",
				(void*) uioinst->memalloc, (void*) __pa(uioinst->memalloc),
				(unsigned int) regsize);
		if (!uioinst->memalloc) {
			printk(KERN_WARNING "Failing to allocate mappable memory\n");
			error = -ENOMEM;
			goto exit_func;
		}
		/* use physical address mapping so that
		 * pgprot_noncached() and remap_pfn_range()
		 * are called by uio core.
		 * */
		uiomem->addr = (phys_addr_t) __pa(uioinst->memalloc);
	} else {
		printk(KERN_INFO "regstart %px, pa=%px, size=%u bytes\n",
				(void*) regstart, (void*) __pa(regstart), (unsigned int) regsize);
		uiomem->addr = (phys_addr_t)(regstart);
	}
	uiomem->memtype = UIO_MEM_PHYS;

	printk(KERN_INFO "uiomem->addr = %px\n", (void*) uiomem->addr);
	printk(KERN_INFO "uiomem->size = %u\n", (unsigned int) uiomem->size);
	printk(KERN_INFO "uiomem->memtype = %s\n", (uiomem->memtype==UIO_MEM_PHYS)?"UIO_MEM_PHYS":"UIO_MEM_LOGICAL");

	uioinst->info.mem[1].size = 0; // sentinel, we only use 1 mapped region

	if (uio_register_device(&uioinst->dev, &uioinst->info) < 0) {
		printk(KERN_INFO "Failing to register uio device\n");
		error = -ENODEV;
		goto exit_func;
	} else {
		printk(KERN_INFO "Registered UIO handler for IRQ=%d\n", (int) uioinst->info.irq);
		error = 0;
	}

	exit_func: if (error) {
		if (uioinst) {
			if (devregistered) {
				device_unregister(&uioinst->dev);
			}
			kfree(uioinst->memalloc);
			kfree(uioinst);
			if (module.uioinst == uioinst) {
				module.uioinst = 0;
				module.instancecount = 0;
			}
		}
	}
	return error;
}

static int simpledriver_instance_add(const char* params)
{
	int error = 0;
	int irqparam;
	uintptr_t regstartparam;
	uint32_t regsizeparam;
	int sscanfret;
	char reststring[256];

	printk( KERN_NOTICE "vdw-driver simpledriver_instance_add, regions (irq,start,size[,...]) = %s\n",
			params?params:"NULL");

	if (!params || !strlen(params)) return -1;

	memset(reststring, 0, sizeof(reststring));
	strncpy(reststring, params, sizeof(reststring) - 1);
	do {
		sscanfret = sscanf(reststring, "%d,%lx,%u%s", &irqparam, &regstartparam,
				&regsizeparam, reststring);
		printk(KERN_INFO "sscanfret %d, irqparam %d, regstartparam %lx, regsizeparam %u, rest = %s\r\n",
				sscanfret, irqparam, regstartparam, regsizeparam, reststring);
		error = -1;
		if (sscanfret >= 3) {
			error = simpledriver_instance_init(irqparam, regstartparam, regsizeparam);
		}
		if (error) { // either sscanf failed or instance_init
			break;
		}
		if (reststring[0] == ',') {
			memmove(reststring, reststring + 1, strlen(reststring));
		}
		else { // no valid reststring
			break;
		}
	} while (sscanfret > 3);

	return error;
}

static int simpledriver_init(void) {
	printk( KERN_NOTICE "vdw-driver init\n");
	return simpledriver_instance_add(devregions);
}

static void simpledriver_exit(void) {
	vdw_uio_dev_priv_ptr uioinst = module.uioinst;
	vdw_uio_dev_priv_ptr uioinstnext;
	printk( KERN_NOTICE "vdw-driver exit begin, %d instances\n", module.instancecount);
	while (uioinst) {
		uioinstnext = uioinst->pnext;
		printk(KERN_INFO "UnRegister UIO handler for IRQ=%d name=%s\n",
				(int) uioinst->info.irq,
				uioinst->info.name);
		uio_unregister_device(&uioinst->info);
		device_unregister(&uioinst->dev);
		kfree(uioinst->memalloc);
		kfree(uioinst);
		uioinst = uioinstnext;
		--module.instancecount;
	}
	printk( KERN_NOTICE "vdw-driver exit done, %d instances\n", module.instancecount);
}

/* GBO: either use probe or this, not both */
module_init( simpledriver_init);
module_exit( simpledriver_exit);

#endif

MODULE_AUTHOR("Gert Boddaert");
MODULE_DESCRIPTION("Userspace I/O platform driver with IRQ handling for VDW");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

