/* $Id: app_plci.c,v 1.9 2004/01/30 23:46:37 keil Exp $
 *
 */

#include "m_capi.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"

#define AppPlciDebug(aplci, lev, fmt, args...) \
	capidebug(lev, fmt, ## args)

static void 	AppPlciClearOtherApps(AppPlci_t *);
static void 	AppPlciInfoIndMsg(AppPlci_t *,  __u32, unsigned char);
static void 	AppPlciInfoIndIE(AppPlci_t *, unsigned char, __u32, Q931_info_t *);
static int 	AppPlciLinkUp(AppPlci_t *);
static int	AppPlciLinkDown(AppPlci_t *);

static u_char BEARER_SPEECH_64K_ALAW[] = {4, 3, 0x80, 0x90, 0xA3};
static u_char BEARER_SPEECH_64K_ULAW[] = {4, 3, 0x80, 0x90, 0xA2};
static u_char BEARER_UNRES_DIGITAL_64K[] = {4, 2, 0x88, 0x90};
static u_char BEARER_RES_DIGITAL_64K[] = {4, 2, 0x89, 0x90};
static u_char BEARER_31AUDIO_64K_ALAW[] = {4, 3, 0x90, 0x90, 0xA3};
static u_char BEARER_31AUDIO_64K_ULAW[] = {4, 3, 0x90, 0x90, 0xA2};
static u_char HLC_TELEPHONY[] = {0x7d, 2, 0x91, 0x81};
static u_char HLC_FACSIMILE[] = {0x7d, 2, 0x91, 0x84};

__u16 q931CIPValue(Q931_info_t *qi)
{
	__u16	CIPValue = 0;
	u_char	*p;

	if (!qi)
		return 0;
	if (!qi->bearer_capability)
		return 0;
	p = (u_char *)qi;
	p += L3_EXTRA_SIZE + qi->bearer_capability;
	if (memcmp(p, BEARER_SPEECH_64K_ALAW, 5) == 0
	    || memcmp(p, BEARER_SPEECH_64K_ULAW, 5) == 0) {
		CIPValue = 1;
	} else if (memcmp(p, BEARER_UNRES_DIGITAL_64K, 4) == 0) {
		CIPValue = 2;
	} else if (memcmp(p, BEARER_RES_DIGITAL_64K, 4) == 0) {
		CIPValue = 3;
	} else if (memcmp(p, BEARER_31AUDIO_64K_ALAW, 5) == 0
		   || memcmp(p, BEARER_31AUDIO_64K_ULAW, 5) == 0) {
		CIPValue = 4;
	} else {
		CIPValue = 0;
	}

	if (!qi->hlc)
		return CIPValue;

	p = (u_char *)qi;
	p += L3_EXTRA_SIZE + qi->hlc;
	if ((CIPValue == 1) || (CIPValue == 4)) {
		if (memcmp(p, HLC_TELEPHONY, 4) == 0) {
			CIPValue = 16;
		} else if (memcmp(p, HLC_FACSIMILE, 4) == 0) {
			CIPValue = 17;
		}
	}
	return CIPValue;
}

u_int plci_parse_channel_id(u_char *p)
{
	u_int	cid = -1;
	int	l;

	if (p) {
		p++;
		capidebug(CAPI_DBG_PLCI_INFO, "%s: l(%d) %x", __FUNCTION__, p[0], p[1]);
		l = *p++;
		if (l == 1) {
			cid = *p;
		} else if (l == 3) {
			cid =  *p++ << 16;
			cid |= *p++ << 8;
			cid |= *p;
		}
	}
	return(cid);
}

__u16 CIPValue2setup(__u16 CIPValue, struct sk_buff *skb)
{
	switch (CIPValue) {
		case 16:
			mISDN_AddvarIE(skb, BEARER_31AUDIO_64K_ALAW);
			mISDN_AddvarIE(skb, HLC_TELEPHONY);
			break;
		case 17:
			mISDN_AddvarIE(skb, BEARER_31AUDIO_64K_ALAW);
			mISDN_AddvarIE(skb, HLC_FACSIMILE);
			break;
		case 1:
			mISDN_AddvarIE(skb, BEARER_SPEECH_64K_ALAW);
			break;
		case 2:
			mISDN_AddvarIE(skb, BEARER_UNRES_DIGITAL_64K);
			break;
		case 3:
			mISDN_AddvarIE(skb, BEARER_RES_DIGITAL_64K);
			break;
		case 4:
			mISDN_AddvarIE(skb, BEARER_31AUDIO_64K_ALAW);
			break;
		default:
			return CapiIllMessageParmCoding;
	}
	return 0;
}

__u16 cmsg2setup_req(_cmsg *cmsg, struct sk_buff *skb)
{
	if (CIPValue2setup(cmsg->CIPValue, skb))
		goto err;
	mISDN_AddIE(skb, IE_CALLING_PN, cmsg->CallingPartyNumber);
	mISDN_AddIE(skb, IE_CALLING_SUB, cmsg->CallingPartySubaddress);
	mISDN_AddIE(skb, IE_CALLED_PN, cmsg->CalledPartyNumber);
	mISDN_AddIE(skb, IE_CALLED_SUB, cmsg->CalledPartySubaddress);
	mISDN_AddIE(skb, IE_BEARER, cmsg->BC);
	mISDN_AddIE(skb, IE_LLC, cmsg->LLC);
	mISDN_AddIE(skb, IE_HLC, cmsg->HLC);
	return 0;
 err:
	return CapiIllMessageParmCoding;
}

__u16 cmsg2info_req(_cmsg *cmsg, struct sk_buff *skb)
{
	mISDN_AddIE(skb, IE_KEYPAD, cmsg->Keypadfacility);
	mISDN_AddIE(skb, IE_CALLED_PN, cmsg->CalledPartyNumber);
	return 0;
}

__u16 cmsg2alerting_req(_cmsg *cmsg, struct sk_buff *skb)
{
	mISDN_AddIE(skb, IE_USER_USER, cmsg->Useruserdata);
	return 0;
}

__u16 AppPlciCheckBprotocol(AppPlci_t *aplci, _cmsg *cmsg)
{
	struct capi_ctr	*ctrl = aplci->contr->ctrl;
	u_long		sprot;

	sprot = ctrl->profile.support1;
	if (!test_bit(cmsg->B1protocol, &sprot))
		return CapiB1ProtocolNotSupported;
	sprot = ctrl->profile.support2;
	if (!test_bit(cmsg->B2protocol, &sprot))
		return CapiB2ProtocolNotSupported;
	sprot = ctrl->profile.support3;
	if (!test_bit(cmsg->B3protocol, &sprot))
		return CapiB3ProtocolNotSupported;
	aplci->Bprotocol.B1 = cmsg->B1protocol;
	aplci->Bprotocol.B2 = cmsg->B2protocol;
	aplci->Bprotocol.B3 = cmsg->B3protocol;
	if (cmsg->B1configuration && cmsg->B1configuration[0]) {
		if (cmsg->B1configuration[0] > 15) {
			int_errtxt("B1cfg too large(%d)", cmsg->B1configuration[0]);
			return CapiB1ProtocolParameterNotSupported;
		}
		memcpy(&aplci->Bprotocol.B1cfg[0], cmsg->B1configuration, cmsg->B1configuration[0] + 1);
	} else
		aplci->Bprotocol.B1cfg[0] = 0;
	if (cmsg->B2configuration && cmsg->B2configuration[0]) {
		if (cmsg->B2configuration[0] > 15) {
			int_errtxt("B2cfg too large(%d)", cmsg->B2configuration[0]);
			return CapiB2ProtocolParameterNotSupported;
		}
		memcpy(&aplci->Bprotocol.B2cfg[0], cmsg->B2configuration, cmsg->B2configuration[0] + 1);
	} else
		aplci->Bprotocol.B2cfg[0] = 0;
	if (cmsg->B3configuration && cmsg->B3configuration[0]) {
		if (cmsg->B3configuration[0] > 79) {
			int_errtxt("B3cfg too large(%d)", cmsg->B3configuration[0]);
			return CapiB3ProtocolParameterNotSupported;
		}
		memcpy(&aplci->Bprotocol.B3cfg[0], cmsg->B3configuration, cmsg->B3configuration[0] + 1);
	} else
		aplci->Bprotocol.B3cfg[0] = 0;
	return 0;
}

// --------------------------------------------------------------------
// PLCI state machine
//
// Some rules:
//   *  EV_AP_*  events come from CAPI Application
//   *  EV_L3_*  events come from the ISDN stack
//   *  EV_PI_*  events generated in PLCI handling
//   *  messages are send in the routine that handle the event
//
// --------------------------------------------------------------------

enum {
	ST_PLCI_P_0,
	ST_PLCI_P_0_1,
	ST_PLCI_P_1,
	ST_PLCI_P_2,
	ST_PLCI_P_3,
	ST_PLCI_P_4,
	ST_PLCI_P_ACT,
	ST_PLCI_P_5,
	ST_PLCI_P_6,
	ST_PLCI_P_RES,
}

const ST_PLCI_COUNT = ST_PLCI_P_RES + 1;

static char *str_st_plci[] = {	
	"ST_PLCI_P_0",
	"ST_PLCI_P_0_1",
	"ST_PLCI_P_1",
	"ST_PLCI_P_2",
	"ST_PLCI_P_3",
	"ST_PLCI_P_4",
	"ST_PLCI_P_ACT",
	"ST_PLCI_P_5",
	"ST_PLCI_P_6",
	"ST_PLCI_P_RES",
}; 

enum {
	EV_AP_CONNECT_REQ,
	EV_PI_CONNECT_CONF,
	EV_PI_CONNECT_IND,
	EV_AP_CONNECT_RESP,
	EV_PI_CONNECT_ACTIVE_IND,
	EV_AP_CONNECT_ACTIVE_RESP,
	EV_AP_ALERT_REQ,
	EV_AP_INFO_REQ,
	EV_PI_INFO_IND,
	EV_PI_FACILITY_IND,
	EV_AP_SELECT_B_PROTOCOL_REQ,
	EV_AP_DISCONNECT_REQ,
	EV_PI_DISCONNECT_IND,
	EV_AP_DISCONNECT_RESP,
	EV_AP_SUSPEND_REQ,
	EV_PI_SUSPEND_CONF,
	EV_AP_RESUME_REQ,
	EV_PI_RESUME_CONF,
	EV_PI_CHANNEL_ERR,
	EV_L3_SETUP_IND,
	EV_L3_SETUP_CONF_ERR,
	EV_L3_SETUP_CONF,
	EV_L3_SETUP_COMPL_IND,
	EV_L3_DISCONNECT_IND,
	EV_L3_RELEASE_IND,
	EV_L3_RELEASE_PROC_IND,
	EV_L3_NOTIFY_IND,
	EV_L3_SUSPEND_ERR,
	EV_L3_SUSPEND_CONF,
	EV_L3_RESUME_ERR,
	EV_L3_RESUME_CONF,
	EV_L3_REJECT_IND,
	EV_PH_CONTROL_IND,
	EV_AP_RELEASE,
}

const EV_PLCI_COUNT = EV_AP_RELEASE + 1;

static char* str_ev_plci[] = {
	"EV_AP_CONNECT_REQ",
	"EV_PI_CONNECT_CONF",
	"EV_PI_CONNECT_IND",
	"EV_AP_CONNECT_RESP",
	"EV_PI_CONNECT_ACTIVE_IND",
	"EV_AP_CONNECT_ACTIVE_RESP",
	"EV_AP_ALERT_REQ",
	"EV_AP_INFO_REQ",
	"EV_PI_INFO_IND",
	"EV_PI_FACILITY_IND",
	"EV_AP_SELECT_B_PROTOCOL_REQ",
	"EV_AP_DISCONNECT_REQ",
	"EV_PI_DISCONNECT_IND",
	"EV_AP_DISCONNECT_RESP",
	"EV_AP_SUSPEND_REQ",
	"EV_PI_SUSPEND_CONF",
	"EV_AP_RESUME_REQ",
	"EV_PI_RESUME_CONF",
	"EV_PI_CHANNEL_ERR",
	"EV_L3_SETUP_IND",
	"EV_L3_SETUP_CONF_ERR",
	"EV_L3_SETUP_CONF",
	"EV_L3_SETUP_COMPL_IND",
	"EV_L3_DISCONNECT_IND",
	"EV_L3_RELEASE_IND",
	"EV_L3_RELEASE_PROC_IND",
	"EV_L3_NOTIFY_IND",
	"EV_L3_SUSPEND_ERR",
	"EV_L3_SUSPEND_CONF",
	"EV_L3_RESUME_ERR",
	"EV_L3_RESUME_CONF",
	"EV_L3_REJECT_IND",
	"EV_PH_CONTROL_IND",
	"EV_AP_RELEASE",
};

static struct Fsm plci_fsm =
{ 0, 0, 0, 0, 0 };

static void
AppPlci_debug(struct FsmInst *fi, char *fmt, ...)
{
	char tmp[128];
	char *p = tmp;
	va_list args;
	AppPlci_t *aplci = fi->userdata;
  
	va_start(args, fmt);
	p += sprintf(p, "APLCI 0x%x: ", aplci->addr);
	p += vsprintf(p, fmt, args);
	*p = 0;
	AppPlciDebug(aplci, CAPI_DBG_PLCI_STATE, tmp);
	va_end(args);
}

static inline void
Send2Application(AppPlci_t *aplci, _cmsg *cmsg)
{
	SendCmsg2Application(aplci->appl, cmsg);
}

static void
SendingDelayedMsg(AppPlci_t *aplci)
{
	struct sk_buff  *skb;

	while((skb = skb_dequeue(&aplci->delayedq))) {
		if (test_bit(APPL_STATE_RELEASE, &aplci->appl->state)) {
			printk(KERN_WARNING "%s: Application allready released\n", __FUNCTION__);
			dev_kfree_skb(skb);
		} else {
#ifdef OLDCAPI_DRIVER_INTERFACE
			aplci->appl->contr->ctrl->handle_capimsg(aplci->appl->contr->ctrl, aplci->appl->ApplId, skb);
#else
			capi_ctr_handle_message(aplci->appl->contr->ctrl, aplci->appl->ApplId, skb);
#endif
		}
	}
	test_and_clear_bit(PLCI_STATE_SENDDELAYED, &aplci->plci->state);
}

static void
Send2ApplicationDelayed(AppPlci_t *aplci, _cmsg *cmsg)
{
	struct sk_buff	*skb;
	
	if (test_bit(APPL_STATE_RELEASE, &aplci->appl->state)) {
		printk(KERN_WARNING "%s: Application allready released\n", __FUNCTION__);
		cmsg_free(cmsg);
		return;
	}
	if (!(skb = alloc_skb(CAPI_MSG_DEFAULT_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING "%s: no mem for %d bytes\n", __FUNCTION__, CAPI_MSG_DEFAULT_LEN);
		int_error();
		cmsg_free(cmsg);
		return;
	}
	capi_cmsg2message(cmsg, skb->data);
	AppPlciDebug(aplci, CAPI_DBG_APPL_MSG, "%s: len(%d) applid(%x) %s msgnr(%d) addr(%08x)",
		__FUNCTION__, CAPIMSG_LEN(skb->data), cmsg->ApplId, capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		cmsg->Messagenumber, cmsg->adr.adrController);
	cmsg_free(cmsg);
	if (CAPI_MSG_DEFAULT_LEN < CAPIMSG_LEN(skb->data)) {
		printk(KERN_ERR "%s: CAPI_MSG_DEFAULT_LEN overrun (%d/%d)\n", __FUNCTION__,
			CAPIMSG_LEN(skb->data), CAPI_MSG_DEFAULT_LEN);
		int_error();
		dev_kfree_skb(skb);
		return;
	}
	skb_put(skb, CAPIMSG_LEN(skb->data));
	skb_queue_tail(&aplci->delayedq, skb);
	if (test_bit(PLCI_STATE_STACKREADY, &aplci->plci->state) &&
		!test_and_set_bit(PLCI_STATE_SENDDELAYED, &aplci->plci->state))
		SendingDelayedMsg(aplci);
}

static inline void
AppPlciCmsgHeader(AppPlci_t *aplci, _cmsg *cmsg, __u8 cmd, __u8 subcmd)
{
	capi_cmsg_header(cmsg, aplci->appl->ApplId, cmd, subcmd, 
			 aplci->appl->MsgId++, aplci->addr);
}

static void
plci_connect_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	struct sk_buff	*skb;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;

	mISDN_FsmChangeState(fi, ST_PLCI_P_0_1);
	test_and_set_bit(PLCI_STATE_OUTGOING, &plci->state);

	skb = mISDN_alloc_l3msg(260, MT_SETUP);
	
	if (!skb) {
		Info = CapiNoPlciAvailable;
		goto answer;
	}
	if ((Info = cmsg2setup_req(cmsg, skb))) {
		goto answer;
	}
	if ((Info = AppPlciCheckBprotocol(aplci, cmsg))) {
		goto answer;
	}

	plciNewCrReq(plci);
	plciL4L3(plci, CC_SETUP | REQUEST, skb);
answer:
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	if (cmsg->Info == 0) 
		cmsg->adr.adrPLCI = aplci->addr;
	mISDN_FsmEvent(fi, EV_PI_CONNECT_CONF, cmsg);
}

static void
plci_connect_conf(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg = arg;
  
	if (cmsg->Info == 0) {
		Send2Application(aplci, cmsg);
		mISDN_FsmChangeState(fi, ST_PLCI_P_1);
	} else {
		Send2Application(aplci, cmsg);
		mISDN_FsmChangeState(fi, ST_PLCI_P_0);
		AppPlciDestr(aplci);
	}
}

static void
plci_connect_ind(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_PLCI_P_2);
	Send2Application(fi->userdata, arg);
}

