/* $Id: appl.c,v 0.3 2001/02/27 17:45:44 kkeil Exp $
 *
 */

#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

#define applDebug(appl, lev, fmt, args...) \
        capidebug(lev, fmt, ## args)

void applConstr(Appl_t *appl, Contr_t *contr, __u16 ApplId, capi_register_params *rp)
{
	memset(appl, 0, sizeof(Appl_t));
	appl->contr = contr;
	appl->ApplId = ApplId;
	appl->MsgId = 1;
	appl->NotificationMask = 0;
	memcpy(&appl->rp, rp, sizeof(capi_register_params));
	listenConstr(&appl->listen, contr, ApplId);
}

void applDestr(Appl_t *appl)
{
	int i;

	listenDestr(&appl->listen);
	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (appl->cplcis[i]) {
			cplciDestr(appl->cplcis[i]);
			kfree(appl->cplcis[i]);
			appl->cplcis[i] = NULL;
		}
	}
}

Cplci_t *applAdr2cplci(Appl_t *appl, __u32 adr)
{
	int i = (adr >> 8) & 0xff;

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return 0;
	}
	return appl->cplcis[i - 1];
}

void applSendMessage(Appl_t *appl, struct sk_buff *skb)
{
	Plci_t *plci;
	Cplci_t *cplci;

	switch (CAPICMD(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data))) {

	// for NCCI state machine
	case CAPI_DATA_B3_REQ:
	case CAPI_DATA_B3_RESP:
	case CAPI_CONNECT_B3_REQ:
	case CAPI_CONNECT_B3_RESP:
	case CAPI_CONNECT_B3_ACTIVE_RESP:
	case CAPI_DISCONNECT_B3_REQ:
	case CAPI_DISCONNECT_B3_RESP:
		cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
		if (!cplci) {
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		if (!cplci->ncci) {
			int_error();
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		ncciSendMessage(cplci->ncci, skb);
		break;
	// for PLCI state machine
	case CAPI_INFO_REQ:
	case CAPI_ALERT_REQ:
	case CAPI_CONNECT_RESP:
	case CAPI_CONNECT_ACTIVE_RESP:
	case CAPI_DISCONNECT_REQ:
	case CAPI_DISCONNECT_RESP:
	case CAPI_SELECT_B_PROTOCOL_REQ:
		cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
		if (!cplci) {
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			goto free;
		}
		cplciSendMessage(cplci, skb);
		break;
	case CAPI_CONNECT_REQ:
		plci = contrNewPlci(appl->contr);
		if (!plci) {
			contrAnswerMessage(appl->contr, skb, CapiNoPlciAvailable);
			goto free;
		}
		cplci = applNewCplci(appl, plci);
		if (!cplci) {
			contrDelPlci(appl->contr, plci);
			contrAnswerMessage(appl->contr, skb, CapiNoPlciAvailable);
			goto free;
		}
		cplciSendMessage(cplci, skb);
		break;

	// for LISTEN state machine
	case CAPI_LISTEN_REQ:
		listenSendMessage(&appl->listen, skb);
		break;

	// other
	case CAPI_FACILITY_REQ:
		applFacilityReq(appl, skb);
		break;
	case CAPI_FACILITY_RESP:
		goto free;
	case CAPI_MANUFACTURER_REQ:
		applManufacturerReq(appl, skb);
		break;
	case CAPI_INFO_RESP:
		goto free;
	default:
		applDebug(appl, LL_DEB_WARN, "applSendMessage: %#x %#x not handled!", 
			  CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data));
		if (CAPIMSG_SUBCOMMAND(skb->data) == CAPI_REQ)
			contrAnswerMessage(appl->contr, skb, 
					   CapiMessageNotSupportedInCurrentState);
		goto free;
	}

	return;

 free:
	dev_kfree_skb(skb);
}

void applFacilityReq(Appl_t *appl, struct sk_buff *skb)
{
	_cmsg cmsg;
	capi_message2cmsg(&cmsg, skb->data);

	switch (cmsg.FacilitySelector) {
	case 0x0003: // SupplementaryServices
		applSuppFacilityReq(appl, &cmsg);
		break;
	default:
		int_error();
	}
	
	dev_kfree_skb(skb);
}

Cplci_t *applNewCplci(Appl_t *appl, Plci_t *plci)
{
	Cplci_t *cplci;
	int i = (plci->adrPLCI >> 8);

	if (appl->cplcis[i - 1]) {
		int_error();
		return 0;
	}
	cplci = kmalloc(sizeof(Cplci_t), GFP_ATOMIC);
	cplciConstr(cplci, appl, plci);
	appl->cplcis[i - 1] = cplci;
	plciAttachCplci(plci, cplci);
	return cplci;
}

