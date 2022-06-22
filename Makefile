TARGET_MODULE:=uio_vdw

# If we are running by kernel building system
ifneq ($(KERNELRELEASE),)
	$(TARGET_MODULE)-objs := uio_vdw_driver.o
	obj-m := $(TARGET_MODULE).o
# If we running without kernel build system
else
	BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	PWD:=$(shell pwd)

all: kernel_module
	
kernel_module:
	# run kernel build system to make module
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules
	
app:
	$(CC) -Wall uio_vdw_userapp.c -o uiouser

clean:
	# run kernel build system to cleanup in current directory
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean
	rm -f uiouser

load:
	/sbin/insmod ./$(TARGET_MODULE).ko

unload:
	/sbin/rmmod ./$(TARGET_MODULE).ko

endif