static void plci_suspend_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t	*plci = aplci->plci;

	plciL4L3(plci, CC_SUSPEND | REQUEST, arg); 
}

static void plci_resume_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;

	// we already sent CONF with Info = SuppInfo = 0
	mISDN_FsmChangeState(fi, ST_PLCI_P_RES);
	plciNewCrReq(plci);
	plciL4L3(plci, CC_RESUME | REQUEST, arg);
}

static void
plci_alert_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;
	
	if (test_and_set_bit(PLCI_STATE_ALERTING, &plci->state)) {
		Info = 0x0003; // other app is already alerting
	} else {
		struct sk_buff	*skb = mISDN_alloc_l3msg(10, MT_ALERTING);
		if (!skb) {
			int_error();
			goto answer;
		}
		Info = cmsg2alerting_req(cmsg, skb);
		if (Info == 0) {
			plciL4L3(plci, CC_ALERTING | REQUEST, skb);
		}
	}
answer:	
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	Send2Application(aplci, cmsg);
}

static void
plci_connect_resp(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	unsigned char	cause[4];
	_cmsg		*cmsg = arg;
	struct sk_buff	*skb;

	if (cmsg->Reject == 0) { // accept
		if (AppPlciCheckBprotocol(aplci, cmsg)) {
			int_error();
		}
		AppPlciClearOtherApps(aplci);
		plciL4L3(plci, CC_CONNECT | REQUEST, NULL);
		mISDN_FsmChangeState(fi, ST_PLCI_P_4);
		cmsg_free(cmsg);
		return;
	}
	// ignore, reject 
	memcpy(cause, "\x02\x80", 2); // IE CAUSE, location = local
	switch (cmsg->Reject) {
		case 2: cause[2] = 0x90; break; // normal call clearing
		case 3: cause[2] = 0x91; break; // user busy
		case 4: cause[2] = 0xac; break; // req circuit/channel not avail
		case 5: cause[2] = 0x9d; break; // fac rejected
		case 6: cause[2] = 0x86; break; // channel unacceptable
		case 7: cause[2] = 0xd8; break; // incompatible dest
		case 8: cause[2] = 0x9b; break; // dest out of order
		default:
			if ((cmsg->Reject & 0xff00) == 0x3400) {
				cause[2] = cmsg->Reject & 0xff;
			} else {
				cause[2] = 0x90; break; // normal call clearing
			}
	}
	// FIXME
	// WHY ???
	// if (cmsg->Reject != 1) {
		// ignore
	//	AppPlciClearOtherApps(aplci);
	// }
	// plciDetachAppPlci(plci, aplci);
	if (plci->nAppl == 1) {
		int prim;
		if (test_bit(PLCI_STATE_ALERTING, &plci->state))
			prim = CC_DISCONNECT | REQUEST;
		else 
			// if we already answered, we can't just ignore but must clear actively
			prim = CC_RELEASE_COMPLETE | REQUEST;
		skb = mISDN_alloc_l3msg(10, MT_DISCONNECT);
		if (!skb) {
			plciL4L3(plci, prim, NULL);
		} else {
			mISDN_AddIE(skb, IE_CAUSE, cause);
			plciL4L3(plci, prim, skb);
		}
	}
	cmsg->Command = CAPI_DISCONNECT;
	cmsg->Subcommand = CAPI_IND;
	cmsg->Messagenumber = aplci->appl->MsgId++;
	cmsg->Reject = 0x3400 | cause[2];
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_connect_active_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t *aplci = fi->userdata;

	mISDN_FsmChangeState(fi, ST_PLCI_P_ACT);
	AppPlciLinkUp(aplci);
	if (test_bit(PLCI_STATE_STACKREADY, &aplci->plci->state))
		Send2Application(aplci, arg);
	else
		Send2ApplicationDelayed(aplci, arg);
}

