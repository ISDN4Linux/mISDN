#ifndef __mISDNdsp_H__
#define __mISDNdsp_H__

typedef struct _mISDN_dsp_element_arg {
	char * name;
	char * def;
	char * desc;
} mISDN_dsp_element_arg_t;

typedef struct _mISDN_dsp_element {
	char   * name;
	void* (* new)        (const char *arg);
	void  (* free)       (void *p);
	void  (* process_tx) (void *p, unsigned char *data, int len);
	void  (* process_rx) (void *p, unsigned char *data, int len);
	int                       num_args;
	mISDN_dsp_element_arg_t * args;
} mISDN_dsp_element_t;

extern int  mISDN_dsp_element_register   (mISDN_dsp_element_t *elem);
extern void mISDN_dsp_element_unregister (mISDN_dsp_element_t *elem);

struct dsp_features {
	int		hfc_id; /* unique id to identify the chip (or -1) */
	int		hfc_dtmf; /* set if HFCmulti card supports dtmf */
	int		hfc_loops; /* set if card supports tone loops */
	int		hfc_echocanhw; /* set if card supports echocancelation*/
	int		pcm_id; /* unique id to identify the pcm bus (or -1) */
	int		pcm_slots; /* number of slots on the pcm bus */
	int		pcm_banks; /* number of IO banks of pcm bus */
	int		has_jitter; /* data is jittered and unsorted */
};		

#endif

