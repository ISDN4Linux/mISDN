/* $Id: cplci.c,v 1.10 2003/07/21 12:44:45 kkeil Exp $
 *
 */

#include "capi.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"

#define cplciDebug(cplci, lev, fmt, args...) \
	capidebug(lev, fmt, ## args)

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
		capidebug(CAPI_DBG_PLCI_INFO, "%s: l(%d) %x\n", __FUNCTION__, p[0], p[1]);
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

__u16 cplciCheckBprotocol(Cplci_t *cplci, _cmsg *cmsg)
{
	struct capi_ctr *ctrl = cplci->contr->ctrl;

	if (!test_bit(cmsg->B1protocol, &ctrl->profile.support1))
		return CapiB1ProtocolNotSupported;
	if (!test_bit(cmsg->B2protocol, &ctrl->profile.support2))
		return CapiB2ProtocolNotSupported;
	if (!test_bit(cmsg->B3protocol, &ctrl->profile.support3))
		return CapiB3ProtocolNotSupported;

	cplci->Bprotocol.B1protocol = cmsg->B1protocol;
	cplci->Bprotocol.B2protocol = cmsg->B2protocol;
	cplci->Bprotocol.B3protocol = cmsg->B3protocol;
	return 0;
}

// =============================================================== plci ===

// --------------------------------------------------------------------
// PLCI state machine

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
	EV_PLCI_CONNECT_REQ,
	EV_PLCI_CONNECT_CONF,
	EV_PLCI_CONNECT_IND,
	EV_PLCI_CONNECT_RESP,
	EV_PLCI_CONNECT_ACTIVE_IND,
	EV_PLCI_CONNECT_ACTIVE_RESP,
	EV_PLCI_ALERT_REQ,
	EV_PLCI_INFO_REQ,
	EV_PLCI_INFO_IND,
	EV_PLCI_FACILITY_IND,
	EV_PLCI_SELECT_B_PROTOCOL_REQ,
	EV_PLCI_DISCONNECT_REQ,
	EV_PLCI_DISCONNECT_IND,
	EV_PLCI_DISCONNECT_RESP,
	EV_PLCI_SUSPEND_REQ,
	EV_PLCI_SUSPEND_CONF,
	EV_PLCI_RESUME_REQ,
	EV_PLCI_RESUME_CONF,
	EV_PLCI_CHANNEL_ERR,
	EV_PLCI_CC_SETUP_IND,
	EV_PLCI_CC_SETUP_CONF_ERR,
	EV_PLCI_CC_SETUP_CONF,
	EV_PLCI_CC_SETUP_COMPL_IND,
	EV_PLCI_CC_DISCONNECT_IND,
	EV_PLCI_CC_RELEASE_IND,
	EV_PLCI_CC_RELEASE_PROC_IND,
	EV_PLCI_CC_NOTIFY_IND,
	EV_PLCI_CC_SUSPEND_ERR,
	EV_PLCI_CC_SUSPEND_CONF,
	EV_PLCI_CC_RESUME_ERR,
	EV_PLCI_CC_RESUME_CONF,
	EV_PLCI_CC_REJECT_IND,
	EV_PLCI_CC_PH_CONTROL_IND,
}

const EV_PLCI_COUNT = EV_PLCI_CC_PH_CONTROL_IND + 1;

static char* str_ev_plci[] = {
	"EV_PLCI_CONNECT_REQ",
	"EV_PLCI_CONNECT_CONF",
	"EV_PLCI_CONNECT_IND",
	"EV_PLCI_CONNECT_RESP",
	"EV_PLCI_CONNECT_ACTIVE_IND",
	"EV_PLCI_CONNECT_ACTIVE_RESP",
	"EV_PLCI_ALERT_REQ",
	"EV_PLCI_INFO_REQ",
	"EV_PLCI_INFO_IND",
	"EV_PLCI_FACILITY_IND",
	"EV_PLCI_SELECT_B_PROTOCOL_REQ",
	"EV_PLCI_DISCONNECT_REQ",
	"EV_PLCI_DISCONNECT_IND",
	"EV_PLCI_DISCONNECT_RESP",
	"EV_PLCI_SUSPEND_REQ",
	"EV_PLCI_SUSPEND_CONF",
	"EV_PLCI_RESUME_REQ",
	"EV_PLCI_RESUME_CONF",
	"EV_PLCI_CHANNEL_ERR",
	"EV_PLCI_CC_SETUP_IND",
	"EV_PLCI_CC_SETUP_CONF_ERR",
	"EV_PLCI_CC_SETUP_CONF",
	"EV_PLCI_CC_SETUP_COMPL_IND",
	"EV_PLCI_CC_DISCONNECT_IND",
	"EV_PLCI_CC_RELEASE_IND",
	"EV_PLCI_CC_RELEASE_PROC_IND",
	"EV_PLCI_CC_NOTIFY_IND",
	"EV_PLCI_CC_SUSPEND_ERR",
	"EV_PLCI_CC_SUSPEND_CONF",
	"EV_PLCI_CC_RESUME_ERR",
	"EV_PLCI_CC_RESUME_CONF",
	"EV_PLCI_CC_REJECT_IND",
	"EV_PLCI_CC_PH_CONTROL_IND",
};

