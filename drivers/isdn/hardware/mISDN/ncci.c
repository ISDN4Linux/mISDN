/* $Id: ncci.c,v 1.18 2003/12/10 23:01:16 keil Exp $
 *
 */

#include "m_capi.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"
#include "mISDNManufacturer.h"

static int	ncciL4L3(Ncci_t *, u_int, int, int, void *, struct sk_buff *);
static void	ncciInitSt(Ncci_t *);
static void	ncciReleaseSt(Ncci_t *);

static char	logbuf[8000];

void
log_skbdata(struct sk_buff *skb)
{
	char *t = logbuf;

	t += sprintf(t, "skbdata(%d):", skb->len);
	QuickHex(t, skb->data, skb->len);
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
	EV_NC_CONNECT_B3_T90_ACTIVE_IND,
	EV_AP_DISCONNECT_B3_REQ,
	EV_NC_DISCONNECT_B3_IND,
	EV_NC_DISCONNECT_B3_CONF,
	EV_AP_DISCONNECT_B3_RESP,
	EV_AP_FACILITY_REQ,
	EV_AP_MANUFACTURER_REQ,
	EV_AP_SELECT_B_PROTOCOL,
	EV_DL_ESTABLISH_IND,
	EV_DL_ESTABLISH_CONF,
	EV_DL_RELEASE_IND,
	EV_DL_RELEASE_CONF,
	EV_DL_DOWN_IND,
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
	"EV_NC_CONNECT_B3_T90_ACTIVE_IND",
	"EV_AP_DISCONNECT_B3_REQ",
	"EV_NC_DISCONNECT_B3_IND",
	"EV_NC_DISCONNECT_B3_CONF",
	"EV_AP_DISCONNECT_B3_RESP",
	"EV_AP_FACILITY_REQ",
	"EV_AP_MANUFACTURER_REQ",
	"EV_AP_SELECT_B_PROTOCOL",
	"EV_DL_ESTABLISH_IND",
	"EV_DL_ESTABLISH_CONF",
	"EV_DL_RELEASE_IND",
	"EV_DL_RELEASE_CONF",
	"EV_DL_DOWN_IND",
	"EV_AP_RELEASE",
};

