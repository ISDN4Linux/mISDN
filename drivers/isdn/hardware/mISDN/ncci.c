/* $Id: ncci.c,v 1.23 2004/01/26 22:21:30 keil Exp $
 *
 */

#include "m_capi.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"
#include "mISDNManufacturer.h"

static int	ncciL4L3(Ncci_t *, u_int, int, int, void *, struct sk_buff *);

static char	logbuf[8000];

void
log_skbdata(struct sk_buff *skb)
{
	char *t = logbuf;

	t += sprintf(t, "skbdata(%d):", skb->len);
	mISDN_QuickHex(t, skb->data, skb->len);
	printk(KERN_DEBUG "%s\n", logbuf);
}

// --------------------------------------------------------------------
// NCCI state machine
//
// Some rules:
//   *  EV_AP_*  events come from CAPI Application
//   *  EV_DL_*  events come from the ISDN stack
//   *  EV_NC_*  events generated in NCCI handling
//   *  messages are send in the routine that handle the event
//
// --------------------------------------------------------------------
enum {
	ST_NCCI_N_0,
	ST_NCCI_N_0_1,
	ST_NCCI_N_1,
	ST_NCCI_N_2,
	ST_NCCI_N_ACT,
	ST_NCCI_N_3,
	ST_NCCI_N_4,
	ST_NCCI_N_5,
}

const ST_NCCI_COUNT = ST_NCCI_N_5 + 1;

static char *str_st_ncci[] = {
	"ST_NCCI_N_0",
	"ST_NCCI_N_0_1",
	"ST_NCCI_N_1",
	"ST_NCCI_N_2",
	"ST_NCCI_N_ACT",
	"ST_NCCI_N_3",
	"ST_NCCI_N_4",
	"ST_NCCI_N_5",
}; 

enum {
	EV_AP_CONNECT_B3_REQ,
	EV_NC_CONNECT_B3_CONF,
	EV_NC_CONNECT_B3_IND,
	EV_AP_CONNECT_B3_RESP,
	EV_NC_CONNECT_B3_ACTIVE_IND,
	EV_AP_CONNECT_B3_ACTIVE_RESP,
	EV_AP_RESET_B3_REQ,
	EV_NC_RESET_B3_IND,
	EV_NC_RESET_B3_CONF,
	EV_AP_RESET_B3_RESP,
	EV_NC_CONNECT_B3_T90_ACTIVE_IND,
	EV_AP_DISCONNECT_B3_REQ,
	EV_NC_DISCONNECT_B3_IND,
	EV_NC_DISCONNECT_B3_CONF,
	EV_AP_DISCONNECT_B3_RESP,
	EV_AP_FACILITY_REQ,
	EV_AP_MANUFACTURER_REQ,
	EV_DL_ESTABLISH_IND,
	EV_DL_ESTABLISH_CONF,
	EV_DL_RELEASE_IND,
	EV_DL_RELEASE_CONF,
	EV_DL_DOWN_IND,
	EV_NC_LINKDOWN,
	EV_AP_RELEASE,
}

const EV_NCCI_COUNT = EV_AP_RELEASE + 1;

static char* str_ev_ncci[] = {
	"EV_AP_CONNECT_B3_REQ",
	"EV_NC_CONNECT_B3_CONF",
	"EV_NC_CONNECT_B3_IND",
	"EV_AP_CONNECT_B3_RESP",
	"EV_NC_CONNECT_B3_ACTIVE_IND",
	"EV_AP_CONNECT_B3_ACTIVE_RESP",
	"EV_AP_RESET_B3_REQ",
	"EV_NC_RESET_B3_IND",
	"EV_NC_RESET_B3_CONF",
	"EV_AP_RESET_B3_RESP",
	"EV_NC_CONNECT_B3_T90_ACTIVE_IND",
	"EV_AP_DISCONNECT_B3_REQ",
	"EV_NC_DISCONNECT_B3_IND",
	"EV_NC_DISCONNECT_B3_CONF",
	"EV_AP_DISCONNECT_B3_RESP",
	"EV_AP_FACILITY_REQ",
	"EV_AP_MANUFACTURER_REQ",
	"EV_DL_ESTABLISH_IND",
	"EV_DL_ESTABLISH_CONF",
	"EV_DL_RELEASE_IND",
	"EV_DL_RELEASE_CONF",
	"EV_DL_DOWN_IND",
	"EV_NC_LINKDOWN",
	"EV_AP_RELEASE",
};

static struct Fsm ncci_fsm = { 0, 0, 0, 0, 0 };
static struct Fsm ncciD_fsm = { 0, 0, 0, 0, 0 };


static int
select_NCCIaddr(Ncci_t *ncci) {
	__u32	addr;
	Ncci_t	*test;

	if (!ncci->AppPlci)
		return(-EINVAL);
	addr = 0x00010000 | ncci->AppPlci->addr;
	while (addr < 0x00ffffff) { /* OK not more as 255 NCCI */
		test = getNCCI4addr(ncci->AppPlci, addr, GET_NCCI_EXACT);
		if (!test) {
			ncci->addr = addr;
#ifdef OLDCAPI_DRIVER_INTERFACE
			ncci->contr->ctrl->new_ncci(ncci->contr->ctrl, ncci->appl->ApplId, ncci->addr, ncci->window);
#endif
			return(0);
		}
		addr += 0x00010000;
	}
	ncci->addr = ncci->AppPlci->addr;
	return(-EBUSY);
}

static void
ncci_debug(struct FsmInst *fi, char *fmt, ...)
{
	char tmp[128];
	char *p = tmp;
	va_list args;
	Ncci_t *ncci = fi->userdata;
	
	if (!ncci->ncci_m.debug)
		return;
	va_start(args, fmt);
	p += sprintf(p, "NCCI 0x%x: ", ncci->addr);
	p += vsprintf(p, fmt, args);
	*p++ = '\n';
	*p = 0;
	printk(KERN_DEBUG "%s", tmp);
	va_end(args);
}

static inline void
SKB2Application(Ncci_t *ncci, struct sk_buff *skb)
{
	if (!test_bit(NCCI_STATE_APPLRELEASED, &ncci->state)) {
#ifdef OLDCAPI_DRIVER_INTERFACE
		ncci->contr->ctrl->handle_capimsg(ncci->contr->ctrl, ncci->appl->ApplId, skb);
#else
		capi_ctr_handle_message(ncci->contr->ctrl, ncci->appl->ApplId, skb);
#endif
	}
}

static inline int
SKB_l4l3(Ncci_t *ncci, struct sk_buff *skb)
{
	if (!ncci->link || !ncci->link->inst.down.func)
		return(-ENXIO);
	return(ncci->link->inst.down.func(&ncci->link->inst.down, skb));
}

static inline void
Send2Application(Ncci_t *ncci, _cmsg *cmsg)
{
	SendCmsg2Application(ncci->appl, cmsg);
}