static void plci_connect_active_resp(struct FsmInst *fi, int event, void *arg)
{
	cmsg_free(arg);
}

static void plci_disconnect_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	u_char		cause[4];
	_cmsg		*cmsg = arg;

	mISDN_FsmChangeState(fi, ST_PLCI_P_5);
	
	if (!plci) {
		int_error();
		return;
	}
	// FIXME handle additional Inf
	capi_cmsg_answer(cmsg);
	cmsg->Reason = 0; // disconnect initiated
	Send2Application(aplci, cmsg);

	AppPlciLinkDown(aplci);

	if (!aplci->cause[0]) { // FIXME handle additional Info
		struct sk_buff	*skb;

		skb = mISDN_alloc_l3msg(10, MT_DISCONNECT);
		if (!skb) {
			plciL4L3(plci, CC_DISCONNECT | REQUEST, NULL);
		} else {
			memcpy(cause, "\x02\x80\x90", 3); // normal call clearing
			mISDN_AddIE(skb, IE_CAUSE, cause);
			plciL4L3(plci, CC_DISCONNECT | REQUEST, skb);
		}
	} else {
		/* release physical link */
		// FIXME
		plciL4L3(plci, CC_RELEASE | REQUEST, NULL);
	}
}

static void plci_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_PLCI_P_5);
}

static void plci_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	// facility_ind Resume: Reason = 0
	AppPlci_t	*aplci = fi->userdata;

	mISDN_FsmChangeState(fi, ST_PLCI_P_ACT);
	AppPlciLinkUp(aplci);
	if (test_bit(PLCI_STATE_STACKREADY, &aplci->plci->state))
		Send2Application(aplci, arg);
	else
		Send2ApplicationDelayed(aplci, arg);
}

static void
plci_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	mISDN_FsmChangeState(fi, ST_PLCI_P_6);
	Send2Application(fi->userdata, arg);
}

static void
plci_disconnect_resp(struct FsmInst *fi, int event, void *arg)
{
	if (arg)
		cmsg_free(arg);
	mISDN_FsmChangeState(fi, ST_PLCI_P_0);
	AppPlciDestr(fi->userdata);
}

static void
plci_appl_release(struct FsmInst *fi, int event, void *arg)
{
	AppPlciDestr(fi->userdata);
}

