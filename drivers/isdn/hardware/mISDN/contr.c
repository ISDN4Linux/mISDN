/* $Id: contr.c,v 0.9 2001/08/02 14:51:56 kkeil Exp $
 *
 */

#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

#define contrDebug(contr, lev, fmt, args...) \
	capidebug(lev, fmt, ## args)

int contrConstr(Contr_t *contr, hisaxstack_t *st, hisax_pid_t *pid, hisaxobject_t *ocapi)
{ 
	char tmp[10];
	hisaxstack_t *cst = st->child;
	BInst_t	*binst;

	memset(contr, 0, sizeof(Contr_t));
	memcpy(&contr->inst.pid, pid, sizeof(hisax_pid_t));
	contr->adrController = st->id;
	sprintf(contr->inst.name, "CAPI %d", st->id);
	contr->inst.obj = ocapi;
	if (!SetHandledPID(ocapi, &contr->inst.pid)) {
		int_error();
		return(-ENOPROTOOPT);
	}
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
		binst->inst.pid.layermask |= ISDN_LAYER(4);
		binst->inst.down.stat = IF_NOACTIV;
		APPEND_TO_LIST(binst, contr->binst);
		cst = cst->next;
	}
	APPEND_TO_LIST(contr, ocapi->ilist);
	sprintf(tmp, "HiSax%d", contr->adrController);
	contr->ctrl = cdrv_if->attach_ctr(&hisax_driver, tmp, contr);
	if (!contr->ctrl)
		return -ENODEV;
	contr->inst.data = contr;
	ocapi->ctrl(st, MGR_REGLAYER | INDICATION, &contr->inst);
	contr->inst.up.stat = IF_DOWN;
	return 0;
}

void contrDestr(Contr_t *contr)
{
	int i;
	hisaxinstance_t *inst = &contr->inst;

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
	if (inst->up.peer) {
		inst->up.peer->obj->ctrl(inst->up.peer,
			MGR_DISCONNECT | REQUEST, &inst->up);
	}
	if (inst->down.peer) {
		inst->down.peer->obj->ctrl(inst->down.peer,
			MGR_DISCONNECT | REQUEST, &inst->down);
	}
	inst->obj->ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	REMOVE_FROM_LISTBASE(contr, ((Contr_t *)inst->obj->ilist));
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
	struct firm {
		int	len;
		void	*data;
	} firm;
	
	firm.len  = len;
	firm.data = data;
	contr->inst.obj->ctrl(contr->inst.st, MGR_LOADFIRM | REQUEST, &firm);
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

	contrDebug(contr, LL_DEB_INFO, __FUNCTION__ ": PLCI(%x)", plci->adrPLCI);
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
contrL3L4(hisaxif_t *hif, struct sk_buff *skb)
{
	Contr_t		*contr;
	Plci_t		*plci;
	__u32		*id;
	int		ret = -EINVAL;
	hisax_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	if (skb->len < HISAX_FRAME_MIN)
		return(ret);
	hh = (hisax_head_t *)skb->data;
	contr = hif->fdata;
	skb_pull(skb, HISAX_HEAD_SIZE);
	if (hh->prim == (CC_NEW_CR | INDICATION)) {
		plci = contrNewPlci(contr);
		if (!plci)
			return(-EBUSY);
		if (skb->len >= sizeof(void)) {
			id = (__u32 *)skb->data;
			*id = plci->adrPLCI;
			dev_kfree_skb(skb);
			ret = 0;
		}
	} else if ((hh->dinfo & ~CONTROLER_MASK) == DUMMY_CR_FLAG) {
		ret = contrDummyInd(contr, hh->prim, skb);
	} else {
		if (!(plci = contrGetPLCI4addr(contr, hh->dinfo))) {
			contrDebug(contr, LL_DEB_WARN, __FUNCTION__
			": unknown plci prim(%x) id(%x)",
			hh->prim, hh->dinfo);
			return(-ENODEV);
		}
		ret = plci_l3l4(plci, hh->prim, skb);
	}
	return(ret);
}

int contrL4L3(Contr_t *contr, u_int prim, int dinfo, struct sk_buff *skb)
{
	return(if_newhead(&contr->inst.down, prim, dinfo, skb));
}

void contrPutStatus(Contr_t *contr, char *msg)
{
	printk(KERN_DEBUG "HiSax: %s", msg);
}

Contr_t *newContr(hisaxobject_t *ocapi, hisaxstack_t *st, hisax_pid_t *pid)
{
	Contr_t *contr;

	if (!pid)
		return(NULL);
	if (!st) {
		printk(KERN_ERR "newContr no stack\n");
		return(NULL);
	}
	contr = kmalloc(sizeof(Contr_t), GFP_KERNEL);
	if (!contr)
		return(NULL);

	if (contrConstr(contr, st, pid, ocapi) != 0) {
		contrDestr(contr);
		kfree(contr);
		return(NULL);
	}
	return contr;
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
			binst->inst.pid.layermask = ISDN_LAYER(4);
			binst->inst.down.stat = IF_NOACTIV;
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
