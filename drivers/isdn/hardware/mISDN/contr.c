/* $Id: contr.c,v 0.3 2001/02/27 17:45:44 kkeil Exp $
 *
 */

#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

#define contrDebug(contr, lev, fmt, args...) \
	capidebug(lev, fmt, ## args)

//static void d2_listener(struct IsdnCardState *cs, u_char *buf, int len);

int contrConstr(Contr_t *contr, hisaxstack_t *st, hisaxif_t *hif, hisaxobject_t *ocapi)
{ 
	char tmp[10];
	int lay, err;
	hisaxstack_t *cst = st->child;
	BInst_t	*binst;

	memset(contr, 0, sizeof(Contr_t));
	contr->adrController = st->id;
	contr->inst.obj = ocapi;
	APPEND_TO_LIST(contr, ocapi->ilist);
	while(cst) {
		if (!(binst = kmalloc(sizeof(BInst_t), GFP_KERNEL))) {
			printk(KERN_ERR "no mem for Binst\n");
			int_error();
			return -ENOMEM;
		}
		memset(binst, 0, sizeof(BInst_t));
		binst->inst.st = cst;
		binst->inst.data = binst;
		binst->inst.obj = ocapi;
		binst->inst.layer = 4;
		binst->inst.down.stat = IF_NOACTIV;
		binst->inst.down.protocol = ISDN_PID_NONE;
		binst->inst.down.layer = 3;
		APPEND_TO_LIST(binst, contr->binst);
		cst = cst->next;
	}
	sprintf(tmp, "HiSax%d", contr->adrController);
	contr->ctrl = cdrv_if->attach_ctr(&hisax_driver, tmp, contr);
	if (!contr->ctrl)
		return -ENODEV;
	contr->inst.protocol = hif->protocol;
	contr->inst.layer = hif->layer;
	contr->inst.data = contr;
	ocapi->ctrl(st, MGR_ADDLAYER | INDICATION, &contr->inst);
	contr->inst.up.protocol = ISDN_PID_NONE;
	contr->inst.up.layer = 0;
	contr->inst.up.stat = IF_DOWN;
	lay = contr->inst.layer - 1;
	if ((lay<0) || (lay>MAX_LAYER)) {
		lay = 0;
		contr->inst.down.protocol = ISDN_PID_NONE;
	} else
		contr->inst.down.protocol = st->protocols[lay];
	contr->inst.down.layer = lay;
	contr->inst.down.stat = IF_UP;
	err = ocapi->ctrl(st, MGR_ADDIF | REQUEST, &contr->inst.down);
	if (err) {
		printk(KERN_ERR "contrConstr down interface request failed %d\n", err);
		return(-EIO);
	}
	return 0;
}

void contrDestr(Contr_t *contr)
{
	int i;
	Contr_t *base = contr->inst.obj->ilist;

	for (i = 0; i < CAPI_MAXAPPL; i++) {
		if (contr->appls[i]) {
			applDestr(contr->appls[i]);
			kfree(contr->appls[i]);
			contr->appls[i] = NULL;
		}
	}
	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (contr->plcis[i]) {
			plciDestr(contr->plcis[i]);
			kfree(contr->plcis[i]);
			contr->plcis[i] = NULL;
		}
	}
	for (i = 0; i < CAPI_MAXDUMMYPCS; i++) {
		if (contr->dummy_pcs[i]) {
			dummyPcDestr(contr->dummy_pcs[i]);
			kfree(contr->dummy_pcs[i]);
			contr->dummy_pcs[i] = NULL;
		}
	}
	if (contr->ctrl)
		cdrv_if->detach_ctr(contr->ctrl);
	
	while (contr->binst) {
		BInst_t *binst = contr->binst;
		REMOVE_FROM_LISTBASE(binst, contr->binst);
		kfree(binst);
	}
	REMOVE_FROM_LISTBASE(contr, base);
	contr->inst.obj->ilist = base;
}

void contrRun(Contr_t *contr)
{
	struct capi_ctr *ctrl = contr->ctrl;


	strncpy(ctrl->manu, "ISDN4Linux, (C) Kai Germaschewski", CAPI_MANUFACTURER_LEN);
	strncpy(ctrl->serial, "0002", CAPI_SERIAL_LEN);
	ctrl->version.majorversion = 2;
	ctrl->version.minorversion = 0;
	ctrl->version.majormanuversion = 1;
	ctrl->version.minormanuversion = 1;
	memset(&ctrl->profile, 0, sizeof(struct capi_profile));
	ctrl->profile.ncontroller = 1;
	ctrl->profile.nbchannel = 2;
	ctrl->profile.goptions = 0x11; // internal controller, supplementary services
	ctrl->profile.support1 = 3; // HDLC, TRANS
	ctrl->profile.support2 = 3; // X75SLP, TRANS
	ctrl->profile.support3 = 1; // TRANS
	ctrl->ready(ctrl);
}

