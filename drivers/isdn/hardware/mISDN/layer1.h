/* $Id: layer1.h,v 1.4 2004/01/28 11:34:46 keil Exp $
 *
 * Layer 1 defines
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/mISDNif.h>
#include "fsm.h"
#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#endif

#define D_RCVBUFREADY	0
#define D_XMTBUFREADY	1
#define D_L1STATECHANGE	2
#define D_CLEARBUSY	3
#define D_RX_MON0	4
#define D_RX_MON1	5
#define D_TX_MON0	6
#define D_TX_MON1	7
#define D_BLOCKEDATOMIC	8
#define D_LOS		9
#define D_LOS_OFF	10
#define D_AIS		11
#define D_AIS_OFF	12
#define D_SLIP_TX	13
#define D_SLIP_RX	14

#define B_RCVBUFREADY	0
#define B_XMTBUFREADY	1
#define B_BLOCKEDATOMIC	2
#define B_DTMFREADY	3

#define FLG_L1_ACTIVATING	1
#define FLG_L1_ACTIVATED	2
#define FLG_L1_DEACTTIMER	3
#define FLG_L1_ACTTIMER		4
#define FLG_L1_T3RUN		5
#define FLG_L1_PULL_REQ		6
#define FLG_L1_UINT		7
#define FLG_L1_DBLOCKED		8

/* L1 Debug */
#define	L1_DEB_WARN		0x01
#define	L1_DEB_INTSTAT		0x02
#define	L1_DEB_ISAC		0x04
#define	L1_DEB_ISAC_FIFO	0x08
#define	L1_DEB_HSCX		0x10
#define	L1_DEB_HSCX_FIFO	0x20
#define	L1_DEB_LAPD	        0x40
#define	L1_DEB_IPAC	        0x80
#define	L1_DEB_RECEIVE_FRAME    0x100
#define L1_DEB_MONITOR		0x200
#define DEB_DLOG_HEX		0x400
#define DEB_DLOG_VERBOSE	0x800

