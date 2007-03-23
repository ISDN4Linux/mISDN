#ifndef __mISDNdsp_H__
#define __mISDNdsp_H__

typedef struct _mISDN_dsp_element {
	void* (* new)        (const char *arg);
	void  (* free)       (void *p);
	void  (* process_tx) (void *p, u8 *data, int len);
	void  (* process_rx) (void *p, u8 *data, int len);
	char  name[];
} mISDN_dsp_element_t;

extern int  mISDN_dsp_element_register   (mISDN_dsp_element_t *elem);
extern void mISDN_dsp_element_unregister (mISDN_dsp_element_t *elem);

#endif