static struct Fsm ncci_fsm = { 0, 0, 0, 0, 0 };


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
	FsmChangeState(fi, ST_NCCI_N_0_1);
	capi_cmsg_answer(cmsg);

	// TODO: NCPI handling
	/* We need a real addr now */
	if (0xffffffff == ncci->addr) {
		cmsg->Info = 0;
		if (select_NCCIaddr(ncci)) {
			int_error();
			cmsg->Info = CapiNoNcciAvailable;
		}
	} else {
		int_error();
		cmsg->Info = CapiNoNcciAvailable;
		ncci->addr = ncci->AppPlci->addr;
	}
	cmsg->adr.adrNCCI = ncci->addr;
	ncci_debug(fi, "ncci_connect_b3_req NCCI %x cmsg->Info(%x)",
		ncci->addr, cmsg->Info);
	if (FsmEvent(fi, EV_NC_CONNECT_B3_CONF, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_connect_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	// from DL_ESTABLISH
	FsmChangeState(fi, ST_NCCI_N_1);
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
		FsmChangeState(fi, ST_NCCI_N_2);
		ncciCmsgHeader(ncci, cmsg, CAPI_CONNECT_B3_ACTIVE, CAPI_IND);
		event = EV_NC_CONNECT_B3_ACTIVE_IND;
	} else {
		FsmChangeState(fi, ST_NCCI_N_4);
		cmsg->Info = 0;
		ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
		event = EV_NC_DISCONNECT_B3_IND;
	}
	if (FsmEvent(&ncci->ncci_m, event, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_connect_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	_cmsg	*cmsg = arg;
  
	if (cmsg->Info == 0) {
		FsmChangeState(fi, ST_NCCI_N_2);
		Send2Application(fi->userdata, cmsg);
		ncciL4L3(fi->userdata, DL_ESTABLISH | REQUEST, 0, 0, NULL, NULL);
	} else {
		FsmChangeState(fi, ST_NCCI_N_0);
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
		if (FsmEvent(fi, EV_NC_DISCONNECT_B3_CONF, cmsg))
			cmsg_free(cmsg);
	} else {
		cmsg_free(cmsg);
		FsmChangeState(fi, ST_NCCI_N_4);
	}
	ncciL4L3(ncci, DL_RELEASE | REQUEST, 0, 0, NULL, NULL);
}

static void
ncci_disconnect_b3_conf(struct FsmInst *fi, int event, void *arg)
{
	_cmsg	*cmsg = arg;

	if (cmsg->Info == 0) {
		FsmChangeState(fi, ST_NCCI_N_4);
	}
	Send2Application(fi->userdata, cmsg);
}

static void
ncci_disconnect_b3_ind(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;

	FsmChangeState(fi, ST_NCCI_N_5);
	if (ncci->appl) { // FIXME
		Send2Application(ncci, arg);
	} else {
		cmsg_free(arg);
		FsmChangeState(fi, ST_NCCI_N_0);
		ncciDestr(ncci);
	}
}

static void
ncci_disconnect_b3_resp(struct FsmInst *fi, int event, void *arg)
{
	if (arg)
		cmsg_free(arg);
	FsmChangeState(fi, ST_NCCI_N_0);
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

	FsmChangeState(fi, ST_NCCI_N_ACT);
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
	if (0xffffffff == ncci->addr) {
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
	if (FsmEvent(&ncci->ncci_m, EV_NC_CONNECT_B3_IND, cmsg))
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
	if (FsmEvent(&ncci->ncci_m, EV_NC_CONNECT_B3_ACTIVE_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_dl_release_ind_conf(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t	*ncci = fi->userdata;
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	ncciCmsgHeader(ncci, cmsg, CAPI_DISCONNECT_B3, CAPI_IND);
	if (FsmEvent(&ncci->ncci_m, EV_NC_DISCONNECT_B3_IND, cmsg))
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
	if (FsmEvent(&ncci->ncci_m, EV_NC_DISCONNECT_B3_IND, cmsg))
		cmsg_free(cmsg);
}

static void
ncci_select_b_protocol(struct FsmInst *fi, int event, void *arg)
{
	Ncci_t *ncci = fi->userdata;

	ncciReleaseSt(ncci);
	ncciInitSt(ncci);
}


static void
ncci_appl_release(struct FsmInst *fi, int event, void *arg)
{
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
  {ST_NCCI_N_0,		EV_AP_SELECT_B_PROTOCOL,	ncci_select_b_protocol},
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

  {ST_NCCI_N_2,		EV_NC_CONNECT_B3_ACTIVE_IND,	ncci_connect_b3_active_ind},
  {ST_NCCI_N_2,		EV_AP_DISCONNECT_B3_REQ,	ncci_disconnect_b3_req},
  {ST_NCCI_N_2,		EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_2,		EV_DL_ESTABLISH_CONF,		ncci_dl_establish_conf},
  {ST_NCCI_N_2,		EV_DL_RELEASE_IND,		ncci_dl_release_ind_conf},
  {ST_NCCI_N_2,		EV_AP_MANUFACTURER_REQ,		ncci_manufacturer_req},
  {ST_NCCI_N_2,		EV_AP_RELEASE,			ncci_appl_release_disc},
     
#if 0
  {ST_NCCI_N_3,		EV_NC_RESET_B3_IND,		ncci_reset_b3_ind},
  {ST_NCCI_N_3,		EV_AP_DISCONNECT_B3_REQ,	ncci_disconnect_b3_req},
  {ST_NCCI_N_3,		EV_NC_DISCONNECT_B3_IND,	ncci_disconnect_b3_ind},
  {ST_NCCI_N_3,		EV_AP_RELEASE,			ncci_appl_release_disc},
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

  {ST_NCCI_N_5,		EV_AP_DISCONNECT_B3_RESP,	ncci_disconnect_b3_resp},
  {ST_NCCI_N_5,		EV_AP_RELEASE,			ncci_disconnect_b3_resp},
};

const int FN_NCCI_COUNT = sizeof(fn_ncci_list)/sizeof(struct FsmNode);

Ncci_t *
ncciConstr(AppPlci_t *aplci)
{
	Ncci_t	*ncci = ncci_alloc();

	if (!ncci)
		return(NULL);

	memset(ncci, 0, sizeof(Ncci_t));
	ncci->ncci_m.fsm        = &ncci_fsm;
	ncci->ncci_m.state      = ST_NCCI_N_0;
	ncci->ncci_m.debug      = aplci->plci->contr->debug & CAPI_DBG_NCCI_STATE;
	ncci->ncci_m.userdata   = ncci;
	ncci->ncci_m.printdebug = ncci_debug;
	/* unused NCCI */
	ncci->addr = 0xffffffff;
	ncci->AppPlci = aplci;
	ncci->contr = aplci->contr;
	ncci->appl = aplci->appl;
	ncci->window = aplci->appl->reg_params.datablkcnt;
	skb_queue_head_init(&ncci->squeue);
	if (ncci->window > CAPI_MAXDATAWINDOW) {
		ncci->window = CAPI_MAXDATAWINDOW;
	}
	INIT_LIST_HEAD(&ncci->head);
	list_add(&ncci->head, &aplci->Nccis);
	printk(KERN_DEBUG "%s: ncci(%p) NCCI(%x) debug (%x/%x)\n",
		__FUNCTION__, ncci, ncci->addr, aplci->plci->contr->debug, CAPI_DBG_NCCI_STATE); 
	return(ncci);
}

static void
ncciInitSt(Ncci_t *ncci)
{
	mISDN_pid_t	pid;
	mISDN_stPara_t	stpara;
	int		retval;
	AppPlci_t	*aplci = ncci->AppPlci;

	memset(&pid, 0, sizeof(mISDN_pid_t));
	pid.layermask = ISDN_LAYER(1) | ISDN_LAYER(2) | ISDN_LAYER(3) |
		ISDN_LAYER(4);
	if (test_bit(PLCI_STATE_OUTGOING, &aplci->plci->state))
		pid.global = 1; // DTE, orginate
	else
		pid.global = 2; // DCE, answer
	if (aplci->Bprotocol.B1 > 23) {
		int_errtxt("wrong B1 prot %x", aplci->Bprotocol.B1);
		return;
	}
	pid.protocol[1] = (1 << aplci->Bprotocol.B1) |
		ISDN_PID_LAYER(1) | ISDN_PID_BCHANNEL_BIT;
	if (aplci->Bprotocol.B2 > 23) {
		int_errtxt("wrong B2 prot %x", aplci->Bprotocol.B2);
		return;
	}
	if (aplci->Bprotocol.B2 == 0) /* X.75 has own flowctrl */
		ncci->state = 0;
	else
		ncci->state = NCCI_STATE_FCTRL;
	pid.protocol[2] = (1 << aplci->Bprotocol.B2) |
		ISDN_PID_LAYER(2) | ISDN_PID_BCHANNEL_BIT;
	/* handle DTMF TODO */
	if ((pid.protocol[2] == ISDN_PID_L2_B_TRANS) &&
		(pid.protocol[1] == ISDN_PID_L1_B_64TRANS))
		pid.protocol[2] = ISDN_PID_L2_B_TRANSDTMF;
	if (aplci->Bprotocol.B3 > 23) {
		int_errtxt("wrong B3 prot %x", aplci->Bprotocol.B3);
		return;
	}
	pid.protocol[3] = (1 << aplci->Bprotocol.B3) |
		ISDN_PID_LAYER(3) | ISDN_PID_BCHANNEL_BIT;
	capidebug(CAPI_DBG_NCCI, "ncciInitSt B1(%x) B2(%x) B3(%x) global(%d) ch(%x)",
   		pid.protocol[1], pid.protocol[2], pid.protocol[3], pid.global, 
		aplci->channel);
	capidebug(CAPI_DBG_NCCI, "ncciInitSt ch(%d) aplci->contr->binst(%p)",
		aplci->channel & 3, aplci->contr->binst);
	pid.protocol[4] = ISDN_PID_L4_B_CAPI20;
	ncci->binst = ControllerSelChannel(aplci->contr, aplci->channel);
	if (!ncci->binst) {
		int_error();
		return;
	}
	capidebug(CAPI_DBG_NCCI, "ncciInitSt ncci->binst(%p)", ncci->binst);
	memset(&ncci->binst->inst.pid, 0, sizeof(mISDN_pid_t));
	ncci->binst->inst.data = ncci;
	ncci->binst->inst.pid.layermask = ISDN_LAYER(4);
	ncci->binst->inst.pid.protocol[4] = ISDN_PID_L4_B_CAPI20;
	if (pid.protocol[3] == ISDN_PID_L3_B_TRANS) {
		ncci->binst->inst.pid.protocol[3] = ISDN_PID_L3_B_TRANS;
		ncci->binst->inst.pid.layermask |= ISDN_LAYER(3);
		test_and_set_bit(NCCI_STATE_L3TRANS, &ncci->state);
	}
	retval = ncci->binst->inst.obj->ctrl(ncci->binst->bst,
		MGR_REGLAYER | INDICATION, &ncci->binst->inst); 
	if (retval) {
		printk(KERN_WARNING "%s MGR_REGLAYER | INDICATION ret(%d)\n",
			__FUNCTION__, retval);
		return;
	}
	stpara.maxdatalen = ncci->appl->reg_params.datablklen;
	stpara.up_headerlen = CAPI_B3_DATA_IND_HEADER_SIZE;
	stpara.down_headerlen = 0;
                        
	retval = ncci->binst->inst.obj->ctrl(ncci->binst->bst,
		MGR_ADDSTPARA | REQUEST, &stpara);
	if (retval) {
		printk(KERN_WARNING "%s MGR_SETSTACK | REQUEST ret(%d)\n",
			__FUNCTION__, retval);
	}
	retval = ncci->binst->inst.obj->ctrl(ncci->binst->bst,
		MGR_SETSTACK | REQUEST, &pid);
	if (retval) {
		printk(KERN_WARNING "%s MGR_SETSTACK | REQUEST ret(%d)\n",
			__FUNCTION__, retval);
		return;
	}
}

static void
ncciReleaseSt(Ncci_t *ncci)
{
	int retval;

	if (ncci->ncci_m.state != ST_NCCI_N_0)
		ncciL4L3(ncci, DL_RELEASE | REQUEST, 0, 0, NULL, NULL);
	if (ncci->binst) {
		retval = ncci->binst->inst.obj->ctrl(ncci->binst->inst.st,
			MGR_CLEARSTACK | REQUEST, NULL);
		if (retval)
			int_error();
		ncci->binst = NULL;
	}
}

void
ncciLinkUp(Ncci_t *ncci)
{
	ncciInitSt(ncci);
}

void
ncciLinkDown(Ncci_t *ncci)
{
	if (ncci->binst)
		ncciReleaseSt(ncci);
	FsmEvent(&ncci->ncci_m, EV_DL_DOWN_IND, 0);
}

__u16
ncciSelectBprotocol(Ncci_t *ncci)
{
	int retval;
	retval = FsmEvent(&ncci->ncci_m, EV_AP_SELECT_B_PROTOCOL, 0);
	if (retval)
		return CapiMessageNotSupportedInCurrentState;
	return CapiSuccess;
}

void
ncciDestr(Ncci_t *ncci)
{
	int i;

	printk(KERN_DEBUG "%s: ncci(%p) NCCI(%x)\n",
		__FUNCTION__, ncci, ncci->addr); 
	capidebug(CAPI_DBG_NCCI, "ncciDestr NCCI %x", ncci->addr);
	if (ncci->binst)
		ncciReleaseSt(ncci);

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
	FsmEvent(&ncci->ncci_m, EV_AP_RELEASE, NULL);
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

void
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
		int_error();
		capierr = CAPI_SENDQUEUEFULL;
		goto fail;
	}
	mISDN_HEAD_DINFO(skb) = ControllerNextId(ncci->contr);
	ncci->xmit_skb_handles[i].PktId = mISDN_HEAD_DINFO(skb);
	ncci->xmit_skb_handles[i].DataHandle = CAPIMSG_REQ_DATAHANDLE(skb->data);
	ncci->xmit_skb_handles[i].MsgId = CAPIMSG_MSGID(skb->data);

	/* the data begins behind the header, we don't use Data32/Data64 here */
	skb_pull(skb, len);

	if (ncci->state & NCCI_STATE_FCTRL) {
		if (test_and_set_bit(NCCI_STATE_BUSY, &ncci->state)) {
			skb_queue_tail(&ncci->squeue, skb);
			return;
		}
		if (skb_queue_len(&ncci->squeue)) {
			skb_queue_tail(&ncci->squeue, skb);
			skb = skb_dequeue(&ncci->squeue);
			i = -1;
		}
	}
	
	err = ncciL4L3(ncci, DL_DATA | REQUEST, mISDN_HEAD_DINFO(skb), 0, NULL, skb);
	if (!err)
		return;

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
	}
fail:
	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		dev_kfree_skb(skb);
		return;
	}
	capi_cmsg_header(cmsg, ncci->AppPlci->appl->ApplId, CAPI_DATA_B3, CAPI_CONF, 
		CAPIMSG_MSGID(skb->data), ncci->addr);
	/* illegal len (too short) ??? */
	cmsg->DataHandle = CAPIMSG_REQ_DATAHANDLE(skb->data);
	cmsg->Info = capierr;
	Send2Application(ncci, cmsg);
	dev_kfree_skb(skb);
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
	if (ncci->state & NCCI_STATE_FCTRL) {
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
	int		prim, dinfo, ret;

	if (ncci->ncci_m.debug)
		log_skbdata(skb);
	if (skb->len < CAPIMSG_BASELEN) {
		int_error();
		ret = -EINVAL;
	} else {
		prim = CAPIMSG_CMD(skb->data);
		dinfo = CAPIMSG_MSGID(skb->data);
// should use an other statemachine
		skb_pull(skb, CAPIMSG_BASELEN);
		if (ncci->ncci_m.debug)
			log_skbdata(skb);
		ret = if_newhead(&ncci->binst->inst.down, prim, dinfo, skb);
		if (prim == CAPI_DISCONNECT_B3_RESP) {
			FsmChangeState(&ncci->ncci_m, ST_NCCI_N_0);
			ncciDestr(ncci);
		}
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
			retval = FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_REQ, cmsg);
			break;
		case CAPI_CONNECT_B3_RESP:
			retval = FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_RESP, cmsg);
			break;
		case CAPI_CONNECT_B3_ACTIVE_RESP:
			retval = FsmEvent(&ncci->ncci_m, EV_AP_CONNECT_B3_ACTIVE_RESP, cmsg);
			break;
		case CAPI_DISCONNECT_B3_REQ:
			retval = FsmEvent(&ncci->ncci_m, EV_AP_DISCONNECT_B3_REQ, cmsg);
			break;
		case CAPI_DISCONNECT_B3_RESP:
			retval = FsmEvent(&ncci->ncci_m, EV_AP_DISCONNECT_B3_RESP, cmsg);
			break;
		case CAPI_FACILITY_REQ:
			retval = FsmEvent(&ncci->ncci_m, EV_AP_FACILITY_REQ, cmsg);
			break;
		case CAPI_MANUFACTURER_REQ:
			retval = FsmEvent(&ncci->ncci_m, EV_AP_MANUFACTURER_REQ, cmsg);
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

void
ncciSendMessage(Ncci_t *ncci, struct sk_buff *skb)
{
	_cmsg	*cmsg;

	if (!test_bit(NCCI_STATE_L3TRANS, &ncci->state)) {
		ncci_l4l3_direct(ncci, skb);
		return;
	}
	// we're not using the cmsg for DATA_B3 for performance reasons
	switch (CAPICMD(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data))) {
		case CAPI_DATA_B3_REQ:
			if (ncci->ncci_m.state == ST_NCCI_N_ACT) {
				ncciDataReq(ncci, skb);
			} else {
				AnswerMessage2Application(ncci->appl, skb, 
					CapiMessageNotSupportedInCurrentState);
				dev_kfree_skb(skb);
			}
			return;
		case CAPI_DATA_B3_RESP:
			ncciDataResp(ncci, skb);
			return;
	}
	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		dev_kfree_skb(skb);
		return;
	}
	capi_message2cmsg(cmsg, skb->data);
	ncciGetCmsg(ncci, cmsg);
	dev_kfree_skb(skb);
}

static int
ncci_l3l4_direct(Ncci_t *ncci, mISDN_head_t *hh, struct sk_buff *skb)
{
	struct sk_buff	*nskb;
	__u16		msgnr,tlen, prim = hh->prim;

	capidebug(CAPI_DBG_NCCI_L3, "%s: NCCI %x prim(%x) dinfo (%x) skb(%p) s(%x)",
		__FUNCTION__, ncci->addr, hh->prim, hh->dinfo, skb, ncci->state);
	if (ncci->ncci_m.debug)
		log_skbdata(skb);
	switch (hh->prim) {
		case CAPI_CONNECT_B3_IND:
			if (ncci->addr == 0xffffffff) {
				ncci->addr = CAPIMSG_U32(skb->data, 0);
				ncci->addr &= 0xffff0000;
				ncci->addr |= ncci->AppPlci->addr;
#ifdef OLDCAPI_DRIVER_INTERFACE
				ncci->contr->ctrl->new_ncci(ncci->contr->ctrl, ncci->appl->ApplId, ncci->addr, ncci->window);
#endif
			} else {
				int_error();
				return(-EBUSY);
			}
			capimsg_setu32(skb->data, 0, ncci->addr);
		case CAPI_DATA_B3_IND:
		case CAPI_CONNECT_B3_ACTIVE_IND:
		case CAPI_DISCONNECT_B3_IND:
		case CAPI_FACILITY_IND:
		case CAPI_MANUFACTURER_IND:
			msgnr = ncci->appl->MsgId++;
			break;
		case CAPI_CONNECT_B3_CONF:
			if (ncci->addr == 0xffffffff) {
				ncci->addr = CAPIMSG_U32(skb->data, 0);
#ifdef OLDCAPI_DRIVER_INTERFACE
				ncci->contr->ctrl->new_ncci(ncci->contr->ctrl, ncci->appl->ApplId, ncci->addr, ncci->window);
#endif
			}
		case CAPI_DATA_B3_CONF:
		case CAPI_DISCONNECT_B3_CONF:
		case CAPI_FACILITY_CONF:
		case CAPI_MANUFACTURER_CONF:
			msgnr = hh->dinfo & 0xffff;
			break;
		default:
			int_error();
			return(-EINVAL);
	}
	if (skb_headroom(skb) < CAPIMSG_BASELEN) {
		capidebug(CAPI_DBG_NCCI_L3, "%s: only %d bytes headroom, need %d",
			__FUNCTION__, skb_headroom(skb), CAPIMSG_BASELEN);
		nskb = skb_realloc_headroom(skb, CAPIMSG_BASELEN);
		if (!nskb) {
			int_error();
			return(-ENOMEM);
		}
		dev_kfree_skb(skb);
		#warning TODO adjust DATA_B3_IND data
      	} else { 
		nskb = skb;
	}
	skb_push(nskb, CAPIMSG_BASELEN);
	if (prim == CAPI_DATA_B3_IND)
		tlen = CAPI_B3_DATA_IND_HEADER_SIZE;
	else
		tlen = nskb->len + CAPIMSG_BASELEN;
	CAPIMSG_SETLEN(nskb->data, tlen);
	CAPIMSG_SETAPPID(nskb->data, ncci->appl->ApplId);
	CAPIMSG_SETCOMMAND(nskb->data, (prim>>8) & 0xff);
	CAPIMSG_SETSUBCOMMAND(nskb->data, prim & 0xff); 
	CAPIMSG_SETMSGID(nskb->data, msgnr);
	if (ncci->ncci_m.debug)
		log_skbdata(nskb);
#ifdef OLDCAPI_DRIVER_INTERFACE
	ncci->contr->ctrl->handle_capimsg(ncci->contr->ctrl, ncci->appl->ApplId, nskb);
#else
	capi_ctr_handle_message(ncci->contr->ctrl, ncci->appl->ApplId, nskb);
#endif
	return(0);
}

int
ncci_l3l4(mISDNif_t *hif, struct sk_buff *skb)
{
	Ncci_t 		*ncci;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	ncci = hif->fdata;
	if (!test_bit(NCCI_STATE_L3TRANS, &ncci->state))
		return(ncci_l3l4_direct(ncci, hh, skb));
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
			FsmEvent(&ncci->ncci_m, EV_DL_ESTABLISH_IND, skb);
			break;
		case DL_ESTABLISH | CONFIRM:
			FsmEvent(&ncci->ncci_m, EV_DL_ESTABLISH_CONF, skb);
			break;
		case DL_RELEASE | INDICATION:
			FsmEvent(&ncci->ncci_m, EV_DL_RELEASE_IND, skb);
			break;
		case DL_RELEASE | CONFIRM:
			FsmEvent(&ncci->ncci_m, EV_DL_RELEASE_CONF, skb);
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
	capidebug(CAPI_DBG_NCCI_L3, "%s: NCCI %x prim(%x) dtyp(%x) skb(%p)",
		__FUNCTION__, ncci->addr, prim, dtyp, skb);
	if (skb)
		return(if_newhead(&ncci->binst->inst.down, prim, dtyp, skb));
	else
		return(if_link(&ncci->binst->inst.down, prim, dtyp,
			len, arg, 8));
}

void
init_ncci(void)
{
	ncci_fsm.state_count = ST_NCCI_COUNT;
	ncci_fsm.event_count = EV_NCCI_COUNT;
	ncci_fsm.strEvent = str_ev_ncci;
	ncci_fsm.strState = str_st_ncci;
	
	FsmNew(&ncci_fsm, fn_ncci_list, FN_NCCI_COUNT);
}

void
free_ncci(void)
{
	FsmFree(&ncci_fsm);
}
