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
	D_RX = 1<<0,
	D_TX = 1<<1,
	L1   = 1<<2,
	L2   = 1<<3,
};

typedef struct mISDN_dt_header {
	unsigned char version;
	unsigned char type;
	unsigned short reserved;
	unsigned int stack_id;
	struct timespec time;
	unsigned int plength;
} mISDN_dt_header_t;

#endif

