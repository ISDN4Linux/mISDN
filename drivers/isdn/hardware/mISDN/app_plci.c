/* $Id: app_plci.c,v 1.1 2003/11/21 22:29:41 keil Exp $
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
static void 	AppPlciLinkUp(AppPlci_t *);
static void	AppPlciLinkDown(AppPlci_t *);

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
			AddvarIE(skb, BEARER_31AUDIO_64K_ALAW);
			AddvarIE(skb, HLC_TELEPHONY);
			break;
		case 17:
			AddvarIE(skb, BEARER_31AUDIO_64K_ALAW);
			AddvarIE(skb, HLC_FACSIMILE);
			break;
		case 1:
			AddvarIE(skb, BEARER_SPEECH_64K_ALAW);
			break;
		case 2:
			AddvarIE(skb, BEARER_UNRES_DIGITAL_64K);
			break;
		case 3:
			AddvarIE(skb, BEARER_RES_DIGITAL_64K);
			break;
		case 4:
			AddvarIE(skb, BEARER_31AUDIO_64K_ALAW);
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
	AddIE(skb, IE_CALLING_PN, cmsg->CallingPartyNumber);
	AddIE(skb, IE_CALLING_SUB, cmsg->CallingPartySubaddress);
	AddIE(skb, IE_CALLED_PN, cmsg->CalledPartyNumber);
	AddIE(skb, IE_CALLED_SUB, cmsg->CalledPartySubaddress);
	AddIE(skb, IE_BEARER, cmsg->BC);
	AddIE(skb, IE_LLC, cmsg->LLC);
	AddIE(skb, IE_HLC, cmsg->HLC);
	return 0;
 err:
	return CapiIllMessageParmCoding;
}

__u16 cmsg2info_req(_cmsg *cmsg, struct sk_buff *skb)
{
	AddIE(skb, IE_KEYPAD, cmsg->Keypadfacility);
	AddIE(skb, IE_CALLED_PN, cmsg->CalledPartyNumber);
	return 0;
}

__u16 cmsg2alerting_req(_cmsg *cmsg, struct sk_buff *skb)
{
	AddIE(skb, IE_USER_USER, cmsg->Useruserdata);
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

	FsmChangeState(fi, ST_PLCI_P_0_1);
	test_and_set_bit(PLCI_STATE_OUTGOING, &plci->state);

	skb = alloc_l3msg(260, MT_SETUP);
	
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
	Send2Application(aplci, cmsg);
	FsmEvent(fi, EV_PI_CONNECT_CONF, cmsg);
}

static void
plci_connect_conf(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg	*cmsg = arg;
  
	if (cmsg->Info == 0) {
		FsmChangeState(fi, ST_PLCI_P_1);
	} else {
		FsmChangeState(fi, ST_PLCI_P_0);
		AppPlciDestr(aplci);
	}
}

static void
plci_connect_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_2);
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
	Plci_t	*plci = aplci->plci;

	// we already sent CONF with Info = SuppInfo = 0
	FsmChangeState(fi, ST_PLCI_P_RES);
	plciNewCrReq(plci);
	plciL4L3(plci, CC_RESUME | REQUEST, arg);
}

static void
plci_alert_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t		*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;
	
	if (test_and_set_bit(PLCI_STATE_ALERTING, &plci->state)) {
		Info = 0x0003; // other app is already alerting
	} else {
		struct sk_buff	*skb = alloc_l3msg(10, MT_ALERTING);
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
		FsmChangeState(fi, ST_PLCI_P_4);
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
		skb = alloc_l3msg(10, MT_DISCONNECT);
		if (!skb) {
			plciL4L3(plci, prim, NULL);
		} else {
			AddIE(skb, IE_CAUSE, cause);
			plciL4L3(plci, prim, skb);
		}
	}
	cmsg->Command = CAPI_DISCONNECT;
	cmsg->Subcommand = CAPI_IND;
	cmsg->Messagenumber = aplci->appl->MsgId++;
	cmsg->Reject = 0x3400 | cause[2];
	if (FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_connect_active_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t *aplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_ACT);
	AppPlciLinkUp(aplci);
	Send2Application(aplci, arg);
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

	FsmChangeState(fi, ST_PLCI_P_5);
	
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

		skb = alloc_l3msg(10, MT_DISCONNECT);
		if (!skb) {
			plciL4L3(plci, CC_DISCONNECT | REQUEST, NULL);
		} else {
			memcpy(cause, "\x02\x80\x90", 3); // normal call clearing
			AddIE(skb, IE_CAUSE, cause);
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
	FsmChangeState(fi, ST_PLCI_P_5);
}

static void plci_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	// facility_ind Resume: Reason = 0
	AppPlci_t	*aplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_ACT);
	AppPlciLinkUp(aplci);
	Send2Application(aplci, arg);
}

static void
plci_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_6);
	Send2Application(fi->userdata, arg);
}

static void
plci_disconnect_resp(struct FsmInst *fi, int event, void *arg)
{
	if (arg)
		cmsg_free(arg);
	FsmChangeState(fi, ST_PLCI_P_0);
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

	FsmChangeState(fi, ST_PLCI_P_5);
	
	if (!plci) {
		int_error();
		return;
	}

	AppPlciLinkDown(aplci);

	if (!aplci->cause[0]) {
		struct sk_buff	*skb;

		skb = alloc_l3msg(10, MT_DISCONNECT);
		if (!skb) {
			plciL4L3(plci, CC_DISCONNECT | REQUEST, NULL);
		} else {
			u_char *cause = "\x02\x80\x9f";

			AddIE(skb, IE_CAUSE, cause);
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
		FsmEvent(fi, EV_PI_CHANNEL_ERR, NULL);
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
	if (FsmEvent(fi, EV_PI_CONNECT_ACTIVE_IND, cmsg))
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
	if (FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_channel_err(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;
	u_char		cause[4];
	struct sk_buff	*skb;

	skb = alloc_l3msg(10, MT_RELEASE_COMPLETE);
	if (skb) {
		cause[0] = 2;
		cause[1] = 0x80;
		cause[2] = 0x86; /* channel unacceptable */
		AddIE(skb, IE_CAUSE, cause);
		plciL4L3(aplci->plci, CC_RELEASE_COMPLETE | REQUEST, skb);
	} else
		int_error();
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_DISCONNECT, CAPI_IND);
	cmsg->Reason = CapiProtocolErrorLayer3;
	if (FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
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
	if (FsmEvent(&aplci->plci_m, EV_PI_CONNECT_IND, cmsg))
		cmsg_free(cmsg);
}