static struct Fsm plci_fsm =
{ 0, 0, 0, 0, 0 };

static void cplci_debug(struct FsmInst *fi, char *fmt, ...)
{
	char tmp[128];
	char *p = tmp;
	va_list args;
	Cplci_t *cplci = fi->userdata;
  
	va_start(args, fmt);
	p += sprintf(p, "PLCI 0x%x: ", cplci->adrPLCI);
	p += vsprintf(p, fmt, args);
	*p = 0;
	cplciDebug(cplci, CAPI_DBG_PLCI_STATE, tmp);
	va_end(args);
}

inline void cplciRecvCmsg(Cplci_t *cplci, _cmsg *cmsg)
{
	contrRecvCmsg(cplci->contr, cmsg);
}

inline void cplciCmsgHeader(Cplci_t *cplci, _cmsg *cmsg, __u8 cmd, __u8 subcmd)
{
	capi_cmsg_header(cmsg, cplci->appl->ApplId, cmd, subcmd, 
			 cplci->appl->MsgId++, cplci->adrPLCI);
}

static void plci_connect_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Plci_t		*plci = cplci->plci;
	struct sk_buff	*skb;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;

	FsmChangeState(fi, ST_PLCI_P_0_1);
	test_and_set_bit(PLCI_FLAG_OUTGOING, &plci->flags);

	skb = alloc_l3msg(260, MT_SETUP);
	
	if (!skb) {
		Info = CapiNoPlciAvailable;
		goto answer;
	}
	if ((Info = cmsg2setup_req(cmsg, skb))) {
		goto answer;
	}
	if ((Info = cplciCheckBprotocol(cplci, cmsg))) {
		goto answer;
	}

	plciNewCrReq(plci);
	plciL4L3(plci, CC_SETUP | REQUEST, skb);
answer:
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	if (cmsg->Info == 0) 
		cmsg->adr.adrPLCI = cplci->adrPLCI;
	cplciRecvCmsg(cplci, cmsg);
	FsmEvent(fi, EV_PLCI_CONNECT_CONF, cmsg);
}

static void plci_connect_conf(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	_cmsg	*cmsg = arg;
  
	if (cmsg->Info == 0) {
		FsmChangeState(fi, ST_PLCI_P_1);
	} else {
		FsmChangeState(fi, ST_PLCI_P_0);
		applDelCplci(cplci->appl, cplci);
	}
}

static void plci_connect_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_2);
}

static void plci_suspend_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	Plci_t	*plci = cplci->plci;

	plciL4L3(plci, CC_SUSPEND | REQUEST, arg); 
}

static void plci_resume_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	Plci_t	*plci = cplci->plci;

	// we already sent CONF with Info = SuppInfo = 0
	FsmChangeState(fi, ST_PLCI_P_RES);
	plciNewCrReq(plci);
	plciL4L3(plci, CC_RESUME | REQUEST, arg);
}

static void plci_alert_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Plci_t		*plci = cplci->plci;
	_cmsg		*cmsg = arg;
	__u16		Info = 0;
	
	if (test_and_set_bit(PLCI_FLAG_ALERTING, &plci->flags)) {
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
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_connect_resp(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Plci_t		*plci = cplci->plci;
	unsigned char	cause[4];
	_cmsg		*cmsg = arg;
	struct sk_buff	*skb;

	switch (cmsg->Reject) {
		case 0 : // accept
			if (cplciCheckBprotocol(cplci, cmsg)) {
				int_error();
			}
			cplciClearOtherApps(cplci);
			plciL4L3(plci, CC_CONNECT | REQUEST, NULL);
			FsmChangeState(fi, ST_PLCI_P_4);
			break;
		default : // ignore, reject 
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
			if (cmsg->Reject != 1) { // ignore
				cplciClearOtherApps(cplci);
			}
			plciDetachCplci(plci, cplci);
			if (plci->nAppl == 0) {
				if (test_bit(PLCI_FLAG_ALERTING, &plci->flags)) { 
				// if we already answered, we can't just ignore but must clear actively
					skb = alloc_l3msg(10, MT_DISCONNECT);
					if (!skb) {
						plciL4L3(plci, CC_DISCONNECT | REQUEST, NULL);
					} else {
						AddIE(skb, IE_CAUSE, cause);
						plciL4L3(plci, CC_DISCONNECT | REQUEST, skb);
					}
				} else {
					plciL4L3(plci, CC_RELEASE_COMPLETE | REQUEST, NULL);
				}
			}
		
			capi_cmsg_answer(cmsg);
			cmsg->Command = CAPI_DISCONNECT;
			cmsg->Subcommand = CAPI_IND;
			cmsg->Messagenumber = cplci->appl->MsgId++;
			FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, cmsg);
			cplciRecvCmsg(cplci, cmsg);
	}
}

