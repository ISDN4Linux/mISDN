/* $Id: contr.c,v 1.15 2003/11/11 09:59:00 keil Exp $
 *
 */

#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include "capi.h"
#include "helper.h"
#include "debug.h"

#define contrDebug(contr, lev, fmt, args...) \
	if (contr->debug & lev) capidebug(lev, fmt, ## args)

void
contrDestr(Contr_t *contr)
{
	int i;
	mISDNinstance_t *inst = &contr->inst;

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
#ifdef OLDCAPI_DRIVER_INTERFACE
	if (contr->ctrl)
		cdrv_if->detach_ctr(contr->ctrl);
#else
	detach_capi_ctr(contr->ctrl);
	kfree(contr->ctrl);
	contr->ctrl = NULL;
#endif
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
	if (contr->entity != MISDN_ENTITY_NONE)
		inst->obj->ctrl(inst, MGR_DELENTITY | REQUEST, (void *)contr->entity);
}

void
contrRun(Contr_t *contr)
{
	BInst_t	*binst;
	int	nb, ret;

	nb = 0;
	binst = contr->binst;
	while(binst) {
		nb++;
		binst = binst->next;
	}
	if (contr->inst.st && contr->inst.st->mgr)
		sprintf(contr->ctrl->manu, "mISDN CAPI controller %s", contr->inst.st->mgr->name);
	else
		sprintf(contr->ctrl->manu, "mISDN CAPI");
	strncpy(contr->ctrl->serial, "0002", CAPI_SERIAL_LEN);
	contr->ctrl->version.majorversion = 2; 
	contr->ctrl->version.minorversion = 0;
	contr->ctrl->version.majormanuversion = 1;
	contr->ctrl->version.minormanuversion = 0;
	memset(&contr->ctrl->profile, 0, sizeof(struct capi_profile));
	contr->ctrl->profile.ncontroller = 1;
	contr->ctrl->profile.nbchannel = nb;
	contrDebug(contr, CAPI_DBG_INFO, "%s: %s version(%s)",
		__FUNCTION__, contr->ctrl->manu, contr->ctrl->serial);
	// FIXME
	ret = contr->inst.obj->ctrl(contr->inst.st, MGR_GLOBALOPT | REQUEST, &contr->ctrl->profile.goptions);
	if (ret) {
		/* Fallback on error, minimum set */
		contr->ctrl->profile.goptions = GLOBALOPT_INTERNAL_CTRL;
	}
	/* add options we allways know about FIXME: DTMF */
	contr->ctrl->profile.goptions |= GLOBALOPT_DTMF |
					 GLOBALOPT_SUPPLEMENTARY_SERVICE;

	if (nb) {
		mISDN_pid_t	pidmask;

		memset(&pidmask, 0, sizeof(mISDN_pid_t));
		pidmask.protocol[1] = 0x03ff;
		pidmask.protocol[2] = 0x1fff;
		pidmask.protocol[3] = 0x00ff;
		binst = contr->binst;
		ret = binst->inst.obj->ctrl(binst->bst, MGR_EVALSTACK | REQUEST, &pidmask);
		if (ret) {
			/* Fallback on error, minimum set */
			int_error();
			contr->ctrl->profile.support1 = 3; // HDLC, TRANS
			contr->ctrl->profile.support2 = 3; // X75SLP, TRANS
			contr->ctrl->profile.support3 = 1; // TRANS
		} else {
			contr->ctrl->profile.support1 = pidmask.protocol[1];
			contr->ctrl->profile.support2 = pidmask.protocol[2];
			contr->ctrl->profile.support3 = pidmask.protocol[3];
		}
	}
	contrDebug(contr, CAPI_DBG_INFO, "%s: GLOBAL(%08X) B1(%08X) B2(%08X) B3(%08X)",
		__FUNCTION__, contr->ctrl->profile.goptions, contr->ctrl->profile.support1,
		contr->ctrl->profile.support2, contr->ctrl->profile.support2);
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->ready(contr->ctrl);
#else
	capi_ctr_ready(contr->ctrl);
#endif
}

Appl_t
*contrId2appl(Contr_t *contr, __u16 ApplId)
{
	if ((ApplId < 1) || (ApplId > CAPI_MAXAPPL)) {
		int_error();
		return 0;
	}
	return contr->appls[ApplId - 1];
}

Plci_t
*contrAdr2plci(Contr_t *contr, __u32 adr)
{
	int i = (adr >> 8);

	if ((i < 1) || (i > CAPI_MAXPLCI)) {
		int_error();
		return 0;
	}
	return contr->plcis[i - 1];
}

static void
RegisterAppl(struct capi_ctr *ctrl, __u16 ApplId, capi_register_params *rp)
{
	Contr_t	*contr = ctrl->driverdata;
	Appl_t	*appl;

	contrDebug(contr, CAPI_DBG_APPL, "%s: ApplId(%x)", __FUNCTION__, ApplId);
	appl = contrId2appl(contr, ApplId);
	if (appl) {
		int_error();
		return;
	}
	appl = kmalloc(sizeof(Appl_t), GFP_ATOMIC);
	if (!appl) {
		int_error();
		return;
	}
	contr->appls[ApplId - 1] = appl;
	applConstr(appl, contr, ApplId, rp);
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->appl_registered(contr->ctrl, ApplId);
#endif
}

static void
ReleaseAppl(struct capi_ctr *ctrl, __u16 ApplId)
{
	Contr_t *contr = ctrl->driverdata;
	Appl_t	*appl;

	contrDebug(contr, CAPI_DBG_APPL, "%s: ApplId(%x) caller:%lx", __FUNCTION__, ApplId, __builtin_return_address(0));
	appl = contrId2appl(contr, ApplId);
	if (!appl) {
		int_error();
		return;
	}
	applDestr(appl);
	kfree(appl);
	contr->appls[ApplId - 1] = NULL;
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->appl_released(contr->ctrl, ApplId);
#endif
}

#ifdef OLDCAPI_DRIVER_INTERFACE
static void
#else
static u16
#endif
SendMessage(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	Contr_t	*contr = ctrl->driverdata;
	Appl_t	*appl;
	int	ApplId;
	int	err = CAPI_NOERROR;

	ApplId = CAPIMSG_APPID(skb->data);
	appl = contrId2appl(contr, ApplId);
	if (!appl) {
		int_error();
		err = CAPI_ILLAPPNR;
	} else
		applSendMessage(appl, skb);
#ifndef OLDCAPI_DRIVER_INTERFACE
	return(err);
#endif
}

static int
LoadFirmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	Contr_t	*contr = ctrl->driverdata;
	struct firm {
		int	len;
		void	*data;
	} firm;
	int	retval;
	
	firm.len  = data->firmware.len;
	if (data->firmware.user) {
		firm.data = vmalloc(data->firmware.len);
		if (!firm.data)
			return(-ENOMEM);
		retval = copy_from_user(firm.data, data->firmware.data, data->firmware.len);
		if (retval) {
			vfree(firm.data);
			return(retval);
		}
	} else
		firm.data = data;
	contr->inst.obj->ctrl(contr->inst.st, MGR_LOADFIRM | REQUEST, &firm);
	if (data->firmware.user)
		vfree(firm.data);
	return(0);
}

