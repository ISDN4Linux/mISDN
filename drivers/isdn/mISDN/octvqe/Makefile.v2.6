#
# Makefile for Octasic OCTVQE echo canceller software
#
# Copyright (C) 2006 Octasic, Inc.
#
#

.PHONY: clean all install modules linux24 linux26
INSTALL=/usr/bin/install
CC=gcc

#kernel version
KVERS:=$(shell uname -r)

ifeq ($(PWD),)
PWD=$(shell pwd)
endif
ifeq ($(DEB_HOST_GNU_TYPE),)
UNAME_M:=$(shell uname -m)
else
UNAME_M:=$(DEB_HOST_GNU_TYPE)
endif

ifdef CONFIG_MODULES
ifdef CONFIG_MODVERSIONS
MODFLAGS += -DMODVERSIONS -include $(HPATH)/linux/modversions.h
endif
endif

ifndef KSRC
  ifneq (,$(wildcard /lib/modules/$(KVERS)/build))
    KSRC:=/lib/modules/$(KVERS)/build
  else
    KSRC_SEARCH_PATH:=/usr/src/linux-2.4 /usr/src/linux
    KSRC:=$(shell for dir in $(KSRC_SEARCH_PATH); do if [ -d $$dir ]; then echo $$dir; break; fi; done)
  endif
endif
KVERS_MAJ:=$(shell echo $(KVERS) | cut -d. -f1-2)
KINCLUDES:=$(KSRC)/include
KFLAGS=-I$(KINCLUDES) -O2
KFLAGS+=-DMODULE -D__KERNEL__ -DEXPORT_SYMTAB -Wall -I. -Wno-unused-function -Wstrict-prototypes -fomit-frame-pointer -fno-strict-aliasing

ifneq (,$(wildcard $(KINCLUDES)/linux/modversions.h))
  KFLAGS+=-DMODVERSIONS -include $(KINCLUDES)/linux/modversions.h
endif
ifneq (,$(findstring ppc,$(UNAME_M)))
KFLAGS_PPC:=-msoft-float -fsigned-char
endif
KFLAGS+=$(KFLAGS_PPC)
ifeq ($(KVERS_MAJ),2.4)
  ifneq (,$(findstring x86_64,$(UNAME_M)))
    KFLAGS+=-mcmodel=kernel
  endif
endif

OCTOBJS = octvqe_linux.o
obj-m:= octvqe.o
octvqe-objs:= $(OCTOBJS)

KMAKE  = $(MAKE) -C $(KSRC) SUBDIRS=$(PWD)
KMAKE_INST = $(KMAKE) \
  INSTALL_MOD_PATH=$(INSTALL_PREFIX) INSTALL_MOD_DIR=misc modules_install

ifeq ($(KVERS_MAJ),2.4)
  BUILDVER:=linux24
else
  BUILDVER:=linux26
endif

MOD_DIR:=$(INSTALL_PREFIX)/lib/modules/$(KVERS)/misc

all: $(BUILDVER)

linux24: octvqe.o

linux26: 
	@if [ -z "$(KSRC)" -o ! -d "$(KSRC)" ]; then echo "The sources for $(KVERS) kernel cannot be found."; exit 1 ; fi
	$(KMAKE) modules

octvqe.o: octvqe_linux.c 
	$(CC) $(KFLAGS) -o octvqe.o -c octvqe_linux.c

install: all
ifeq ($(BUILDVER),linux26)
	$(KMAKE_INST)
else
	$(INSTALL) -d $(MOD_DIR)
	$(INSTALL) -m 644 octvqe.o $(MOD_DIR)
endif

clean: 
	rm -f Modules.symvers
	rm -f octvqe.ko
	rm -f octvqe.o
	rm -f octvqe_linux.o
	rm -f .*o.cmd
	rm -f *.mod.c
	rm -f *.mod.o
	
