L_OBJS :=
M_OBJS :=
LX_OBJS :=
MX_OBJS :=
O_OBJS :=
OX_OBJS :=
L_TARGET :=
O_TARGET :=

# EXTRA_CFLAGS += -S -g


# multi objects

SEDLFAXOBJ := sedl_fax.o debug.o helper.o
SEDLFAXOBJ += isac.o arcofi.o fsm.o isar.o

FRITZOBJ := fritz_pci.o debug.o helper.o
FRITZOBJ += isac.o arcofi.o fsm.o

OBJ_core := hisax_core.o hisax_stack.o hisax_dev.o helper.o
OBJ_l1 := hisax_l1.o helper.o debug.o fsm.o
OBJ_l2 := hisax_l2.o tei.o helper.o debug.o fsm.o
OBJ_l3udss1 := hisax_l3.o helper.o debug.o fsm.o l3_udss1.o
OBJ_capi := capi.o contr.o listen.o appl.o plci.o cplci.o ncci.o asn1.o \
            asn1_aoc.o asn1_comp.o asn1_generic.o asn1_diversion.o \
            asn1_basic_service.o asn1_address.o asn1_enc.o capi_enc.o \
            supp_serv.o helper.o debug.o fsm.o

LX_OBJS += hisax_core.o

#ifeq ($(CONFIG_ISDN_DRV_HISAX),y)
#  O_TARGET += hisax.o
#else
#  ifeq ($(CONFIG_ISDN_DRV_HISAX),m)
#    O_TARGET += hisax.o
#    M_OBJS += hisax.o
#  endif
#endif

M_OBJS += hisaxcore.o
M_OBJS += hisaxl1.o
M_OBJS += hisaxl2.o
M_OBJS += l3udss1.o
M_OBJS += fritzpci.o
M_OBJS += sedlfax.o
M_OBJS += hisaxcapi.o

include $(TOPDIR)/Rules.make

MD5FILES += isac.c isdnl1.c isdnl2.c isdnl3.c \
	    tei.c callc.c cert.c l3dss1.c l3_1tr6.c
#	    elsa.c diva.c sedlbauer.c

CERT = $(shell md5sum -c md5sums.asc >> /dev/null;echo $$?)

#cert.o:	$(MD5FILES) md5sums.asc
#	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -D CERTIFICATION=$(CERT) -c -o cert.o cert.c

hisaxcore.o: $(OBJ_core)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)

hisaxl1.o: $(OBJ_l1)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)

hisaxl2.o: $(OBJ_l2)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)

l3udss1.o: $(OBJ_l3udss1)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)

fritzpci.o: $(FRITZOBJ)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)

sedlfax.o: $(SEDLFAXOBJ)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)

hisaxcapi.o: $(OBJ_capi)
	$(RM) $@
	$(LD) -r -o $@ $(^:dummy=)
