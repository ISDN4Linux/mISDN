/* $Id: supp_serv.c,v 1.8 2004/01/26 22:21:30 keil Exp $
 *
 */

#include "m_capi.h"
#include "asn1_comp.h"
#include "asn1_enc.h"
#include "dss1.h"
#include "helper.h"

#define T_ACTIVATE    4000
#define T_DEACTIVATE  4000
#define T_INTERROGATE 4000

static void	SSProcessAddTimer(SSProcess_t *, int);

static __u8 *
encodeInvokeComponentHead(__u8 *p)
{
	*p++ = 0;     // length -- not known yet
	*p++ = 0x91;  // remote operations protocol
	*p++ = 0xa1;  // invoke component
	*p++ = 0;     // length -- not known yet
	return p;
}

static void
encodeInvokeComponentLength(__u8 *msg, __u8 *p)
{
	msg[3] = p - &msg[5];
	msg[0] = p - &msg[1];
}


static int
SSProcess_L4L3(SSProcess_t *spc, __u32 prim, struct sk_buff *skb) {
	int	err;

// FIXME	err = ControllerL4L3(contr, prim, contr->addr | DUMMY_CR_FLAG, skb);
	err = ControllerL4L3(spc->contr, prim, MISDN_ID_DUMMY, skb);
	if (err)
		dev_kfree_skb(skb);
	return(err);
}

static __inline__ int capiGetWord(__u8 *p, __u8 *end, __u16 *word)
{
	if (p + 2 > end) {
		return -1;
	}
	*word = *p + (*(p+1) << 8);

	return 2;
}

static __inline__ int capiGetDWord(__u8 *p, __u8 *end, __u32 *dword)
{
	if (p + 4 > end) {
		return -1;
	}
	*dword = *p + (*(p+1) << 8) + (*(p+2) << 16) + (*(p+3) << 24);

	return 4;
}

static __inline__ int capiGetStruct(__u8 *p, __u8 *_end, __u8 **strct)
{
	int len = *p++;
	__u8 *end = p + len;

	if (end > _end) return -1;

	*strct = p - 1;

	return len + 1;
}

#define CAPI_GET(func, parm) \
	ret = func(p, end, parm); \
	if (ret < 0) return -1; \
	p += ret

static __inline__ int capiGetFacReqListen(__u8 *p, __u8 *_end, struct FacReqListen *listen)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &listen->NotificationMask);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqSuspend(__u8 *p, __u8 *_end, struct FacReqSuspend *suspend)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetStruct, &suspend->CallIdentity);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqResume(__u8 *p, __u8 *_end, struct FacReqResume *resume)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetStruct, &resume->CallIdentity);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqCFActivate(__u8 *p, __u8 *_end, struct FacReqCFActivate *CFActivate)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &CFActivate->Handle);
	CAPI_GET(capiGetWord, &CFActivate->Procedure);
	CAPI_GET(capiGetWord, &CFActivate->BasicService);
	CAPI_GET(capiGetStruct, &CFActivate->ServedUserNumber);
	CAPI_GET(capiGetStruct, &CFActivate->ForwardedToNumber);
	CAPI_GET(capiGetStruct, &CFActivate->ForwardedToSubaddress);

	if (p != end) return -1;
	return len + 1;
}

static __inline__ int capiGetFacReqCFDeactivate(__u8 *p, __u8 *_end, struct FacReqCFDeactivate *CFDeactivate)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &CFDeactivate->Handle);
	CAPI_GET(capiGetWord, &CFDeactivate->Procedure);
	CAPI_GET(capiGetWord, &CFDeactivate->BasicService);
	CAPI_GET(capiGetStruct, &CFDeactivate->ServedUserNumber);

	if (p != end) return -1;
	return len + 1;
}

#define capiGetFacReqCFInterrogateParameters capiGetFacReqCFDeactivate

static __inline__ int capiGetFacReqCFInterrogateNumbers(__u8 *p, __u8 *_end, struct FacReqCFInterrogateNumbers *CFInterrogateNumbers)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	if (end > _end) return -1;

	CAPI_GET(capiGetDWord, &CFInterrogateNumbers->Handle);

	if (p != end)
		return(-1);
	return(len + 1);
}