static void plci_connect_active_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_ACT);
	cplciLinkUp(cplci);
}

static void plci_connect_active_resp(struct FsmInst *fi, int event, void *arg)
{
}

static void plci_disconnect_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	Plci_t	*plci = cplci->plci;
	u_char	cause[4];
	_cmsg	*cmsg = arg;

	FsmChangeState(fi, ST_PLCI_P_5);
	
	if (!plci) {
		int_error();
		return;
	}
	capi_cmsg_answer(cmsg);
	cmsg->Reason = 0; // disconnect initiated
	cplciRecvCmsg(cplci, cmsg);

	cplciLinkDown(cplci);

	if (!cplci->cause[0]) { // FIXME
		struct sk_buff	*skb;

//		plciL4L3(cplci->plci, CC_STATUS_ENQUIRY | REQUEST, NULL);
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
//	plciL4L3(cplci->plci, CC_STATUS_ENQUIRY | REQUEST, NULL);
}

static void plci_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_5);
}

static void plci_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	// facility_ind Resume: Reason = 0
	Cplci_t *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_ACT);
	cplciLinkUp(cplci);
}

static void plci_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_PLCI_P_6);
}

static void plci_disconnect_resp(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;

	FsmChangeState(fi, ST_PLCI_P_0);
	applDelCplci(cplci->appl, cplci);
}

static void plci_cc_setup_conf(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	_cmsg		cmsg;
	Q931_info_t	*qi = arg;
	u_char		*p;

	if (cplci->bchannel == -1) {/* no valid channel set */
		FsmEvent(fi, EV_PLCI_CHANNEL_ERR, NULL);
		return;
	}
	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		if (qi->connected_nr)
			cmsg.ConnectedNumber = &p[qi->connected_nr + 1];
		if (qi->connected_sub)
			cmsg.ConnectedSubaddress = &p[qi->connected_sub + 1];
		if (qi->llc)
			cmsg.LLC = &p[qi->llc + 1];
	}
	FsmEvent(fi, EV_PLCI_CONNECT_ACTIVE_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_conf_err(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	_cmsg cmsg;

	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	cmsg.Reason = CapiProtocolErrorLayer3;
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_channel_err(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Plci_t		*plci = cplci->plci;
	_cmsg		cmsg;
	unsigned char	cause[4];
	struct sk_buff	*skb;

	if (!plci) {
		int_error();
		return;
	}
	skb = alloc_l3msg(10, MT_RELEASE_COMPLETE);
	if (skb) {
		cause[0] = 2;
		cause[1] = 0x80;
		cause[2] = 0x86; /* channel unacceptable */
		AddIE(skb, IE_CAUSE, cause);
		plciL4L3(plci, CC_RELEASE_COMPLETE | REQUEST, skb);
	} else
		int_error();
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	cmsg.Reason = CapiProtocolErrorLayer3;
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_ind(struct FsmInst *fi, int event, void *arg)
{ 
	Cplci_t		*cplci = fi->userdata;
	Q931_info_t	*qi = arg;
	_cmsg		cmsg;
	u_char		*p;

	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT, CAPI_IND);
	
	// FIXME: CW
	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		cmsg.CIPValue = q931CIPValue(qi);
		if (qi->called_nr)
			cmsg.CalledPartyNumber = &p[qi->called_nr + 1];
		if (qi->called_sub)
			cmsg.CalledPartySubaddress = &p[qi->called_sub + 1];
		if (qi->calling_nr)
			cmsg.CallingPartyNumber = &p[qi->calling_nr + 1];
		if (qi->calling_sub)
			cmsg.CallingPartySubaddress = &p[qi->calling_sub + 1];
		if (qi->bearer_capability)
			cmsg.BC = &p[qi->bearer_capability + 1];
		if (qi->llc)
			cmsg.LLC = &p[qi->llc + 1];
		if (qi->hlc)
			cmsg.HLC = &p[qi->hlc + 1];
		// all else set to default
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_compl_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	_cmsg	cmsg;

	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_ACTIVE_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;

	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		if (qi->cause)
			memcpy(cplci->cause, &p[qi->cause + 1], 3);
	}
	if (!(cplci->appl->listen.InfoMask & CAPI_INFOMASK_EARLYB3)) {
		cplciLinkDown(cplci);
		plciL4L3(cplci->plci, CC_RELEASE | REQUEST, NULL);
	}
}

static void plci_cc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;
	_cmsg		cmsg;
	
	plciDetachCplci(cplci->plci, cplci);

	cplciLinkDown(cplci);
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (qi) {
		p = (u_char *)qi;
		p += L3_EXTRA_SIZE;
		if (qi->cause)
			cmsg.Reason = 0x3400 | p[qi->cause + 3];
		else if (cplci->cause[0]) // cause from CC_DISCONNECT IND
			cmsg.Reason = 0x3400 | cplci->cause[2];
		else
			cmsg.Reason = 0;
	} else {
		cmsg.Reason = CapiProtocolErrorLayer1;
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_notify_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Q931_info_t	*qi = arg;
	_cmsg		cmsg;
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
			if (!(cplci->appl->NotificationMask & SuppServiceTP))
				break;
			cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
			p = &tmp[1];
			p += capiEncodeWord(p, 0x8002 + (nf[1] & 1)); // Suspend/Resume Notification
			*p++ = 0; // empty struct
			tmp[0] = p - &tmp[1];
			cmsg.FacilitySelector = 0x0003;
			cmsg.FacilityIndicationParameter = tmp;
			cplciRecvCmsg(cplci, &cmsg);
			break;
	}
}