Appl_t *contrId2appl(Contr_t *contr, __u16 ApplId)
{
	if ((ApplId < 1) || (ApplId > CAPI_MAXAPPL)) {
		int_error();
		return 0;
	}
	return contr->appls[ApplId - 1];
}

Plci_t *contrAdr2plci(Contr_t *contr, __u32 adr)
{
	int i = (adr >> 8);

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return 0;
	}
	return contr->plcis[i - 1];
}

void contrRegisterAppl(Contr_t *contr, __u16 ApplId, capi_register_params *rp)
{ 
	Appl_t *appl;

	appl = contrId2appl(contr, ApplId);
	if (appl) {
		int_error();
		return;
	}
	appl = kmalloc(sizeof(Appl_t), GFP_KERNEL);
	if (!appl) {
		int_error();
		return;
	}
	contr->appls[ApplId - 1] = appl;
	applConstr(appl, contr, ApplId, rp);
	contr->ctrl->appl_registered(contr->ctrl, ApplId);
}

void contrReleaseAppl(Contr_t *contr, __u16 ApplId)
{ 
	Appl_t *appl;

	appl = contrId2appl(contr, ApplId);
	if (!appl) {
		int_error();
		return;
	}
	applDestr(appl);
	kfree(appl);
	contr->appls[ApplId - 1] = NULL;
	contr->ctrl->appl_released(contr->ctrl, ApplId);
}

void contrSendMessage(Contr_t *contr, struct sk_buff *skb)
{ 
	Appl_t *appl;
	int ApplId;

	ApplId = CAPIMSG_APPID(skb->data);
	appl = contrId2appl(contr, ApplId);
	if (!appl) {
		int_error();
		return;
	}
	applSendMessage(appl, skb);
}

void contrLoadFirmware(Contr_t *contr, int len, void *data)
{
	l3msg_t fmsg;
	
	fmsg.id = len;
	fmsg.arg = data;
	contr->inst.obj->ctrl(contr->inst.st, MGR_LOADFIRM | REQUEST, &fmsg);	
	contrRun(contr);
}

void contrReset(Contr_t *contr)
{
	int ApplId;
	Appl_t *appl;

	for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
		appl = contrId2appl(contr, ApplId);
		if (appl)
			applDestr(appl);
		kfree(appl);
		contr->appls[ApplId - 1] = NULL;
	}

	contr->ctrl->reseted(contr->ctrl);
}

void contrD2Trace(Contr_t *contr, u_char *buf, int len)
{
	Appl_t *appl;
	__u16 applId;

	for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
		appl = contrId2appl(contr, applId);
		if (appl) {
			applD2Trace(appl, buf, len);
		}
	}
}

void contrRecvCmsg(Contr_t *contr, _cmsg *cmsg)
{
	struct sk_buff *skb;
	int len;
	
	capi_cmsg2message(cmsg, contr->msgbuf);
	len = CAPIMSG_LEN(contr->msgbuf);
	if (!(skb = alloc_skb(len, GFP_ATOMIC))) {
		int_error();
		return;
	}
	
	memcpy(skb_put(skb, len), contr->msgbuf, len);
	contr->ctrl->handle_capimsg(contr->ctrl, cmsg->ApplId, skb);
}

void contrAnswerCmsg(Contr_t *contr, _cmsg *cmsg, __u16 Info)
{
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	contrRecvCmsg(contr, cmsg);
}

void contrAnswerMessage(Contr_t *contr, struct sk_buff *skb, __u16 Info)
{
	_cmsg cmsg;
	capi_message2cmsg(&cmsg, skb->data);
	contrAnswerCmsg(contr, &cmsg, Info);
}

Plci_t *contrNewPlci(Contr_t *contr)
{
	Plci_t *plci;
	int i;

	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (!contr->plcis[i])
			break;
	}
	if (i == CAPI_MAXPLCI) {
		return 0;
	}
	plci = kmalloc(sizeof(Plci_t), GFP_ATOMIC);
	if (!plci) {
		int_error();
		return 0;
	}
	contr->plcis[i] = plci;
	plciConstr(plci, contr, (i+1) << 8 | contr->adrController);
	return plci;
}

