/* $Id: appl.c,v 1.7 2003/11/11 20:31:34 keil Exp $
 *
 */

#include "capi.h"
#include "helper.h"
#include "debug.h"
#include "mISDNManufacturer.h"

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
		plci = contrNewPlci(appl->contr, MISDN_ID_ANY);
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
		applDebug(appl, CAPI_DBG_WARN, "applSendMessage: %#x %#x not handled!", 
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
	_cmsg	cmsg;
	Cplci_t	*cplci;

	capi_message2cmsg(&cmsg, skb->data);
	switch (cmsg.FacilitySelector) {
		case 0x0000: // Handset
		case 0x0001: // DTMF
			cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
			if (cplci && cplci->ncci) {
				ncciSendMessage(cplci->ncci, skb);
				return;
			}
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			break;
		case 0x0003: // SupplementaryServices
			applSuppFacilityReq(appl, &cmsg);
			break;
		default:
			int_error();
			contrAnswerMessage(appl->contr, skb, CapiFacilityNotSupported);
			break;
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

#define AVM_MANUFACTURER_ID	0x214D5641 /* "AVM!" */
#define CLASS_AVM		0x00
#define FUNCTION_AVM_D2_TRACE	0x01

struct AVMD2Trace {
	__u8 Length;
	__u8 data[4];
};

void applManufacturerReqAVM(Appl_t *appl, _cmsg *cmsg, struct sk_buff *skb)
{
	struct AVMD2Trace *at;

	if (cmsg->Class != CLASS_AVM) {
		applDebug(appl, CAPI_DBG_APPL_INFO, "CAPI: unknown class %#x\n", cmsg->Class);
		dev_kfree_skb(skb);
		return;
	}
	switch (cmsg->Function) {
		case FUNCTION_AVM_D2_TRACE:
			at = (struct AVMD2Trace *)cmsg->ManuData;
			if (!at || at->Length != 4) {
				int_error();
				break;
			}
			if (memcmp(at->data, "\200\014\000\000", 4) == 0) {
				test_and_set_bit(APPL_FLAG_D2TRACE, &appl->flags);
			} else if (memcmp(at->data, "\000\000\000\000", 4) == 0) {
				test_and_clear_bit(APPL_FLAG_D2TRACE, &appl->flags);
			} else {
				int_error();
			}
			break;
		default:
			applDebug(appl, CAPI_DBG_APPL_INFO, "CAPI: unknown function %#x\n", cmsg->Function);
	}
	dev_kfree_skb(skb);
}

void applManufacturerReqmISDN(Appl_t *appl, _cmsg *cmsg, struct sk_buff *skb)
{
	Cplci_t	*cplci;

	switch (cmsg->Class) {
		case mISDN_MF_CLASS_HANDSET:
			/* Note normally MANUFATURER messages are only defined for
			 * controller address we extent it here to PLCI/NCCI
			 */
			cplci = applAdr2cplci(appl, CAPIMSG_CONTROL(skb->data));
			if (cplci && cplci->ncci) {
				ncciSendMessage(cplci->ncci, skb);
				return;
			}
			contrAnswerMessage(appl->contr, skb, CapiIllContrPlciNcci);
			break;
		default:
			dev_kfree_skb(skb);
			break;
	}
}

void applManufacturerReq(Appl_t *appl, struct sk_buff *skb)
{
	_cmsg	cmsg;

	if (skb->len < 16 + 8) {
		dev_kfree_skb(skb);
		return;
	}
	capi_message2cmsg(&cmsg, skb->data);
	switch (cmsg.ManuID) {
		case mISDN_MANUFACTURER_ID:
			applManufacturerReqmISDN(appl, &cmsg, skb);
			break;
		case AVM_MANUFACTURER_ID:
			applManufacturerReqAVM(appl, &cmsg, skb);
			break;
		default:
			applDebug(appl, CAPI_DBG_APPL_INFO, "CAPI: unknown ManuID %#x\n", cmsg.ManuID);
			dev_kfree_skb(skb);
			break;
	}
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
	cmsg.ManuID = AVM_MANUFACTURER_ID;
	cmsg.Class = CLASS_AVM;
	cmsg.Function = FUNCTION_AVM_D2_TRACE;
	cmsg.ManuData = (_cstruct) &manuData;
	manuData[0] = 2 + len; // length
	manuData[1] = 0x80;
	manuData[2] = 0x0f;
	memcpy(&manuData[3], buf, len);
	
	contrRecvCmsg(appl->contr, &cmsg);
}