static void plci_suspend_reply(Cplci_t *cplci, __u16 SuppServiceReason)
{
	_cmsg	cmsg;
	__u8	tmp[10], *p;

	cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
	p = &tmp[1];
	p += capiEncodeWord(p, 0x0004); // Suspend
	p += capiEncodeFacIndSuspend(p, SuppServiceReason);
	tmp[0] = p - &tmp[1];
	cmsg.FacilitySelector = 0x0003;
	cmsg.FacilityIndicationParameter = tmp;
	cplciRecvCmsg(cplci, &cmsg);

	if (SuppServiceReason == CapiSuccess)
		FsmEvent(&cplci->plci_m, EV_PLCI_SUSPEND_CONF, &cmsg);
}

static void plci_cc_suspend_err(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
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
	plci_suspend_reply(cplci, SuppServiceReason);
}

static void plci_cc_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	_cmsg	cmsg;

	cplciLinkDown(cplci);

	plci_suspend_reply(cplci, CapiSuccess);
	
	plciDetachCplci(cplci->plci, cplci);
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_resume_err(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Q931_info_t	*qi = arg;
	u_char		*p;
	_cmsg		cmsg;
	
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (qi) { // reject from network
		plciDetachCplci(cplci->plci, cplci);
		if (qi->cause) {
			p = (u_char *)qi;
			p += L3_EXTRA_SIZE + qi->cause;
			cmsg.Reason = 0x3400 | p[3];
		} else
			cmsg.Reason = 0;
	} else { // timeout
		cmsg.Reason = CapiProtocolErrorLayer1;
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Q931_info_t	*qi = arg;
	_cmsg		cmsg;
	__u8		tmp[10], *p;
	
	if (!qi || !qi->channel_id) {
		int_error();
		return;
	}
	p = (u_char *)qi;
	p += L3_EXTRA_SIZE + qi->channel_id;
	cplci->bchannel = plci_parse_channel_id(p);
	cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
	p = &tmp[1];
	p += capiEncodeWord(p, 0x0005); // Suspend
	p += capiEncodeFacIndSuspend(p, CapiSuccess);
	tmp[0] = p - &tmp[1];
	cmsg.FacilitySelector = 0x0003;
	cmsg.FacilityIndicationParameter = tmp;
	contrRecvCmsg(cplci->contr, &cmsg);

	FsmEvent(&cplci->plci_m, EV_PLCI_RESUME_CONF, &cmsg);
}

static void plci_select_b_protocol_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	Ncci_t	*ncci = cplci->ncci;
	_cmsg	*cmsg = arg;
	__u16	Info;

	Info = cplciCheckBprotocol(cplci, cmsg);

	if (!ncci) {
		int_error();
		return;
	}

	Info = ncciSelectBprotocol(ncci);

	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_info_req_overlap(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t		*cplci = fi->userdata;
	Plci_t		*plci = cplci->plci;
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
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_cc_ph_control_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t	*cplci = fi->userdata;
	int	*tt = arg;
	_cmsg	cmsg;
	__u8	tmp[2];

	if (!arg)
		return;

	cplciDebug(cplci, CAPI_DBG_PLCI_INFO, "%s: tt(%x)\n", __FUNCTION__, *tt);
	if ((*tt & ~DTMF_TONE_MASK) != DTMF_TONE_VAL)
		return;

	cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
	tmp[0] = 1;
	tmp[1] = *tt & DTMF_TONE_MASK;
	cmsg.FacilitySelector = 0x0001;
	cmsg.FacilityIndicationParameter = tmp;
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_info_req(struct FsmInst *fi, int event, void *arg)
{
}

static struct FsmNode fn_plci_list[] =
{
  {ST_PLCI_P_0,		EV_PLCI_CONNECT_REQ,		plci_connect_req},
  {ST_PLCI_P_0,		EV_PLCI_CONNECT_IND,		plci_connect_ind},
  {ST_PLCI_P_0,		EV_PLCI_RESUME_REQ,		plci_resume_req},
  {ST_PLCI_P_0,		EV_PLCI_CC_SETUP_IND,		plci_cc_setup_ind},

  {ST_PLCI_P_0_1,	EV_PLCI_CONNECT_CONF,		plci_connect_conf},

  {ST_PLCI_P_1,		EV_PLCI_CONNECT_ACTIVE_IND,	plci_connect_active_ind},
  {ST_PLCI_P_1,		EV_PLCI_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_1,		EV_PLCI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_1,		EV_PLCI_INFO_REQ,		plci_info_req_overlap},
  {ST_PLCI_P_1,		EV_PLCI_CC_SETUP_CONF,		plci_cc_setup_conf},
  {ST_PLCI_P_1,		EV_PLCI_CC_SETUP_CONF_ERR,	plci_cc_setup_conf_err},
  {ST_PLCI_P_1,		EV_PLCI_CC_DISCONNECT_IND,	plci_cc_disconnect_ind},
  {ST_PLCI_P_1,		EV_PLCI_CC_RELEASE_PROC_IND,	plci_cc_setup_conf_err},
  {ST_PLCI_P_1,		EV_PLCI_CC_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_1,		EV_PLCI_CC_REJECT_IND,		plci_cc_release_ind},
  {ST_PLCI_P_1,		EV_PLCI_CHANNEL_ERR,		plci_channel_err},

  {ST_PLCI_P_2,		EV_PLCI_ALERT_REQ,		plci_alert_req},
  {ST_PLCI_P_2,		EV_PLCI_CONNECT_RESP,		plci_connect_resp},
  {ST_PLCI_P_2,		EV_PLCI_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_2,		EV_PLCI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_2,		EV_PLCI_INFO_REQ,		plci_info_req},
  {ST_PLCI_P_2,		EV_PLCI_CC_RELEASE_IND,		plci_cc_release_ind},

  {ST_PLCI_P_4,		EV_PLCI_CONNECT_ACTIVE_IND,	plci_connect_active_ind},
  {ST_PLCI_P_4,		EV_PLCI_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_4,		EV_PLCI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_4,		EV_PLCI_INFO_REQ,		plci_info_req},
  {ST_PLCI_P_4,		EV_PLCI_CC_SETUP_COMPL_IND,	plci_cc_setup_compl_ind},
  {ST_PLCI_P_4,		EV_PLCI_CC_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_4,		EV_PLCI_CHANNEL_ERR,		plci_channel_err},

  {ST_PLCI_P_ACT,	EV_PLCI_CONNECT_ACTIVE_RESP,	plci_connect_active_resp},
  {ST_PLCI_P_ACT,	EV_PLCI_DISCONNECT_REQ,		plci_disconnect_req},
  {ST_PLCI_P_ACT,	EV_PLCI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_ACT,	EV_PLCI_INFO_REQ,		plci_info_req},
  {ST_PLCI_P_ACT,	EV_PLCI_SELECT_B_PROTOCOL_REQ,	plci_select_b_protocol_req},
  {ST_PLCI_P_ACT,	EV_PLCI_SUSPEND_REQ,		plci_suspend_req},
  {ST_PLCI_P_ACT,	EV_PLCI_SUSPEND_CONF,		plci_suspend_conf},
  {ST_PLCI_P_ACT,	EV_PLCI_CC_DISCONNECT_IND,	plci_cc_disconnect_ind},
  {ST_PLCI_P_ACT,	EV_PLCI_CC_RELEASE_IND,		plci_cc_release_ind},
  {ST_PLCI_P_ACT,	EV_PLCI_CC_NOTIFY_IND,		plci_cc_notify_ind},
  {ST_PLCI_P_ACT,	EV_PLCI_CC_SUSPEND_ERR,		plci_cc_suspend_err},
  {ST_PLCI_P_ACT,	EV_PLCI_CC_SUSPEND_CONF,	plci_cc_suspend_conf},
  {ST_PLCI_P_ACT,	EV_PLCI_CC_PH_CONTROL_IND,	plci_cc_ph_control_ind},

  {ST_PLCI_P_5,		EV_PLCI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_5,		EV_PLCI_CC_RELEASE_IND,		plci_cc_release_ind},

  {ST_PLCI_P_6,		EV_PLCI_DISCONNECT_RESP,	plci_disconnect_resp},

  {ST_PLCI_P_RES,	EV_PLCI_RESUME_CONF,		plci_resume_conf},
  {ST_PLCI_P_RES,	EV_PLCI_DISCONNECT_IND,		plci_disconnect_ind},
  {ST_PLCI_P_RES,	EV_PLCI_CC_RESUME_ERR,		plci_cc_resume_err},
  {ST_PLCI_P_RES,	EV_PLCI_CC_RESUME_CONF,		plci_cc_resume_conf},

#if 0
  {ST_PLCI_P_0,                EV_PLCI_FACILITY_IND,          plci_facility_ind_p_0_off_hook},

  {ST_PLCI_P_0_1,              EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_1,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_2,                EV_PLCI_FACILITY_IND,          plci_facility_ind_p_2_off_hook},

  {ST_PLCI_P_3,                EV_PLCI_CONNECT_RESP,          plci_connect_resp},
  {ST_PLCI_P_3,                EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_3,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_3,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_3,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},
  {ST_PLCI_P_3,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_4,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_ACT,              EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},

  {ST_PLCI_P_5,                EV_PLCI_FACILITY_IND,          plci_facility_ind_on_hook},
#endif
};

const int FN_PLCI_COUNT = sizeof(fn_plci_list)/sizeof(struct FsmNode);

void cplciConstr(Cplci_t *cplci, Appl_t *appl, Plci_t *plci)
{
	memset(cplci, 0, sizeof(Cplci_t));
	cplci->adrPLCI = plci->adrPLCI;
	cplci->appl = appl;
	cplci->plci = plci;
	cplci->contr = plci->contr;
	cplci->plci_m.fsm        = &plci_fsm;
	cplci->plci_m.state      = ST_PLCI_P_0;
	cplci->plci_m.debug      = plci->contr->debug & CAPI_DBG_PLCI_STATE;
	cplci->plci_m.userdata   = cplci;
	cplci->plci_m.printdebug = cplci_debug;
	cplci->bchannel = -1;
}

void cplciDestr(Cplci_t *cplci)
{
	if (cplci->plci) {
		cplciDebug(cplci, CAPI_DBG_PLCI, "%s plci state %s\n", __FUNCTION__,
			str_st_plci[cplci->plci_m.state]);
		if (cplci->plci_m.state != ST_PLCI_P_0) {
			struct sk_buff	*skb = alloc_l3msg(10, MT_RELEASE_COMPLETE);
			unsigned char cause[] = {2,0x80,0x80| CAUSE_RESOURCES_UNAVAIL};

			if (skb) {
				AddIE(skb, IE_CAUSE, cause);
				plciL4L3(cplci->plci, CC_RELEASE_COMPLETE | REQUEST, skb);
			}
		}
 		plciDetachCplci(cplci->plci, cplci);
	}
	if (cplci->ncci) {
		ncciDestr(cplci->ncci);
		kfree(cplci->ncci);
		cplci->ncci = NULL;
	}
}

void cplci_l3l4(Cplci_t *cplci, int pr, void *arg)
{
	Q931_info_t	*qi = arg;
	u_char		*ie;

	cplciDebug(cplci, CAPI_DBG_PLCI_L3, "%s: cplci(%x) plci(%x) pr(%x) arg(%p)\n",
		__FUNCTION__, cplci->adrPLCI, cplci->plci->adrPLCI, pr, arg);
	switch (pr) {
		case CC_SETUP | INDICATION:
			if (!qi)
				return;
			cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
			cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
			cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, qi);
			cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
			if (qi->channel_id) {
				ie = (u_char *)qi;
				ie += L3_EXTRA_SIZE + qi->channel_id;
				cplci->bchannel = plci_parse_channel_id(ie);
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_IND, arg); 
			break;
		case CC_TIMEOUT | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_CONF_ERR, arg); 
			break;
		case CC_CONNECT | INDICATION:
			if (qi) {	
				cplciInfoIndIE(cplci, IE_DATE, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, qi);
				cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					cplci->bchannel = plci_parse_channel_id(ie);
				}
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_CONF, arg); 
			break;
		case CC_CONNECT_ACKNOWLEDGE | INDICATION:
			if (qi) {
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					cplci->bchannel = plci_parse_channel_id(ie);
				}
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_COMPL_IND, arg); 
			break;
		case CC_DISCONNECT | INDICATION:
			if (qi) {
				cplciInfoIndMsg(cplci, CAPI_INFOMASK_EARLYB3, MT_DISCONNECT);
				cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS, qi);
				cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
		  	FsmEvent(&cplci->plci_m, EV_PLCI_CC_DISCONNECT_IND, arg); 
			break;
		case CC_RELEASE | INDICATION:
			if (qi) {
				cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
		        FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_IND, arg); 
			break;
		case CC_RELEASE_COMPLETE | INDICATION:
			if (qi) {
				cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_IND, arg);
			break;
		case CC_RELEASE_CR | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_PROC_IND, arg); 
			break;
		case CC_SETUP_ACKNOWLEDGE | INDICATION:
			if (qi) {
				cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, MT_SETUP_ACKNOWLEDGE);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					cplci->bchannel = plci_parse_channel_id(ie);
				}
			}
			break;
		case CC_PROCEEDING | INDICATION:
			if (qi) {
				cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, MT_CALL_PROCEEDING);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					cplci->bchannel = plci_parse_channel_id(ie);
				}
			}
			break;
		case CC_ALERTING | INDICATION:
			if (qi) {
				cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, MT_ALERTING);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
				cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY, qi);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID, qi);
				if (qi->channel_id) {
					ie = (u_char *)qi;
					ie += L3_EXTRA_SIZE + qi->channel_id;
					cplci->bchannel = plci_parse_channel_id(ie);
				}
			}
			break;
		case CC_PROGRESS | INDICATION:
			if (qi) {
				cplciInfoIndMsg(cplci, CAPI_INFOMASK_PROGRESS, MT_PROGRESS);
				cplciInfoIndIE(cplci, IE_CAUSE, CAPI_INFOMASK_CAUSE, qi);
				cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY, qi);
				cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER, qi);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3, qi);
			}
			break;
		case CC_SUSPEND_ACKNOWLEDGE | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SUSPEND_CONF, arg); 
			break;
		case CC_SUSPEND_REJECT | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SUSPEND_ERR, arg); 
			break;
		case CC_RESUME_ACKNOWLEDGE | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_RESUME_CONF, arg); 
			break;
		case CC_RESUME_REJECT | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_RESUME_ERR, arg); 
			break;
		case CC_NOTIFY | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_NOTIFY_IND, arg); 
			break;
		case PH_CONTROL | INDICATION:
			/* TOUCH TONE */
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_PH_CONTROL_IND, arg);
			break;
		default:
			cplciDebug(cplci, CAPI_DBG_WARN, 
			   "%s: pr 0x%x not handled", __FUNCTION__, pr);
			break;
	}
}

