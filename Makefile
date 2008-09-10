BASEDIR=$(shell pwd)

MAJOR=1
MINOR=2
SUBMINOR=0

INSTALL_PREFIX := /
export INSTALL_PREFIX

#PATH to linux source/headers
#LINUX=/usr/src/linux

ifndef KVERS
KVERS:=$(shell uname -r)
endif

MODS=/lib/modules/$(KVERS)
LINUX=$(MODS)/build
LINUX_SOURCE=$(MODS)/source
UPDATE_MODULES=$(shell which update-modules)
MODULES_UPDATE=$(shell which modules-update)
DEPMOD=$(shell which depmod)


MISDNDIR=$(BASEDIR)
MISDN_SRC=$(MISDNDIR)/drivers/isdn/hardware/mISDN
MISDN_CORE_SRC=$(MISDNDIR)/drivers/isdn/mISDN

########################################
# USER CONFIGS END
########################################

#CONFIGS+=CONFIG_MISDN_DRV=m 
CONFIGS+=CONFIG_MISDN_DSP=m 
CONFIGS+=CONFIG_MISDN_MEMDEBUG=y 
CONFIGS+=CONFIG_MISDN_HFCMULTI=m 
CONFIGS+=CONFIG_MISDN_HFCPCI=m
CONFIGS+=CONFIG_MISDN_HFCUSB=m
CONFIGS+=CONFIG_MISDN_XHFC=m
#CONFIGS+=CONFIG_MISDN_HFCMINI=m
#CONFIGS+=CONFIG_MISDN_W6692=m
#CONFIGS+=CONFIG_MISDN_SPEEDFAX=m
#CONFIGS+=CONFIG_MISDN_AVM_FRITZ=m
#CONFIGS+=CONFIG_MISDN_NETJET=m
CONFIGS+=CONFIG_MISDN_L1OIP=m 
#CONFIGS+=CONFIG_MISDN_DEBUGTOOL=m 
#CONFIGS+=CONFIG_MISDN_HWSKEL=m
CONFIGS+=CONFIG_MISDN_L1LOOP=m

CONFIGS+=CONFIG_MISDN=m

#CONFIGS+=CONFIG_MISDN_NETDEV=y

MISDNVERSION=$(shell cat VERSION)

MINCLUDES+=-I$(MISDNDIR)/include

all: VERSION test_old_misdn
	cp $(MISDNDIR)/drivers/isdn/hardware/mISDN/Makefile.v2.6 $(MISDNDIR)/drivers/isdn/hardware/mISDN/Makefile
	cp $(MISDNDIR)/drivers/isdn/mISDN/Makefile.v2.6 $(MISDNDIR)/drivers/isdn/mISDN/Makefile
	export MINCLUDES=$(MISDNDIR)/include ; export MISDNVERSION=$(MISDNVERSION); make -C $(LINUX) SUBDIRS=$(MISDN_CORE_SRC) modules $(CONFIGS)  
	cp $(MISDN_CORE_SRC)/Module.symvers $(MISDN_SRC)
	export MINCLUDES=$(MISDNDIR)/include ; export MISDNVERSION=$(MISDNVERSION); make -C $(LINUX) SUBDIRS=$(MISDN_SRC) modules $(CONFIGS)  

install: all modules-install
	$(DEPMOD) 
	$(UPDATE_MODULES)
	$(MODULES_UPDATE)
	make -C config install

uninstall:
	export MISDNDIR=$(MISDNDIR); ./makelib.sh uninstall

modules-install:
	cd $(LINUX) ; make INSTALL_MOD_PATH=$(INSTALL_PREFIX) SUBDIRS=$(MISDN_CORE_SRC) modules_install 
	cd $(LINUX) ; make INSTALL_MOD_PATH=$(INSTALL_PREFIX) SUBDIRS=$(MISDN_SRC) modules_install 

test_old_misdn:
	export LINUX=$(LINUX); ./makelib.sh test_old_misdn


.PHONY: modules-install install all clean VERSION

force:
	rm -f $(LINUX)/include/linux/mISDNif.h
	rm -f $(LINUX)/include/linux/isdn_compat.h
	rm -f /usr/include/linux/mISDNif.h
	rm -f /usr/include/linux/isdn_compat.h

clean:
	rm -rf drivers/isdn/hardware/mISDN/*.o
	rm -rf drivers/isdn/hardware/mISDN/*.ko
	rm -rf drivers/isdn/mISDN/*.o
	rm -rf drivers/isdn/mISDN/*.ko
	rm -rf drivers/isdn/mISDN/octvqe/*.o
	rm -rf drivers/isdn/mISDN/octvqe/*.ko
	rm -rf *~
	find . -iname ".*.cmd" -exec rm -rf {} \;
	find . -iname ".*.d" -exec rm -rf {} \;
	find . -iname "*.mod.c" -exec rm -rf {} \;
	find . -iname "*.mod" -exec rm -rf {} \;

VERSION:
	echo $(MAJOR)_$(MINOR)_$(SUBMINOR) > VERSION ; \

snapshot: clean
	DIR=mISDN-$$(date +"20%y_%m_%d") ; \
	echo $(MAJOR)_$(MINOR)_$(SUBMINOR)-$$(date +"20%y_%m_%d" | sed -e "s/\//_/g") > VERSION ; \
	mkdir -p /tmp/$$DIR ; \
	cp -a * /tmp/$$DIR ; \
	cd /tmp/; \
	tar czf $$DIR.tar.gz $$DIR

release: clean
	DIR=mISDN-$(MAJOR)_$(MINOR)_$(SUBMINOR) ; \
	echo $(MAJOR)_$(MINOR)_$(SUBMINOR) > VERSION ; \
	mkdir -p /tmp/$$DIR ; \
	cp -a * /tmp/$$DIR ; \
	cd /tmp/; \
	tar czf $$DIR.tar.gz $$DIR

%-package:
	git-archive --format=tar $* | gzip > $*-$$(date +"20%y_%m_%d").tar.gz