static inline void
ncciCmsgHeader(Ncci_t *ncci, _cmsg *cmsg, __u8 cmd, __u8 subcmd)
{
	capi_cmsg_header(cmsg, ncci->appl->ApplId, cmd, subcmd, 
			 ncci->appl->MsgId++, ncci->addr);
}

static void
ncci_connect_b3_req(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg = arg;

	// FIXME
	if (!ncci->appl) {
		cmsg_free(cmsg);
		return;
	}
	mISDN_FsmChangeState(fi, ST_NCCI_N_0_1);
	capi_cmsg_answer(cmsg);

	// TODO: NCPI handling
	/* We need a real addr now */
	if (0xffff0000 & ncci->addr) {
		int_error();
		cmsg->Info = CapiNoNcciAvailable;
		ncci->addr = ncci->AppPlci->addr;
	} else {
		cmsg->Info = 0;
		if (select_NCCIaddr(ncci)) {
			int_error();
			cmsg->Info = CapiNoNcciAvailable;
		}
	}
	cmsg->adr.adrNCCI = ncci->addr;
	ncci_debug(fi, "ncci_connect_b3_req NCCI %x cmsg->Info(%x)",
		ncci->addr, cmsg->Info);
	if (mISDN_FsmEvent(fi, EV_NC_CONNECT_B3_CONF, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_connect_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	// from DL_ESTABLISH
	mISDN_FsmChangeState(fi, ST_NCCI_N_1);
	Send2Application(fi->userdata, arg);
}

static void
ncci_connect_b3_resp(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t *ncci = fi->userdata;
	_cmsg *cmsg = arg;
  
	// FIXME
	if (!ncci->appl) {
		cmsg_free(cmsg);
		return;
	}
	if (cmsg->Info == 0) {
		mISDN_FsmChangeState(fi, ST_NCCI_N_2);
		ncciCmsgHeader(ncci, cmsg, CAPI_CONNECT_B3_ACTIVE, CAPI_IND);
		event = EV_NC_CONNECT_B3_ACTIVE_IND;
	} else {
		mISDN_FsmChangeState(fi, ST_NCCI_N_4);
		cmsg->Info = 0;
		ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
		event = EV_NC_DISCONNECT_B3_IND;
	}
	if (mISDN_FsmEvent(&ncci->ncci_m, event, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_connect_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	_cmsg	*cmsg = arg;
  
	if (cmsg->Info == 0) {
		mISDN_FsmChangeState(fi, ST_NCCI_N_2);
		Send2Application(fi->userdata, cmsg);
		ncciL4L3(fi->userdata, DL_ESTABLISH | REQUEST, 0, 0, NULL, NULL);
	} else {
		mISDN_FsmChangeState(fi, ST_NCCI_N_0);
		Send2Application(fi->userdata, cmsg);
		ncciDestr(fi->userdata);
	}
}

static void
ncci_disconnect_b3_req(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg = arg;
	__u16	Info = 0;

	if (ncci->appl) { //FIXME
		/* TODO: handle NCPI and wait for all DATA_B3_REQ confirmed on
		 * related protocols (voice, T30)
		 */ 
		capi_cmsg_answer(cmsg);
		cmsg->Info = Info;
		if (mISDN_FsmEvent(fi, EV_NC_DISCONNECT_B3_CONF, cmsg))
			cmsg_free(cmsg);
	} else {
		cmsg_free(cmsg);
		mISDN_FsmChangeState(fi, ST_NCCI_N_4);
	}
	ncciL4L3(ncci, DL_RELEASE | REQUEST, 0, 0, NULL, NULL);
}

static void
ncci_disconnect_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	_cmsg	*cmsg = arg;

	if (cmsg->Info == 0) {
		mISDN_FsmChangeState(fi, ST_NCCI_N_4);
	}
	Send2Application(fi->userdata, cmsg);
}

static void
ncci_disconnect_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;

	mISDN_FsmChangeState(fi, ST_NCCI_N_5);
	if (ncci->appl) { // FIXME
		Send2Application(ncci, arg);
	} else {
		cmsg_free(arg);
		mISDN_FsmChangeState(fi, ST_NCCI_N_0);
		ncciDestr(ncci);
	}
}

static void
ncci_disconnect_b3_resp(struct FsmInst *fi, int event, void *arg)
{
	if (arg)
		cmsg_free(arg);
	mISDN_FsmChangeState(fi, ST_NCCI_N_0);
	ncciDestr(fi->userdata);
}

static void
ncci_facility_req(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg = arg;
	u_char	*p = cmsg->FacilityRequestParameter;
	u16	func;
	int	op;

	if (!ncci->appl)
		return;
	capi_cmsg_answer(cmsg);
	cmsg->Info = CAPI_NOERROR;
	if (cmsg->FacilitySelector == 0) { // Handset
		int err = ncciL4L3(ncci, PH_CONTROL | REQUEST, HW_POTS_ON, 0, NULL, NULL);
		if (err)
			cmsg->Info = CapiFacilityNotSupported;
	} else if (cmsg->FacilitySelector != 1) { // not DTMF
		cmsg->Info = CapiIllMessageParmCoding;
	} else if (p && p[0]) {
		func = CAPIMSG_U16(p, 1);
		ncci_debug(fi, "%s: p %02x %02x %02x func(%x)",
			__FUNCTION__, p[0], p[1], p[2], func);
		switch (func) {
			case 1:
				op = DTMF_TONE_START;
				ncciL4L3(ncci, PH_CONTROL | REQUEST, 0, sizeof(int), &op, NULL);
				break;
			case 2:
				op = DTMF_TONE_STOP;
				ncciL4L3(ncci, PH_CONTROL | REQUEST, 0, sizeof(int), &op, NULL);
				break;
			default:
				cmsg->Info = CapiFacilityNotSupported;
				break;
		}
	} else
		cmsg->Info = CapiIllMessageParmCoding;
		
	Send2Application(ncci, cmsg);
}

static void
ncci_manufacturer_req(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg = arg;
	int	err, op;
	struct  _manu_conf_para {
			u8	len	__attribute__((packed));
			u16	Info	__attribute__((packed));
			u16	vol	__attribute__((packed));
		} mcp = {2, CAPI_NOERROR,0};
	struct  _manu_req_para {
			u8	len	__attribute__((packed));
			u16	vol	__attribute__((packed));
		} *mrp;

	if (!ncci->appl)
		return;
	mrp = (struct  _manu_req_para *)cmsg->ManuData;
	capi_cmsg_answer(cmsg);
	if (cmsg->Class == mISDN_MF_CLASS_HANDSET) { // Handset
		switch(cmsg->Function) {
			case mISDN_MF_HANDSET_ENABLE:
				err = ncciL4L3(ncci, PH_CONTROL | REQUEST, HW_POTS_ON, 0, NULL, NULL);
				if (err)
					mcp.Info = CapiFacilityNotSupported;
				break;
			case mISDN_MF_HANDSET_DISABLE:
				err = ncciL4L3(ncci, PH_CONTROL | REQUEST, HW_POTS_OFF, 0, NULL, NULL);
				if (err)
					mcp.Info = CapiFacilityNotSupported;
				break;
			case mISDN_MF_HANDSET_SETMICVOLUME:
			case mISDN_MF_HANDSET_SETSPKVOLUME:
				if (!mrp || mrp->len != 2) {
					mcp.Info = CapiIllMessageParmCoding;
					break;
				}
				op = (cmsg->Function == mISDN_MF_HANDSET_SETSPKVOLUME) ?
					HW_POTS_SETSPKVOL : HW_POTS_SETMICVOL;
				err = ncciL4L3(ncci, PH_CONTROL | REQUEST, op, 2, &mrp->vol, NULL);
				if (err == -ENODEV)
					mcp.Info = CapiFacilityNotSupported;
				else if (err)
					mcp.Info = CapiIllMessageParmCoding;
				break;
			/* not handled yet */
			case mISDN_MF_HANDSET_GETMICVOLUME:
			case mISDN_MF_HANDSET_GETSPKVOLUME:
			default:
				mcp.Info = CapiFacilityNotSupported;
				break;
		}
	} else
		mcp.Info = CapiIllMessageParmCoding;

	cmsg->ManuData = (_cstruct)&mcp;
	Send2Application(ncci, cmsg);
}

static void
ncci_connect_b3_active_ind(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t *ncci = fi->userdata;
	int i;

	mISDN_FsmChangeState(fi, ST_NCCI_N_ACT);
	for (i = 0; i < CAPI_MAXDATAWINDOW; i++) {
		ncci->xmit_skb_handles[i].PktId = 0;
		ncci->recv_skb_handles[i] = 0;
	}
	Send2Application(ncci, arg);
}

static void
ncci_connect_b3_active_resp(struct FsmInst *fi, int event, void *arg)
{
	cmsg_free(arg);
}

static void
ncci_n0_dl_establish_ind_conf(struct FsmInst *fi, int event, void *arg)
{
	_cmsg   *cmsg;
	Ncci_t	*ncci = fi->userdata;

	if (!ncci->appl)
		return;
	if (!(0xffff0000 & ncci->addr)) {
		if (select_NCCIaddr(ncci)) {
			int_error();
			return;
		}
	} else {
		int_error();
		return;
	}
	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_CONNECT_B3, CAPI_IND);
	if (mISDN_FsmEvent(&ncci->ncci_m, EV_NC_CONNECT_B3_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_dl_establish_conf(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg;

	if (!ncci->appl)
		return;
	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_CONNECT_B3_ACTIVE, CAPI_IND);
	if (mISDN_FsmEvent(&ncci->ncci_m, EV_NC_CONNECT_B3_ACTIVE_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_dl_release_ind_conf(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
	if (mISDN_FsmEvent(&ncci->ncci_m, EV_NC_DISCONNECT_B3_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_linkdown(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
	if (mISDN_FsmEvent(&ncci->ncci_m, EV_NC_DISCONNECT_B3_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_dl_down_ind(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
	cmsg->Reason_B3 = CapiProtocolErrorLayer1;
	if (mISDN_FsmEvent(&ncci->ncci_m, EV_NC_DISCONNECT_B3_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_appl_release(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_0);
	ncciDestr(fi->userdata);
}

static void
ncci_appl_release_disc(struct FsmInst *fi, int event, void *arg)
{
	ncciL4L3(fi->userdata, DL_RELEASE | REQUEST, 0, 0, NULL, NULL);
}

static struct FsmNode fn_ncci_list[] =
{
  {ST_NCCI_N_0,		EV_AP_CONNECT_B3_REQ,		ncci_connect_b3_req},
  {ST_NCCI_N_0,		EV_NC_CONNECT_B3_IND,		ncci_connect_b3_ind},
  {ST_NCCI_N_0,		EV_DL_ESTABLISH_CONF,		ncci_n0_dl_establish_ind_conf},
  {ST_NCCI_N_0,		EV_DL_ESTABLISH_IND,		ncci_n0_dl_establish_ind_conf},
  {ST_NCCI_N_0,		EV_AP_RELEASE,			ncci_appl_release},

  {ST_NCCI_N_0_1,	EV_NC_CONNECT_B3_CONF,		ncci_connect_b3_conf},
  {ST_NCCI_N_0_1,	EV_AP_MANUFACTURER_REQ,		ncci_manufacturer_req},
  {ST_NCCI_N_0_1,	EV_AP_RELEASE,			ncci_appl_release},

  {ST_NCCI_N_1,		EV_AP_CONNECT_B3_RESP,		ncci_connect_b3_resp},
  {ST_NCCI_N_1,		EV_AP_DISCONNECT_B3_REQ,	ncci_disconnect_b3_req},
  {ST_NCCI_N_1,		EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_1,		EV_AP_MANUFACTURER_REQ,		ncci_manufacturer_req},
  {ST_NCCI_N_1,		EV_AP_RELEASE,			ncci_appl_release_disc},
  {ST_NCCI_N_1,		EV_NC_LINKDOWN,			ncci_linkdown},

  {ST_NCCI_N_2,		EV_NC_CONNECT_B3_ACTIVE_IND,	ncci_connect_b3_active_ind},
  {ST_NCCI_N_2,		EV_AP_DISCONNECT_B3_REQ,	ncci_disconnect_b3_req},
  {ST_NCCI_N_2,		EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_2,		EV_DL_ESTABLISH_CONF,		ncci_dl_establish_conf},
  {ST_NCCI_N_2,		EV_DL_RELEASE_IND,		ncci_dl_release_ind_conf},
  {ST_NCCI_N_2,		EV_AP_MANUFACTURER_REQ,		ncci_manufacturer_req},
  {ST_NCCI_N_2,		EV_AP_RELEASE,			ncci_appl_release_disc},
  {ST_NCCI_N_2,		EV_NC_LINKDOWN,			ncci_linkdown},
     
#if 0
  {ST_NCCI_N_3,		EV_NC_RESET_B3_IND,		ncci_reset_b3_ind},
  {ST_NCCI_N_3,		EV_DL_DOWN_IND,			ncci_dl_down_ind},
  {ST_NCCI_N_3,		EV_AP_DISCONNECT_B3_REQ,	ncci_disconnect_b3_req},
  {ST_NCCI_N_3,		EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_3,		EV_AP_RELEASE,			ncci_appl_release_disc},
  {ST_NCCI_N_3,		EV_NC_LINKDOWN,			ncci_linkdown},
#endif

  {ST_NCCI_N_ACT,	EV_AP_CONNECT_B3_ACTIVE_RESP,	ncci_connect_b3_active_resp},
  {ST_NCCI_N_ACT,	EV_AP_DISCONNECT_B3_REQ,	ncci_disconnect_b3_req},
  {ST_NCCI_N_ACT,	EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_ACT,	EV_DL_RELEASE_IND,		ncci_dl_release_ind_conf},
  {ST_NCCI_N_ACT,	EV_DL_RELEASE_CONF,		ncci_dl_release_ind_conf},
  {ST_NCCI_N_ACT,	EV_DL_DOWN_IND,			ncci_dl_down_ind},
  {ST_NCCI_N_ACT,	EV_AP_FACILITY_REQ,		ncci_facility_req},
  {ST_NCCI_N_ACT,	EV_AP_MANUFACTURER_REQ,		ncci_manufacturer_req},
  {ST_NCCI_N_ACT,	EV_AP_RELEASE,			ncci_appl_release_disc},
  {ST_NCCI_N_ACT,	EV_NC_LINKDOWN,			ncci_linkdown},
#if 0
  {ST_NCCI_N_ACT,	EV_AP_RESET_B3_REQ,		ncci_reset_b3_req},
  {ST_NCCI_N_ACT,	EV_NC_RESET_B3_IND,		ncci_reset_b3_ind},
  {ST_NCCI_N_ACT,	EV_NC_CONNECT_B3_T90_ACTIVE_IND,ncci_connect_b3_t90_active_ind},
#endif

  {ST_NCCI_N_4,		EV_NC_DISCONNECT_B3_CONF,	ncci_disconnect_b3_conf},
  {ST_NCCI_N_4,		EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_4,		EV_DL_RELEASE_CONF,		ncci_dl_release_ind_conf},
  {ST_NCCI_N_4,		EV_DL_DOWN_IND,			ncci_dl_down_ind},
  {ST_NCCI_N_4,		EV_AP_MANUFACTURER_REQ,		ncci_manufacturer_req},
  {ST_NCCI_N_4,		EV_NC_LINKDOWN,			ncci_linkdown},

  {ST_NCCI_N_5,		EV_AP_DISCONNECT_B3_RESP,	ncci_disconnect_b3_resp},
  {ST_NCCI_N_5,		EV_AP_RELEASE,			ncci_appl_release},
};
const int FN_NCCI_COUNT = sizeof(fn_ncci_list)/sizeof(struct FsmNode);

static void
ncciD_connect_b3_req(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_0_1);
	if (SKB_l4l3(fi->userdata, arg))
		dev_kfree_skb(arg);
}

static void
ncciD_connect_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff	*skb = arg;
	__u16		info = CAPIMSG_U16(skb->data, 12);

	if (info == 0)
		mISDN_FsmChangeState(fi, ST_NCCI_N_2);
	else
		mISDN_FsmChangeState(fi, ST_NCCI_N_0);
	SKB2Application(fi->userdata, skb);
	if (info != 0)
		ncciDestr(fi->userdata);
}

static void
ncciD_connect_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_1);
	SKB2Application(fi->userdata, arg);
}

static void
ncciD_connect_b3_resp(struct FsmInst *fi, int event, void *arg)
{
	struct sk_buff	*skb = arg;
	__u16		rej = CAPIMSG_U16(skb->data, 4);

	if (rej)
		mISDN_FsmChangeState(fi, ST_NCCI_N_4);
	else
		mISDN_FsmChangeState(fi, ST_NCCI_N_2);
	if (SKB_l4l3(fi->userdata, arg))
		dev_kfree_skb(arg);
}

static void
ncciD_connect_b3_active_ind(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_ACT);
	SKB2Application(fi->userdata, arg);
}

static void
ncciD_connect_b3_active_resp(struct FsmInst *fi, int event, void *arg)
{
	if (SKB_l4l3(fi->userdata, arg))
		dev_kfree_skb(arg);
}

static void
ncciD_reset_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_ACT);
	SKB2Application(fi->userdata, arg);
}

static void
ncciD_reset_b3_resp(struct FsmInst *fi, int event, void *arg)
{
	if (SKB_l4l3(fi->userdata, arg))
		dev_kfree_skb(arg);
}

static void
ncciD_reset_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_3);
	SKB2Application(fi->userdata, arg);
}

static void
ncciD_reset_b3_req(struct FsmInst *fi, int event, void *arg)
{
	SKB2Application(fi->userdata, arg);
}

static void
ncciD_disconnect_b3_req(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;

	ncci->savedstate = fi->state;
	mISDN_FsmChangeState(fi, ST_NCCI_N_4);
	if (SKB_l4l3(fi->userdata, arg))
		dev_kfree_skb(arg);
}

static void
ncciD_disconnect_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	struct sk_buff	*skb = arg;
	__u16		info = CAPIMSG_U16(skb->data, 12);

	if (test_bit(NCCI_STATE_APPLRELEASED, &ncci->state))
		return;
	if (info != 0)
		mISDN_FsmChangeState(fi, ncci->savedstate);
	SKB2Application(ncci, skb);
}

static void
ncciD_disconnect_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	struct sk_buff	*skb = arg;

	mISDN_FsmChangeState(fi, ST_NCCI_N_5);
	if (test_bit(NCCI_STATE_APPLRELEASED, &ncci->state)) {
		skb_pull(skb, CAPIMSG_BASELEN);
		skb_trim(skb, 4);
		if_newhead(&ncci->link->inst.down, CAPI_DISCONNECT_B3_RESP, 0, skb);
		ncciDestr(ncci);
	} else
		SKB2Application(ncci, arg);
}

static void
ncciD_disconnect_b3_resp(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_NCCI_N_0);
	if (SKB_l4l3(fi->userdata, arg))
		dev_kfree_skb(arg);
	ncciDestr(fi->userdata);
}

static void
ncciD_linkdown(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
	mISDN_FsmChangeState(fi, ST_NCCI_N_5);
	Send2Application(ncci, cmsg);
}

static void
ncciD_appl_release_disc(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	u_char	parm[5];

	capimsg_setu32(parm, 0, ncci->addr);
	parm[4] = 0;
	mISDN_FsmChangeState(fi, ST_NCCI_N_4);
	if_link(&ncci->link->inst.down, CAPI_DISCONNECT_B3_REQ, 0, 5, parm, 0); 
}

static struct FsmNode fn_ncciD_list[] =
{
  {ST_NCCI_N_0,		EV_AP_CONNECT_B3_REQ,		ncciD_connect_b3_req},
  {ST_NCCI_N_0,		EV_NC_CONNECT_B3_IND,		ncciD_connect_b3_ind},
  {ST_NCCI_N_0,		EV_AP_RELEASE,			ncci_appl_release},

  {ST_NCCI_N_0_1,	EV_NC_CONNECT_B3_CONF,		ncciD_connect_b3_conf},
  {ST_NCCI_N_0_1,	EV_AP_RELEASE,			ncci_appl_release},

  {ST_NCCI_N_1,		EV_AP_CONNECT_B3_RESP,		ncciD_connect_b3_resp},
  {ST_NCCI_N_1,		EV_AP_DISCONNECT_B3_REQ,	ncciD_disconnect_b3_req},
  {ST_NCCI_N_1,		EV_NC_DISCONNECT_B3_IND,	ncciD_disconnect_b3_ind},
  {ST_NCCI_N_1,		EV_AP_RELEASE,			ncciD_appl_release_disc},
  {ST_NCCI_N_1,		EV_NC_LINKDOWN,			ncciD_linkdown},

  {ST_NCCI_N_2,		EV_NC_CONNECT_B3_ACTIVE_IND,	ncciD_connect_b3_active_ind},
  {ST_NCCI_N_2,		EV_AP_DISCONNECT_B3_REQ,	ncciD_disconnect_b3_req},
  {ST_NCCI_N_2,		EV_NC_DISCONNECT_B3_IND,	ncciD_disconnect_b3_ind},
  {ST_NCCI_N_2,		EV_AP_RELEASE,			ncciD_appl_release_disc},
  {ST_NCCI_N_2,		EV_NC_LINKDOWN,			ncciD_linkdown},
     
  {ST_NCCI_N_3,		EV_NC_RESET_B3_IND,		ncciD_reset_b3_ind},
  {ST_NCCI_N_3,		EV_AP_DISCONNECT_B3_REQ,	ncciD_disconnect_b3_req},
  {ST_NCCI_N_3,		EV_NC_DISCONNECT_B3_IND,	ncciD_disconnect_b3_ind},
  {ST_NCCI_N_3,		EV_AP_RELEASE,			ncciD_appl_release_disc},
  {ST_NCCI_N_3,		EV_NC_LINKDOWN,			ncciD_linkdown},

  {ST_NCCI_N_ACT,	EV_AP_CONNECT_B3_ACTIVE_RESP,	ncciD_connect_b3_active_resp},
  {ST_NCCI_N_ACT,	EV_AP_DISCONNECT_B3_REQ,	ncciD_disconnect_b3_req},
  {ST_NCCI_N_ACT,	EV_NC_DISCONNECT_B3_IND,	ncciD_disconnect_b3_ind},
  {ST_NCCI_N_ACT,	EV_AP_RELEASE,			ncciD_appl_release_disc},
  {ST_NCCI_N_ACT,	EV_NC_LINKDOWN,			ncciD_linkdown},
  {ST_NCCI_N_ACT,	EV_AP_RESET_B3_REQ,		ncciD_reset_b3_req},
  {ST_NCCI_N_ACT,	EV_NC_RESET_B3_IND,		ncciD_reset_b3_ind},
  {ST_NCCI_N_ACT,	EV_NC_RESET_B3_CONF,		ncciD_reset_b3_conf},
  {ST_NCCI_N_ACT,	EV_AP_RESET_B3_RESP,		ncciD_reset_b3_resp},
//{ST_NCCI_N_ACT,	EV_NC_CONNECT_B3_T90_ACTIVE_IND,ncciD_connect_b3_t90_active_ind},

  {ST_NCCI_N_4,		EV_NC_DISCONNECT_B3_CONF,	ncciD_disconnect_b3_conf},
  {ST_NCCI_N_4,		EV_NC_DISCONNECT_B3_IND,	ncciD_disconnect_b3_ind},
  {ST_NCCI_N_4,		EV_NC_LINKDOWN,			ncciD_linkdown},

  {ST_NCCI_N_5,		EV_AP_DISCONNECT_B3_RESP,	ncciD_disconnect_b3_resp},
  {ST_NCCI_N_5,		EV_AP_RELEASE,			ncci_appl_release},
};
const int FN_NCCID_COUNT = sizeof(fn_ncciD_list)/sizeof(struct FsmNode);

Ncci_t *
ncciConstr(AppPlci_t *aplci)
{
	Ncci_t	*ncci = ncci_alloc();

	if (!ncci)
		return(NULL);

	memset(ncci, 0, sizeof(Ncci_t));
	ncci->ncci_m.state      = ST_NCCI_N_0;
	ncci->ncci_m.debug      = aplci->plci->contr->debug & CAPI_DBG_NCCI_STATE;
	ncci->ncci_m.userdata   = ncci;
	ncci->ncci_m.printdebug = ncci_debug;
	/* unused NCCI */
	ncci->addr = aplci->addr;
	ncci->AppPlci = aplci;
	ncci->link = aplci->link;
	ncci->contr = aplci->contr;
	ncci->appl = aplci->appl;
	ncci->window = aplci->appl->reg_params.datablkcnt;
	if (aplci->Bprotocol.B2 != 0) /* X.75 has own flowctrl */
		test_and_set_bit(NCCI_STATE_FCTRL, &ncci->state);
	if (aplci->Bprotocol.B3 == 0) {
		test_and_set_bit(NCCI_STATE_L3TRANS, &ncci->state);
		ncci->ncci_m.fsm = &ncci_fsm;
	} else
		ncci->ncci_m.fsm = &ncciD_fsm;
	skb_queue_head_init(&ncci->squeue);
	if (ncci->window > CAPI_MAXDATAWINDOW) {
		ncci->window = CAPI_MAXDATAWINDOW;
	}
	INIT_LIST_HEAD(&ncci->head);
	list_add(&ncci->head, &aplci->Nccis);
	if (ncci->ncci_m.debug)
		printk(KERN_DEBUG "%s: ncci(%p) NCCI(%x) debug (%x/%x)\n",
			__FUNCTION__, ncci, ncci->addr, aplci->plci->contr->debug, CAPI_DBG_NCCI_STATE); 
	return(ncci);
}

void
ncciDestr(Ncci_t *ncci)
{
	int i;

	capidebug(CAPI_DBG_NCCI, "ncciDestr NCCI %x", ncci->addr);

#ifdef OLDCAPI_DRIVER_INTERFACE
	if (!test_bit(NCCI_STATE_APPLRELEASED, &ncci->state))
		ncci->contr->ctrl->free_ncci(ncci->contr->ctrl, ncci->appl->ApplId, ncci->addr);
#endif
	/* cleanup data queues */
	discard_queue(&ncci->squeue);
	for (i = 0; i < ncci->window; i++) {
		if (ncci->xmit_skb_handles[i].PktId)
			ncci->xmit_skb_handles[i].PktId = 0;
	}
	AppPlciDelNCCI(ncci);
	ncci_free(ncci);
}

void
ncciApplRelease(Ncci_t *ncci)
{
	test_and_set_bit(NCCI_STATE_APPLRELEASED, &ncci->state);
	mISDN_FsmEvent(&ncci->ncci_m, EV_AP_RELEASE, NULL);
}

void
ncciDelAppPlci(Ncci_t *ncci)
{
	printk(KERN_DEBUG "%s: ncci(%p) NCCI(%x)\n",
		__FUNCTION__, ncci, ncci->addr); 
	ncci->AppPlci = NULL;
	/* maybe we should release the NCCI here */
}

void
ncciReleaseLink(Ncci_t *ncci)
{
	/* this is normal shutdown on speech and other transparent protocols */
	mISDN_FsmEvent(&ncci->ncci_m, EV_NC_LINKDOWN, NULL);
}

void
ncciDataInd(Ncci_t *ncci, int pr, struct sk_buff *skb)
{
	struct sk_buff *nskb;
	int i;

	for (i = 0; i < CAPI_MAXDATAWINDOW; i++) {
		if (ncci->recv_skb_handles[i] == 0)
			break;
	}

	if (i == CAPI_MAXDATAWINDOW) {
		// FIXME: trigger flow control if supported by L2 protocol
		printk(KERN_DEBUG "%s: frame %d dropped\n", __FUNCTION__, skb->len);
		dev_kfree_skb(skb);
		return;
	}

	if (skb_headroom(skb) < CAPI_B3_DATA_IND_HEADER_SIZE) {
		capidebug(CAPI_DBG_NCCI_L3, "%s: only %d bytes headroom, need %d",
			__FUNCTION__, skb_headroom(skb), CAPI_B3_DATA_IND_HEADER_SIZE);
		nskb = skb_realloc_headroom(skb, CAPI_B3_DATA_IND_HEADER_SIZE);
		dev_kfree_skb(skb);
		if (!nskb) {
			int_error();
			return;
		}
      	} else { 
		nskb = skb;
	}
	ncci->recv_skb_handles[i] = nskb;

	skb_push(nskb, CAPI_B3_DATA_IND_HEADER_SIZE);
	*((__u16*) nskb->data) = CAPI_B3_DATA_IND_HEADER_SIZE;
	*((__u16*)(nskb->data+2)) = ncci->appl->ApplId;
	*((__u8*) (nskb->data+4)) = CAPI_DATA_B3;
	*((__u8*) (nskb->data+5)) = CAPI_IND;
	*((__u16*)(nskb->data+6)) = ncci->appl->MsgId++;
	*((__u32*)(nskb->data+8)) = ncci->addr;
	if (sizeof(nskb) == 4) {
		*((__u32*)(nskb->data+12)) = (__u32)(nskb->data + CAPI_B3_DATA_IND_HEADER_SIZE);
		*((__u64*)(nskb->data+22)) = 0;
	} else {
		*((__u32*)(nskb->data+12)) = 0;
		*((__u64*)(nskb->data+22)) = (u_long)(nskb->data + CAPI_B3_DATA_IND_HEADER_SIZE);
	}
	*((__u16*)(nskb->data+16)) = nskb->len - CAPI_B3_DATA_IND_HEADER_SIZE;
	*((__u16*)(nskb->data+18)) = i;
	// FIXME FLAGS
	*((__u16*)(nskb->data+20)) = 0;
#ifdef OLDCAPI_DRIVER_INTERFACE
	ncci->contr->ctrl->handle_capimsg(ncci->contr->ctrl, ncci->appl->ApplId, nskb);
#else
	capi_ctr_handle_message(ncci->contr->ctrl, ncci->appl->ApplId, nskb);
#endif
}

__u16
ncciDataReq(Ncci_t *ncci, struct sk_buff *skb)
{
	int	i, err;
	__u16	len, capierr = 0;
	_cmsg	*cmsg;
	
	len = CAPIMSG_LEN(skb->data);
	if (len != 22 && len != 30) {
		capierr = CapiIllMessageParmCoding;
		int_error();
		goto fail;
	}
	for (i = 0; i < ncci->window; i++) {
		if (ncci->xmit_skb_handles[i].PktId == 0)
			break;
	}
	if (i == ncci->window) {
		return(CAPI_SENDQUEUEFULL);
	}
	mISDN_HEAD_DINFO(skb) = ControllerNextId(ncci->contr);
	ncci->xmit_skb_handles[i].PktId = mISDN_HEAD_DINFO(skb);
	ncci->xmit_skb_handles[i].DataHandle = CAPIMSG_REQ_DATAHANDLE(skb->data);
	ncci->xmit_skb_handles[i].MsgId = CAPIMSG_MSGID(skb->data);

	/* the data begins behind the header, we don't use Data32/Data64 here */
	skb_pull(skb, len);

	if (test_bit(NCCI_STATE_FCTRL, &ncci->state)) {
		if (test_and_set_bit(NCCI_STATE_BUSY, &ncci->state)) {
			skb_queue_tail(&ncci->squeue, skb);
			return(CAPI_NOERROR);
		}
		if (skb_queue_len(&ncci->squeue)) {
			skb_queue_tail(&ncci->squeue, skb);
			skb = skb_dequeue(&ncci->squeue);
			i = -1;
		}
	}
	
	err = ncciL4L3(ncci, DL_DATA | REQUEST, mISDN_HEAD_DINFO(skb), 0, NULL, skb);
	if (!err)
		return(CAPI_NOERROR);

	int_error();
	skb_push(skb, len);
	capierr = CAPI_MSGBUSY;
	if (i == -1) {
		for (i = 0; i < ncci->window; i++) {
			if (ncci->xmit_skb_handles[i].PktId == mISDN_HEAD_DINFO(skb))
				break;
		}
		if (i == ncci->window)
			int_error();
		else
			ncci->xmit_skb_handles[i].PktId = 0;
	} else {
		ncci->xmit_skb_handles[i].PktId = 0;
		return(capierr);
	}
fail:
	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		if (capierr != CAPI_MSGBUSY)
			return(CAPI_MSGOSRESOURCEERR);
		/* we can not do error handling on a skb from the queue here */	
		dev_kfree_skb(skb);
		return(CAPI_NOERROR);
	}
	capi_cmsg_header(cmsg, ncci->AppPlci->appl->ApplId, CAPI_DATA_B3, CAPI_CONF, 
		CAPIMSG_MSGID(skb->data), ncci->addr);
	/* illegal len (too short) ??? */
	cmsg->DataHandle = CAPIMSG_REQ_DATAHANDLE(skb->data);
	cmsg->Info = capierr;
	Send2Application(ncci, cmsg);
	dev_kfree_skb(skb);
	return(CAPI_NOERROR);
}

int
ncciDataConf(Ncci_t *ncci, int pr, struct sk_buff *skb)
{
	int	i;
	_cmsg	*cmsg;

	for (i = 0; i < ncci->window; i++) {
		if (ncci->xmit_skb_handles[i].PktId == mISDN_HEAD_DINFO(skb))
			break;
	}
	if (i == ncci->window) {
		int_error();
		return(-EINVAL);
	}
	ncci->xmit_skb_handles[i].PktId = 0;
	capidebug(CAPI_DBG_NCCI_L3, "%s: entry %d/%d handle (%x)",
		__FUNCTION__, i, ncci->window, ncci->xmit_skb_handles[i].DataHandle);

	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		return(-ENOMEM);
	}
	dev_kfree_skb(skb);
	capi_cmsg_header(cmsg, ncci->AppPlci->appl->ApplId, CAPI_DATA_B3, CAPI_CONF, 
			 ncci->xmit_skb_handles[i].MsgId, ncci->addr);
	cmsg->DataHandle = ncci->xmit_skb_handles[i].DataHandle;
	cmsg->Info = 0;
	Send2Application(ncci, cmsg);
	if (test_bit(NCCI_STATE_FCTRL, &ncci->state)) {
		if (skb_queue_len(&ncci->squeue)) {
			skb = skb_dequeue(&ncci->squeue);
			if (ncciL4L3(ncci, DL_DATA | REQUEST, mISDN_HEAD_DINFO(skb),
				0, NULL, skb)) {
				int_error();
				dev_kfree_skb(skb);
			}
		} else
			test_and_clear_bit(NCCI_STATE_BUSY, &ncci->state);
	}
	return(0);
}	
	