static void
plci_appl_release_disc(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;

	mISDN_FsmChangeState(fi, ST_PLCI_P_5);
	
	if (!plci) {
		int_error();
		return;
	}

	AppPlciLinkDown(aplci);

	if (!aplci->cause[0]) {
		struct sk_buff	*skb;

		skb = mISDN_alloc_l3msg(10, MT_DISCONNECT);
		if (!skb) {
			plciL4L3(plci, CC_DISCONNECT | REQUEST, NULL);
		} else {
			u_char *cause = "\x02\x80\x9f";

			mISDN_AddIE(skb, IE_CAUSE, cause);
			plciL4L3(plci, CC_DISCONNECT | REQUEST, skb);
		}
	} else {
		/* release physical link */
		// FIXME
		plciL4L3(plci, CC_RELEASE | REQUEST, NULL);
	}
}

static void
plci_cc_setup_conf(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;
	Q931_info_t	*qi = arg;
	u_char		*p;

	if (aplci->channel == -1) {/* no valid channel set */
		mISDN_FsmEvent(fi, EV_PI_CHANNEL_ERR, NULL);
		return;
	}
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		if (qi->connected_nr)
			cmsg->ConnectedNumber = &p[qi->connected_nr + 1];
		if (qi->connected_sub)
			cmsg->ConnectedSubaddress = &p[qi->connected_sub + 1];
		if (qi->llc)
			cmsg->LLC = &p[qi->llc + 1];
	}
	if (mISDN_FsmEvent(fi, EV_PI_CONNECT_ACTIVE_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_setup_conf_err(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;

	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_DISCONNECT, CAPI_IND);
	cmsg->Reason = CapiProtocolErrorLayer3;
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_channel_err(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;
	u_char		cause[4];
	struct sk_buff	*skb;

	skb = mISDN_alloc_l3msg(10, MT_RELEASE_COMPLETE);
	if (skb) {
		cause[0] = 2;
		cause[1] = 0x80;
		cause[2] = 0x86; /* channel unacceptable */
		mISDN_AddIE(skb, IE_CAUSE, cause);
		plciL4L3(aplci->plci, CC_RELEASE_COMPLETE | REQUEST, skb);
	} else
		int_error();
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_DISCONNECT, CAPI_IND);
	cmsg->Reason = CapiProtocolErrorLayer3;
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_setup_ind(struct FsmInst *fi, int event, void *arg)
{ 
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	_cmsg		*cmsg;
	u_char		*p;

	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_CONNECT, CAPI_IND);

	// FIXME: CW
	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		cmsg->CIPValue = q931CIPValue(qi);
		if (qi->called_nr)
			cmsg->CalledPartyNumber = &p[qi->called_nr + 1];
		if (qi->called_sub)
			cmsg->CalledPartySubaddress = &p[qi->called_sub + 1];
		if (qi->calling_nr)
			cmsg->CallingPartyNumber = &p[qi->calling_nr + 1];
		if (qi->calling_sub)
			cmsg->CallingPartySubaddress = &p[qi->calling_sub + 1];
		if (qi->bearer_capability)
			cmsg->BC = &p[qi->bearer_capability + 1];
		if (qi->llc)
			cmsg->LLC = &p[qi->llc + 1];
		if (qi->hlc)
			cmsg->HLC = &p[qi->hlc + 1];
		// all else set to default
	}
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_CONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_setup_compl_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;

	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_CONNECT_ACTIVE_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;

	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		if (qi->cause)
			memcpy(aplci->cause, &p[qi->cause + 1], 3);
	}
	if (aplci->appl->InfoMask & CAPI_INFOMASK_EARLYB3)
		return;

	AppPlciLinkDown(aplci);
	plciL4L3(aplci->plci, CC_RELEASE | REQUEST, NULL);
}

static void
plci_cc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;
	_cmsg		*cmsg;

	AppPlciLinkDown(aplci);
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		if (qi->cause)
			cmsg->Reason = 0x3400 | p[qi->cause + 3];
		else if (aplci->cause[0]) // cause from CC_DISCONNECT IND
			cmsg->Reason = 0x3400 | aplci->cause[2];
		else
			cmsg->Reason = 0;
	} else {
		cmsg->Reason = CapiProtocolErrorLayer1;
	}
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_notify_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	_cmsg		*cmsg;
	__u8		tmp[10], *p, *nf;

	if (!qi || !qi->notify)
		return;
	nf = (u_char *)qi;
	nf += L3_EXTRA_SIZE + qi->notify + 1;
	if (nf[0] != 1) // len != 1
		return;
	switch (nf[1]) {
		case 0x80: // user suspended
		case 0x81: // user resumed
			if (!aplci->appl)
				break;
			if (!(aplci->appl->NotificationMask & SuppServiceTP))
				break;
			CMSG_ALLOC(cmsg);
			AppPlciCmsgHeader(aplci, cmsg, CAPI_FACILITY, CAPI_IND);
			p = &tmp[1];
			p += capiEncodeWord(p, 0x8002 + (nf[1] & 1)); // Suspend/Resume Notification
			*p++ = 0; // empty struct
			tmp[0] = p - &tmp[1];
			cmsg->FacilitySelector = 0x0003;
			cmsg->FacilityIndicationParameter = tmp;
			Send2Application(aplci, cmsg);
			break;
	}
}

static void
AppPlci_suspend_reply(AppPlci_t *aplci, __u16 SuppServiceReason)
{
	_cmsg	*cmsg;
	__u8	tmp[10], *p;

	if (aplci->appl) {
		CMSG_ALLOC(cmsg);
		AppPlciCmsgHeader(aplci, cmsg, CAPI_FACILITY, CAPI_IND);
		p = &tmp[1];
		p += capiEncodeWord(p, 0x0004); // Suspend
		p += capiEncodeFacIndSuspend(p, SuppServiceReason);
		tmp[0] = p - &tmp[1];
		cmsg->FacilitySelector = 0x0003;
		cmsg->FacilityIndicationParameter = tmp;
		Send2Application(aplci, cmsg);
	}
	if (SuppServiceReason == CapiSuccess)
		mISDN_FsmEvent(&aplci->plci_m, EV_PI_SUSPEND_CONF, NULL);
}

static void
plci_cc_suspend_err(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;
	__u16		SuppServiceReason;
	
	if (qi) { // reject from network
		if (qi->cause) {
			p = (u_char *)qi;
			p += L3_EXTRA_SIZE + qi->cause;
			SuppServiceReason = 0x3400 | p[3];
		} else
			SuppServiceReason = CapiProtocolErrorLayer3;
	} else { // timeout
		SuppServiceReason = CapiTimeOut;
	}
	AppPlci_suspend_reply(aplci, SuppServiceReason);
}

static void
plci_cc_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;

	AppPlciLinkDown(aplci);

	AppPlci_suspend_reply(aplci, CapiSuccess);
	
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_resume_err(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;
	_cmsg		*cmsg;
	
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (qi) { // reject from network
		if (qi->cause) {
			p = (u_char *)qi;
			p += L3_EXTRA_SIZE + qi->cause;
			cmsg->Reason = 0x3400 | p[3];
		} else
			cmsg->Reason = 0;
	} else { // timeout
		cmsg->Reason = CapiProtocolErrorLayer1;
	}
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Q931_info_t	*qi = arg;
	_cmsg		*cmsg;
	__u8		tmp[10], *p;
	
	if (!qi || !qi->channel_id) {
		int_error();
		return;
	}
	p = (u_char *)qi;
	p += L3_EXTRA_SIZE + qi->channel_id;
	aplci->channel = plci_parse_channel_id(p);
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_FACILITY, CAPI_IND);
	p = &tmp[1];
	p += capiEncodeWord(p, 0x0005); // Suspend
	p += capiEncodeFacIndSuspend(p, CapiSuccess);
	tmp[0] = p - &tmp[1];
	cmsg->FacilitySelector = 0x0003;
	cmsg->FacilityIndicationParameter = tmp;
	if (mISDN_FsmEvent(&aplci->plci_m, EV_PI_RESUME_CONF, cmsg))
		cmsg_free(cmsg);
}

