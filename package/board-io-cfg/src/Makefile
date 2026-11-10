obj-m := board_io_cfg.o
board_io_cfg-y := board_io_config.o board_io_config_data.o

ccflags-y := -I$(src)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f board_io_cfg.ko
