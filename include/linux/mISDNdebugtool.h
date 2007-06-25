#ifndef __mISDNdebugtool_H__
#define __mISDNdebugtool_H__

/*
 * 0        8        16       24      31
 * +--------+--------+--------+--------+
 * |  vers  |  type  |    reserved     +
 * +--------+--------+--------+--------+
 * |         stack identifier          +
 * +--------+--------+--------+--------+
 * |        seconds since epoch        |
 * +--------+--------+--------+--------+
 * |            nanoseconds            |
 * +--------+--------+--------+--------+
 * |          payload length           |
 * +--------+--------+--------+--------+
 */

enum mISDN_dt_type {

	D_RX = 1,  /* payload: copy of dchannel payload */
	D_TX,      /* payload: copy of dchannel payload */

	L1_UP,     /* no payload */
	L1_DOWN,   /* no payload */

	CRC_ERR,   /* no payload */

	NEWSTATE,  /* payload: state-id (uint) :: message (NULL-terminated charstring)
	              thrown by: hfcmulti */
};

typedef struct mISDN_dt_header {
	unsigned char version;
	unsigned char type;
	unsigned short reserved;
	unsigned int stack_id;
	unsigned int stack_protocol;
	struct timespec time;
	unsigned int plength;
} mISDN_dt_header_t;

#endif