static void
plci_select_b_protocol_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg = arg;
	__u16		Info;
	int		ret;

	Info = AppPlciCheckBprotocol(aplci, cmsg);
	if (Info)
		goto answer;

	ret = AppPlciLinkDown(aplci);
	if (ret) {
		Info = 0x2001;
		goto answer;
	}
	ret = AppPlciLinkUp(aplci);
	if (ret < 0)
		Info = 0x2001;
	else
		Info = ret;
answer:
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	if (test_bit(PLCI_STATE_STACKREADY, &aplci->plci->state))
		Send2Application(aplci, arg);
	else
		Send2ApplicationDelayed(aplci, arg);
}

static void
plci_info_req_overlap(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;
	struct sk_buff	*skb;

	skb = mISDN_alloc_l3msg(100, MT_INFORMATION);
	if (skb) {
		Info = cmsg2info_req(cmsg, skb);
		if (Info == CapiSuccess)
			plciL4L3(plci, CC_INFORMATION | REQUEST, skb);
		else
			kfree_skb(skb);
	}
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	Send2Application(aplci, cmsg);
}

static void
plci_cc_ph_control_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	int		*tt = arg;
	_cmsg		*cmsg;
	__u8		tmp[2];

	if (!arg)
		return;
	AppPlciDebug(aplci, CAPI_DBG_PLCI_INFO, "%s: tt(%x)", __FUNCTION__, *tt);
	if ((*tt & ~DTMF_TONE_MASK) != DTMF_TONE_VAL)
		return;

	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_FACILITY, CAPI_IND);
	tmp[0] = 1;
	tmp[1] = *tt & DTMF_TONE_MASK;
	cmsg->FacilitySelector = 0x0001;
	cmsg->FacilityIndicationParameter = tmp;
	Send2Application(aplci, cmsg);
}

static void
plci_info_req(struct FsmInst *fi, int event, void *arg)
{
	// FIXME handle INFO CONF
	if (arg)
		cmsg_free(arg);
}

static struct FsmNode fn_plci_list[] =
{
  {ST_PLCI_P_0,		EV_AP_CONNECT_REQ,		plci_connect_req},
  {ST_PLCI_P_0,		EV_PI_CONNECT_IND,		plci_connect_ind},
  {ST_PLCI_P_0,		EV_AP_RESUME_REQ,		plci_resume_req},
  {ST_PLCI_P_0,		EV_L3_SETUP_IND,		plci_cc_setup_ind},
  {ST_PLCI_P_0,		EV_AP_RELEASE,			plci_appl_release},

  {ST_PLCI_P_0_1,	EV_PI_CONNECT_CONF,		plci_connect_conf},
  {ST_PLCI_P_0_1,	EV_AP_RELEASE,			plci_appl_release},

  {ST_PLCI_P_1,		EV_PI_CONNECT_ACTIVE_IND,	plci_connect_active_ind},
  {ST_PLCI_P_1,		EV_AP_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_1,		EV_PI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_1,		EV_AP_INFO_REQ,			plci_info_req_overlap},
  {ST_PLCI_P_1,		EV_L3_SETUP_CONF,		plci_cc_setup_conf},
  {ST_PLCI_P_1,		EV_L3_SETUP_CONF_ERR,		plci_cc_setup_conf_err},
  {ST_PLCI_P_1,		EV_L3_DISCONNECT_IND,		plci_cc_disconnect_ind},
  {ST_PLCI_P_1,		EV_L3_RELEASE_PROC_IND,		plci_cc_setup_conf_err},
  {ST_PLCI_P_1,		EV_L3_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_1,		EV_L3_REJECT_IND,		plci_cc_release_ind},
  {ST_PLCI_P_1,		EV_PI_CHANNEL_ERR,		plci_channel_err},
  {ST_PLCI_P_1,		EV_AP_RELEASE,			plci_appl_release_disc},

  {ST_PLCI_P_2,		EV_AP_ALERT_REQ,		plci_alert_req},
  {ST_PLCI_P_2,		EV_AP_CONNECT_RESP,		plci_connect_resp},
  {ST_PLCI_P_2,		EV_AP_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_2,		EV_PI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_2,		EV_AP_INFO_REQ,			plci_info_req},
  {ST_PLCI_P_2,		EV_L3_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_2,		EV_AP_RELEASE,			plci_appl_release_disc},

  {ST_PLCI_P_4,		EV_PI_CONNECT_ACTIVE_IND,	plci_connect_active_ind},
  {ST_PLCI_P_4,		EV_AP_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_4,		EV_PI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_4,		EV_AP_INFO_REQ,			plci_info_req},
  {ST_PLCI_P_4,		EV_L3_SETUP_COMPL_IND,		plci_cc_setup_compl_ind},
  {ST_PLCI_P_4,		EV_L3_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_4,		EV_PI_CHANNEL_ERR,		plci_channel_err},
  {ST_PLCI_P_4,		EV_AP_RELEASE,			plci_appl_release_disc},

  {ST_PLCI_P_ACT,	EV_AP_CONNECT_ACTIVE_RESP,	plci_connect_active_resp},
  {ST_PLCI_P_ACT,	EV_AP_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_ACT,	EV_PI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_ACT,	EV_AP_INFO_REQ,			plci_info_req},
  {ST_PLCI_P_ACT,	EV_AP_SELECT_B_PROTOCOL_REQ,	plci_select_b_protocol_req},
  {ST_PLCI_P_ACT,	EV_AP_SUSPEND_REQ,		plci_suspend_req},
  {ST_PLCI_P_ACT,	EV_PI_SUSPEND_CONF,		plci_suspend_conf},
  {ST_PLCI_P_ACT,	EV_L3_DISCONNECT_IND,		plci_cc_disconnect_ind},
  {ST_PLCI_P_ACT,	EV_L3_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_ACT,	EV_L3_NOTIFY_IND,		plci_cc_notify_ind},
  {ST_PLCI_P_ACT,	EV_L3_SUSPEND_ERR,		plci_cc_suspend_err},
  {ST_PLCI_P_ACT,	EV_L3_SUSPEND_CONF,		plci_cc_suspend_conf},
  {ST_PLCI_P_ACT,	EV_PH_CONTROL_IND,		plci_cc_ph_control_ind},
  {ST_PLCI_P_ACT,	EV_AP_RELEASE,			plci_appl_release_disc},

  {ST_PLCI_P_5,		EV_PI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_5,		EV_L3_RELEASE_IND,		plci_cc_release_ind},

  {ST_PLCI_P_6,		EV_AP_DISCONNECT_RESP,		plci_disconnect_resp},
  {ST_PLCI_P_6,		EV_AP_RELEASE,			plci_disconnect_resp},

  {ST_PLCI_P_RES,	EV_PI_RESUME_CONF,		plci_resume_conf},
  {ST_PLCI_P_RES,	EV_PI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_RES,	EV_L3_RESUME_ERR,		plci_cc_resume_err},
  {ST_PLCI_P_RES,	EV_L3_RESUME_CONF,		plci_cc_resume_conf},
  {ST_PLCI_P_RES,	EV_AP_RELEASE,			plci_appl_release_disc},
};

const int FN_PLCI_COUNT = sizeof(fn_plci_list)/sizeof(struct FsmNode);

int
AppPlciConstr(AppPlci_t **aplci, Application_t *appl, Plci_t *plci)
{
	AppPlci_t	*apl = AppPlci_alloc();	

	if (!apl)
		return(-ENOMEM);
	memset(apl, 0, sizeof(AppPlci_t));
	INIT_LIST_HEAD(&apl->head);
	INIT_LIST_HEAD(&apl->Nccis);
	apl->addr = plci->addr;
	apl->appl = appl;
	apl->plci = plci;
	apl->contr = plci->contr;
	apl->plci_m.fsm        = &plci_fsm;
	apl->plci_m.state      = ST_PLCI_P_0;
	apl->plci_m.debug      = plci->contr->debug & CAPI_DBG_PLCI_STATE;
	apl->plci_m.userdata   = apl;
	apl->plci_m.printdebug = AppPlci_debug;
	apl->channel = -1;
	skb_queue_head_init(&apl->delayedq);
	*aplci = apl;
	return(0);
}

