L_OBJS :=
M_OBJS :=
LX_OBJS :=
MX_OBJS :=
O_OBJS :=
OX_OBJS :=
L_TARGET :=
O_TARGET := vmlinux-obj.o

# EXTRA_CFLAGS += -S -g

# multi objects

sedlfax-objs := sedl_fax.o debug.o helper.o fsm.o isar.o dchannel.o bchannel.o
avmfritz-objs := avm_fritz.o debug.o helper.o fsm.o dchannel.o bchannel.o
hfcpci-objs := hfc_pci.o debug.o helper.o dchannel.o bchannel.o
mISDN_isac-objs := isac.o arcofi.o debug.o
mISDN_core-objs := core.o stack.o udevice.o helper.o
mISDN_l1-objs := layer1.o helper.o debug.o fsm.o
mISDN_l2-objs := layer2.o tei.o helper.o debug.o fsm.o
l3udss1-objs := layer3.o helper.o l3helper.o debug.o fsm.o l3_udss1.o
mISDN_capi-objs := capi.o contr.o listen.o appl.o plci.o cplci.o ncci.o asn1.o \
                   asn1_aoc.o asn1_comp.o asn1_generic.o asn1_diversion.o \
                   asn1_basic_service.o asn1_address.o asn1_enc.o capi_enc.o \
                   supp_serv.o helper.o l3helper.o debug.o fsm.o
mISDN_dtmf-objs := dtmf.o helper.o debug.o

ifdef CONFIG_I4L_CAPI_LAYER
I4LmISDN-objs := i4l_mISDN.o helper.o l3helper.o debug.o fsm.o
endif

LX_OBJS += core.o

ifdef MEMDBG
EXTRA_CFLAGS += -DMEMDBG
MX_OBJS += memdbg.o
endif

#ifeq ($(CONFIG_ISDN_DRV_mISDN),y)
#  O_TARGET += mISDN.o
#else
#  ifeq ($(CONFIG_ISDN_DRV_mISDN),m)
#    O_TARGET += mISDN.o
#    M_OBJS += mISDN.o
#  endif
#endif

export-objs := core.o isac.o

obj-$(CONFIG_ISDN_DRV_mISDN) :=
obj-$(CONFIG_ISDN_DRV_mISDN) += mISDN_core.o
obj-$(CONFIG_ISDN_DRV_mISDN) += mISDN_isac.o
obj-$(CONFIG_ISDN_DRV_mISDN) += avmfritz.o
obj-$(CONFIG_ISDN_DRV_mISDN) += sedlfax.o
obj-$(CONFIG_ISDN_DRV_mISDN) += hfcpci.o
obj-$(CONFIG_ISDN_DRV_mISDN) += mISDN_l1.o
obj-$(CONFIG_ISDN_DRV_mISDN) += mISDN_l2.o
obj-$(CONFIG_ISDN_DRV_mISDN) += l3udss1.o
obj-$(CONFIG_ISDN_DRV_mISDN) += mISDN_capi.o
obj-$(CONFIG_ISDN_DRV_mISDN) += mISDN_dtmf.o
obj-$(CONFIG_ISDN_DRV_mISDN) += I4LmISDN.o

M_OBJS := $(obj-m)

include $(TOPDIR)/Rules.make


mISDN_core.o: $(mISDN_core-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

mISDN_isac.o: $(mISDN_isac-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

avmfritz.o: $(avmfritz-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

sedlfax.o: $(sedlfax-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hfcpci.o: $(hfcpci-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

mISDN_l1.o: $(mISDN_l1-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

mISDN_l2.o: $(mISDN_l2-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

l3udss1.o: $(l3udss1-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

mISDN_capi.o: $(mISDN_capi-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

mISDN_dtmf.o: $(mISDN_dtmf-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

ifdef CONFIG_I4L_CAPI_LAYER
I4LmISDN.o: $(I4LmISDN-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

endif