void cplciSendMessage(Cplci_t *cplci, struct sk_buff *skb)
{
	int retval = 0;
	_cmsg cmsg;
	capi_message2cmsg(&cmsg, skb->data);

	switch (CMSGCMD(&cmsg)) {
	case CAPI_INFO_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_INFO_REQ, &cmsg);
		break;
	case CAPI_ALERT_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_ALERT_REQ, &cmsg);
		break;
	case CAPI_CONNECT_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_REQ, &cmsg);
		break;
	case CAPI_CONNECT_RESP:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_RESP, &cmsg);
		break;
	case CAPI_DISCONNECT_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_REQ, &cmsg);
		break;
	case CAPI_DISCONNECT_RESP:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_RESP, &cmsg);
		break;
	case CAPI_CONNECT_ACTIVE_RESP:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_ACTIVE_RESP, &cmsg);
		break;
	case CAPI_SELECT_B_PROTOCOL_REQ:
		retval = FsmEvent(&cplci->plci_m, EV_PLCI_SELECT_B_PROTOCOL_REQ, &cmsg);
		break;
	default:
		int_error();
		retval = -1;
	}
	if (retval) { 
		if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_REQ) {
			contrAnswerMessage(cplci->contr, skb, 
					   CapiMessageNotSupportedInCurrentState);
		}
	}
	dev_kfree_skb(skb);
}

