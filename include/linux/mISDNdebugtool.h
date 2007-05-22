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
	D_RX = 1,
	D_TX,
	L1_UP,
	L1_DOWN,
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