static __inline__ int capiGetFacReqParm(__u8 *p, struct FacReqParm *facReqParm)
{
	int len = *p++;
	int ret;
	__u8 *end = p + len;

	CAPI_GET(capiGetWord, &facReqParm->Function);

	switch (facReqParm->Function) {
		case 0x0000: // GetSupportedServices
			if (*p++ != 0x00) return -1; // empty struct
			break;
		case 0x0001: // Listen
			CAPI_GET(capiGetFacReqListen, &facReqParm->u.Listen);
			break;
		case 0x0004: // Suspend
			CAPI_GET(capiGetFacReqSuspend, &facReqParm->u.Suspend);
			break;
		case 0x0005: // Resume
			CAPI_GET(capiGetFacReqResume, &facReqParm->u.Resume);
			break;
		case 0x0009: // CF Activate
			CAPI_GET(capiGetFacReqCFActivate, &facReqParm->u.CFActivate);
			break;
		case 0x000a: // CF Deactivate
			CAPI_GET(capiGetFacReqCFDeactivate, &facReqParm->u.CFDeactivate);
			break;
		case 0x000b: // CF Interrogate Parameters
			CAPI_GET(capiGetFacReqCFInterrogateParameters, &facReqParm->u.CFInterrogateParameters);
			break;
		case 0x000c: // CF Interrogate Numbers
			CAPI_GET(capiGetFacReqCFInterrogateNumbers, &facReqParm->u.CFInterrogateNumbers);
			break;
		default:
			return(len + 1);
	}

	if (p != end)
		return(-1);
	return(len + 1);
}

static int
GetSupportedServices(FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	facConfParm->u.GetSupportedServices.SupplementaryServiceInfo = CapiSuccess;
	facConfParm->u.GetSupportedServices.SupportedServices = mISDNSupportedServices;
	return CapiSuccess;
}
static int
FacListen(Application_t *appl, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	if (facReqParm->u.Listen.NotificationMask &~ mISDNSupportedServices) {
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSupplementaryServiceNotSupported;
	} else {
		appl->NotificationMask = facReqParm->u.Listen.NotificationMask;
		facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	}
	return CapiSuccess;
}