void
ncciDataResp(Ncci_t *ncci, struct sk_buff *skb)
{
	// FIXME: incoming flow control doesn't work yet

	int i;

	i = CAPIMSG_RESP_DATAHANDLE(skb->data);
	if (i < 0 || i > CAPI_MAXDATAWINDOW) {
		int_error();
		return;
	}
	if (!ncci->recv_skb_handles[i]) {
		int_error();
		return;
	}
	ncci->recv_skb_handles[i] = 0;

	dev_kfree_skb(skb);
}

int
ncci_l4l3_direct(Ncci_t *ncci, struct sk_buff *skb) {
	mISDN_head_t	*hh;
	int		ret;

	hh = mISDN_HEAD_P(skb);
	if (ncci->ncci_m.debug)
		log_skbdata(skb);
	hh->prim = CAPIMSG_CMD(skb->data);
	hh->dinfo = CAPIMSG_MSGID(skb->data);
	skb_pull(skb, CAPIMSG_BASELEN);
	if (ncci->ncci_m.debug)
		log_skbdata(skb);
	switch (hh->prim) {
		case CAPI_DATA_B3_REQ:
		case CAPI_DATA_B3_RESP:
		case CAPI_FACILITY_REQ:
		case CAPI_FACILITY_RESP:
		case CAPI_MANUFACTURER_REQ:
		case CAPI_MANUFACTURER_RESP:
			return(SKB_l4l3(ncci, skb));
		case CAPI_CONNECT_B3_REQ:
			ret = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_REQ, skb);
			break;
		case CAPI_CONNECT_B3_RESP:
			ret = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_RESP, skb);
			break;
		case CAPI_CONNECT_B3_ACTIVE_RESP:
			ret = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_ACTIVE_RESP, skb);
			break;
		case CAPI_DISCONNECT_B3_REQ:
			ret = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_DISCONNECT_B3_REQ, skb);
			break;
		case CAPI_DISCONNECT_B3_RESP:
			ret = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_DISCONNECT_B3_RESP, skb);
			break;
		default:
			int_error();
			ret = -1;
	}
	if (ret) {
		int_error();
		dev_kfree_skb(skb);
	}
	return(0);
}