void AppPlciDestr(AppPlci_t *aplci)
{
	struct list_head	*item, *next;
	
	if (aplci->plci) {
		AppPlciDebug(aplci, CAPI_DBG_PLCI, "%s plci state %s", __FUNCTION__,
			str_st_plci[aplci->plci_m.state]);
		if (aplci->plci_m.state != ST_PLCI_P_0) {
			struct sk_buff	*skb = mISDN_alloc_l3msg(10, MT_RELEASE_COMPLETE);
			unsigned char cause[] = {2,0x80,0x80| CAUSE_RESOURCES_UNAVAIL};

			if (skb) {
				mISDN_AddIE(skb, IE_CAUSE, cause);
				plciL4L3(aplci->plci, CC_RELEASE_COMPLETE | REQUEST, skb);
			}
		}
 		plciDetachAppPlci(aplci->plci, aplci);
	}
	list_for_each_safe(item, next, &aplci->Nccis) {
		ncciDelAppPlci((Ncci_t *)item);
	}
	if (aplci->appl)
		ApplicationDelAppPlci(aplci->appl, aplci);
	skb_queue_purge(&aplci->delayedq);
	AppPlci_free(aplci);
}

void
AppPlciRelease(AppPlci_t *aplci)
{
	struct list_head	*item, *next;

	list_for_each_safe(item, next, &aplci->Nccis) {
		ncciApplRelease((Ncci_t *)item);
	}
	mISDN_FsmEvent(&aplci->plci_m, EV_AP_RELEASE, NULL);
}

static int
AppPlciLinkUp(AppPlci_t *aplci)
{
	mISDN_pid_t	pid;
	mISDN_stPara_t	stpara;
	int		retval;

	if (aplci->channel == -1) {/* no valid channel set */
		int_error();
		return(-EINVAL);
	}
	memset(&pid, 0, sizeof(mISDN_pid_t));
	pid.layermask = ISDN_LAYER(1) | ISDN_LAYER(2) | ISDN_LAYER(3) |
		ISDN_LAYER(4);
	if (test_bit(PLCI_STATE_OUTGOING, &aplci->plci->state))
		pid.global = 1; // DTE, orginate
	else
		pid.global = 2; // DCE, answer
	if (aplci->Bprotocol.B1 > 23) {
		int_errtxt("wrong B1 prot %x", aplci->Bprotocol.B1);
		return(0x3001);
	}
	pid.protocol[1] = (1 << aplci->Bprotocol.B1) |
		ISDN_PID_LAYER(1) | ISDN_PID_BCHANNEL_BIT;
	if (aplci->Bprotocol.B1cfg[0])
		pid.param[1] = &aplci->Bprotocol.B1cfg[0];
	if (aplci->Bprotocol.B2 > 23) {
		int_errtxt("wrong B2 prot %x", aplci->Bprotocol.B2);
		return(0x3002);
	}
	pid.protocol[2] = (1 << aplci->Bprotocol.B2) |
		ISDN_PID_LAYER(2) | ISDN_PID_BCHANNEL_BIT;
	if (aplci->Bprotocol.B2cfg[0])
		pid.param[2] = &aplci->Bprotocol.B2cfg[0];
	/* handle DTMF TODO */
	if ((pid.protocol[2] == ISDN_PID_L2_B_TRANS) &&
		(pid.protocol[1] == ISDN_PID_L1_B_64TRANS))
		pid.protocol[2] = ISDN_PID_L2_B_TRANSDTMF;
	if (aplci->Bprotocol.B3 > 23) {
		int_errtxt("wrong B3 prot %x", aplci->Bprotocol.B3);
		return(0x3003);
	}
	pid.protocol[3] = (1 << aplci->Bprotocol.B3) |
		ISDN_PID_LAYER(3) | ISDN_PID_BCHANNEL_BIT;
	if (aplci->Bprotocol.B3cfg[0])
		pid.param[3] = &aplci->Bprotocol.B3cfg[0];
	capidebug(CAPI_DBG_PLCI, "AppPlciLinkUp B1(%x) B2(%x) B3(%x) global(%d) ch(%x)",
   		pid.protocol[1], pid.protocol[2], pid.protocol[3], pid.global, 
		aplci->channel);
	capidebug(CAPI_DBG_PLCI, "AppPlciLinkUp ch(%d) aplci->contr->linklist(%p)",
		aplci->channel & 3, aplci->contr->linklist);
	pid.protocol[4] = ISDN_PID_L4_B_CAPI20;
	aplci->link = ControllerSelChannel(aplci->contr, aplci->channel);
	if (!aplci->link) {
		int_error();
		return(-EBUSY);
	}
	capidebug(CAPI_DBG_NCCI, "AppPlciLinkUp aplci->link(%p)", aplci->link);
	memset(&aplci->link->inst.pid, 0, sizeof(mISDN_pid_t));
	aplci->link->inst.data = aplci;
	aplci->link->inst.pid.layermask = ISDN_LAYER(4);
	aplci->link->inst.pid.protocol[4] = ISDN_PID_L4_B_CAPI20;
	if (pid.protocol[3] == ISDN_PID_L3_B_TRANS) {
		aplci->link->inst.pid.protocol[3] = ISDN_PID_L3_B_TRANS;
		aplci->link->inst.pid.layermask |= ISDN_LAYER(3);
	}
	retval = aplci->link->inst.obj->ctrl(aplci->link->st,
		MGR_REGLAYER | INDICATION, &aplci->link->inst); 
	if (retval) {
		printk(KERN_WARNING "%s MGR_REGLAYER | INDICATION ret(%d)\n",
			__FUNCTION__, retval);
		return(retval);
	}
	stpara.maxdatalen = aplci->appl->reg_params.datablklen;
	stpara.up_headerlen = CAPI_B3_DATA_IND_HEADER_SIZE;
	stpara.down_headerlen = 0;
                        
	retval = aplci->link->inst.obj->ctrl(aplci->link->st,
		MGR_ADDSTPARA | REQUEST, &stpara);
	if (retval) {
		printk(KERN_WARNING "%s MGR_SETSTACK | REQUEST ret(%d)\n",
			__FUNCTION__, retval);
	}
	retval = aplci->link->inst.obj->ctrl(aplci->link->st,
		MGR_SETSTACK | REQUEST, &pid);
	if (retval) {
		printk(KERN_WARNING "%s MGR_SETSTACK | REQUEST ret(%d)\n",
			__FUNCTION__, retval);
		return(retval);
	}
	return(0);
}

static int
ReleaseLink(AppPlci_t *aplci)
{
	int retval = 0;

	if (aplci->link) {
#if 0
		if (ncci->ncci_m.state != ST_NCCI_N_0)
			ncciL4L3(ncci, DL_RELEASE | REQUEST, 0, 0, NULL, NULL);
#endif
		retval = aplci->link->inst.obj->ctrl(aplci->link->inst.st,
			MGR_CLEARSTACK | REQUEST, NULL);
		if (retval)
			int_error();
		aplci->link = NULL;
		skb_queue_purge(&aplci->delayedq);
		test_and_clear_bit(PLCI_STATE_STACKREADY, &aplci->plci->state);
	}
	return(retval);
}

Ncci_t	*
getNCCI4addr(AppPlci_t *aplci, __u32 addr, int mode)
{
	Ncci_t			*ncci;
	struct list_head	*item;
	int			cnt = 0;

	list_for_each(item, &aplci->Nccis) {
		cnt++;
		ncci = (Ncci_t *)item;
		if (ncci->addr == addr)
			return(ncci);
		if (mode == GET_NCCI_ONLY_PLCI) {
			if (ncci->addr == (addr & 0xffff))
				return(ncci);
		}
	}
	if (!cnt)
		return(NULL);
	if (mode != GET_NCCI_PLCI)
		return(NULL);
	if (1 == cnt) {
		if (!(addr & 0xffff0000))
			return(ncci);
	}
	return(NULL);
}

