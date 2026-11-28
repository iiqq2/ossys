ifneq ($(KERNELRELEASE),)
obj-m := tsulab.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" modules

clean:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" clean
endif