void
ncciGetCmsg(Ncci_t *ncci, _cmsg *cmsg)
{
	int	retval = 0;

	if (!test_bit(NCCI_STATE_L3TRANS, &ncci->state)) {
		int_error();
		cmsg_free(cmsg);
		return;
	}
	switch (CMSGCMD(cmsg)) {
		case CAPI_CONNECT_B3_REQ:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_REQ, cmsg);
			break;
		case CAPI_CONNECT_B3_RESP:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_RESP, cmsg);
			break;
		case CAPI_CONNECT_B3_ACTIVE_RESP:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_ACTIVE_RESP, cmsg);
			break;
		case CAPI_DISCONNECT_B3_REQ:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_DISCONNECT_B3_REQ, cmsg);
			break;
		case CAPI_DISCONNECT_B3_RESP:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_DISCONNECT_B3_RESP, cmsg);
			break;
		case CAPI_FACILITY_REQ:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_FACILITY_REQ, cmsg);
			break;
		case CAPI_MANUFACTURER_REQ:
			retval = mISDN_FsmEvent(&ncci->ncci_m, EV_AP_MANUFACTURER_REQ, cmsg);
			break;
		default:
			int_error();
			retval = -1;
	}
	if (retval) { 
		if (cmsg->Command == CAPI_REQ) {
			capi_cmsg_answer(cmsg);
			cmsg->Info = CapiMessageNotSupportedInCurrentState;
			Send2Application(ncci, cmsg);
		} else
			cmsg_free(cmsg);
	}
}

