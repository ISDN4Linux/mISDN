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

SEDLFAXOBJ := sedl_fax.o debug.o helper.o fsm.o isar.o hisax_dch.o hisax_bch.o

FRITZOBJ := fritz_pci.o debug.o helper.o fsm.o hisax_dch.o hisax_bch.o

HFC_PCIOBJ := hfc_pci.o debug.o helper.o hisax_dch.o hisax_bch.o

hisaxisac-objs := isac.o arcofi.o debug.o

OBJ_core := hisax_core.o hisax_stack.o hisax_dev.o helper.o
OBJ_l1 := hisax_l1.o helper.o debug.o fsm.o
OBJ_l2 := hisax_l2.o tei.o helper.o debug.o fsm.o
OBJ_l3udss1 := hisax_l3.o helper.o debug.o fsm.o l3_udss1.o
OBJ_capi := capi.o contr.o listen.o appl.o plci.o cplci.o ncci.o asn1.o \
            asn1_aoc.o asn1_comp.o asn1_generic.o asn1_diversion.o \
            asn1_basic_service.o asn1_address.o asn1_enc.o capi_enc.o \
            supp_serv.o helper.o debug.o fsm.o
OBJ_dtmf := hisax_dtmf.o helper.o debug.o

LX_OBJS += hisax_core.o

ifdef MEMDBG
EXTRA_CFLAGS += -DMEMDBG
MX_OBJS += memdbg.o
endif

#ifeq ($(CONFIG_ISDN_DRV_HISAX),y)
#  O_TARGET += hisax.o
#else
#  ifeq ($(CONFIG_ISDN_DRV_HISAX),m)
#    O_TARGET += hisax.o
#    M_OBJS += hisax.o
#  endif
#endif

export-objs := hisax_core.o isac.o

obj-$(CONFIG_ISDN_DRV_NEWHISAX) :=
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hisaxcore.o

obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hisaxisac.o

obj-$(CONFIG_ISDN_DRV_NEWHISAX) += fritzpci.o
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += sedlfax.o
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hfcpci.o

obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hisaxl1.o
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hisaxl2.o
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += l3udss1.o
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hisaxcapi.o
obj-$(CONFIG_ISDN_DRV_NEWHISAX) += hisaxdtmf.o

M_OBJS := $(obj-m)

include $(TOPDIR)/Rules.make


hisaxcore.o: $(OBJ_core)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hisaxisac.o: $(hisaxisac-objs)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

fritzpci.o: $(FRITZOBJ)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

sedlfax.o: $(SEDLFAXOBJ)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hfcpci.o: $(HFC_PCIOBJ)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hisaxl1.o: $(OBJ_l1)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hisaxl2.o: $(OBJ_l2)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

l3udss1.o: $(OBJ_l3udss1)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hisaxcapi.o: $(OBJ_capi)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

hisaxdtmf.o: $(OBJ_dtmf)
	$(RM) $@
	$(LD) -r -o $@ $(filter-out $(MODVERFILE) dummy ,$^)