static char *
procinfo(struct capi_ctr *ctrl)
{
	Contr_t *contr = ctrl->driverdata;

	if (CAPI_DBG_INFO & contr->debug)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);
	if (!contr)
		return "";
	sprintf(contr->infobuf, "-");
	return contr->infobuf;
}

static int
read_proc(char *page, char **start, off_t off, int count, int *eof, struct capi_ctr *ctrl)
{
       int len = 0;

       len += sprintf(page+len, "mISDN_read_proc\n");
       if (off+count >= len)
          *eof = 1;
       if (len < off)
           return 0;
       *start = page + off;
       return ((count < len-off) ? count : len-off);
};


static void
ResetContr(struct capi_ctr *ctrl)
{
	Contr_t	*contr = ctrl->driverdata;
	int	ApplId;
	Appl_t	*appl;

	for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
		appl = contrId2appl(contr, ApplId);
		if (appl) {
			applDestr(appl);
			kfree(appl);
		}
		contr->appls[ApplId - 1] = NULL;
	}
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->reseted(contr->ctrl);
#else
	capi_ctr_reseted(contr->ctrl);
#endif
}

#ifdef OLDCAPI_DRIVER_INTERFACE
static void
Remove_Contr(struct capi_ctr *ctrl)
{
	Contr_t *contr = ctrl->driverdata;

	if (CAPI_DBG_INFO & contr->debug)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);
}

