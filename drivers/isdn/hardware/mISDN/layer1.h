/* $Id: layer1.h,v 0.4 2001/03/03 08:07:30 kkeil Exp $
 *
 * Layer 1 defines
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#include <linux/hisaxif.h>
#include "fsm.h"
#ifdef MEMDBG
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

#define B_RCVBUFREADY 0
#define B_XMTBUFREADY 1

#define FLG_L1_ACTIVATING	1
#define FLG_L1_ACTIVATED	2
#define FLG_L1_DEACTTIMER	3
#define FLG_L1_ACTTIMER		4
#define FLG_L1_T3RUN		5
#define FLG_L1_PULL_REQ		6
#define FLG_L1_UINT		7
#define FLG_L1_DBLOCKED		8
