# Makefile for the modular ISDN driver
#
# EXTRA_CFLAGS += -S -g
#

obj-$(CONFIG_MISDN_DRV) += mISDN_core.o
obj-$(CONFIG_MISDN_DRV) += mISDN_isac.o
obj-$(CONFIG_MISDN_DRV) += mISDN_l1.o
obj-$(CONFIG_MISDN_DRV) += mISDN_l2.o
obj-$(CONFIG_MISDN_DRV) += l3udss1.o
obj-$(CONFIG_MISDN_DRV) += mISDN_capi.o
obj-$(CONFIG_MISDN_DRV) += mISDN_dtmf.o

ifdef CONFIG_MISDN_AVM_FRITZ
obj-$(CONFIG_MISDN_DRV) += avmfritz.o
endif

ifdef CONFIG_MISDN_HFCPCI
obj-$(CONFIG_MISDN_DRV) += hfcpci.o
endif

ifdef CONFIG_MISDN_SPEEDFAX
obj-$(CONFIG_MISDN_DRV) += sedlfax.o
endif

ifdef CONFIG_MISDN_W6692
obj-$(CONFIG_MISDN_DRV) += w6692pci.o
endif

ifdef CONFIG_MISDN_DSP
obj-$(CONFIG_MISDN_DRV) += mISDN_dsp.o
endif

ifdef CONFIG_I4L_CAPI_LAYER
obj-$(CONFIG_MISDN_DRV) += I4LmISDN.o
endif

# multi objects

sedlfax-objs := sedl_fax.o debug.o helper.o fsm.o isar.o dchannel.o bchannel.o
avmfritz-objs := avm_fritz.o debug.o helper.o fsm.o dchannel.o bchannel.o
hfcpci-objs := hfc_pci.o debug.o helper.o dchannel.o bchannel.o
w6692pci-objs := w6692.o debug.o helper.o dchannel.o bchannel.o
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
mISDN_dsp-objs := dsp_core.o dsp_cmx.o dsp_tones.o dsp_dtmf.o dsp_audio.o helper.o debug.o
I4LmISDN-objs := i4l_mISDN.o helper.o l3helper.o debug.o fsm.o

include Rules.mISDN