static int
FacCFInterrogateParameters(Application_t *appl, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	SSProcess_t	*sspc;
	struct sk_buff	*skb = mISDN_alloc_l3msg(260, MT_FACILITY);
	__u8		*p;

	if (!skb)
		return CAPI_MSGOSRESOURCEERR;
	sspc = SSProcessConstr(appl, facReqParm->Function, 
				  facReqParm->u.CFInterrogateParameters.Handle);
	if (!sspc) {
		kfree_skb(skb);
		return CAPI_MSGOSRESOURCEERR;
	}

	p = encodeInvokeComponentHead(sspc->buf);
	p += encodeInt(p, sspc->invokeId);
	p += encodeInt(p, 0x0b); // interrogationDiversion
	p += encodeInterrogationDiversion(p,  &facReqParm->u.CFInterrogateParameters);
	encodeInvokeComponentLength(sspc->buf, p);
	mISDN_AddIE(skb, IE_FACILITY, sspc->buf);

	SSProcess_L4L3(sspc, CC_FACILITY | REQUEST, skb);
	SSProcessAddTimer(sspc, T_INTERROGATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

static int
FacCFInterrogateNumbers(Application_t *appl, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	SSProcess_t	*sspc;
	struct sk_buff	*skb = mISDN_alloc_l3msg(260, MT_FACILITY);
	__u8		*p;

	if (!skb)
		return CAPI_MSGOSRESOURCEERR;
	sspc = SSProcessConstr(appl, facReqParm->Function, 
				  facReqParm->u.CFInterrogateNumbers.Handle);
	if (!sspc) {
		kfree_skb(skb);
		return CAPI_MSGOSRESOURCEERR;
	}

	p = encodeInvokeComponentHead(sspc->buf);
	p += encodeInt(p, sspc->invokeId);
	p += encodeInt(p, 0x11); // InterrogateServedUserNumbers
	encodeInvokeComponentLength(sspc->buf, p);
	mISDN_AddIE(skb, IE_FACILITY, sspc->buf);
	SSProcess_L4L3(sspc, CC_FACILITY | REQUEST, skb);
	SSProcessAddTimer(sspc, T_INTERROGATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

static int
FacCFActivate(Application_t *appl, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	SSProcess_t	*sspc;
	struct sk_buff	*skb = mISDN_alloc_l3msg(260, MT_FACILITY);
	__u8		*p;

	if (!skb)
		return CAPI_MSGOSRESOURCEERR;
	sspc = SSProcessConstr(appl, facReqParm->Function, facReqParm->u.CFActivate.Handle);
	if (!sspc) {
		kfree_skb(skb);
		return CAPI_MSGOSRESOURCEERR;
	}
	p = encodeInvokeComponentHead(sspc->buf);
	p += encodeInt(p, sspc->invokeId);
	p += encodeInt(p, 0x07); // activationDiversion
	p += encodeActivationDiversion(p, &facReqParm->u.CFActivate);
	encodeInvokeComponentLength(sspc->buf, p);
	mISDN_AddIE(skb, IE_FACILITY, sspc->buf);
	SSProcess_L4L3(sspc, CC_FACILITY | REQUEST, skb);
	SSProcessAddTimer(sspc, T_ACTIVATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}

static int
FacCFDeactivate(Application_t *appl, FacReqParm_t *facReqParm, FacConfParm_t *facConfParm)
{
	SSProcess_t	*sspc;
	struct sk_buff	*skb = mISDN_alloc_l3msg(260, MT_FACILITY);
	__u8		*p;

	if (!skb)
		return CAPI_MSGOSRESOURCEERR;
	sspc = SSProcessConstr(appl, facReqParm->Function, facReqParm->u.CFDeactivate.Handle);
	if (!sspc) {
		kfree_skb(skb);
		return CAPI_MSGOSRESOURCEERR;
	}
	p = encodeInvokeComponentHead(sspc->buf);
	p += encodeInt(p, sspc->invokeId);
	p += encodeInt(p, 0x08); // dectivationDiversion
	p += encodeDeactivationDiversion(p, &facReqParm->u.CFDeactivate);
	encodeInvokeComponentLength(sspc->buf, p);
	mISDN_AddIE(skb, IE_FACILITY, sspc->buf);

	SSProcess_L4L3(sspc, CC_FACILITY | REQUEST, skb);
	SSProcessAddTimer(sspc, T_DEACTIVATE);

	facConfParm->u.Info.SupplementaryServiceInfo = CapiSuccess;
	return CapiSuccess;
}
 
void
SupplementaryFacilityReq(Application_t *appl, _cmsg *cmsg)
{
	__u8		tmp[10];
	__u16		Info;
	FacReqParm_t	facReqParm;
	FacConfParm_t	facConfParm;
	Plci_t		*plci;
	AppPlci_t	*aplci;
	int		ret;

	if (capiGetFacReqParm(cmsg->FacilityRequestParameter, &facReqParm) < 0) {
		SendCmsgAnswer2Application(appl, cmsg, CapiIllMessageParmCoding);
		return;
	}
	facConfParm.Function = facReqParm.Function;
	switch (facReqParm.Function) {
		case 0x0000: // GetSupportedServices
			Info = GetSupportedServices(&facReqParm, &facConfParm);
			break;
		case 0x0001: // Listen
			Info = FacListen(appl, &facReqParm, &facConfParm);
			break;
		case 0x0004: // Suspend
			aplci = getAppPlci4addr(appl, cmsg->adr.adrPLCI);
			if (!aplci) {
				Info = CapiIllContrPlciNcci;
				break;
			}
			Info = AppPlciFacSuspendReq(aplci, &facReqParm, &facConfParm);
			break;
		case 0x0005: // Resume
			ret = ControllerNewPlci(appl->contr, &plci, MISDN_ID_ANY);
			if (ret) {
				Info = CapiNoPlciAvailable;
				break;
			}
			aplci = ApplicationNewAppPlci(appl, plci);
			if (!aplci) {
				ControllerReleasePlci(plci);
				Info = CapiNoPlciAvailable;
				break;
			}
			Info = AppPlciFacResumeReq(aplci, &facReqParm, &facConfParm);
			if (Info == CapiSuccess)
				cmsg->adr.adrPLCI = plci->addr;
			break;
		case 0x0009: // CF Activate
			Info = FacCFActivate(appl, &facReqParm, &facConfParm);
			break;
		case 0x000a: // CF Deactivate
			Info = FacCFDeactivate(appl, &facReqParm, &facConfParm);
			break;
		case 0x000b: // CF Interrogate Parameters
			Info = FacCFInterrogateParameters(appl, &facReqParm, &facConfParm);
			break;
		case 0x000c: // CF Interrogate Numbers
			Info = FacCFInterrogateNumbers(appl, &facReqParm, &facConfParm);
			break;
		default:
			Info = CapiSuccess;
			facConfParm.u.Info.SupplementaryServiceInfo = CapiSupplementaryServiceNotSupported;
	}

	if (Info == 0x0000)
		capiEncodeFacConfParm(tmp, &facConfParm);
	else
		tmp[0] = 0;
	cmsg->FacilityConfirmationParameter = tmp;
	SendCmsgAnswer2Application(appl, cmsg, Info);
}

SSProcess_t *
SSProcessConstr(Application_t *appl, __u16 Function, __u32 Handle)
{
	SSProcess_t	*sspc;

	sspc = SSProcess_alloc();
	if (!sspc)
		return(NULL);
	memset(sspc, 0, sizeof(SSProcess_t));
	sspc->ApplId = appl->ApplId;
	sspc->Function = Function;
	sspc->Handle = Handle;
	ControllerAddSSProcess(appl->contr, sspc);
	return(sspc);
}

void SSProcessDestr(SSProcess_t *sspc)
{
	del_timer(&sspc->tl);
	list_del_init(&sspc->head);
	SSProcess_free(sspc);
}

static void
SendSSFacilityInd(Application_t *appl, __u32 addr, __u8 *para)
{
	_cmsg	*cmsg;

	CMSG_ALLOC(cmsg);
	capi_cmsg_header(cmsg, appl->ApplId, CAPI_FACILITY, CAPI_IND, appl->MsgId++, addr);
	cmsg->FacilitySelector = 0x0003;
	cmsg->FacilityIndicationParameter = para;
	SendCmsg2Application(appl, cmsg);
}

static void
SendSSFacilityInd2All(Controller_t *contr, __u32 nMask, __u8 *para)
{
	struct list_head	*item;
	Application_t		*appl;

	list_for_each(item, &contr->Applications) {
		appl = (Application_t *)item;
		if (test_bit(APPL_STATE_RELEASE, &appl->state))
			continue;
		if (!(appl->NotificationMask & nMask))
			continue;
		SendSSFacilityInd(appl, contr->addr, para);
	}
}

void
SSProcessTimeout(unsigned long arg)
{
	SSProcess_t	*sspc = (SSProcess_t *) arg;
	Application_t	*appl;
	__u8		tmp[10], *p;

	appl = getApplication4Id(sspc->contr, sspc->ApplId);
	if (!appl) {
		SSProcessDestr(sspc);
		return;
	}
	p = &tmp[1];
	p += capiEncodeWord(p, sspc->Function);
	p += capiEncodeFacIndCFact(p, CapiTimeOut, sspc->Handle);
	tmp[0] = p - &tmp[1];
	SendSSFacilityInd(appl, sspc->addr, tmp);
	SSProcessDestr(sspc);
}

void SSProcessAddTimer(SSProcess_t *sspc, int msec)
{
	sspc->tl.function = SSProcessTimeout;
	sspc->tl.data = (unsigned long) sspc;
	init_timer(&sspc->tl);
	sspc->tl.expires = jiffies + (msec * HZ) / 1000;
	add_timer(&sspc->tl);
}


#if 0
void printPublicPartyNumber(struct PublicPartyNumber *publicPartyNumber)
{
	printk("(%d) %s\n", publicPartyNumber->publicTypeOfNumber, 
	       publicPartyNumber->numberDigits);
}

void printPartyNumber(struct PartyNumber *partyNumber)
{
	switch (partyNumber->type) {
	case 0: 
		printk("unknown %s\n", partyNumber->p.unknown);
		break;
	case 1:
		printPublicPartyNumber(&partyNumber->p.publicPartyNumber);
		break;
	}
}

void printServedUserNr(struct ServedUserNr *servedUserNr)
{
	if (servedUserNr->all) {
		printk("all\n");
	} else {
		printPartyNumber(&servedUserNr->partyNumber);
	}
}

void printAddress(struct Address *address)
{
	printPartyNumber(&address->partyNumber);
	if (address->partySubaddress[0]) {
		printk("sub %s\n", address->partySubaddress);
	}
}
#endif

static void 
SSProcessFacility(Controller_t *contr, Q931_info_t *qi)
{
	Application_t		*appl;
        int			ie_len;
	struct asn1_parm	parm;
	SSProcess_t		*sspc;
	__u8			tmp[256];
        __u8			*p, *end;

        if (!qi || !qi->facility) {
		int_error();
                return;
	}
	p = (__u8 *)qi;
	p += L3_EXTRA_SIZE + qi->facility;
	p++;
        ie_len = *p++;
        end = p + ie_len;
//        if (end > skb->data + skb->len) {
//                int_error();
//                return;
//        }

        if (*p++ != 0x91) { // Supplementary Service Applications
		int_error();
                return;
        }
	ParseComponent(&parm, p, end);
	switch (parm.comp) {
		case invoke:
#if 0
			printk("invokeId %d\n", parm.u.inv.invokeId);
			printk("operationValue %d\n", parm.u.inv.operationValue);
#endif
			switch (parm.u.inv.operationValue) {
				case 0x0009: 
#if 0
					printk("procedure %d basicService %d\n", parm.c.inv.o.actNot.procedure,
						parm.c.inv.o.actNot.basicService);
					printServedUserNr(&parm.c.inv.o.actNot.servedUserNr);
					printAddress(&parm.c.inv.o.actNot.address);
#endif
					p = &tmp[1];
					p += capiEncodeWord(p, 0x8006);
					p += capiEncodeFacIndCFNotAct(p, &parm.u.inv.o.actNot);
					tmp[0] = p - &tmp[1];
					SendSSFacilityInd2All(contr, SuppServiceCF, tmp);
					break;
				case 0x000a: 
#if 0
					printk("procedure %d basicService %d\n", parm.c.inv.o.deactNot.procedure,
						parm.c.inv.o.deactNot.basicService);
					printServedUserNr(&parm.c.inv.o.deactNot.servedUserNr);
#endif
					p = &tmp[1];
					p += capiEncodeWord(p, 0x8007);
					p += capiEncodeFacIndCFNotDeact(p, &parm.u.inv.o.deactNot);
					tmp[0] = p - &tmp[1];
					SendSSFacilityInd2All(contr, SuppServiceCF, tmp);
					break;
				default:
					int_error();
			}
			break;
		case returnResult:
			sspc = getSSProcess4Id(contr, parm.u.retResult.invokeId);
			if (!sspc)
				return;
			appl = getApplication4Id(contr, sspc->ApplId);
			if (!appl)
				return;
			p = &tmp[1];
			p += capiEncodeWord(p, sspc->Function);
			switch (sspc->Function) {
				case 0x0009:
					p += capiEncodeFacIndCFact(p, 0, sspc->Handle);
					break;
				case 0x000a:
					p += capiEncodeFacIndCFdeact(p, 0, sspc->Handle);
					break;
				case 0x000b:
					p += capiEncodeFacIndCFinterParameters(p, 0, sspc->Handle, 
						&parm.u.retResult.o.resultList);
					break;
				case 0x000c:
					p += capiEncodeFacIndCFinterNumbers(p, 0, sspc->Handle, 
						&parm.u.retResult.o.list);
					break;
				default:
					int_error();
					break;
			}
			tmp[0] = p - &tmp[1];
			SendSSFacilityInd(appl, sspc->addr, tmp);
			SSProcessDestr(sspc);
			break;
		case returnError:
			sspc = getSSProcess4Id(contr, parm.u.retResult.invokeId);
			if (!sspc)
				return;
			appl = getApplication4Id(contr, sspc->ApplId);
			if (!appl)
				return;
			p = &tmp[1];
			p += capiEncodeWord(p, sspc->Function);
			p += capiEncodeFacIndCFact(p, 0x3600 | (parm.u.retError.errorValue & 0xff), 
				sspc->Handle);
			tmp[0] = p - &tmp[1];
			SendSSFacilityInd(appl, sspc->addr, tmp);
			SSProcessDestr(sspc);
			break;
		default:
			int_error();
	}
}

int
Supplementary_l3l4(Controller_t *contr, __u32 prim, struct sk_buff *skb)
{
	int ret = -EINVAL;

	if (!skb)
		return(ret);
	switch (prim) {
		case CC_FACILITY | INDICATION:
			if (skb->len)
				SSProcessFacility(contr, (Q931_info_t *)skb->data);
			dev_kfree_skb(skb);
			ret = 0;
			break;
		default:
			int_error();
	}
	return(ret);
}
