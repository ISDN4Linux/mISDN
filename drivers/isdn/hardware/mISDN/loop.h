/*

 * see notice in loop.c
 */

#define DEBUG_LOOP_MSG	0x0001
#define DEBUG_LOOP_INIT 0x0004
#define DEBUG_LOOP_MGR	0x0008

#define MAX_FRAME_SIZE	2048

#define LOOP_CHANNELS 128

struct misdn_loop {
	struct list_head	list;
	char		name[32];
	int		idx;	/* chip index for module parameters */
	int		id;	/* chip number starting with 1 */

	spinlock_t	lock;	/* the lock */

	channel_t	*dch;
	channel_t	*bch[LOOP_CHANNELS];
};

typedef struct misdn_loop	loop_t;