struct capi_driver mISDN_driver = {
	"mISDN",
	"0.01",
	LoadFirmware,
	ResetContr,
	Remove_Contr,
	RegisterAppl,
	ReleaseAppl,
	SendMessage,
	procinfo,
	read_proc,
	0,
	0,
};
#endif

void
contrD2Trace(Contr_t *contr, u_char *buf, int len)
{
	Appl_t	*appl;
	__u16	applId;

	for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
		appl = contrId2appl(contr, applId);
		if (appl) {
			applD2Trace(appl, buf, len);
		}
	}
}

void
contrRecvCmsg(Contr_t *contr, _cmsg *cmsg)
{
	struct sk_buff	*skb;
	int		len;
	
	capi_cmsg2message(cmsg, contr->msgbuf);
	len = CAPIMSG_LEN(contr->msgbuf);
	contrDebug(contr, CAPI_DBG_CONTR_MSG, "%s: len(%d) applid(%x) %s msgnr(%d) addr(%08x)",
		__FUNCTION__, len, cmsg->ApplId, capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		cmsg->Messagenumber, cmsg->adr.adrController);
	if (!(skb = alloc_skb(len, GFP_ATOMIC))) {
		printk(KERN_ERR "%s: no mem for %d bytes\n", __FUNCTION__, len);
		int_error();
		return;
	}
	memcpy(skb_put(skb, len), contr->msgbuf, len);
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->handle_capimsg(contr->ctrl, cmsg->ApplId, skb);
#else
	capi_ctr_handle_message(contr->ctrl, cmsg->ApplId, skb);
#endif
}

void
contrAnswerCmsg(Contr_t *contr, _cmsg *cmsg, __u16 Info)
{
	capi_cmsg_answer(cmsg);
	cmsg->Info = Info;
	contrRecvCmsg(contr, cmsg);
}

void
contrAnswerMessage(Contr_t *contr, struct sk_buff *skb, __u16 Info)
{
	_cmsg	cmsg;

	capi_message2cmsg(&cmsg, skb->data);
	contrAnswerCmsg(contr, &cmsg, Info);
}

static Plci_t *
contrGetPLCI4ID(Contr_t *contr, u_int id)
{
	int	i;

	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (!contr->plcis[i])
			continue;
		if (contr->plcis[i]->id == id)
			return(contr->plcis[i]);
	}
	return(NULL);
}

Plci_t *
contrNewPlci(Contr_t *contr, u_int id)
{
	Plci_t	*plci;
	int	i;

	for (i = 0; i < CAPI_MAXPLCI; i++) {
		if (!contr->plcis[i])
			break;
	}
	if (i == CAPI_MAXPLCI) {
		return 0;
	}
	if (id == MISDN_ID_ANY) {
		if (contr->entity == MISDN_ENTITY_NONE) {
			printk(KERN_ERR "mISDN %s: no ENTITY id\n",
				__FUNCTION__);
			return NULL;
		}
		id = (contr->entity << 16) | ((i+1) << 8) | (contr->adrController & 0xFF);
	}
	plci = contrGetPLCI4ID(contr, id);
	if (plci) {
		printk(KERN_ERR "mISDN %s: PLCI(%x) allready has id(%x)\n",
			__FUNCTION__, plci->adrPLCI, id);
		return NULL;
	}
	plci = kmalloc(sizeof(Plci_t), GFP_ATOMIC);
	if (!plci) {
		int_error();
		return NULL;
	}
	contr->plcis[i] = plci;
	plciConstr(plci, contr, (i+1) << 8 | contr->adrController, id);
	contrDebug(contr, CAPI_DBG_PLCI, "%s: PLCI(%x) plci(%p,%d) id(%x)",
		__FUNCTION__, plci->adrPLCI, plci, sizeof(*plci), plci->id);
	return plci;
}

