BASEDIR=$(shell pwd)


INSTALL_PREFIX := /
export INSTALL_PREFIX

#PATH to linux source/headers
#LINUX=/usr/src/linux
MODS=/lib/modules/$(shell uname -r)
LINUX=$(MODS)/build
LINUX_SOURCE=$(MODS)/source

DEPMOD=$(which depmod)


MISDNDIR=$(BASEDIR)
MISDN_SRC=$(MISDNDIR)/drivers/isdn/hardware/mISDN

########################################
# USER CONFIGS END
########################################

CONFIGS+=CONFIG_MISDN_DRV=m CONFIG_MISDN_DSP=m 
CONFIGS+=CONFIG_MISDN_HFCMULTI=m 
CONFIGS+=CONFIG_MISDN_HFCPCI=m
CONFIGS+=CONFIG_MISDN_HFCUSB=m
CONFIGS+=CONFIG_MISDN_XHFC=m
CONFIGS+=CONFIG_MISDN_HFCMINI=m
CONFIGS+=CONFIG_MISDN_W6692=m
CONFIGS+=CONFIG_MISDN_SPEEDFAX=m
CONFIGS+=CONFIG_MISDN_AVM_FRITZ=m

#CONFIGS+=CONFIG_MISDN_NETDEV=y



MINCLUDES+=-I$(MISDNDIR)/include

all: test_old_misdn
	@echo
	@echo "Makeing mISDN"
	@echo "============="
	@echo
	cp $(MISDNDIR)/drivers/isdn/hardware/mISDN/Makefile.v2.6 $(MISDNDIR)/drivers/isdn/hardware/mISDN/Makefile

	export MINCLUDES=$(MISDNDIR)/include ; make -C $(LINUX) SUBDIRS=$(MISDN_SRC) modules $(CONFIGS)  


install: all
	cd $(LINUX) ; make INSTALL_MOD_PATH=$(INSTALL_PREFIX) SUBDIRS=$(MISDN_SRC) modules_install 
	mkdir -p $(INSTALL_PREFIX)/usr/include/linux/
	cp $(MISDNDIR)/include/linux/*.h $(INSTALL_PREFIX)/usr/include/linux/
	mkdir -p $(INSTALL_PREFIX)/etc/init.d/
	install -m755 misdn-init $(INSTALL_PREFIX)/etc/init.d/
	mkdir -p $(INSTALL_PREFIX)/etc/modprobe.d/mISDN
	cp mISDN.modprobe.d $(INSTALL_PREFIX)/etc/modprobe.d/mISDN
	$(DEPMOD) 

test_old_misdn:
	@if echo -ne "#include <linux/mISDNif.h>" | gcc -C -E - 2>/dev/null 1>/dev/null  ; then \
		if ! echo -ne "#include <linux/mISDNif.h>\n#ifndef FLG_MSG_DOWN\n#error old mISDNif.h\n#endif\n" | gcc -C -E - 2>/dev/null 1>/dev/null ; then \
			echo -ne "\n!!You should remove the following files:\n\n$(LINUX)/include/linux/mISDNif.h\n$(LINUX)/include/linux/isdn_compat.h\n/usr/include/linux/mISDNif.h\n/usr/include/linux/isdn_compat.h\n\nIn order to upgrade to the mqueue branch\n\n"; \
			echo -ne "I can do that for you, just type: make force\n\n" ; \
			exit 1; \
		fi ;\
	fi


.PHONY: install all clean 

force:
	rm -f $(LINUX)/include/linux/mISDNif.h
	rm -f $(LINUX)/include/linux/isdn_compat.h
	rm -f /usr/include/linux/mISDNif.h
	rm -f /usr/include/linux/isdn_compat.h

clean:
	rm -rf drivers/isdn/hardware/mISDN/*.o
	rm -rf drivers/isdn/hardware/mISDN/*.ko
	rm -rf *~
	find . -iname ".*.cmd" -exec rm -rf {} \;
	find . -iname ".*.d" -exec rm -rf {} \;
	find . -iname "*.mod.c" -exec rm -rf {} \;
	find . -iname "*.mod" -exec rm -rf {} \;

