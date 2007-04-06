/*
 * see notice in l1oip.c
 */

/* debugging */
#define DEBUG_L1OIP_INIT	0x0001
#define DEBUG_L1OIP_SOCKET	0x0002
#define DEBUG_L1OIP_MGR		0x0004
#define DEBUG_L1OIP_MSG		0x0008


/* frames */
#define L1OIP_MAX_LEN		2048		/* maximum packet size form layer 2 */
#define L1OIP_MAX_PERFRAME	1400		/* maximum data size in one frame */


/* timers */
#define L1OIP_KEEPALIVE		15
#define L1OIP_TIMEOUT		65


/* socket */
#define L1OIP_DEFAULTPORT	931


/* channel structure */
struct l1oip_chan {
	channel_t       	*ch;
	u16			counter;	/* counts bytes/packets */
};


/* card structure */
struct _l1oip_t {
	struct list_head        list;

	/* card */
	char			name[32];
	spinlock_t		dummylock;	/* for future use */
	int			idx;		/* card index */
	int			pri;		/* 1=pri, 0=bri */
	int			dch;		/* current dchannel number */
	int			numbch;		/* number of bchannels */
	u32			id;		/* id of connection */
	int			ondemand;	/* if transmission is on demand */
	int			bundle;		/* bundle channels in one frame */
	int			codec;		/* codec to use for transmission */
	int			limit;		/* limit number of bchannels */

	/* timer */
	struct timer_list 	keep_tl;
	struct timer_list 	timeout_tl;
	
	/* socket */
	struct socket 		*socket;	/* if set, socket is created */
	struct completion 	socket_complete;/* completion of socket thread */
	int			socket_pid;
	spinlock_t 		socket_lock;	/* lock before access socket outside socket thread */	
	u8			remoteip[4];	/* if all set, ip is assigned */
	u16	 		remoteport;	/* must always be set */
	struct sockaddr_in	sin_local;	/* local socket name */
	struct sockaddr_in	sin_remote;	/* remote socket name */
	struct msghdr		sendmsg;	/* ip message to send */
	struct iovec		sendiov;	/* iov for message */

	/* frame */
	struct l1oip_chan	chan[32];	/* channel instances */
};

typedef struct _l1oip_t		l1oip_t;

