/* $Id: appl.c,v 1.12 2004/01/12 16:20:26 keil Exp $
 *
 *  Applications are owned by the controller and only
 *  handle this controller, multiplexing multiple
 *  controller with one application is done in the higher
 *  driver independ CAPI driver. The application contain
 *  the Listen state machine.
 *
 */

#include "m_capi.h"
#include "helper.h"
#include "debug.h"
#include "mISDNManufacturer.h"

#define applDebug(appl, lev, fmt, args...) \
        capidebug(lev, fmt, ## args)

static struct list_head	garbage_applications = LIST_HEAD_INIT(garbage_applications);

int
ApplicationConstr(Controller_t *contr, __u16 ApplId, capi_register_params *rp)
{
	Application_t	*appl = kmalloc(sizeof(Application_t), GFP_ATOMIC);

	if (!appl) {
		return(-ENOMEM);
	}
	memset(appl, 0, sizeof(Application_t));
	INIT_LIST_HEAD(&appl->head);
	appl->contr = contr;
	appl->maxplci = contr->maxplci;
	appl->AppPlcis  = kmalloc(appl->maxplci * sizeof(AppPlci_t *), GFP_ATOMIC);
	if (!appl->AppPlcis) {
		kfree(appl);
		return(-ENOMEM);
	}
	memset(appl->AppPlcis, 0, appl->maxplci * sizeof(AppPlci_t *));
	appl->ApplId = ApplId;
	appl->MsgId = 1;
	appl->NotificationMask = 0;
	memcpy(&appl->reg_params, rp, sizeof(capi_register_params));
	listenConstr(appl);
	list_add(&appl->head, &contr->Applications);
	test_and_set_bit(APPL_STATE_ACTIV, &appl->state);
	return(0);
}

/*
 * Destroy the Application
 *
 * depending who initiate this we cannot release imediatly, if
 * any AppPlci is still in use.
 *
 * @who:   0 - a AppPlci is released in state APPL_STATE_RELEASE
 *         1 - Application is released from CAPI application
 *         2 - the controller is resetted
 *         3 - the controller is removed
 *         4 - the CAPI module will be unload
 */
int
ApplicationDestr(Application_t *appl, int who)
{
	int		i, used = 0;
	AppPlci_t	**aplci_p = appl->AppPlcis;

	if (test_and_set_bit(APPL_STATE_DESTRUCTOR, &appl->state)) {
		// we are allready in this function
		return(-EBUSY);
	}
	test_and_set_bit(APPL_STATE_RELEASE, &appl->state);
	test_and_clear_bit(APPL_STATE_ACTIV, &appl->state);
	listenDestr(appl);
	if (who > 2) {
		appl->contr = NULL;
	}
	if (aplci_p) {
		for (i = 0; i < appl->maxplci; i++) {
			if (*aplci_p) {
				switch (who) {
					case 4:
						AppPlciDestr(*aplci_p);
						*aplci_p = NULL;
						break;
					case 1:
					case 2:
					case 3:
						AppPlciRelease(*aplci_p);
					case 0:
						if ((volatile AppPlci_t *)(*aplci_p))
							used++;
						break;
				}
			}
			aplci_p++;
		}
	}
	if (used) {
		if (who == 3) {
			list_del_init(&appl->head);
			list_add(&appl->head, &garbage_applications);
		}
		test_and_clear_bit(APPL_STATE_DESTRUCTOR, &appl->state);
		return(-EBUSY);
	}
	list_del_init(&appl->head);
	appl->maxplci = 0;
	kfree(appl->AppPlcis);
	appl->AppPlcis = NULL;
	kfree(appl);
	return(0);
}

AppPlci_t *
getAppPlci4addr(Application_t *appl, __u32 addr)
{
	int plci_idx = (addr >> 8) & 0xff;

	if ((plci_idx < 1) || (plci_idx >= appl->maxplci)) {
		int_error();
		return NULL;
	}
	return(appl->AppPlcis[plci_idx - 1]);
}

static void
FacilityReq(Application_t *appl, struct sk_buff *skb)
{
	_cmsg		*cmsg;
	AppPlci_t	*aplci;
	Ncci_t		*ncci;

	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		dev_kfree_skb(skb);
		return;
	}
	capi_message2cmsg(cmsg, skb->data);
	switch (cmsg->FacilitySelector) {
		case 0x0000: // Handset
		case 0x0001: // DTMF
			aplci = getAppPlci4addr(appl, CAPIMSG_CONTROL(skb->data));
			if (aplci) {
				ncci = getNCCI4addr(aplci, CAPIMSG_NCCI(skb->data), GET_NCCI_PLCI);
				if (ncci) {
					ncciGetCmsg(ncci, cmsg);
					break;
				}
			}
			SendCmsgAnswer2Application(appl, cmsg, CapiIllContrPlciNcci);
			break;
		case 0x0003: // SupplementaryServices
			SupplementaryFacilityReq(appl, cmsg);
			break;
		default:
			int_error();
			SendCmsgAnswer2Application(appl, cmsg, CapiFacilityNotSupported);
			break;
	}
	dev_kfree_skb(skb);
}

