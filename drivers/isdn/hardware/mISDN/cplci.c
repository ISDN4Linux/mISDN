/* $Id: cplci.c,v 0.5 2001/03/11 21:05:20 kkeil Exp $
 *
 */

#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"
#include "dss1.h"

#define cplciDebug(cplci, lev, fmt, args...) \
	capidebug(lev, fmt, ## args)

static u_char BEARER_SPEECH_64K_ALAW[4] = {3, 0x80, 0x90, 0xA3};
static u_char BEARER_SPEECH_64K_ULAW[4] = {3, 0x80, 0x90, 0xA2};
static u_char BEARER_UNRES_DIGITAL_64K[3] = {2, 0x88, 0x90};
static u_char BEARER_RES_DIGITAL_64K[3] = {2, 0x89, 0x90};
static u_char BEARER_31AUDIO_64K_ALAW[4] = {3, 0x90, 0x90, 0xA3};
static u_char BEARER_31AUDIO_64K_ULAW[4] = {3, 0x90, 0x90, 0xA2};
static u_char HLC_TELEPHONY[3] = {2, 0x91, 0x81};
static u_char HLC_FACSIMILE[3] = {2, 0x91, 0x84};

__u16 q931CIPValue(SETUP_t *setup)
{
	__u16 CIPValue = 0;

	if (!setup)
		return 0;
	if (!setup->BEARER)
		return 0;
	if (memcmp(setup->BEARER, BEARER_SPEECH_64K_ALAW, 4) == 0
	    || memcmp(setup->BEARER, BEARER_SPEECH_64K_ULAW, 4) == 0) {
		CIPValue = 1;
	} else if (memcmp(setup->BEARER, BEARER_UNRES_DIGITAL_64K, 3) == 0) {
		CIPValue = 2;
	} else if (memcmp(setup->BEARER, BEARER_RES_DIGITAL_64K, 3) == 0) {
		CIPValue = 3;
	} else if (memcmp(setup->BEARER, BEARER_31AUDIO_64K_ALAW, 4) == 0
		   || memcmp(setup->BEARER, BEARER_31AUDIO_64K_ULAW, 4) == 0) {
		CIPValue = 4;
	} else {
		CIPValue = 0;
	}

	if (!setup->HLC) {
		return CIPValue;
	}

	if ((CIPValue == 1) || (CIPValue == 4)) {
		if (memcmp(setup->HLC, HLC_TELEPHONY, 3) == 0) {
			CIPValue = 16;
		} else if (memcmp(setup->HLC, HLC_FACSIMILE, 3) == 0) {
			CIPValue = 17;
		}
	}
	return CIPValue;
}

__u16 CIPValue2setup(__u16 CIPValue, SETUP_t *setup)
{
	switch (CIPValue) {
		case 16:
			setup->HLC = HLC_TELEPHONY;
			setup->BEARER = BEARER_31AUDIO_64K_ALAW;
			break;
		case 17:
			setup->HLC = HLC_FACSIMILE;
			setup->BEARER = BEARER_31AUDIO_64K_ALAW;
			break;
		case 1:
			setup->BEARER = BEARER_SPEECH_64K_ALAW;
			break;
		case 2:
			setup->BEARER = BEARER_UNRES_DIGITAL_64K;
			break;
		case 3:
			setup->BEARER = BEARER_RES_DIGITAL_64K;
			break;
		case 4:
			setup->BEARER = BEARER_31AUDIO_64K_ALAW;
			break;
		default:
			return CapiIllMessageParmCoding;
	}
	return 0;
}

__u16 cmsg2setup_req(_cmsg *cmsg, SETUP_t *setup)
{
	memset(setup, 0, sizeof(SETUP_t));
	if (CIPValue2setup(cmsg->CIPValue, setup))
		goto err;
	if (cmsg->CalledPartyNumber && cmsg->CalledPartyNumber[0])
		setup->CALLED_PN = cmsg->CalledPartyNumber;
	if (cmsg->CalledPartySubaddress && cmsg->CalledPartySubaddress[0])
		setup->CALLED_SUB = cmsg->CalledPartySubaddress;
	if (cmsg->CallingPartyNumber && cmsg->CallingPartyNumber[0])
		setup->CALLING_PN = cmsg->CallingPartyNumber;
	if (cmsg->CallingPartySubaddress && cmsg->CallingPartySubaddress[0])
		setup->CALLING_SUB = cmsg->CallingPartySubaddress;
	if (cmsg->BC && cmsg->BC[0])
		setup->BEARER = cmsg->BC;
	if (cmsg->LLC && cmsg->LLC[0])
		setup->LLC = cmsg->LLC;
	if (cmsg->HLC && cmsg->HLC[0])
		setup->HLC = cmsg->HLC;
	return 0;
 err:
	return CapiIllMessageParmCoding;
}

__u16 cmsg2info_req(_cmsg *cmsg, INFORMATION_t *info)
{
	memset(info, 0, sizeof(INFORMATION_t));
	if (cmsg->Keypadfacility && cmsg->Keypadfacility[0])
		info->KEYPAD = cmsg->Keypadfacility;
	if (cmsg->CalledPartyNumber && cmsg->CalledPartyNumber[0])
		info->CALLED_PN = cmsg->CalledPartyNumber;
	return 0;
}

__u16 cmsg2alerting_req(_cmsg *cmsg, ALERTING_t *alert)
{
	memset(alert, 0, sizeof(ALERTING_t));
	if (cmsg->Useruserdata && cmsg->Useruserdata[0])
		alert->USER_USER = cmsg->Useruserdata;
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
}

const EV_PLCI_COUNT = EV_PLCI_CC_REJECT_IND + 1;

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
	cplciDebug(cplci, LL_DEB_STATE, tmp);
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
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	SETUP_t setup_req;
	_cmsg *cmsg = arg;
	__u16 Info = 0;

	FsmChangeState(fi, ST_PLCI_P_0_1);
	test_and_set_bit(PLCI_FLAG_OUTGOING, &plci->flags);

	if ((Info = cmsg2setup_req(cmsg, &setup_req))) {
		goto answer;
	}
	if ((Info = cplciCheckBprotocol(cplci, cmsg))) {
		goto answer;
	}

	plciNewCrReq(plci);
	plciL4L3(plci, CC_SETUP | REQUEST, sizeof(SETUP_t), &setup_req);
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
	Cplci_t *cplci = fi->userdata;
	_cmsg *cmsg = arg;
  
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
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	int len;

	if (arg)
		len = sizeof(SUSPEND_t);
	else
		len = 0;
	plciL4L3(plci, CC_SUSPEND | REQUEST, len, arg); 
}

