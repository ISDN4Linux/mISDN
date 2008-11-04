/*
 * hwskel.h
 */

#ifndef __HWSKEL_H__
#define __HWSKEL_H__


#define DRIVER_NAME "hwskel"

/* layer1 commands */
#define L1_ACTIVATE_TE		1
#define L1_ACTIVATE_NT		2
#define L1_DEACTIVATE_NT	3
#define L1_FORCE_DEACTIVATE_TE	4

/* timers */
#define NT_ACTIVATION_TIMER	0x01	/* enables NT mode activation Timer */
#define NT_T1_COUNT		10


struct hwskel;

struct port {
	spinlock_t	lock;
	char		name[MISDN_MAX_IDLEN];
	struct dchannel	dch;
	struct bchannel	bch[2];
	int		nt_timer;
	__u8		protocol;
	__u8		timers;
	__u8		initdone;
	struct hwskel	*hw;
};

struct hwskel {
	spinlock_t		lock;
	struct list_head	list;
	struct port		*ports;
};

#endif /* __HWSKEL_H__ */