void cplciLinkUp(Cplci_t *cplci)
{
	if (cplci->ncci)
		return;
	if (cplci->bchannel == -1) {/* no valid channel set */
		int_error();
		return;
	}
	
	cplci->ncci = kmalloc(sizeof(Ncci_t), GFP_ATOMIC);
	if (!cplci->ncci) {
		int_error();
		return;
	}
	ncciConstr(cplci->ncci, cplci);
	ncciLinkUp(cplci->ncci);
}

void cplciLinkDown(Cplci_t *cplci)
{
	if (!cplci->ncci) {
		return;
	}
	ncciLinkDown(cplci->ncci);
}

int cplciFacSuspendReq(Cplci_t *cplci, struct FacReqParm *facReqParm,
		     struct FacConfParm *facConfParm)
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

	if (FsmEvent(&cplci->plci_m, EV_PLCI_SUSPEND_REQ, skb)) {
		// no routine
		facConfParm->u.Info.SupplementaryServiceInfo = 
			CapiRequestNotAllowedInThisState;
		kfree(skb);
	} else {
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	}
	return CapiSuccess;
}

int cplciFacResumeReq(Cplci_t *cplci, struct FacReqParm *facReqParm,
		     struct FacConfParm *facConfParm)
{
	__u8		*CallIdentity;
	struct sk_buff	*skb;