__u16
ncciSendMessage(Ncci_t *ncci, struct sk_buff *skb)
{
	int	ret;
	_cmsg	*cmsg;

	if (!test_bit(NCCI_STATE_L3TRANS, &ncci->state)) {
		ret = ncci_l4l3_direct(ncci, skb);
		switch(ret) {
			case 0:
				break;
			case -EINVAL:
			case -ENXIO:
				return(CAPI_MSGBUSY);
			case -EXFULL:
				return(CAPI_SENDQUEUEFULL);
			default:
				int_errtxt("ncci_l4l3_direct return(%d)", ret);
				dev_kfree_skb(skb);
				break;
		}
		return(CAPI_NOERROR);
	}
	// we're not using the cmsg for DATA_B3 for performance reasons
	switch (CAPIMSG_CMD(skb->data)) {
		case CAPI_DATA_B3_REQ:
			if (ncci->ncci_m.state == ST_NCCI_N_ACT) {
				return(ncciDataReq(ncci, skb));
			} else {
				AnswerMessage2Application(ncci->appl, skb, 
					CapiMessageNotSupportedInCurrentState);
				dev_kfree_skb(skb);
			}
			return(CAPI_NOERROR);
		case CAPI_DATA_B3_RESP:
			ncciDataResp(ncci, skb);
			return(CAPI_NOERROR);
	}
	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		return(CAPI_MSGOSRESOURCEERR);
	}
	capi_message2cmsg(cmsg, skb->data);
	ncciGetCmsg(ncci, cmsg);
	dev_kfree_skb(skb);
	return(CAPI_NOERROR);
}