void applDelCplci(Appl_t *appl, Cplci_t *cplci)
{
	int i = cplci->adrPLCI >> 8;

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return;
	}
	if (appl->cplcis[i-1] != cplci) {
		int_error();
		return;
	}
	cplciDestr(cplci);
	kfree(cplci);
	appl->cplcis[i-1] = NULL;
}

#define CLASS_I4L                   0x00
#define FUNCTION_I4L_LEASED_IN      0x01
#define FUNCTION_I4L_DEC_USE_COUNT  0x02
#define FUNCTION_I4L_INC_USE_COUNT  0x03

#define CLASS_AVM                   0x00
#define FUNCTION_AVM_D2_TRACE       0x01


struct I4LLeasedManuData {
	__u8 Length;
	__u8 BChannel;
};

struct AVMD2Trace {
	__u8 Length;
	__u8 data[4];
};

struct ManufacturerReq {
	__u32 Class;
	__u32 Function;
	union {
		struct I4LLeasedManuData leased;
		struct AVMD2Trace d2trace;
	} f;
}; 

void applManufacturerReqAVM(Appl_t *appl, struct sk_buff *skb)
{
	struct ManufacturerReq *manuReq;

	manuReq = (struct ManufacturerReq *)&skb->data[16];
	if (manuReq->Class != CLASS_AVM) {
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown class %#x\n", manuReq->Class);
		goto out;
	}
	switch (manuReq->Function) {
	case FUNCTION_AVM_D2_TRACE:
		if (skb->len != 16 + 8 + 5)
			goto out;
		if (manuReq->f.d2trace.Length != 4)
			goto out;
		if (memcmp(manuReq->f.d2trace.data, "\200\014\000\000", 4) == 0) {
			test_and_set_bit(APPL_FLAG_D2TRACE, &appl->flags);
		} else if (memcmp(manuReq->f.d2trace.data, "\000\000\000\000", 4) == 0) {
			test_and_clear_bit(APPL_FLAG_D2TRACE, &appl->flags);
		} else {
			int_error();
		}
		break;
	default:
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown function %#x\n", manuReq->Function);
	}

 out:
	dev_kfree_skb(skb);
}

void applManufacturerReqI4L(Appl_t *appl, struct sk_buff *skb)
{
	int bchannel;
	struct ManufacturerReq *manuReq;

	manuReq = (struct ManufacturerReq *)&skb->data[16];
	if (manuReq->Class != CLASS_I4L) {
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown class %#x\n", manuReq->Class);
		goto out;
	}
	switch (manuReq->Function) {
	case FUNCTION_I4L_LEASED_IN:
		if (skb->len < 16 + 8 + 2)
			goto out;
		if (manuReq->f.leased.Length != 2)
			goto out;
		bchannel = manuReq->f.leased.BChannel;
		if (bchannel < 1 || bchannel > 2)
			goto out;
// FIXME
//		contrL4L3(appl->contr, CC_SETUP | INDICATION, &bchannel);
		break;
	case FUNCTION_I4L_DEC_USE_COUNT:
		break;
	case FUNCTION_I4L_INC_USE_COUNT:
		break;
	default:
		applDebug(appl, LL_DEB_INFO, "CAPI: unknown function %#x\n", manuReq->Function);
	}

 out:
	dev_kfree_skb(skb);
}

void applManufacturerReq(Appl_t *appl, struct sk_buff *skb)
{
	if (skb->len < 16 + 8) {
		return;
	}
	if (memcmp(&skb->data[12], "AVM!", 4) == 0) {
		applManufacturerReqAVM(appl, skb);
	}
	if (memcmp(&skb->data[12], "I4L!", 4) == 0) {
		applManufacturerReqI4L(appl, skb);
	}
	return;
}

void applD2Trace(Appl_t *appl, u_char *buf, int len)
{
	_cmsg cmsg;
	__u8 manuData[255];

	if (!test_bit(APPL_FLAG_D2TRACE, &appl->flags))
		return;
	
	memset(&cmsg, 0, sizeof(_cmsg));
	capi_cmsg_header(&cmsg, appl->ApplId, CAPI_MANUFACTURER, CAPI_IND, 
			 appl->MsgId++, appl->contr->adrController);
	cmsg.ManuID = 0x214D5641; // "AVM!"
	cmsg.Class = CLASS_AVM;
	cmsg.Function = FUNCTION_AVM_D2_TRACE;
	cmsg.ManuData = (_cstruct) &manuData;
	manuData[0] = 2 + len; // length
	manuData[1] = 0x80;
	manuData[2] = 0x0f;
	memcpy(&manuData[3], buf, len);
	
	contrRecvCmsg(appl->contr, &cmsg);
}