	CallIdentity = facReqParm->u.Resume.CallIdentity;
	if (CallIdentity && CallIdentity[0] > 8) {
		applDelCplci(cplci->appl, cplci);
		return CapiIllMessageParmCoding;
	}
	skb = alloc_l3msg(20, MT_RESUME);
	if (!skb) {
		int_error();
		return CapiIllMessageParmCoding;
	}
	if (CallIdentity && CallIdentity[0])
		AddIE(skb, IE_CALL_ID, CallIdentity);
	if (FsmEvent(&cplci->plci_m, EV_PLCI_RESUME_REQ, skb))
		kfree(skb);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

void cplciClearOtherApps(Cplci_t *cplci)
{
	Plci_t *plci = cplci->plci;
	Cplci_t *cp;
	__u16 applId;
	_cmsg cm;

	for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
		cp = plci->cplcis[applId - 1];
		if (cp && (cp != cplci)) {
			plciDetachCplci(plci, cp);
			cplciCmsgHeader(cp, &cm, CAPI_DISCONNECT, CAPI_IND);
			cm.Reason = 0x3304; // other application got the call
			FsmEvent(&cp->plci_m, EV_PLCI_DISCONNECT_IND, &cm);
			cplciRecvCmsg(cp, &cm);
		}
	} 
}