__u16
ApplicationSendMessage(Application_t *appl, struct sk_buff *skb)
{
	Plci_t		*plci;
	AppPlci_t	*aplci;
	Ncci_t		*ncci;
	__u16		ret = CAPI_NOERROR;

	switch (CAPICMD(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data))) {
		// for NCCI state machine
		case CAPI_DATA_B3_REQ:
		case CAPI_DATA_B3_RESP:
		case CAPI_CONNECT_B3_RESP:
		case CAPI_CONNECT_B3_ACTIVE_RESP:
		case CAPI_DISCONNECT_B3_REQ:
		case CAPI_DISCONNECT_B3_RESP:
			aplci = getAppPlci4addr(appl, CAPIMSG_CONTROL(skb->data));
			if (!aplci) {
				AnswerMessage2Application(appl, skb, CapiIllContrPlciNcci);
				goto free;
			}
			ncci = getNCCI4addr(aplci, CAPIMSG_NCCI(skb->data), GET_NCCI_EXACT);
			if (!ncci) {
				int_error();
				AnswerMessage2Application(appl, skb, CapiIllContrPlciNcci);
				goto free;
			}
			ret = ncciSendMessage(ncci, skb);
			break;
		// new NCCI
		case CAPI_CONNECT_B3_REQ:
			aplci = getAppPlci4addr(appl, CAPIMSG_CONTROL(skb->data));
			if (!aplci) {
				AnswerMessage2Application(appl, skb, CapiIllContrPlciNcci);
				goto free;
			}
			ConnectB3Request(aplci, skb);
			break;
		// for PLCI state machine
		case CAPI_INFO_REQ:
		case CAPI_ALERT_REQ:
		case CAPI_CONNECT_RESP:
		case CAPI_CONNECT_ACTIVE_RESP:
		case CAPI_DISCONNECT_REQ:
		case CAPI_DISCONNECT_RESP:
		case CAPI_SELECT_B_PROTOCOL_REQ:
			aplci = getAppPlci4addr(appl, CAPIMSG_CONTROL(skb->data));
			if (!aplci) {
				AnswerMessage2Application(appl, skb, CapiIllContrPlciNcci);
				goto free;
			}
			ret = AppPlciSendMessage(aplci, skb);
			break;
		case CAPI_CONNECT_REQ:
			if (ControllerNewPlci(appl->contr, &plci, MISDN_ID_ANY)) {
				AnswerMessage2Application(appl, skb, CapiNoPlciAvailable);
				goto free;
			}
			aplci = ApplicationNewAppPlci(appl, plci);
			if (!aplci) {
				AnswerMessage2Application(appl, skb, CapiNoPlciAvailable);
				goto free;
			}
			ret = AppPlciSendMessage(aplci, skb);
			break;

		// for LISTEN state machine
		case CAPI_LISTEN_REQ:
			ret = listenSendMessage(appl, skb);
			break;

		// other
		case CAPI_FACILITY_REQ:
			FacilityReq(appl, skb);
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
			ret = CAPI_ILLCMDORSUBCMDORMSGTOSMALL;
			break;
	}
	return(ret);
 free:
	dev_kfree_skb(skb);
	return(ret);
}

AppPlci_t *
ApplicationNewAppPlci(Application_t *appl, Plci_t *plci)
{
	AppPlci_t	*aplci;
	int		plci_idx = (plci->addr >> 8) & 0xff;

	if (test_bit(APPL_STATE_RELEASE, &appl->state))
		return(NULL);
	if ((plci_idx < 1) || (plci_idx >= appl->maxplci)) {
		int_error();
		return(NULL);
	}
	if (appl->AppPlcis[plci_idx - 1]) {
		int_error();
		return(NULL);
	}
	if (AppPlciConstr(&aplci, appl, plci)) {
		int_error();
		return(NULL);
	}
	applDebug(appl, CAPI_DBG_APPL_INFO, "ApplicationNewAppPlci: idx(%d) aplci(%p) appl(%p) plci(%p)",
		plci_idx, aplci, appl, plci);
	appl->AppPlcis[plci_idx - 1] = aplci;
	plciAttachAppPlci(plci, aplci);
	return(aplci);
}

