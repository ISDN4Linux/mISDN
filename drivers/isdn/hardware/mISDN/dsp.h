/* $Id: dsp.h,v 1.2 2003/11/09 09:43:10 keil Exp $
 *
 * Audio support data for ISDN4Linux.
 *
 * Copyright 2002/2003 by Andreas Eversberg (jolly@jolly.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

/* compile using hardware features (if supported by hardware) */
//#define WITH_HARDWARE

#define DEBUG_DSP_MGR		0x0001
#define DEBUG_DSP_CORE		0x0002
#define DEBUG_DSP_DTMF		0x0004
#define DEBUG_DSP_DTMFCOEFF	0x0008
#define DEBUG_DSP_CMX		0x0010
#define DEBUG_DSP_TONE		0x0020

/* options may be:
 *
 * bit 0 = use ulaw instead of alaw
 * bit 1 = disable hfc hardware accelleration
 *
 */
#define DSP_OPT_ULAW		(1<<0)
#define DSP_OPT_NOHARDWARE	(1<<1)
//#define DSP_OPT_NOFLIP		(1<<2)

#ifdef HAS_WORKQUEUE
#include <linux/workqueue.h>
#else
#include <linux/tqueue.h>
#endif

extern int options;

/***************
 * audio stuff *
 ***************/

extern signed long dsp_audio_alaw_to_s32[256];
extern signed long dsp_audio_ulaw_to_s32[256];
extern signed long *dsp_audio_law_to_s32;
extern unsigned char dsp_audio_s16_to_law[65536];
extern unsigned char dsp_audio_alaw_to_ulaw[256];
extern unsigned char dsp_audio_mix_law[65536];
extern void dsp_audio_generate_s2law_table(void);
extern void dsp_audio_generate_mix_table(void);
extern void dsp_audio_generate_ulaw_samples(void);
extern void dsp_audio_generate_volume_changes(void);
extern unsigned char silence;


/*************
 * cmx stuff *
 *************/

#define CMX_BUFF_SIZE	0x4000	/* must be 2**n */
#define CMX_BUFF_HALF	0x2000	/* CMX_BUFF_SIZE / 2 */
#define CMX_BUFF_MASK	0x3fff	/* CMX_BUFF_SIZE - 1 */

// jolly patch start
#define SEND_LEN	64	/* initial chunk length for mixed data to card */
// jolly patch stop

/* the structure of conferences:
 *
 * each conference has a unique number, given by user space.
 * the conferences are linked in a chain.
 * each conference has members linked in a chain.
 * each dsplayer points to a member, each member points to a dsplayer.
 */

/* all members within a conference (this is linked 1:1 with the dsp) */
struct _dsp;
typedef struct _conf_member {
	struct _conf_member *prev;
	struct _conf_member *next;
	struct _dsp	*dsp;
} conf_member_t;

/* the list of all conferences */
typedef struct _conference {
	struct _conference *prev;
	struct _conference *next;
	u_int		id; /* all cmx stacks with the same ID are connected */
	conf_member_t	*mlist;
	int		solution; /* currently connected via -1=software 0=hardware 1-8=conference unit (hardware is only possible on the same chip) */
	u_int		hfc_id; /* unique id to identify the chip */
	int		largest; /* largest frame received in conf's life. */
	int		W_min, W_max; /* min/maximum rx-write pointer of members */
	signed long	conf_buff[CMX_BUFF_SIZE];
} conference_t;

extern mISDNobject_t dsp_obj;


/**************
 * DTMF stuff *
 **************/

#define DSP_DTMF_NPOINTS 102

typedef struct _dmtf_t {
	int		software; /* dtmf uses software decoding */
	int 		hardware; /* dtmf uses hardware decoding */
	int 		size; /* number of bytes in buffer */
	signed short	buffer[DSP_DTMF_NPOINTS]; /* buffers one full dtmf frame */
	unsigned char	lastwhat, lastdigit;
	int		count;
	unsigned char	digits[16]; /* just the dtmf result */
} dtmf_t;


/***************
 * tones stuff *
 ***************/

typedef struct _tone_t {
	int tone;
	void *pattern;
	int count;
	int index;
} tone_t;


/*****************
 * general stuff *
 *****************/

typedef struct _dsp {
	struct _dsp	*prev;
	struct _dsp	*next;
	int		debug;
	u_int		hfc_id; /* unique id to identify the chip (or 0) */
	mISDNinstance_t	inst;
	int		largest; /* largest frame received in dsp's life. */
	int		b_active;
	int		tx_pending;
	int		conf_id;
	int		echo;
	int		rx_disabled;
	int		tx_mix;
	conference_t	*conf;
	conf_member_t	*member;
	tone_t		tone;
	dtmf_t		dtmf;
	int		tx_volume, rx_volume;
	struct work_struct sendwork; /* event for sending data */
	int		R_tx, W_tx; /* pointers of transmit buffer */
	int		R_rx, W_rx; /* pointers of receive buffer and conference buffer */
	unsigned char	tx_buff[CMX_BUFF_SIZE];
	unsigned char	rx_buff[CMX_BUFF_SIZE];
} dsp_t;



extern void dsp_change_volume(struct sk_buff *skb, int volume);

extern conference_t *Conf_list;
extern void dsp_cmx_debug(dsp_t *dsp);
extern int dsp_cmx(dsp_t *dsp);
extern void dsp_cmx_receive(dsp_t *dsp, struct sk_buff *skb);
extern struct sk_buff *dsp_cmx_send(dsp_t *dsp, int len, int dinfo);
extern void dsp_cmx_transmit(dsp_t *dsp, struct sk_buff *skb);
extern int dsp_cmx_del_conf_member(dsp_t *dsp);
extern int dsp_cmx_del_conf(conference_t *conf);

extern void dsp_dtmf_goertzel_init(dsp_t *dsp);
extern unsigned char *dsp_dtmf_goertzel_decode(dsp_t *dsp, unsigned char *data, int len, int fmt);

extern int dsp_tone(dsp_t *dsp, int tone);
extern void dsp_tone_copy(dsp_t *dsp, unsigned char *data, int len);