void cplciInfoIndMsg(Cplci_t *cplci,  __u32 mask, unsigned char mt)
{
	_cmsg cmsg;

	if (!(cplci->appl->listen.InfoMask & mask))
		return;
	cplciCmsgHeader(cplci, &cmsg, CAPI_INFO, CAPI_IND);
	cmsg.InfoNumber = 0x8000 | mt;
	cmsg.InfoElement = 0;
	cplciRecvCmsg(cplci, &cmsg);
}

void cplciInfoIndIE(Cplci_t *cplci, unsigned char ie, __u32 mask, Q931_info_t *qi)
{
	_cmsg		cmsg;
	u_char		*iep = NULL;
	u16		*ies;
	

	if (!(cplci->appl->listen.InfoMask & mask))
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
	if (ie == IE_PROGRESS && cplci->appl->listen.InfoMask & CAPI_INFOMASK_EARLYB3) {
		if (iep[0] == 0x02 && iep[2] == 0x88) { // in-band information available
			cplciLinkUp(cplci);
		}
	}
	cplciCmsgHeader(cplci, &cmsg, CAPI_INFO, CAPI_IND);
	cmsg.InfoNumber = ie;
	cmsg.InfoElement = iep;
	cplciRecvCmsg(cplci, &cmsg);
}

void init_cplci(void)
{
	plci_fsm.state_count = ST_PLCI_COUNT;
	plci_fsm.event_count = EV_PLCI_COUNT;
	plci_fsm.strEvent = str_ev_plci;
	plci_fsm.strState = str_st_plci;
	
	FsmNew(&plci_fsm, fn_plci_list, FN_PLCI_COUNT);
}


void free_cplci(void)
{
	FsmFree(&plci_fsm);
}
