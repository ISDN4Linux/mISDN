/*
 * hwskel.h
 */

#ifndef __L1LOOP_H__
#define __L1LOOP_H__


#define DRIVER_NAME "mISDN_l1loop"

/* layer1 ph commands */
#define L1_ACTIVATE_TE		1
#define L1_ACTIVATE_NT		2
#define L1_DEACTIVATE_NT	3

/* virtual ISDN line modes */
#define VLINE_NONE		0
#define VLINE_BUS		1
#define VLINE_LOOP		2
#define VLINE_LINK		3
#define MAX_VLINE_OPTION	3

const char *VLINE_MODES[] = {
	"none",
	"bus",
	"loop",
	"link"
};

#define ISDN_P_MAX ISDN_P_NT_E1
char *ISDN_P_TEXT[] = {
	"ISDN_P_NONE",
	"ISDN_P_TE_S0",
	"ISDN_P_NT_S0",
	"ISDN_P_TE_E1",
	"ISDN_P_NT_E1",
	"<unknown/illegal>"
};

/* virtual bus states */
#define VBUS_ACTIVE	1
#define VBUS_INACTIVE	0


struct hwskel;

struct port {
	spinlock_t	lock; /* port lock */
	int		instance;
	char		name[MISDN_MAX_IDLEN];
	struct dchannel	dch;
	struct bchannel	*bch;
	int		nrbchan;
	__u8		protocol;
	__u8		initdone;
	struct hwskel	*hw;
};

struct l1loop {
	struct list_head	list;
	struct port		*ports;
};

#endif /* __L1LOOP_H__ */