void contrDelPlci(Contr_t *contr, Plci_t *plci)
{
	int i = plci->adrPLCI >> 8;

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return;
	}
	if (contr->plcis[i-1] != plci) {
		int_error();
		return;
	}
	plciDestr(plci);
	kfree(plci);
	contr->plcis[i-1] = NULL;
}

static Plci_t
*contrGetPLCI4addr(Contr_t *contr, int addr)
{
	int i;

	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (!contr->plcis[i])
			continue;
		if (contr->plcis[i]->adrPLCI == addr)
			return(contr->plcis[i]);
	}
	return(NULL);
}

int
contrL3L4(hisaxif_t *hif, u_int prim, u_int nr, int dtyp, void *arg)
{
	Contr_t	*contr;
	Plci_t	*plci;
	l3msg_t	*l3msg = arg;

	if (!hif || !hif->fdata)
		return(-EINVAL);
	contr = hif->fdata;
	if (!l3msg)
		return(-EINVAL);
	if (prim == (CC_NEW_CR | INDICATION)) {
		plci = contrNewPlci(contr);
		if (!plci)
			return(-EBUSY);
		l3msg->id = plci->adrPLCI;
	} else if ((l3msg->id & ~CONTROLER_MASK) == DUMMY_CR_FLAG) {
		contrDummyInd(contr, prim, l3msg->arg);
	} else {
		if (!(plci = contrGetPLCI4addr(contr, l3msg->id))) {
			contrDebug(contr, LL_DEB_WARN, __FUNCTION__ ": unknown plci prim(%x) id(%x)", prim, l3msg->id);
			return(-ENODEV);
		}
		plci_l3l4(plci, prim, l3msg->arg);
	}
	return(0);
}

int contrL4L3(Contr_t *contr, __u32 prim, l3msg_t *l3msg) {
	int err = -EINVAL;

	if (contr->inst.down.func) {
		err = contr->inst.down.func(&contr->inst.down, prim, 0,
			DTYPE_L3MSGP, l3msg);
	}
	return(err);
}

void contrPutStatus(Contr_t *contr, char *msg)
{
	printk(KERN_DEBUG "HiSax: %s", msg);
}

Contr_t *newContr(hisaxobject_t *ocapi, hisaxstack_t *st, hisaxif_t *hif)
{
	Contr_t *contr;

	if (!hif)
		return(NULL);
	printk(KERN_DEBUG "newContr prot %x\n", hif->protocol);
	if (!st) {
		printk(KERN_ERR "newContr no stack\n");
		return(NULL);
	}
	contr = kmalloc(sizeof(Contr_t), GFP_KERNEL);
	if (!contr)
		return(NULL);

	if (contrConstr(contr, st, hif, ocapi) != 0) {
		contrDestr(contr);
		kfree(contr);
		return(NULL);
	}
	return contr;
}

void delContr(Contr_t *contr)
{
	contrDestr(contr);
	kfree(contr);
}

BInst_t *contrSelChannel(Contr_t *contr, int channr)
{ 
	hisaxstack_t *cst;
	BInst_t	*binst;

	if (!contr->binst) {
		cst = contr->inst.st->child;
		if (!cst)
			return(NULL);
		while(cst) {
			if (!(binst = kmalloc(sizeof(BInst_t), GFP_KERNEL))) {
				printk(KERN_ERR "no mem for Binst\n");
				int_error();
				return(NULL);
			}
			memset(binst, 0, sizeof(BInst_t));
			binst->inst.st = cst;
			binst->inst.data = binst;
			binst->inst.obj = contr->inst.obj;
			binst->inst.layer = 4;
			binst->inst.down.stat = IF_NOACTIV;
			binst->inst.down.protocol = ISDN_PID_NONE;
			binst->inst.down.layer = 3;
			APPEND_TO_LIST(binst, contr->binst);
			cst = cst->next;
		}
	}
	binst = NULL;
	if (channr == 1)
		binst = contr->binst;
	else if (channr == 2)
		binst = contr->binst->next;
	return(binst);
}

#if 0
static void d2_listener(struct IsdnCardState *cs, u_char *buf, int len)
{
	Contr_t *contr = cs->contr;

	if (!contr) {
		int_error();
		return;
	}

	contrD2Trace(contr, buf, len);
}
#endif