int
ncci_l3l4_direct(Ncci_t *ncci, mISDN_head_t *hh, struct sk_buff *skb)
{
	__u16		msgnr;
	int		event;

	capidebug(CAPI_DBG_NCCI_L3, "%s: NCCI %x prim(%x) dinfo (%x) skb(%p) s(%x)",
		__FUNCTION__, ncci->addr, hh->prim, hh->dinfo, skb, ncci->state);
	if (ncci->ncci_m.debug)
		log_skbdata(skb);
	switch (hh->prim & 0xFF) {
		case CAPI_IND:
			msgnr = ncci->appl->MsgId++;
			break;
		case CAPI_CONF:
			msgnr = hh->dinfo & 0xffff;
			break;
		default:
			int_error();
			return(-EINVAL);
	}
	if (skb_headroom(skb) < CAPIMSG_BASELEN) {
		capidebug(CAPI_DBG_NCCI_L3, "%s: only %d bytes headroom, need %d",
			__FUNCTION__, skb_headroom(skb), CAPIMSG_BASELEN);
		int_error();
		return(-ENOSPC);
	}
	skb_push(skb, CAPIMSG_BASELEN);
	CAPIMSG_SETLEN(skb->data, (hh->prim == CAPI_DATA_B3_IND) ?
		CAPI_B3_DATA_IND_HEADER_SIZE : skb->len);
	CAPIMSG_SETAPPID(skb->data, ncci->appl->ApplId);
	CAPIMSG_SETCOMMAND(skb->data, (hh->prim>>8) & 0xff);
	CAPIMSG_SETSUBCOMMAND(skb->data, hh->prim & 0xff); 
	CAPIMSG_SETMSGID(skb->data, msgnr);
	switch (hh->prim) {
		case CAPI_DATA_B3_IND:
		case CAPI_DATA_B3_CONF:
		case CAPI_FACILITY_IND:
		case CAPI_FACILITY_CONF:
		case CAPI_MANUFACTURER_IND:
		case CAPI_MANUFACTURER_CONF:
#ifdef OLDCAPI_DRIVER_INTERFACE
			ncci->contr->ctrl->handle_capimsg(ncci->contr->ctrl, ncci->appl->ApplId, skb);
#else
			capi_ctr_handle_message(ncci->contr->ctrl, ncci->appl->ApplId, skb);
#endif
			return(0);
		case CAPI_CONNECT_B3_IND:
			event = EV_NC_CONNECT_B3_IND;
			break;
		case CAPI_CONNECT_B3_ACTIVE_IND:
			event = EV_NC_CONNECT_B3_ACTIVE_IND;
			break;
		case CAPI_DISCONNECT_B3_IND:
			event = EV_NC_DISCONNECT_B3_IND;
			break;
		case CAPI_RESET_B3_IND:
			event = EV_NC_RESET_B3_IND;
			break;
		case CAPI_CONNECT_B3_CONF:
			event = EV_NC_CONNECT_B3_CONF;
			break;
		case CAPI_DISCONNECT_B3_CONF:
			event = EV_NC_DISCONNECT_B3_CONF;
			break;
		case CAPI_RESET_B3_CONF:
			event = EV_NC_RESET_B3_CONF;
			break;
		default:
			int_error();
			return(-EINVAL);
	}
	if (mISDN_FsmEvent(&ncci->ncci_m, event, skb))
		dev_kfree_skb(skb);
	return(0);
}

