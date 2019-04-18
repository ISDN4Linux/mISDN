# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the modular ISDN hardware drivers
#
#

obj-$(CONFIG_MISDN_HFCPCI) += hfcpci.o
obj-$(CONFIG_MISDN_HFCMULTI) += hfcmulti.o
obj-$(CONFIG_MISDN_HFCUSB) += hfcsusb.o
obj-$(CONFIG_MISDN_XHFC) += xhfc.o
obj-$(CONFIG_MISDN_HWSKEL) += mISDN_hwskel.o
obj-$(CONFIG_MISDN_L1LOOP) += mISDN_l1loop.o
obj-$(CONFIG_MISDN_AVMFRITZ) += avmfritz.o
obj-$(CONFIG_MISDN_SPEEDFAX) += speedfax.o
obj-$(CONFIG_MISDN_INFINEON) += mISDNinfineon.o
obj-$(CONFIG_MISDN_W6692) += w6692.o
obj-$(CONFIG_MISDN_NETJET) += netjet.o
# chip modules
obj-$(CONFIG_MISDN_IPAC) += mISDNipac.o
obj-$(CONFIG_MISDN_ISAR) += mISDNisar.o

obj-$(CONFIG_MISDN_HDLC) += isdnhdlc.o

# multi objects

xhfc-objs := xhfc_su.o xhfc_pci2pi.o

# renamed modules

mISDN_hwskel-objs := hwskel.o
mISDN_l1loop-objs := l1loop.o
