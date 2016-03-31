ifneq ($(KERNELRELEASE),)
    # kbuild part of makefile
obj-m  := rmem_rdma.o
rmem_rdma-y := rdma_library.o rmem.o
ccflags-y=-I/usr/src/mlnx-ofed-kernel-3.2/include/ -I./init

else
    KDIR ?= /lib/modules/`uname -r`/build

PWD := $(shell pwd)

make:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean


endif