int
ncci_l3l4(Ncci_t *ncci, mISDN_head_t *hh, struct sk_buff *skb)
{
	capidebug(CAPI_DBG_NCCI_L3, "%s: NCCI %x prim(%x) dinfo (%x) skb(%p) s(%x)",
		__FUNCTION__, ncci->addr, hh->prim, hh->dinfo, skb, ncci->state);
	switch (hh->prim) {
		// we're not using the Fsm for DL_DATA for performance reasons
		case DL_DATA | INDICATION: 
			if (ncci->ncci_m.state == ST_NCCI_N_ACT) {
				ncciDataInd(ncci, hh->prim, skb);
				return(0);
			} 
			break;
		case DL_DATA | CONFIRM:
			if (ncci->ncci_m.state == ST_NCCI_N_ACT) {
				return(ncciDataConf(ncci, hh->prim, skb));
			}
			break;
		case DL_ESTABLISH | INDICATION:
			mISDN_FsmEvent(&ncci->ncci_m, EV_DL_ESTABLISH_IND, skb);
			break;
		case DL_ESTABLISH | CONFIRM:
			mISDN_FsmEvent(&ncci->ncci_m, EV_DL_ESTABLISH_CONF, skb);
			break;
		case DL_RELEASE | INDICATION:
			mISDN_FsmEvent(&ncci->ncci_m, EV_DL_RELEASE_IND, skb);
			break;
		case DL_RELEASE | CONFIRM:
			mISDN_FsmEvent(&ncci->ncci_m, EV_DL_RELEASE_CONF, skb);
			break;
		case PH_CONTROL | INDICATION: /* e.g touch tones */
			/* handled by AppPlci */
			AppPlci_l3l4(ncci->AppPlci, hh->prim, skb->data);
			break;
		default:
			capidebug(CAPI_DBG_WARN, "%s: unknown prim(%x) dinfo(%x) len(%d) skb(%p)",
				__FUNCTION__, hh->prim, hh->dinfo, skb->len, skb);
			int_error();
			return(-EINVAL);
	}
	dev_kfree_skb(skb);
	return(0);
}