static void plci_resume_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	int len;

	if (arg)
		len = sizeof(RESUME_t);
	else
		len = 0;

	// we already sent CONF with Info = SuppInfo = 0
	FsmChangeState(fi, ST_PLCI_P_RES);
	plciNewCrReq(plci);
	plciL4L3(plci, CC_RESUME | REQUEST, len, arg);
}

static void plci_alert_req(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	ALERTING_t alerting_req;
	_cmsg *cmsg = arg;
	__u16 Info;
	
	if (test_and_set_bit(PLCI_FLAG_ALERTING, &plci->flags)) {
		Info = 0x0003; // other app is already alerting
	} else {
		Info = cmsg2alerting_req(cmsg, &alerting_req);
		if (Info == 0) {
			plciL4L3(plci, CC_ALERTING | REQUEST,
				sizeof(ALERTING_t), &alerting_req); 
		}
	}
	
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_connect_resp(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	DISCONNECT_t disconnect_req;
	unsigned char cause[4];
	_cmsg *cmsg = arg;

	switch (cmsg->Reject) {
		case 0 : // accept
			if (cplciCheckBprotocol(cplci, cmsg)) {
				int_error();
			}
			cplciClearOtherApps(cplci);
			plciL4L3(plci, CC_CONNECT | REQUEST, 0, NULL);
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
					memset(&disconnect_req, 0, sizeof(DISCONNECT_t));
					disconnect_req.CAUSE = cause;
					plciL4L3(plci, CC_DISCONNECT | REQUEST,
						sizeof(DISCONNECT_t), &disconnect_req);
				} else {
					plciL4L3(plci, CC_RELEASE_COMPLETE | REQUEST, 0, NULL);
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
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	DISCONNECT_t disconnect_req;
	u_char cause[4];
	_cmsg *cmsg = arg;

	FsmChangeState(fi, ST_PLCI_P_5);
	
	if (!plci) {
		int_error();
		return;
	}
	capi_cmsg_answer(cmsg);
	cmsg->Reason = 0; // disconnect initiated
	cplciRecvCmsg(cplci, cmsg);

	cplciLinkDown(cplci);

	if (cplci->cause[0]) { // FIXME
		plciL4L3(plci, CC_RELEASE | REQUEST, 0, NULL);
	} else {
		memset(&disconnect_req, 0, sizeof(DISCONNECT_t));
		memcpy(cause, "\x02\x80\x90", 3); // normal call clearing
		disconnect_req.CAUSE = cause;
		plciL4L3(plci, CC_DISCONNECT | REQUEST, sizeof(DISCONNECT_t),
			&disconnect_req);
	}
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
	Cplci_t *cplci = fi->userdata;
	_cmsg cmsg;
	CONNECT_t *conn = arg;

	memset(&cmsg, 0, sizeof(_cmsg));
	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	if (arg) {
		cmsg.ConnectedNumber        = conn->CONNECT_PN;
		cmsg.ConnectedSubaddress    = conn->CONNECT_SUB;
		cmsg.LLC                    = conn->LLC;
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

static void plci_cc_setup_ind(struct FsmInst *fi, int event, void *arg)
{ 
	Cplci_t *cplci = fi->userdata;
	SETUP_t *setup = arg;
	_cmsg cmsg;

	memset(&cmsg, 0, sizeof(_cmsg));
	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT, CAPI_IND);
	
	// FIXME: CW

	cmsg.CIPValue               = q931CIPValue(setup);
	cmsg.CalledPartyNumber      = setup->CALLED_PN;
	cmsg.CallingPartyNumber     = setup->CALLING_PN; // FIXME screen
	cmsg.CalledPartySubaddress  = setup->CALLED_SUB;
	cmsg.CallingPartySubaddress = setup->CALLING_SUB;
	cmsg.BC                     = setup->BEARER;
	cmsg.LLC                    = setup->LLC;
	cmsg.HLC                    = setup->HLC;
	// all else set to default
	
	FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_setup_compl_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	_cmsg cmsg;

	cplciCmsgHeader(cplci, &cmsg, CAPI_CONNECT_ACTIVE, CAPI_IND);
	FsmEvent(&cplci->plci_m, EV_PLCI_CONNECT_ACTIVE_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_disconnect_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	DISCONNECT_t *disc = arg;

	if (disc && disc->CAUSE) 
		memcpy(cplci->cause, disc->CAUSE, 3);
	if (!(cplci->appl->listen.InfoMask & CAPI_INFOMASK_EARLYB3)) {
		cplciLinkDown(cplci);
		plciL4L3(cplci->plci, CC_RELEASE | REQUEST, 0, NULL);
	}
}

static void plci_cc_release_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	RELEASE_t *rel = arg;
	_cmsg cmsg;
	
	plciDetachCplci(cplci->plci, cplci);

	cplciLinkDown(cplci);

	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (rel) {
		if (rel->CAUSE) {
			cmsg.Reason = 0x3400 | rel->CAUSE[2];
		} else if (cplci->cause[0]) { // cause from CC_DISCONNECT IND
			cmsg.Reason = 0x3400 | cplci->cause[2];
		} else {
			cmsg.Reason = 0;
		}
	} else {
		cmsg.Reason = CapiProtocolErrorLayer1;
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_notify_ind(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	NOTIFY_t *notify = arg;
	_cmsg cmsg;
	__u8 tmp[10], *p;

	if (!arg || !notify->NOTIFY)
		return;

	if (notify->NOTIFY[0] != 1) // len != 1
		return;

	switch (notify->NOTIFY[1]) {
		case 0x80: // user suspended
		case 0x81: // user resumed
			if (!(cplci->appl->NotificationMask & SuppServiceTP))
				break;
			cplciCmsgHeader(cplci, &cmsg, CAPI_FACILITY, CAPI_IND);
			p = &tmp[1];
			p += capiEncodeWord(p, 0x8002 + (notify->NOTIFY[1] & 1)); // Suspend/Resume Notification
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
	_cmsg cmsg;
	__u8 tmp[10], *p;

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
	Cplci_t *cplci = fi->userdata;
	SUSPEND_REJECT_t *rej = arg;
	__u16 SuppServiceReason;
	
	if (rej) { // reject from network
		if (rej->CAUSE)
			SuppServiceReason = 0x3400 | rej->CAUSE[2];
		else
			SuppServiceReason = CapiProtocolErrorLayer3;
	} else { // timeout
		SuppServiceReason = CapiTimeOut;
	}
	plci_suspend_reply(cplci, SuppServiceReason);
}

static void plci_cc_suspend_conf(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	_cmsg cmsg;

	cplciLinkDown(cplci);

	plci_suspend_reply(cplci, CapiSuccess);
	
	plciDetachCplci(cplci->plci, cplci);

	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_resume_err(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	RESUME_REJECT_t *rej = arg;
	_cmsg cmsg;
	
	cplciCmsgHeader(cplci, &cmsg, CAPI_DISCONNECT, CAPI_IND);
	if (rej) { // reject from network
		plciDetachCplci(cplci->plci, cplci);
		if (rej->CAUSE)
			cmsg.Reason = 0x3400 | rej->CAUSE[2];
		else
			cmsg.Reason = 0;
	} else { // timeout
		cmsg.Reason = CapiProtocolErrorLayer1;
	}
	FsmEvent(&cplci->plci_m, EV_PLCI_DISCONNECT_IND, &cmsg);
	cplciRecvCmsg(cplci, &cmsg);
}

static void plci_cc_resume_conf(struct FsmInst *fi, int event, void *arg)
{
	Cplci_t *cplci = fi->userdata;
	RESUME_ACKNOWLEDGE_t *ack = arg;
	_cmsg cmsg;
	__u8 tmp[10], *p;
	
	if (!ack || !ack->CHANNEL_ID) {
		int_error();
		return;
	}
	cplci->bchannel = ack->CHANNEL_ID[1];
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
	Cplci_t *cplci = fi->userdata;
	Ncci_t *ncci = cplci->ncci;
	_cmsg *cmsg = arg;
	__u16 Info;

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
	Cplci_t *cplci = fi->userdata;
	Plci_t *plci = cplci->plci;
	_cmsg *cmsg = arg;
	__u16 Info;
	INFORMATION_t info_req;

	Info = cmsg2info_req(cmsg, &info_req);
	if (Info == CapiSuccess) {
		plciL4L3(plci, CC_INFORMATION | REQUEST, sizeof(INFORMATION_t),
			&info_req);
	}
	
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	cplciRecvCmsg(cplci, cmsg);
}

static void plci_info_req(struct FsmInst *fi, int event, void *arg)
{
}

static struct FsmNode fn_plci_list[] =
{
  {ST_PLCI_P_0,                EV_PLCI_CONNECT_REQ,           plci_connect_req},
  {ST_PLCI_P_0,                EV_PLCI_CONNECT_IND,           plci_connect_ind},
  {ST_PLCI_P_0,                EV_PLCI_RESUME_REQ,            plci_resume_req},
  {ST_PLCI_P_0,                EV_PLCI_CC_SETUP_IND,          plci_cc_setup_ind},

  {ST_PLCI_P_0_1,              EV_PLCI_CONNECT_CONF,          plci_connect_conf},

  {ST_PLCI_P_1,                EV_PLCI_CONNECT_ACTIVE_IND,    plci_connect_active_ind},
  {ST_PLCI_P_1,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_1,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_1,                EV_PLCI_INFO_REQ,              plci_info_req_overlap},
  {ST_PLCI_P_1,                EV_PLCI_CC_SETUP_CONF,         plci_cc_setup_conf},
  {ST_PLCI_P_1,                EV_PLCI_CC_SETUP_CONF_ERR,     plci_cc_setup_conf_err},
  {ST_PLCI_P_1,                EV_PLCI_CC_DISCONNECT_IND,     plci_cc_disconnect_ind},
  {ST_PLCI_P_1,                EV_PLCI_CC_RELEASE_PROC_IND,   plci_cc_setup_conf_err},
  {ST_PLCI_P_1,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},
  {ST_PLCI_P_1,                EV_PLCI_CC_REJECT_IND,         plci_cc_release_ind},

  {ST_PLCI_P_2,                EV_PLCI_ALERT_REQ,             plci_alert_req},
  {ST_PLCI_P_2,                EV_PLCI_CONNECT_RESP,          plci_connect_resp},
  {ST_PLCI_P_2,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_2,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_2,                EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_2,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_4,                EV_PLCI_CONNECT_ACTIVE_IND,    plci_connect_active_ind},
  {ST_PLCI_P_4,                EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_4,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_4,                EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_4,                EV_PLCI_CC_SETUP_COMPL_IND,    plci_cc_setup_compl_ind},
  {ST_PLCI_P_4,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_ACT,              EV_PLCI_CONNECT_ACTIVE_RESP,   plci_connect_active_resp},
  {ST_PLCI_P_ACT,              EV_PLCI_DISCONNECT_REQ,        plci_disconnect_req},
  {ST_PLCI_P_ACT,              EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_INFO_REQ,              plci_info_req},
  {ST_PLCI_P_ACT,              EV_PLCI_SELECT_B_PROTOCOL_REQ, plci_select_b_protocol_req},
  {ST_PLCI_P_ACT,              EV_PLCI_SUSPEND_REQ,           plci_suspend_req},
  {ST_PLCI_P_ACT,              EV_PLCI_SUSPEND_CONF,          plci_suspend_conf},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_DISCONNECT_IND,     plci_cc_disconnect_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_NOTIFY_IND,         plci_cc_notify_ind},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_SUSPEND_ERR,        plci_cc_suspend_err},
  {ST_PLCI_P_ACT,              EV_PLCI_CC_SUSPEND_CONF,       plci_cc_suspend_conf},

  {ST_PLCI_P_5,                EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_5,                EV_PLCI_CC_RELEASE_IND,        plci_cc_release_ind},

  {ST_PLCI_P_6,                EV_PLCI_DISCONNECT_RESP,       plci_disconnect_resp},

  {ST_PLCI_P_RES,              EV_PLCI_RESUME_CONF,           plci_resume_conf},
  {ST_PLCI_P_RES,              EV_PLCI_DISCONNECT_IND,        plci_disconnect_ind},
  {ST_PLCI_P_RES,              EV_PLCI_CC_RESUME_ERR,         plci_cc_resume_err},
  {ST_PLCI_P_RES,              EV_PLCI_CC_RESUME_CONF,        plci_cc_resume_conf},

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
	cplci->plci_m.debug      = 1;
	cplci->plci_m.userdata   = cplci;
	cplci->plci_m.printdebug = cplci_debug;
	cplci->bchannel = -1;
}

void cplciDestr(Cplci_t *cplci)
{
	if (cplci->plci) {
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
	union {
		SETUP_t			*setup;
		CONNECT_t		*conn;
		CONNECT_ACKNOWLEDGE_t	*c_ack;
		DISCONNECT_t		*disc;
		RELEASE_t		*rel;
		RELEASE_COMPLETE_t	*r_cmpl;
		CALL_PROCEEDING_t	*proc;
		SETUP_ACKNOWLEDGE_t	*s_ack;
		ALERTING_t		*alert;
		PROGRESS_t		*prog;
	} p;
	
	p.setup = arg;
	printk(KERN_DEBUG "cplci_l3l4: cplci(%x) plci(%x) pr(%x) arg(%p)\n",
		cplci->adrPLCI, cplci->plci->adrPLCI, pr, arg);
	switch (pr) {
		case CC_SETUP | INDICATION:
			cplciInfoIndIE(cplci, IE_DISPLAY, CAPI_INFOMASK_DISPLAY,
				p.setup->DISPLAY);
			cplciInfoIndIE(cplci, IE_USER_USER, CAPI_INFOMASK_USERUSER,
				p.setup->USER_USER);
			cplciInfoIndIE(cplci, IE_PROGRESS, CAPI_INFOMASK_PROGRESS,
				p.setup->PROGRESS);
			cplciInfoIndIE(cplci, IE_FACILITY, CAPI_INFOMASK_FACILITY,
				p.setup->FACILITY);
			cplciInfoIndIE(cplci, IE_CHANNEL_ID, CAPI_INFOMASK_CHANNELID,
				p.setup->CHANNEL_ID);
			if (p.setup->CHANNEL_ID)
				cplci->bchannel = p.setup->CHANNEL_ID[1];
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_IND, arg); 
			break;
		case CC_TIMEOUT | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_CONF_ERR, arg); 
			break;
		case CC_CONNECT | INDICATION:
			if (p.conn) {	
				cplciInfoIndIE(cplci, IE_DATE,
					CAPI_INFOMASK_DISPLAY, p.conn->DATE);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.conn->DISPLAY);
				cplciInfoIndIE(cplci, IE_USER_USER,
					CAPI_INFOMASK_USERUSER, p.conn->USER_USER);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS, p.conn->PROGRESS);
				cplciInfoIndIE(cplci, IE_FACILITY,
					CAPI_INFOMASK_FACILITY, p.conn->FACILITY);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID,
					CAPI_INFOMASK_CHANNELID, p.conn->CHANNEL_ID);
				if (p.conn->CHANNEL_ID)
					cplci->bchannel = p.conn->CHANNEL_ID[1];
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_CONF, arg); 
			break;
		case CC_CONNECT_ACKNOWLEDGE | INDICATION:
			if (p.c_ack) {
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.c_ack->DISPLAY);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID,
					CAPI_INFOMASK_CHANNELID, p.c_ack->CHANNEL_ID);
				if (p.c_ack->CHANNEL_ID)
					cplci->bchannel = p.c_ack->CHANNEL_ID[1];
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_SETUP_COMPL_IND, arg); 
			break;
		case CC_DISCONNECT | INDICATION:
			if (p.disc) {
				cplciInfoIndMsg(cplci, CAPI_INFOMASK_EARLYB3, MT_DISCONNECT);
				cplciInfoIndIE(cplci, IE_CAUSE,
					CAPI_INFOMASK_CAUSE, p.disc->CAUSE);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.disc->DISPLAY);
				cplciInfoIndIE(cplci, IE_USER_USER,
					CAPI_INFOMASK_USERUSER, p.disc->USER_USER);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS, p.disc->PROGRESS);
				cplciInfoIndIE(cplci, IE_FACILITY,
					CAPI_INFOMASK_FACILITY, p.disc->FACILITY);
			}
		  	FsmEvent(&cplci->plci_m, EV_PLCI_CC_DISCONNECT_IND, arg); 
			break;
		case CC_RELEASE | INDICATION:
			if (p.rel) {
				cplciInfoIndIE(cplci, IE_CAUSE,
					CAPI_INFOMASK_CAUSE, p.rel->CAUSE);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.rel->DISPLAY);
				cplciInfoIndIE(cplci, IE_USER_USER,
					CAPI_INFOMASK_USERUSER, p.rel->USER_USER);
				cplciInfoIndIE(cplci, IE_FACILITY,
					CAPI_INFOMASK_FACILITY, p.rel->FACILITY);
			}
		        FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_IND, arg); 
			break;
		case CC_RELEASE_COMPLETE | INDICATION:
			if (p.r_cmpl) {
				cplciInfoIndIE(cplci, IE_CAUSE,
					CAPI_INFOMASK_CAUSE, p.r_cmpl->CAUSE);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.r_cmpl->DISPLAY);
				cplciInfoIndIE(cplci, IE_USER_USER,
					CAPI_INFOMASK_USERUSER, p.r_cmpl->USER_USER);
				cplciInfoIndIE(cplci, IE_FACILITY,
					CAPI_INFOMASK_FACILITY, p.r_cmpl->FACILITY);
			}
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_IND, arg);
			break;
		case CC_RELEASE_CR | INDICATION:
			FsmEvent(&cplci->plci_m, EV_PLCI_CC_RELEASE_PROC_IND, arg); 
			break;
		case CC_SETUP_ACKNOWLEDGE | INDICATION:
			if (p.s_ack) {
				cplciInfoIndMsg(cplci,
					CAPI_INFOMASK_PROGRESS, MT_SETUP_ACKNOWLEDGE);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.s_ack->DISPLAY);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3,
					p.s_ack->PROGRESS);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID,
					CAPI_INFOMASK_CHANNELID, p.s_ack->CHANNEL_ID);
				if (p.s_ack->CHANNEL_ID)
					cplci->bchannel = p.s_ack->CHANNEL_ID[1];
			}
			break;
		case CC_PROCEEDING | INDICATION:
			if (p.proc) {
				cplciInfoIndMsg(cplci,
					CAPI_INFOMASK_PROGRESS, MT_CALL_PROCEEDING);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.proc->DISPLAY);
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3,
					p.proc->PROGRESS);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID,
					CAPI_INFOMASK_CHANNELID, p.proc->CHANNEL_ID);
				if (p.proc->CHANNEL_ID)
					cplci->bchannel = p.proc->CHANNEL_ID[1];
			}
			break;
		case CC_ALERTING | INDICATION:
			if (p.alert) {
				cplciInfoIndMsg(cplci,
					CAPI_INFOMASK_PROGRESS, MT_ALERTING);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.alert->DISPLAY);
				cplciInfoIndIE(cplci, IE_USER_USER,
					CAPI_INFOMASK_USERUSER, p.alert->USER_USER);	
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3,
					p.alert->PROGRESS);
				cplciInfoIndIE(cplci, IE_FACILITY,
					CAPI_INFOMASK_FACILITY, p.alert->FACILITY);
				cplciInfoIndIE(cplci, IE_CHANNEL_ID,
					CAPI_INFOMASK_CHANNELID, p.alert->CHANNEL_ID);
				if (p.alert->CHANNEL_ID)
					cplci->bchannel = p.alert->CHANNEL_ID[1];
			}
			break;
		case CC_PROGRESS | INDICATION:
			if (p.prog) {
				cplciInfoIndMsg(cplci,
					CAPI_INFOMASK_PROGRESS, MT_PROGRESS);
				cplciInfoIndIE(cplci, IE_CAUSE,
					CAPI_INFOMASK_CAUSE, p.prog->CAUSE);
				cplciInfoIndIE(cplci, IE_DISPLAY,
					CAPI_INFOMASK_DISPLAY, p.prog->DISPLAY);
				cplciInfoIndIE(cplci, IE_USER_USER,
					CAPI_INFOMASK_USERUSER, p.prog->USER_USER);	
				cplciInfoIndIE(cplci, IE_PROGRESS,
					CAPI_INFOMASK_PROGRESS | CAPI_INFOMASK_EARLYB3,
						p.prog->PROGRESS);
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
		default:
			cplciDebug(cplci, LL_DEB_WARN, 
			   "cplci_handle_call_control: pr 0x%x not handled", pr);
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
	
	if (!(cplci->bchannel & 3) || ((cplci->bchannel & 3) == 3)) {
		// at the moment only B-channel 1 or B-channel 2 allowed
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
	SUSPEND_t suspend_req;
	__u8 *CallIdentity;

	memset(&suspend_req, 0, sizeof(SUSPEND_t));
	CallIdentity = facReqParm->u.Suspend.CallIdentity;
	if (CallIdentity && CallIdentity[0] > 8) 
		return CapiIllMessageParmCoding;
	if (CallIdentity && CallIdentity[0])
		suspend_req.CALL_ID = CallIdentity;

	if (FsmEvent(&cplci->plci_m, EV_PLCI_SUSPEND_REQ, &suspend_req)) {
		// no routine
		facConfParm->u.Info.SupplementaryServiceInfo = 
			CapiRequestNotAllowedInThisState;
	} else {
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	}
	return CapiSuccess;
}

int cplciFacResumeReq(Cplci_t *cplci, struct FacReqParm *facReqParm,
		     struct FacConfParm *facConfParm)
{
	RESUME_t resume_req;
	__u8 *CallIdentity;

	CallIdentity = facReqParm->u.Resume.CallIdentity;
	memset(&resume_req, 0, sizeof(RESUME_t));
	if (CallIdentity && CallIdentity[0] > 8) {
		applDelCplci(cplci->appl, cplci);
		return CapiIllMessageParmCoding;
	}
	if (CallIdentity && CallIdentity[0])
		resume_req.CALL_ID = CallIdentity;
	FsmEvent(&cplci->plci_m, EV_PLCI_RESUME_REQ, &resume_req);

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

void cplciInfoIndIE(Cplci_t *cplci, unsigned char ie, __u32 mask, u_char *iep)
{
	_cmsg cmsg;

	if (!(cplci->appl->listen.InfoMask & mask))
		return;
	if (!iep)
		return;
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