void
ApplicationDelAppPlci(Application_t *appl, AppPlci_t *aplci)
{
	int	plci_idx = (aplci->addr >> 8) & 0xff;

	if ((plci_idx < 1) || (plci_idx >= appl->maxplci)) {
		int_error();
		return;
	}
	if (appl->AppPlcis[plci_idx - 1] != aplci) {
		int_error();
		return;
	}
	appl->AppPlcis[plci_idx - 1] = NULL;
	if (test_bit(APPL_STATE_RELEASE, &appl->state) &&
		!test_bit(APPL_STATE_DESTRUCTOR, &appl->state))
		ApplicationDestr(appl, 0);
}

void
SendCmsg2Application(Application_t *appl, _cmsg *cmsg)
{
	struct sk_buff	*skb;
	
	if (test_bit(APPL_STATE_RELEASE, &appl->state)) {
		/* Application is released and cannot receive messages
		 * anymore. To avoid stalls in the state machines we
		 * must answer INDICATIONS.
		 */
		AppPlci_t	*aplci;
		Ncci_t		*ncci;

		if (CAPI_IND != cmsg->Subcommand)
			goto free;
		switch(cmsg->Command) {
			// for NCCI state machine
			case CAPI_CONNECT_B3:
				cmsg->Reject = 2;
			case CAPI_CONNECT_B3_ACTIVE:
			case CAPI_DISCONNECT_B3:
				aplci = getAppPlci4addr(appl, (cmsg->adr.adrNCCI & 0xffff));
				if (!aplci)
					goto free;
				ncci = getNCCI4addr(aplci, cmsg->adr.adrNCCI, GET_NCCI_EXACT); 
				if (!ncci) {
					int_error();
					goto free;
				}
				capi_cmsg_answer(cmsg);
				ncciGetCmsg(ncci, cmsg);
				break;
			// for PLCI state machine
			case CAPI_CONNECT:
				cmsg->Reject = 2;
			case CAPI_CONNECT_ACTIVE:
			case CAPI_DISCONNECT:
				aplci = getAppPlci4addr(appl, (cmsg->adr.adrPLCI & 0xffff));
				if (!aplci)
					goto free;
				capi_cmsg_answer(cmsg);
				AppPlciGetCmsg(aplci, cmsg);
				break;
			case CAPI_FACILITY:
			case CAPI_MANUFACTURER:
			case CAPI_INFO:
				goto free;
			default:
				int_error();
				goto free;
		}
		return;
	}
	if (!(skb = alloc_skb(CAPI_MSG_DEFAULT_LEN, GFP_ATOMIC))) {
		printk(KERN_WARNING "%s: no mem for %d bytes\n", __FUNCTION__, CAPI_MSG_DEFAULT_LEN);
		int_error();
		goto free;
	}
	capi_cmsg2message(cmsg, skb->data);
	applDebug(appl, CAPI_DBG_APPL_MSG, "%s: len(%d) applid(%x) %s msgnr(%d) addr(%08x)",
		__FUNCTION__, CAPIMSG_LEN(skb->data), cmsg->ApplId, capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		cmsg->Messagenumber, cmsg->adr.adrController);
	if (CAPI_MSG_DEFAULT_LEN < CAPIMSG_LEN(skb->data)) {
		printk(KERN_ERR "%s: CAPI_MSG_DEFAULT_LEN overrun (%d/%d)\n",  __FUNCTION__,
			CAPIMSG_LEN(skb->data), CAPI_MSG_DEFAULT_LEN);
		int_error();
		dev_kfree_skb(skb);
		goto free; 
	}
	skb_put(skb, CAPIMSG_LEN(skb->data));
#ifdef OLDCAPI_DRIVER_INTERFACE
	appl->contr->ctrl->handle_capimsg(appl->contr->ctrl, cmsg->ApplId, skb);
#else
	capi_ctr_handle_message(appl->contr->ctrl, cmsg->ApplId, skb);
#endif
free:
	cmsg_free(cmsg);
}

void
SendCmsgAnswer2Application(Application_t *appl, _cmsg *cmsg, __u16 Info)
{
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	SendCmsg2Application(appl, cmsg);
}

void
AnswerMessage2Application(Application_t *appl, struct sk_buff *skb, __u16 Info)
{
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	capi_message2cmsg(cmsg, skb->data);
	SendCmsgAnswer2Application(appl, cmsg, Info);
}