static int
ncciL4L3(Ncci_t *ncci, u_int prim, int dtyp, int len, void *arg, struct sk_buff *skb)
{
	capidebug(CAPI_DBG_NCCI_L3, "%s: NCCI %x prim(%x) dtyp(%x) len(%d) skb(%p)",
		__FUNCTION__, ncci->addr, prim, dtyp, len, skb);
	if (skb)
		return(if_newhead(&ncci->link->inst.down, prim, dtyp, skb));
	else
		return(if_link(&ncci->link->inst.down, prim, dtyp,
			len, arg, 8));
}

void
init_ncci(void)
{
	ncci_fsm.state_count = ST_NCCI_COUNT;
	ncci_fsm.event_count = EV_NCCI_COUNT;
	ncci_fsm.strEvent = str_ev_ncci;
	ncci_fsm.strState = str_st_ncci;
	mISDN_FsmNew(&ncci_fsm, fn_ncci_list, FN_NCCI_COUNT);

	ncciD_fsm.state_count = ST_NCCI_COUNT;
	ncciD_fsm.event_count = EV_NCCI_COUNT;
	ncciD_fsm.strEvent = str_ev_ncci;
	ncciD_fsm.strState = str_st_ncci;
	mISDN_FsmNew(&ncciD_fsm, fn_ncciD_list, FN_NCCID_COUNT);
}

void
free_ncci(void)
{
	mISDN_FsmFree(&ncci_fsm);
	mISDN_FsmFree(&ncciD_fsm);
}