void
AppPlciDelNCCI(Ncci_t *ncci) {
	list_del_init(&ncci->head);
}

static __inline__ Ncci_t *
get_single_NCCI(AppPlci_t *aplci)
{
	struct list_head	*item = aplci->Nccis.next;

	if (item == &aplci->Nccis)
		return(NULL);
	if (item->next != &aplci->Nccis)
		return(NULL);	// more as one NCCI
	return((Ncci_t *)item);	
}

static int
PL_l3l4(mISDNif_t *hif, struct sk_buff *skb)
{
	AppPlci_t		*aplci;
	Ncci_t 			*ncci;
	int			ret = -EINVAL;
	mISDN_head_t		*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	aplci = hif->fdata;
	if (!aplci)
		return(-EINVAL);
	ncci = get_single_NCCI(aplci);
	capidebug(CAPI_DBG_NCCI_L3, "%s: prim(%x) dinfo (%x) skb(%p) APLCI(%x) ncci(%p)",
		__FUNCTION__, hh->prim, hh->dinfo, skb, aplci->addr, ncci);
	if (!ncci) {
		if ((hh->prim != (DL_ESTABLISH | INDICATION)) && (hh->prim != (DL_ESTABLISH | CONFIRM))) {
			int_error();
			return(-ENODEV);
		}
		ncci = ncciConstr(aplci);
		if (!ncci) {
			int_error();
			return(-ENOMEM);
		}
	}
	return(ncci_l3l4(ncci, hh, skb));
}

static int
PL_l3l4mux(mISDNif_t *hif, struct sk_buff *skb)
{
	AppPlci_t	*aplci;
	Ncci_t 		*ncci;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;
	__u32		addr;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	aplci = hif->fdata;
	if (!aplci)
		return(-EINVAL);
	
	capidebug(CAPI_DBG_NCCI_L3, "%s: prim(%x) dinfo (%x) skb->len(%d)",
		__FUNCTION__, hh->prim, hh->dinfo, skb->len);
	if (skb->len < 4) {
		int_error();
		return(-EINVAL);
	}
	addr = CAPIMSG_U32(skb->data, 0);
	ncci = getNCCI4addr(aplci, addr, GET_NCCI_ONLY_PLCI);
	if (hh->prim == CAPI_CONNECT_B3_IND) {
		if (ncci) {
			int_error();
			return(-EBUSY);
		}
		ncci = ncciConstr(aplci);
		if (!ncci) {
			int_error();
			return(-ENOMEM);
		}
		addr &= 0xffff0000;
		addr |= aplci->addr;
		ncci->addr = addr;
		capimsg_setu32(skb->data, 0, addr);
#ifdef OLDCAPI_DRIVER_INTERFACE
		ncci->contr->ctrl->new_ncci(ncci->contr->ctrl, ncci->appl->ApplId, addr, ncci->window);
#endif
	} else if (hh->prim == CAPI_CONNECT_B3_CONF) {
		if (ncci && ((addr & 0xffff0000) != 0)) {
			if (ncci->addr != addr) {
				ncci->addr = addr;
#ifdef OLDCAPI_DRIVER_INTERFACE
				ncci->contr->ctrl->new_ncci(ncci->contr->ctrl, ncci->appl->ApplId, addr, ncci->window);
#endif
			} else
				int_error();
		}
	}
	if (!ncci) {
		int_error();
		return(-ENODEV);
	}
	return(ncci_l3l4_direct(ncci, hh, skb));
}

int
AppPlcimISDN_SetIF(AppPlci_t *aplci, u_int prim, void *arg)
{
	int ret;

	if (aplci->Bprotocol.B3 == 0) // transparent
		ret = mISDN_SetIF(&aplci->link->inst, arg, prim, NULL, PL_l3l4, aplci);
	else
		ret = mISDN_SetIF(&aplci->link->inst, arg, prim, NULL, PL_l3l4mux, aplci);
	if (ret)
		return(ret);
	
	if (!test_and_set_bit(PLCI_STATE_SENDDELAYED, &aplci->plci->state)) {
		test_and_set_bit(PLCI_STATE_STACKREADY, &aplci->plci->state);
		SendingDelayedMsg(aplci);
	} else
		test_and_set_bit(PLCI_STATE_STACKREADY, &aplci->plci->state);
	return(0);
}

void
AppPlci_l3l4(AppPlci_t *aplci, int pr, void *arg)
{
	Q931_info_t	*qi = arg;
	u_char		*ie;

	AppPlciDebug(aplci, CAPI_DBG_PLCI_L3, "%s: aplci(%x) pr(%x) arg(%p)",
		__FUNCTION__, aplci->addr, pr, arg);
	switch (pr) {
		case CC_SETUP | INDICATION:
			if (!qi)
				return;
			AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
			AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
			AppPlciInfoIndIE(aplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, qi);
			AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			AppPlciInfoIndIE(aplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
			if (qi->channel_id) {
				ie = (u_char *)qi;
				ie += L3_EXTRA_SIZE + qi->channel_id;
				aplci->channel = plci_parse_channel_id(ie);
			}
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_SETUP_IND, arg); 
			break;
		case CC_TIMEOUT | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_SETUP_CONF_ERR, arg); 
			break;
		case CC_CONNECT | INDICATION:
			if (qi) {	
				AppPlciInfoIndIE(aplci, IE_DATE, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
				AppPlciInfoIndIE(aplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					aplci->channel = plci_parse_channel_id(ie);
				}
			}
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_SETUP_CONF, arg); 
			break;
		case CC_CONNECT_ACKNOWLEDGE | INDICATION:
			if (qi) {
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					aplci->channel = plci_parse_channel_id(ie);
				}
			}
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_SETUP_COMPL_IND, arg); 
			break;
		case CC_DISCONNECT | INDICATION:
			if (qi) {
				AppPlciInfoIndMsg(aplci, CAPI_INFOMASK_EARLYB3, MT_DISCONNECT);
				AppPlciInfoIndIE(aplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
		  	mISDN_FsmEvent(&aplci->plci_m, EV_L3_DISCONNECT_IND, arg); 
			break;
		case CC_RELEASE | INDICATION:
			if (qi) {
				AppPlciInfoIndIE(aplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
		        mISDN_FsmEvent(&aplci->plci_m, EV_L3_RELEASE_IND, arg); 
			break;
		case CC_RELEASE_COMPLETE | INDICATION:
			if (qi) {
				AppPlciInfoIndIE(aplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_RELEASE_IND, arg);
			break;
		case CC_RELEASE_CR | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_RELEASE_PROC_IND, arg); 
			break;
		case CC_SETUP_ACKNOWLEDGE | INDICATION:
			if (qi) {
				AppPlciInfoIndMsg(aplci, CAPI_INFOMASK_PROGRESS, MT_SETUP_ACKNOWLEDGE);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
				AppPlciInfoIndIE(aplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					aplci->channel = plci_parse_channel_id(ie);
				}
			}
			break;
		case CC_PROCEEDING | INDICATION:
			if (qi) {
				AppPlciInfoIndMsg(aplci, CAPI_INFOMASK_PROGRESS, MT_CALL_PROCEEDING);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
				AppPlciInfoIndIE(aplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					aplci->channel = plci_parse_channel_id(ie);
				}
			}
			break;
		case CC_ALERTING | INDICATION:
			if (qi) {
				AppPlciInfoIndMsg(aplci, CAPI_INFOMASK_PROGRESS, MT_ALERTING);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
				AppPlciInfoIndIE(aplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					aplci->channel = plci_parse_channel_id(ie);
				}
			}
			break;
		case CC_PROGRESS | INDICATION:
			if (qi) {
				AppPlciInfoIndMsg(aplci, CAPI_INFOMASK_PROGRESS, MT_PROGRESS);
				AppPlciInfoIndIE(aplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
			}
			break;
		case CC_SUSPEND_ACKNOWLEDGE | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_SUSPEND_CONF, arg); 
			break;
		case CC_SUSPEND_REJECT | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_SUSPEND_ERR, arg); 
			break;
		case CC_RESUME_ACKNOWLEDGE | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_RESUME_CONF, arg); 
			break;
		case CC_RESUME_REJECT | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_RESUME_ERR, arg); 
			break;
		case CC_NOTIFY | INDICATION:
			mISDN_FsmEvent(&aplci->plci_m, EV_L3_NOTIFY_IND, arg); 
			break;
		case PH_CONTROL | INDICATION:
			/* TOUCH TONE */
			mISDN_FsmEvent(&aplci->plci_m, EV_PH_CONTROL_IND, arg);
			break;
		default:
			AppPlciDebug(aplci, CAPI_DBG_WARN, 
			   "%s: pr 0x%x not handled", __FUNCTION__, pr);
			break;
	}
}