#define AVM_MANUFACTURER_ID	0x214D5641 /* "AVM!" */
#define CLASS_AVM		0x00
#define FUNCTION_AVM_D2_TRACE	0x01

struct AVMD2Trace {
	__u8 Length;
	__u8 data[4];
};

void applManufacturerReqAVM(Application_t *appl, _cmsg *cmsg, struct sk_buff *skb)
{
	struct AVMD2Trace *at;

	if (cmsg->Class != CLASS_AVM) {
		applDebug(appl, CAPI_DBG_APPL_INFO, "CAPI: unknown class %#x\n", cmsg->Class);
		cmsg_free(cmsg);
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
				test_and_set_bit(APPL_STATE_D2TRACE, &appl->state);
			} else if (memcmp(at->data, "\000\000\000\000", 4) == 0) {
				test_and_clear_bit(APPL_STATE_D2TRACE, &appl->state);
			} else {
				int_error();
			}
			break;
		default:
			applDebug(appl, CAPI_DBG_APPL_INFO, "CAPI: unknown function %#x\n", cmsg->Function);
	}
	cmsg_free(cmsg);
	dev_kfree_skb(skb);
}

void applManufacturerReqmISDN(Application_t *appl, _cmsg *cmsg, struct sk_buff *skb)
{
	AppPlci_t	*aplci;
	Ncci_t		*ncci;

	switch (cmsg->Class) {
		case mISDN_MF_CLASS_HANDSET:
			/* Note normally MANUFATURER messages are only defined for
			 * controller address we extent it here to PLCI/NCCI
			 */
			aplci = getAppPlci4addr(appl, CAPIMSG_CONTROL(skb->data));
			if (aplci) {
				ncci = getNCCI4addr(aplci, CAPIMSG_NCCI(skb->data), GET_NCCI_PLCI);
				if (ncci) {
					cmsg_free(cmsg);
					ncciSendMessage(ncci, skb);
					return;
				}
			}
			SendCmsgAnswer2Application(appl, cmsg, CapiIllContrPlciNcci);
			break;
		default:
			cmsg_free(cmsg);
			break;
	}
	dev_kfree_skb(skb);
}

void
applManufacturerReq(Application_t *appl, struct sk_buff *skb)
{
	_cmsg	*cmsg;

	if (skb->len < 16 + 8) {
		dev_kfree_skb(skb);
		return;
	}
	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		dev_kfree_skb(skb);
		return;
	}
	capi_message2cmsg(cmsg, skb->data);
	switch (cmsg->ManuID) {
		case mISDN_MANUFACTURER_ID:
			applManufacturerReqmISDN(appl, cmsg, skb);
			break;
		case AVM_MANUFACTURER_ID:
			applManufacturerReqAVM(appl, cmsg, skb);
			break;
		default:
			applDebug(appl, CAPI_DBG_APPL_INFO, "CAPI: unknown ManuID %#x\n", cmsg->ManuID);
			cmsg_free(cmsg);
			dev_kfree_skb(skb);
			break;
	}
}

void applD2Trace(Application_t *appl, u_char *buf, int len)
{
	_cmsg	*cmsg;
	__u8	manuData[255];

	if (!test_bit(APPL_STATE_D2TRACE, &appl->state))
		return;
	
	CMSG_ALLOC(cmsg);
	capi_cmsg_header(cmsg, appl->ApplId, CAPI_MANUFACTURER, CAPI_IND, 
			 appl->MsgId++, appl->contr->addr);
	cmsg->ManuID = AVM_MANUFACTURER_ID;
	cmsg->Class = CLASS_AVM;
	cmsg->Function = FUNCTION_AVM_D2_TRACE;
	cmsg->ManuData = (_cstruct) &manuData;
	manuData[0] = 2 + len; // length
	manuData[1] = 0x80;
	manuData[2] = 0x0f;
	memcpy(&manuData[3], buf, len);
	
	SendCmsg2Application(appl, cmsg);
}

void
free_Application(void)
{
	struct list_head	*item, *next;
	int			n = 0;

	if (list_empty(&garbage_applications)) {
		printk(KERN_DEBUG "%s: no garbage\n", __FUNCTION__);
		return;
	}
	list_for_each_safe(item, next, &garbage_applications) {
		ApplicationDestr((Application_t *)item, 4);
		n++;
	}
	printk(KERN_WARNING"%s: %d garbage items\n", __FUNCTION__, n);
}