static void
plci_cc_setup_compl_ind(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg;

	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	if (FsmEvent(&aplci->plci_m, EV_PI_CONNECT_ACTIVE_IND, cmsg))
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
	if (FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
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
		FsmEvent(&aplci->plci_m, EV_PI_SUSPEND_CONF, NULL);
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
	if (FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
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
	if (FsmEvent(&aplci->plci_m, EV_PI_DISCONNECT_IND, cmsg))
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
	if (FsmEvent(&aplci->plci_m, EV_PI_RESUME_CONF, cmsg))
		cmsg_free(cmsg);
}

static void
plci_select_b_protocol_req(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	_cmsg		*cmsg = arg;
	__u16		Info;

	Info = AppPlciCheckBprotocol(aplci, cmsg);
	if (Info)
		goto answer;

	if (!aplci->ncci) {
		int_error();
		cmsg_free(cmsg);
		return;
	}

	Info = ncciSelectBprotocol(aplci->ncci);
answer:
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	Send2Application(aplci, cmsg);
}

static void
plci_info_req_overlap(struct FsmInst *fi, int event, void *arg)
{
	AppPlci_t	*aplci = fi->userdata;
	Plci_t		*plci = aplci->plci;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;
	struct sk_buff	*skb;

	skb = alloc_l3msg(100, MT_INFORMATION);
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
	*aplci = apl;
	return(0);
}

void AppPlciDestr(AppPlci_t *aplci)
{
	if (aplci->plci) {
		AppPlciDebug(aplci, CAPI_DBG_PLCI, "%s plci state %s", __FUNCTION__,
			str_st_plci[aplci->plci_m.state]);
		if (aplci->plci_m.state != ST_PLCI_P_0) {
			struct sk_buff	*skb = alloc_l3msg(10, MT_RELEASE_COMPLETE);
			unsigned char cause[] = {2,0x80,0x80| CAUSE_RESOURCES_UNAVAIL};

			if (skb) {
				AddIE(skb, IE_CAUSE, cause);
				plciL4L3(aplci->plci, CC_RELEASE_COMPLETE | REQUEST, skb);
			}
		}
 		plciDetachAppPlci(aplci->plci, aplci);
	}
	if (aplci->ncci) {
		ncciDelAppPlci(aplci->ncci);
		aplci->ncci = NULL;
	}
	if (aplci->appl)
		ApplicationDelAppPlci(aplci->appl, aplci);
	AppPlci_free(aplci);
}

void
AppPlciRelease(AppPlci_t *aplci)
{
	if (aplci->ncci)
		ncciApplRelease(aplci->ncci);
	FsmEvent(&aplci->plci_m, EV_AP_RELEASE, NULL);
}

void AppPlciDelNCCI(AppPlci_t *aplci) {
	aplci->ncci = NULL;
}

void AppPlci_l3l4(AppPlci_t *aplci, int pr, void *arg)
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
			FsmEvent(&aplci->plci_m, EV_L3_SETUP_IND, arg); 
			break;
		case CC_TIMEOUT | INDICATION:
			FsmEvent(&aplci->plci_m, EV_L3_SETUP_CONF_ERR, arg); 
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
			FsmEvent(&aplci->plci_m, EV_L3_SETUP_CONF, arg); 
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
			FsmEvent(&aplci->plci_m, EV_L3_SETUP_COMPL_IND, arg); 
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
		  	FsmEvent(&aplci->plci_m, EV_L3_DISCONNECT_IND, arg); 
			break;
		case CC_RELEASE | INDICATION:
			if (qi) {
				AppPlciInfoIndIE(aplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
		        FsmEvent(&aplci->plci_m, EV_L3_RELEASE_IND, arg); 
			break;
		case CC_RELEASE_COMPLETE | INDICATION:
			if (qi) {
				AppPlciInfoIndIE(aplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				AppPlciInfoIndIE(aplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				AppPlciInfoIndIE(aplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				AppPlciInfoIndIE(aplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
			FsmEvent(&aplci->plci_m, EV_L3_RELEASE_IND, arg);
			break;
		case CC_RELEASE_CR | INDICATION:
			FsmEvent(&aplci->plci_m, EV_L3_RELEASE_PROC_IND, arg); 
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
			FsmEvent(&aplci->plci_m, EV_L3_SUSPEND_CONF, arg); 
			break;
		case CC_SUSPEND_REJECT | INDICATION:
			FsmEvent(&aplci->plci_m, EV_L3_SUSPEND_ERR, arg); 
			break;
		case CC_RESUME_ACKNOWLEDGE | INDICATION:
			FsmEvent(&aplci->plci_m, EV_L3_RESUME_CONF, arg); 
			break;
		case CC_RESUME_REJECT | INDICATION:
			FsmEvent(&aplci->plci_m, EV_L3_RESUME_ERR, arg); 
			break;
		case CC_NOTIFY | INDICATION:
			FsmEvent(&aplci->plci_m, EV_L3_NOTIFY_IND, arg); 
			break;
		case PH_CONTROL | INDICATION:
			/* TOUCH TONE */
			FsmEvent(&aplci->plci_m, EV_PH_CONTROL_IND, arg);
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
			retval = FsmEvent(&aplci->plci_m, EV_AP_INFO_REQ, cmsg);
			break;
		case CAPI_ALERT_REQ:
			retval = FsmEvent(&aplci->plci_m, EV_AP_ALERT_REQ, cmsg);
			break;
		case CAPI_CONNECT_REQ:
			retval = FsmEvent(&aplci->plci_m, EV_AP_CONNECT_REQ, cmsg);
			break;
		case CAPI_CONNECT_RESP:
			retval = FsmEvent(&aplci->plci_m, EV_AP_CONNECT_RESP, cmsg);
			break;
		case CAPI_DISCONNECT_REQ:
			retval = FsmEvent(&aplci->plci_m, EV_AP_DISCONNECT_REQ, cmsg);
			break;
		case CAPI_DISCONNECT_RESP:
			retval = FsmEvent(&aplci->plci_m, EV_AP_DISCONNECT_RESP, cmsg);
			break;
		case CAPI_CONNECT_ACTIVE_RESP:
			retval = FsmEvent(&aplci->plci_m, EV_AP_CONNECT_ACTIVE_RESP, cmsg);
			break;
		case CAPI_SELECT_B_PROTOCOL_REQ:
			retval = FsmEvent(&aplci->plci_m, EV_AP_SELECT_B_PROTOCOL_REQ, cmsg);
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

void 
AppPlciSendMessage(AppPlci_t *aplci, struct sk_buff *skb)
{
	_cmsg	*cmsg;

	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		dev_kfree_skb(skb);
		return;
	}
	capi_message2cmsg(cmsg, skb->data);
	AppPlciGetCmsg(aplci, cmsg);
	dev_kfree_skb(skb);
}

static void
AppPlciLinkUp(AppPlci_t *aplci)
{
	if (aplci->ncci)
		return;
	if (aplci->channel == -1) {/* no valid channel set */
		int_error();
		return;
	}
	
	aplci->ncci = ncciConstr(aplci);
	if (!aplci->ncci) {
		int_error();
		return;
	}
	ncciLinkUp(aplci->ncci);
}

static void
AppPlciLinkDown(AppPlci_t *aplci)
{
	if (!aplci->ncci) {
		return;
	}
	ncciLinkDown(aplci->ncci);
}

int
AppPlciFacSuspendReq(AppPlci_t *aplci, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	__u8		*CallIdentity;
	struct sk_buff	*skb;

	CallIdentity = facReqParm->u.Suspend.CallIdentity;
	if (CallIdentity && CallIdentity[0] > 8) 
		return CapiIllMessageParmCoding;
	skb = alloc_l3msg(20, MT_SUSPEND);
	if (!skb) {
		int_error();
		return CapiIllMessageParmCoding;
	}
	if (CallIdentity && CallIdentity[0])
		AddIE(skb, IE_CALL_ID, CallIdentity);

	if (FsmEvent(&aplci->plci_m, EV_AP_SUSPEND_REQ, skb)) {
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
	skb = alloc_l3msg(20, MT_RESUME);
	if (!skb) {
		int_error();
		AppPlciDestr(aplci);
		return CapiIllMessageParmCoding;
	}
	if (CallIdentity && CallIdentity[0])
		AddIE(skb, IE_CALL_ID, CallIdentity);
	if (FsmEvent(&aplci->plci_m, EV_AP_RESUME_REQ, skb))
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
			FsmEvent(&o_aplci->plci_m, EV_PI_DISCONNECT_IND, cm);
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
		if (l3_ie2pos(ie) < 0)
			return;
		ies += l3_ie2pos(ie);
		if (!*ies)
			return;
		iep = (u_char *)qi;
		iep += L3_EXTRA_SIZE + *ies +1;
	}
	if (ie == IE_PROGRESS && aplci->appl->InfoMask & CAPI_INFOMASK_EARLYB3) {
		if (iep[0] == 0x02 && iep[2] == 0x88) { // in-band information available
			AppPlciLinkUp(aplci);
		}
	}
	CMSG_ALLOC(cmsg);
	AppPlciCmsgHeader(aplci, cmsg, CAPI_INFO, CAPI_IND);
	cmsg->InfoNumber = ie;
	cmsg->InfoElement = iep;
	Send2Application(aplci, cmsg);
}

void init_AppPlci(void)
{
	plci_fsm.state_count = ST_PLCI_COUNT;
	plci_fsm.event_count = EV_PLCI_COUNT;
	plci_fsm.strEvent = str_ev_plci;
	plci_fsm.strState = str_st_plci;
	
	FsmNew(&plci_fsm, fn_plci_list, FN_PLCI_COUNT);
}


void free_AppPlci(void)
{
	FsmFree(&plci_fsm);
}
