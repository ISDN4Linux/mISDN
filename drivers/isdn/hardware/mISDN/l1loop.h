/*
 * hwskel.h
 */

#ifndef __L1LOOP_H__
#define __L1LOOP_H__


#define DRIVER_NAME "l1loop"

/* hfcsusb Layer1 commands */
#define L1_ACTIVATE_TE		1
#define L1_ACTIVATE_NT		2
#define L1_DEACTIVATE_NT	3

/* virtual ISDN line modes */
#define VLINE_NONE	0
#define VLINE_BUS	1
#define VLINE_LOOP	2

/* virtual BUS states */
#define VBUS_ACTIVE	1
#define VBUS_INACTIVE	0

struct hwskel;

struct port {
	spinlock_t	lock;
	char		name[MISDN_MAX_IDLEN];
	struct dchannel	dch;
	struct bchannel	*bch;
	int		nt_timer;
	__u8		portmode;
	__u8		timers;
	__u8		initdone;
	__u8		opened;
	struct hwskel	*hw;
};

struct l1loop {
	spinlock_t		lock;
	struct list_head	list;
	struct port		*ports;
};

#endif /* __L1LOOP_H__ */