void
contrDelPlci(Contr_t *contr, Plci_t *plci)
{
	int	i = plci->adrPLCI >> 8;

	contrDebug(contr, CAPI_DBG_PLCI, "%s: PLCI(%x) plci(%p)", __FUNCTION__, plci->adrPLCI, plci);
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

int
contrL3L4(mISDNif_t *hif, struct sk_buff *skb)
{
	Contr_t		*contr;
	Plci_t		*plci;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	if (!hif || !skb)
		return(ret);
	hh = mISDN_HEAD_P(skb);
	contr = hif->fdata;
	contrDebug(contr, CAPI_DBG_CONTR_INFO, "%s: prim(%x) id(%x)",
		__FUNCTION__, hh->prim, hh->dinfo);
	if (hh->prim == (CC_NEW_CR | INDICATION)) {
		plci = contrNewPlci(contr, hh->dinfo);
		if (plci) {
			ret = 0;
			dev_kfree_skb(skb);
		} else 
			ret = -EBUSY;
	} else if (hh->dinfo == MISDN_ID_DUMMY) {
		ret = contrDummyInd(contr, hh->prim, skb);
	} else {
		if (!(plci = contrGetPLCI4ID(contr, hh->dinfo))) {
			contrDebug(contr, CAPI_DBG_WARN, "%s: unknown plci prim(%x) id(%x)",
				__FUNCTION__, hh->prim, hh->dinfo);
			return(-ENODEV);
		}
		contrDebug(contr, CAPI_DBG_PLCI, "%s: PLCI(%x) plci(%p)", __FUNCTION__, plci->adrPLCI, plci);
		ret = plci_l3l4(plci, hh->prim, skb);
	}
	return(ret);
}

int
contrL4L3(Contr_t *contr, u_int prim, int dinfo, struct sk_buff *skb)
{
	return(if_newhead(&contr->inst.down, prim, dinfo, skb));
}

void
contrPutStatus(Contr_t *contr, char *msg)
{
	contrDebug(contr, CAPI_DBG_CONTR, "%s: %s", __FUNCTION__, msg);
}

static int
contrConstr(Contr_t *contr, mISDNstack_t *st, mISDN_pid_t *pid, mISDNobject_t *ocapi)
{ 
	int		retval;
	mISDNstack_t	*cst = st->child;
	BInst_t		*binst;

	memset(contr, 0, sizeof(Contr_t));
	memcpy(&contr->inst.pid, pid, sizeof(mISDN_pid_t));
#ifndef OLDCAPI_DRIVER_INTERFACE
	if (!(contr->ctrl = kmalloc(sizeof(struct capi_ctr), GFP_ATOMIC))) {
		printk(KERN_ERR "no mem for contr->ctrl\n");
		int_error();
		return -ENOMEM;
	}
	memset(contr->ctrl, 0, sizeof(struct capi_ctr));
#endif
	contr->adrController = st->id;
	sprintf(contr->inst.name, "CAPI %d", st->id);
	init_mISDNinstance(&contr->inst, ocapi, contr);
	if (!SetHandledPID(ocapi, &contr->inst.pid)) {
		int_error();
		return(-ENOPROTOOPT);
	}
	while(cst) {
		if (!(binst = kmalloc(sizeof(BInst_t), GFP_ATOMIC))) {
			printk(KERN_ERR "no mem for Binst\n");
			int_error();
			return -ENOMEM;
		}
		memset(binst, 0, sizeof(BInst_t));
		binst->bst = cst;
		binst->inst.st = cst;
		init_mISDNinstance(&binst->inst, ocapi, binst);
		binst->inst.pid.layermask |= ISDN_LAYER(4);
		binst->inst.down.stat = IF_NOACTIV;
		APPEND_TO_LIST(binst, contr->binst);
		cst = cst->next;
	}
	APPEND_TO_LIST(contr, ocapi->ilist);
	contr->entity = 
	retval = ocapi->ctrl(&contr->inst, MGR_NEWENTITY | REQUEST, NULL);
	if (retval) {
		printk(KERN_WARNING "mISDN %s: MGR_NEWENTITY REQUEST failed err(%x)\n",
			__FUNCTION__, retval);
	}
	retval = 0;
#ifdef OLDCAPI_DRIVER_INTERFACE
	{
		char	tmp[10];

		sprintf(tmp, "mISDN%d", st->id);
		contr->ctrl = cdrv_if->attach_ctr(&mISDN_driver, tmp, contr);
		if (!contr->ctrl)
			retval = -ENODEV;
	}
#else
	contr->ctrl->owner = THIS_MODULE;
	sprintf(contr->ctrl->name, "mISDN%d", st->id);
	contr->ctrl->driver_name = "mISDN";
	contr->ctrl->driverdata = contr;
	contr->ctrl->register_appl = RegisterAppl;
	contr->ctrl->release_appl = ReleaseAppl;
	contr->ctrl->send_message = SendMessage;
	contr->ctrl->load_firmware = LoadFirmware;
	contr->ctrl->reset_ctr = ResetContr;
	contr->ctrl->procinfo = procinfo;
	contr->ctrl->ctr_read_proc = read_proc;
	retval = attach_capi_ctr(contr->ctrl);
#endif
	if (!retval) {
		contr->adrController = contr->ctrl->cnr;
		ocapi->ctrl(st, MGR_REGLAYER | INDICATION, &contr->inst);
		contr->inst.up.stat = IF_DOWN;
	}
	return retval;
}

Contr_t *
newContr(mISDNobject_t *ocapi, mISDNstack_t *st, mISDN_pid_t *pid)
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

BInst_t *
contrSelChannel(Contr_t *contr, u_int channel)
{ 
	mISDNstack_t	*cst;
	BInst_t		*binst;
	channel_info_t	ci;
	int		ret;

	if (!contr->binst) {
		cst = contr->inst.st->child;
		if (!cst)
			return(NULL);
		while(cst) {
			if (!(binst = kmalloc(sizeof(BInst_t), GFP_ATOMIC))) {
				printk(KERN_ERR "no mem for Binst\n");
				int_error();
				return(NULL);
			}
			memset(binst, 0, sizeof(BInst_t));
			binst->bst = cst;
			binst->inst.st = cst;
			binst->inst.data = binst;
			binst->inst.obj = contr->inst.obj;
			binst->inst.pid.layermask = ISDN_LAYER(4);
			binst->inst.down.stat = IF_NOACTIV;
			APPEND_TO_LIST(binst, contr->binst);
			cst = cst->next;
		}
	}
	ci.channel = channel;
	ci.st.p = NULL;
	ret = contr->inst.obj->ctrl(contr->inst.st, MGR_SELCHANNEL | REQUEST,
		&ci);
	if (ret) {
		int_errtxt("MGR_SELCHANNEL ret(%d)", ret);
		return(NULL);
	}
	cst = ci.st.p;
	binst = contr->binst;
	while(binst) {
		if (cst == binst->bst)
			break;
		binst = binst->next;
	}
	return(binst);
}

#if 0
static void
d2_listener(struct IsdnCardState *cs, u_char *buf, int len)
{
	Contr_t *contr = cs->contr;

	if (!contr) {
		int_error();
		return;
	}

	contrD2Trace(contr, buf, len);
}
#endif