void
AppPlciGetCmsg(AppPlci_t *aplci, _cmsg *cmsg)
{
	int	retval = 0;

	switch (CMSGCMD(cmsg)) {
		case CAPI_INFO_REQ:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_INFO_REQ, cmsg);
			break;
		case CAPI_ALERT_REQ:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_ALERT_REQ, cmsg);
			break;
		case CAPI_CONNECT_REQ:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_CONNECT_REQ, cmsg);
			break;
		case CAPI_CONNECT_RESP:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_CONNECT_RESP, cmsg);
			break;
		case CAPI_DISCONNECT_REQ:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_DISCONNECT_REQ, cmsg);
			break;
		case CAPI_DISCONNECT_RESP:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_DISCONNECT_RESP, cmsg);
			break;
		case CAPI_CONNECT_ACTIVE_RESP:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_CONNECT_ACTIVE_RESP, cmsg);
			break;
		case CAPI_SELECT_B_PROTOCOL_REQ:
			retval = mISDN_FsmEvent(&aplci->plci_m, EV_AP_SELECT_B_PROTOCOL_REQ, cmsg);
			break;
		default:
			int_error();
			retval = -1;
	}
	if (retval) { 
		if (cmsg->Command == CAPI_REQ) {
			capi_cmsg_answer(cmsg);
			cmsg->Info = CapiMessageNotSupportedInCurrentState;
			Send2Application(aplci, cmsg);
		} else
			cmsg_free(cmsg);
	}
}

__u16
AppPlciSendMessage(AppPlci_t *aplci, struct sk_buff *skb)
{
	_cmsg	*cmsg;
	__u16	ret;

	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		ret = CAPI_REGOSRESOURCEERR;
	} else {
		capi_message2cmsg(cmsg, skb->data);
		AppPlciGetCmsg(aplci, cmsg);
		dev_kfree_skb(skb);
		ret = CAPI_NOERROR;
	}
	return(ret);
}

int
ConnectB3Request(AppPlci_t *aplci, struct sk_buff *skb)
{
	Ncci_t	*ncci = ncciConstr(aplci);

	if (!ncci) {
		int_error();
		return(-ENOMEM);
	}
	ncciSendMessage(ncci, skb);
	return(0);
}

static int
AppPlciLinkDown(AppPlci_t *aplci)
{
	struct list_head	*item, *next;

	list_for_each_safe(item, next, &aplci->Nccis) {
		ncciReleaseLink((Ncci_t *)item);
	}
	ReleaseLink(aplci);
	return(0);
}

int
AppPlciFacSuspendReq(AppPlci_t *aplci, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	__u8		*CallIdentity;
	struct sk_buff	*skb;

	CallIdentity = facReqParm->u.Suspend.CallIdentity;
	if (CallIdentity && CallIdentity[0] > 8) 
		return CapiIllMessageParmCoding;
	skb = mISDN_alloc_l3msg(20, MT_SUSPEND);
	if (!skb) {
		int_error();
		return CapiIllMessageParmCoding;
	}
	if (CallIdentity && CallIdentity[0])
		mISDN_AddIE(skb, IE_CALL_ID, CallIdentity);

	if (mISDN_FsmEvent(&aplci->plci_m, EV_AP_SUSPEND_REQ, skb)) {
		// no routine
		facConfParm->u.Info.SupplementaryServiceInfo = 
			CapiRequestNotAllowedInThisState;
		kfree(skb);
	} else {
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	}
	return CapiSuccess;
}

int
AppPlciFacResumeReq(AppPlci_t *aplci, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	__u8		*CallIdentity;
	struct sk_buff	*skb;

	CallIdentity = facReqParm->u.Resume.CallIdentity;
	if (CallIdentity && CallIdentity[0] > 8) {
		AppPlciDestr(aplci);
		return CapiIllMessageParmCoding;
	}
	skb = mISDN_alloc_l3msg(20, MT_RESUME);
	if (!skb) {
		int_error();
		AppPlciDestr(aplci);
		return CapiIllMessageParmCoding;
	}
	if (CallIdentity && CallIdentity[0])
		mISDN_AddIE(skb, IE_CALL_ID, CallIdentity);
	if (mISDN_FsmEvent(&aplci->plci_m, EV_AP_RESUME_REQ, skb))
		kfree(skb);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

static void
AppPlciClearOtherApps(AppPlci_t *aplci)
{
	AppPlci_t		*o_aplci;
	_cmsg			*cm;
	struct list_head	*item, *next;

	if (aplci->plci)
		return;
	if (aplci->plci->nAppl <= 1)
		return;
	list_for_each_safe(item, next, &aplci->plci->AppPlcis) {
		o_aplci = (AppPlci_t *)item;
		if (o_aplci != aplci) {
			CMSG_ALLOC(cm);
			AppPlciCmsgHeader(o_aplci, cm, CAPI_DISCONNECT, CAPI_IND);
			cm->Reason = 0x3304; // other application got the call
			mISDN_FsmEvent(&o_aplci->plci_m, EV_PI_DISCONNECT_IND, cm);
		}
	} 
}

static void
AppPlciInfoIndMsg(AppPlci_t *aplci,  __u32 mask, unsigned char mt)
{
	_cmsg	*cmsg;

	if ((!aplci->appl) || (!(aplci->appl->InfoMask & mask)))
		return;
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_INFO, CAPI_IND);
	cmsg->InfoNumber = 0x8000 | mt;
	cmsg->InfoElement = 0;
	Send2Application(aplci, cmsg);
}

static void
AppPlciInfoIndIE(AppPlci_t *aplci, unsigned char ie, __u32 mask, Q931_info_t *qi)
{
	_cmsg		*cmsg;
	u_char		*iep = NULL;
	u16		*ies;
	

	if ((!aplci->appl) || (!(aplci->appl->InfoMask & mask)))
		return;
	if (!qi)
		return;
	ies = &qi->bearer_capability;
	if (ie & 0x80) { /* single octett */
		int_error();
		return;
	} else {
		if (mISDN_l3_ie2pos(ie) < 0)
			return;
		ies += mISDN_l3_ie2pos(ie);
		if (!*ies)
			return;
		iep = (u_char *)qi;
		iep += L3_EXTRA_SIZE + *ies +1;
	}
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_INFO, CAPI_IND);
	cmsg->InfoNumber = ie;
	cmsg->InfoElement = iep;
	if (ie == IE_PROGRESS && aplci->appl->InfoMask & CAPI_INFOMASK_EARLYB3) {
		if (iep[0] == 0x02 && iep[2] == 0x88) { // in-band information available
			AppPlciLinkUp(aplci);
			if (!test_bit(PLCI_STATE_STACKREADY, &aplci->plci->state)) {
				Send2ApplicationDelayed(aplci,cmsg);
				return;
			}
		}
	}
	Send2Application(aplci, cmsg);
}

void init_AppPlci(void)
{
	plci_fsm.state_count = ST_PLCI_COUNT;
	plci_fsm.event_count = EV_PLCI_COUNT;
	plci_fsm.strEvent = str_ev_plci;
	plci_fsm.strState = str_st_plci;
	
	mISDN_FsmNew(&plci_fsm, fn_plci_list, FN_PLCI_COUNT);
}


void free_AppPlci(void)
{
	mISDN_FsmFree(&plci_fsm);
}
