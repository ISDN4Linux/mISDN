/* $Id: dsp.h,v 1.7 2005/10/26 14:12:13 keil Exp $
 *
 * Audio support data for ISDN4Linux.
 *
 * Copyright 2002/2003 by Andreas Eversberg (jolly@jolly.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

//#define AUTOJITTER

#define DEBUG_DSP_MGR		0x0001
#define DEBUG_DSP_CORE		0x0002
#define DEBUG_DSP_DTMF		0x0004
#define DEBUG_DSP_DTMFCOEFF	0x0008
#define DEBUG_DSP_CMX		0x0010
#define DEBUG_DSP_TONE		0x0020
#define DEBUG_DSP_BLOWFISH	0x0040
#define DEBUG_DSP_DELAY		0x0080

/* options may be:
 *
 * bit 0 = use ulaw instead of alaw
 * bit 1 = enable hfc hardware accelleration for all channels
 *
 */
#define DSP_OPT_ULAW		(1<<0)
#define DSP_OPT_NOHARDWARE	(1<<1)

#ifdef HAS_WORKQUEUE
#include <linux/workqueue.h>
#else
#include <linux/tqueue.h>
#endif
#include <linux/timer.h>

#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#endif

extern int dsp_options;
extern int dsp_debug;

/***************
 * audio stuff *
 ***************/

extern s32 dsp_audio_alaw_to_s32[256];
extern s32 dsp_audio_ulaw_to_s32[256];
extern s32 *dsp_audio_law_to_s32;
extern u8 dsp_audio_s16_to_law[65536];
extern u8 dsp_audio_alaw_to_ulaw[256];
extern u8 dsp_audio_mix_law[65536];
extern u8 dsp_audio_seven2law[128];
extern u8 dsp_audio_law2seven[256];
extern void dsp_audio_generate_s2law_table(void);
extern void dsp_audio_generate_seven(void);
extern void dsp_audio_generate_mix_table(void);
extern void dsp_audio_generate_ulaw_samples(void);
extern void dsp_audio_generate_volume_changes(void);
extern u8 dsp_silence;


/*************
 * cmx stuff *
 *************/

#define CMX_BUFF_SIZE	0x4000	/* must be 2**n */
#define CMX_BUFF_HALF	0x2000	/* CMX_BUFF_SIZE / 2 */
#define CMX_BUFF_MASK	0x3fff	/* CMX_BUFF_SIZE - 1 */

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
	struct list_head	list;
	struct _dsp		*dsp;
} conf_member_t;

/* the list of all conferences */
typedef struct _conference {
	struct list_head	list;
	u32			id; /* all cmx stacks with the same ID are connected */
	struct list_head	mlist;
	int			software; /* conf is processed by software */
	int			hardware; /* conf is processed by hardware */
//#ifndef AUTOJITTER
	int			largest; /* largest frame received in conf's life. */
//#endif
	int			W_min, W_max; /* min/maximum rx-write pointer of members */
	s32			conf_buff[CMX_BUFF_SIZE];
} conference_t;

extern mISDNobject_t dsp_obj;


/**************
 * DTMF stuff *
 **************/

#define DSP_DTMF_NPOINTS 102

typedef struct _dtmf_t {
	int		software; /* dtmf uses software decoding */
	int 		hardware; /* dtmf uses hardware decoding */
	int 		size; /* number of bytes in buffer */
	signed short	buffer[DSP_DTMF_NPOINTS]; /* buffers one full dtmf frame */
	u8		lastwhat, lastdigit;
	int		count;
	u8		digits[16]; /* just the dtmf result */
} dtmf_t;


/***************
 * tones stuff *
 ***************/

typedef struct _tone_t {
	int		software; /* tones are generated by software */
	int 		hardware; /* tones are generated by hardware */
	int		tone;
	void		*pattern;
	int		count;
	int		index;
	struct timer_list tl;
} tone_t;

/*****************
 * general stuff *
 *****************/

#define DELAY_CHECK 8000

struct dsp_features {
	int		hfc_id; /* unique id to identify the chip (or -1) */
	int		hfc_dtmf; /* set if HFCmulti card supports dtmf */
	int		hfc_loops; /* set if card supports tone loops */
	int		pcm_id; /* unique id to identify the pcm bus (or -1) */
	int		pcm_slots; /* number of slots on the pcm bus */
	int		pcm_banks; /* number of IO banks of pcm bus */
};		

typedef struct _dsp {
	struct list_head list;
	mISDNinstance_t	inst;
	int		b_active;
	int		echo; /* echo is done by software */
	int		rx_disabled;
	int		tx_mix;
	tone_t		tone;
	dtmf_t		dtmf;
	int		tx_volume, rx_volume;
	struct work_struct sendwork; /* event for sending data */
	int		tx_pending;

	/* conference stuff */
	u32		conf_id;
	conference_t	*conf;
	conf_member_t	*member;

	/* buffer stuff */
	int		largest; /* largest frame received in dsp's life. */
	int		R_tx, W_tx; /* pointers of transmit buffer */
	int		R_rx, W_rx; /* pointers of receive buffer and conference buffer */
	u8		tx_buff[CMX_BUFF_SIZE];
	u8		rx_buff[CMX_BUFF_SIZE];
#ifdef AUTOJITTER
	int		tx_delay; /* used to find the delay of tx buffer */
	int		tx_delay_count;
	int		rx_delay; /* used to find the delay of rx buffer */
	int		rx_delay_count;
#endif

	/* hardware stuff */
	struct dsp_features features; /* features */
	int		pcm_slot_rx; /* current PCM slot (or -1) */
	int		pcm_bank_rx;
	int		pcm_slot_tx;
	int		pcm_bank_tx;
	int		hfc_conf; /* unique id of current conference (or -1) */

	/* encryption stuff */
	int		bf_enable;
	u32		bf_p[18];
	u32		bf_s[1024];
	int		bf_crypt_pos;
	u8		bf_data_in[9];
	u8		bf_crypt_out[9];
	int		bf_decrypt_in_pos;
	int		bf_decrypt_out_pos;
	u8		bf_crypt_inring[16];
	u8		bf_data_out[9];
	int		bf_sync;
} dsp_t;

/* functions */

extern void dsp_change_volume(struct sk_buff *skb, int volume);

extern struct list_head Conf_list;
extern void dsp_cmx_debug(dsp_t *dsp);
extern void dsp_cmx_hardware(conference_t *conf, dsp_t *dsp);
extern int dsp_cmx_conf(dsp_t *dsp, u32 conf_id);
extern void dsp_cmx_receive(dsp_t *dsp, struct sk_buff *skb);
extern struct sk_buff *dsp_cmx_send(dsp_t *dsp, int len, int dinfo);
extern void dsp_cmx_transmit(dsp_t *dsp, struct sk_buff *skb);
extern int dsp_cmx_del_conf_member(dsp_t *dsp);
extern int dsp_cmx_del_conf(conference_t *conf);

extern void dsp_dtmf_goertzel_init(dsp_t *dsp);
extern u8 *dsp_dtmf_goertzel_decode(dsp_t *dsp, u8 *data, int len, int fmt);

extern int dsp_tone(dsp_t *dsp, int tone);
extern void dsp_tone_copy(dsp_t *dsp, u8 *data, int len);
extern void dsp_tone_timeout(void *arg);

extern void dsp_bf_encrypt(dsp_t *dsp, u8 *data, int len);
extern void dsp_bf_decrypt(dsp_t *dsp, u8 *data, int len);
extern int dsp_bf_init(dsp_t *dsp, const u8 *key, unsigned int keylen);
extern void dsp_bf_cleanup(dsp_t *dsp);


