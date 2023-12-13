# Makefile for this dm target. 


KERNEL_DIR = /lib/modules/$(shell uname -r)/build
SRC_DIR = $(shell pwd)
BUILD_DIR = $(shell pwd)/bin
BUILD_DIR_MAKEFILE = $(BUILD_DIR)/Makefile

default: $(BUILD_DIR_MAKEFILE)
	make -C $(KERNEL_DIR) M=$(BUILD_DIR) src=$(SRC_DIR) CONFIG_ENT_DEBUG=$(CONFIG_ENT_DEBUG) modules

$(BUILD_DIR_MAKEFILE): $(BUILD_DIR)
	echo "# This Makefile is here because of Kbuild" > $@

$(BUILD_DIR):
	mkdir -p $@

debug: CONFIG_ENT_DEBUG=y
debug: default

clean:
	rm -rf $(BUILD_DIR)