/* $Id: contr.c,v 1.29 2006/09/14 15:36:47 gkelleter Exp $
 *
 */

#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include "m_capi.h"
#include "helper.h"
#include "debug.h"

#define contrDebug(contr, lev, fmt, args...) \
	if (contr->debug & lev) capidebug(lev, fmt, ## args)

void
ControllerDestr(Controller_t *contr)
{
	mISDNinstance_t		*inst = &contr->inst;
	struct list_head	*item, *next;
	u_long			flags;

	spin_lock_irqsave(&contr->list_lock, flags);
	list_for_each_safe(item, next, &contr->Applications) {
		ApplicationDestr(list_entry(item, Application_t, head), 3);
	}
	if (contr->plcis) {
		Plci_t	*plci = contr->plcis;
		int	i;

		for (i = 0; i < contr->maxplci; i++) {
			AppPlci_t	*aplci;
			if (test_bit(PLCI_STATE_ACTIV, &plci->state)) {
				if (plci->nAppl) {
					printk(KERN_ERR "%s: PLCI(%x) still busy (%d)\n",
						__FUNCTION__, plci->addr, plci->nAppl);
					list_for_each_safe(item, next, &plci->AppPlcis) {
						aplci = (AppPlci_t *)item;
						aplci->contr = NULL;
						plciDetachAppPlci(plci, aplci);
						AppPlciDestr(aplci);
					}
				}
			}
			plci++;
		}
		kfree(contr->plcis);
		contr->plcis = NULL;
	}
	list_for_each_safe(item, next, &contr->SSProcesse) {
		SSProcessDestr(list_entry(item, SSProcess_t, head));
	}
#ifdef OLDCAPI_DRIVER_INTERFACE
	if (contr->ctrl)
		cdrv_if->detach_ctr(contr->ctrl);
#else
	if (contr->ctrl) {
		detach_capi_ctr(contr->ctrl);
		kfree(contr->ctrl);
	}
#endif
	contr->ctrl = NULL;
#ifdef FIXME
	if (inst->up.peer) {
		mISDN_ctrl(inst->up.peer, MGR_DISCONNECT | REQUEST, &inst->up);
	}
	if (inst->down.peer) {
		mISDN_ctrl(inst->down.peer, MGR_DISCONNECT | REQUEST, &inst->down);
	}
#endif
	list_for_each_safe(item, next, &contr->linklist) {
		PLInst_t	*plink = list_entry(item, PLInst_t, list);
		list_del(&plink->list);
		kfree(plink);
	}
	if (contr->entity != MISDN_ENTITY_NONE)
		mISDN_ctrl(inst, MGR_DELENTITY | REQUEST, (void *)((u_long)contr->entity));
	mISDN_ctrl(inst, MGR_UNREGLAYER | REQUEST, NULL);
	list_del(&contr->list);
	spin_unlock_irqrestore(&contr->list_lock, flags);
	kfree(contr);
}

void
ControllerRun(Controller_t *contr)
{
	PLInst_t	*plink;
	int		ret;

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
	contr->ctrl->profile.nbchannel = contr->nr_bc;
	contrDebug(contr, CAPI_DBG_INFO, "%s: %s version(%s)",
		__FUNCTION__, contr->ctrl->manu, contr->ctrl->serial);
	// FIXME
	ret = mISDN_ctrl(contr->inst.st, MGR_GLOBALOPT | REQUEST, &contr->ctrl->profile.goptions);
	if (ret) {
		/* Fallback on error, minimum set */
		contr->ctrl->profile.goptions = GLOBALOPT_INTERNAL_CTRL;
	}
	/* add options we allways know about FIXME: DTMF */
	contr->ctrl->profile.goptions |= GLOBALOPT_DTMF |
					 GLOBALOPT_SUPPLEMENTARY_SERVICE;

	if (contr->nr_bc) {
		mISDN_pid_t	pidmask;

		memset(&pidmask, 0, sizeof(mISDN_pid_t));
		pidmask.protocol[1] = 0x03ff;
		pidmask.protocol[2] = 0x1fff;
		pidmask.protocol[3] = 0x00ff;
		if (list_empty(&contr->linklist)) {
			int_error();
			ret = -EINVAL;
		} else {
			plink = list_entry(contr->linklist.next, PLInst_t, list);
			ret = mISDN_ctrl(plink->st, MGR_EVALSTACK | REQUEST, &pidmask);
		}
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
		contr->ctrl->profile.support2, contr->ctrl->profile.support3);
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->ready(contr->ctrl);
#else
	capi_ctr_ready(contr->ctrl);
#endif
}

Application_t
*getApplication4Id(Controller_t *contr, __u16 ApplId)
{
	struct list_head	*item;
	Application_t		*ap = NULL;

	list_for_each(item, &contr->Applications) {
		ap = (Application_t *)item;
		if (ap->ApplId == ApplId)
			break;
		ap = NULL;
	}
	return(ap);
}

Plci_t
*getPlci4Addr(Controller_t *contr, __u32 addr)
{
	int i = (addr >> 8) & 0xff;

	if ((i < 1) || (i > contr->maxplci)) {
		int_error();
		return(NULL);
	}
	return(&contr->plcis[i - 1]);
}

static void
RegisterApplication(struct capi_ctr *ctrl, __u16 ApplId, capi_register_params *rp)
{
	Controller_t	*contr = ctrl->driverdata;
	Application_t	*appl;
	u_long		flags;
	int		ret;

	contrDebug(contr, CAPI_DBG_APPL, "%s: ApplId(%x)", __FUNCTION__, ApplId);
	appl = getApplication4Id(contr, ApplId);
	if (appl) {
		int_error();
		return;
	}
	spin_lock_irqsave(&contr->list_lock, flags);
	ret = ApplicationConstr(contr, ApplId, rp);
	spin_unlock_irqrestore(&contr->list_lock, flags);
	if (ret) {
		int_error();
		return;
	}
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->appl_registered(contr->ctrl, ApplId);
#endif
}

static void
ReleaseApplication(struct capi_ctr *ctrl, __u16 ApplId)
{
	Controller_t	*contr = ctrl->driverdata;
	Application_t	*appl;
	u_long		flags;

	contrDebug(contr, CAPI_DBG_APPL, "%s: ApplId(%x) caller:%lx", __FUNCTION__, ApplId, __builtin_return_address(0));
	spin_lock_irqsave(&contr->list_lock, flags);
	appl = getApplication4Id(contr, ApplId);
	if (!appl) {
		spin_unlock_irqrestore(&contr->list_lock, flags);
		int_error();
		return;
	}
	ApplicationDestr(appl, 1);
	spin_unlock_irqrestore(&contr->list_lock, flags);
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
	Controller_t	*contr = ctrl->driverdata;
	Application_t	*appl;
	int		ApplId;
	int		err = CAPI_NOERROR;
	u16		cmd;
	AppPlci_t	*aplci;
	Ncci_t		*ncci;
	mISDN_head_t	*hh = mISDN_HEAD_P(skb);

	ApplId = CAPIMSG_APPID(skb->data);
	appl = getApplication4Id(contr, ApplId);
	if (!appl) {
		int_error();
		err = CAPI_ILLAPPNR;
		goto end;
	}

	hh->prim = CAPI_MESSAGE_REQUEST;
	hh->dinfo = ApplId;
	cmd = CAPICMD(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data));
	contrDebug(contr, CAPI_DBG_CONTR_MSG, "SendMessage: %s caddr(%x)", 
		capi_cmd2str(CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data)),
		CAPIMSG_CONTROL(skb->data));
	switch (cmd) {
		// for NCCI state machine
		case CAPI_DATA_B3_REQ:
		case CAPI_DATA_B3_RESP:
		case CAPI_CONNECT_B3_RESP:
		case CAPI_CONNECT_B3_ACTIVE_RESP:
		case CAPI_DISCONNECT_B3_REQ:
		case CAPI_RESET_B3_REQ:
		case CAPI_RESET_B3_RESP:
			aplci = getAppPlci4addr(appl, CAPIMSG_CONTROL(skb->data));
			if (!aplci) {
				AnswerMessage2Application(appl, skb, CapiIllContrPlciNcci);
				dev_kfree_skb(skb);
				break;
			}
			ncci = getNCCI4addr(aplci, CAPIMSG_NCCI(skb->data), GET_NCCI_EXACT);
			if ((!ncci) || (!ncci->link)) {
				int_error();
				AnswerMessage2Application(appl, skb, CapiIllContrPlciNcci);
				dev_kfree_skb(skb);
				break;
			}
			err = mISDN_queue_message(&ncci->link->inst, 0, skb);
			if (err) {
				int_errtxt("mISDN_queue_message return(%d)", err);
				err = CAPI_MSGBUSY;
			}
			break;
		// new NCCI
		case CAPI_CONNECT_B3_REQ:
		// maybe already down NCCI
		case CAPI_DISCONNECT_B3_RESP:
		// for PLCI state machine
		case CAPI_INFO_REQ:
		case CAPI_ALERT_REQ:
		case CAPI_CONNECT_REQ:
		case CAPI_CONNECT_RESP:
		case CAPI_CONNECT_ACTIVE_RESP:
		case CAPI_DISCONNECT_REQ:
		case CAPI_DISCONNECT_RESP:
		case CAPI_SELECT_B_PROTOCOL_REQ:
		// for LISTEN state machine
		case CAPI_LISTEN_REQ:
		// other
		case CAPI_FACILITY_REQ:
		case CAPI_MANUFACTURER_REQ:
		case CAPI_INFO_RESP:
			err = mISDN_queue_message(&contr->inst, 0, skb);
			if (err) {
				int_errtxt("mISDN_queue_message return(%d)", err);
				err = CAPI_MSGBUSY;
			}
			break;
		/* need not further action currently, so it can be released here too avoid
		 * overlap with a release application
		 */
		case CAPI_FACILITY_RESP:
			dev_kfree_skb(skb);
			break;
		default:
			contrDebug(contr, CAPI_DBG_WARN, "SendMessage: %#x %#x not handled!", 
				CAPIMSG_COMMAND(skb->data), CAPIMSG_SUBCOMMAND(skb->data));
			err = CAPI_ILLCMDORSUBCMDORMSGTOSMALL;
			break;
	}
end:
#ifndef OLDCAPI_DRIVER_INTERFACE
	return(err);
#endif
}

static int
LoadFirmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	Controller_t	*contr = ctrl->driverdata;
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
	mISDN_ctrl(contr->inst.st, MGR_LOADFIRM | REQUEST, &firm);
	if (data->firmware.user)
		vfree(firm.data);
	return(0);
}

static char *
procinfo(struct capi_ctr *ctrl)
{
	Controller_t *contr = ctrl->driverdata;

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
ResetController(struct capi_ctr *ctrl)
{
	Controller_t		*contr = ctrl->driverdata;
	struct list_head	*item, *next;
	u_long			flags;

	spin_lock_irqsave(&contr->list_lock, flags);
	list_for_each_safe(item, next, &contr->Applications) {
		ApplicationDestr((Application_t *)item, 2);
	}
	list_for_each_safe(item, next, &contr->SSProcesse) {
		SSProcessDestr((SSProcess_t *)item);
	}
	spin_unlock_irqrestore(&contr->list_lock, flags);
#ifdef OLDCAPI_DRIVER_INTERFACE
	contr->ctrl->reseted(contr->ctrl);
#else
	capi_ctr_reseted(contr->ctrl);
#endif
}

#ifdef OLDCAPI_DRIVER_INTERFACE
static void
Remove_Controller(struct capi_ctr *ctrl)
{
	Controller_t *contr = ctrl->driverdata;

	if (CAPI_DBG_INFO & contr->debug)
		printk(KERN_DEBUG "%s\n", __FUNCTION__);
}

struct capi_driver mISDN_driver = {
	"mISDN",
	"0.01",
	LoadFirmware,
	ResetController,
	Remove_Controller,
	RegisterApplication,
	ReleaseApplication,
	SendMessage,
	procinfo,
	read_proc,
	0,
	0,
};
#endif

void
ControllerD2Trace(Controller_t *contr, u_char *buf, int len)
{
	struct list_head	*item;

	list_for_each(item, &contr->Applications) {
		applD2Trace((Application_t *)item, buf, len);
	}
}

static __inline__ Plci_t *
getPlci4L3id(Controller_t *contr, u_int l3id)
{
	Plci_t	*plci = contr->plcis;
	int	i;

	for (i = 0; i < contr->maxplci; i++) {
		if (test_bit(PLCI_STATE_ACTIV, &plci->state) &&
			(plci->l3id == l3id))
			return(plci);
		plci++;
	}
	return(NULL);
}

int
ControllerNewPlci(Controller_t *contr, Plci_t  **plci_p, u_int l3id)
{
	int	i;
	Plci_t	*plci = contr->plcis;

	for (i = 0; i < contr->maxplci; i++) {
		if (!test_and_set_bit(PLCI_STATE_ACTIV, &plci->state))
			break;
		plci++;
	}
	if (i == contr->maxplci) {
		contrDebug(contr, CAPI_DBG_PLCI, "%s: no free PLCI",
			__FUNCTION__);
		return(-EBUSY); //FIXME
	}
	*plci_p = plci;
	if (l3id == MISDN_ID_ANY) {
		if (contr->entity == MISDN_ENTITY_NONE) {
			printk(KERN_ERR "mISDN %s: no ENTITY id\n",
				__FUNCTION__);
			test_and_clear_bit(PLCI_STATE_ACTIV, &plci->state);
			return(-EINVAL); //FIXME
		}
		plci->l3id = (contr->entity << 16) | plci->addr;
	} else {
		plci = getPlci4L3id(contr, l3id);
		if (plci) {
			printk(KERN_WARNING "mISDN %s: PLCI(%x) allready has l3id(%x)\n",
				__FUNCTION__, plci->addr, l3id);
			test_and_clear_bit(PLCI_STATE_ACTIV, &(*plci_p)->state);
			return(-EBUSY); 
		}
		plci = *plci_p;
		plci->l3id = l3id;
	}
	contrDebug(contr, CAPI_DBG_PLCI, "%s: PLCI(%x) plci(%p,%d) id(%x)",
		__FUNCTION__, plci->addr, plci, sizeof(*plci), plci->l3id);
	return(0);
}

int
ControllerReleasePlci(Plci_t *plci)
{
	if (!plci->contr) {
		int_error();
		return(-EINVAL);
	}
	if (plci->nAppl) {
		contrDebug(plci->contr, CAPI_DBG_PLCI, "%s: PLCI(%x) still has %d Applications",
			__FUNCTION__, plci->addr, plci->nAppl);
		return(-EBUSY);
	}
	if (!list_empty(&plci->AppPlcis)) {
		int_errtxt("PLCI(%x) AppPlcis list not empty", plci->addr);
		return(-EBUSY);
	}
	test_and_clear_bit(PLCI_STATE_ALERTING, &plci->state);
	test_and_clear_bit(PLCI_STATE_OUTGOING, &plci->state);
	plci->l3id = MISDN_ID_NONE;
	if (!test_and_clear_bit(PLCI_STATE_ACTIV, &plci->state))
		int_errtxt("PLCI(%x) was not activ", plci->addr);
	return(0);
}

void
ControllerAddSSProcess(Controller_t *contr, SSProcess_t *sp)
{
	u_long	flags;

	INIT_LIST_HEAD(&sp->head);
	sp->contr = contr;
	sp->addr = contr->addr;
	spin_lock_irqsave(&contr->list_lock, flags);
	contr->LastInvokeId++;
	sp->invokeId = contr->LastInvokeId;
	list_add(&sp->head, &contr->SSProcesse);
	spin_unlock_irqrestore(&contr->list_lock, flags);
}

SSProcess_t
*getSSProcess4Id(Controller_t *contr, __u16 id)
{
	struct list_head	*item;
	SSProcess_t		*sp = NULL;

	list_for_each(item, &contr->SSProcesse) {
		sp = (SSProcess_t *)item;
		if (sp->invokeId == id)
			break;
		sp = NULL;
	}
	return(sp);
}

static int
Controller_function(mISDNinstance_t *inst, struct sk_buff *skb)
{
	Controller_t	*contr;
	Plci_t		*plci;
	int		ret = -EINVAL;
	mISDN_head_t	*hh;

	hh = mISDN_HEAD_P(skb);
	contr = inst->privat;
	contrDebug(contr, CAPI_DBG_CONTR_INFO, "%s: prim(%x) id(%x)",
		__FUNCTION__, hh->prim, hh->dinfo);
	if (hh->prim == CAPI_MESSAGE_REQUEST) {
		Application_t	*appl = getApplication4Id(contr, hh->dinfo);
		if (!appl) {
			int_error();
			return(ret);
		}
		ApplicationSendMessage(appl, skb);
		return(0);
	} else if (hh->prim == (CC_NEW_CR | INDICATION)) {
		ret = ControllerNewPlci(contr, &plci, hh->dinfo);
		if(!ret)
			dev_kfree_skb(skb);
	} else if (hh->dinfo == MISDN_ID_DUMMY) {
		contrDebug(contr, CAPI_DBG_CONTR_INFO, "%s: call Supplementary_l3l4 len %d",
			__FUNCTION__, skb->len);
		ret = Supplementary_l3l4(contr, hh->prim, skb);
	} else {
		if (!(plci = getPlci4L3id(contr, hh->dinfo))) {
			contrDebug(contr, CAPI_DBG_WARN, "%s: unknown plci prim(%x) id(%x)",
				__FUNCTION__, hh->prim, hh->dinfo);
			return(-ENODEV);
		}
		contrDebug(contr, CAPI_DBG_PLCI, "%s: PLCI(%x) plci(%p)", __FUNCTION__, plci->addr, plci);
		ret = plci_l3l4(plci, hh->prim, skb);
	}
	return(ret);
}

int
ControllerL4L3(Controller_t *contr, u_int prim, int dinfo, struct sk_buff *skb)
{
	return(mISDN_queuedown_newhead(&contr->inst, 0, prim, dinfo, skb));
}

void
ControllerPutStatus(Controller_t *contr, char *msg)
{
	contrDebug(contr, CAPI_DBG_CONTR, "%s: %s", __FUNCTION__, msg);
}

int
ControllerConstr(Controller_t **contr_p, mISDNstack_t *st, mISDN_pid_t *pid, mISDNobject_t *ocapi)
{
	struct list_head	*head;
	Controller_t		*contr;
	int			retval;
	mISDNstack_t		*cst;
	PLInst_t		*plink;
	u_long			flags;

	if (!st)
		return(-EINVAL);
	if (list_empty(&st->childlist)) {
		if ((st->id & FLG_CLONE_STACK) &&
			(st->childlist.prev != &st->childlist)) {
			head = st->childlist.prev;
		} else {
			printk(KERN_ERR "%s: invalid empty childlist (no clone) stid(%x) childlist(%p<-%p->%p)\n",
				__FUNCTION__, st->id, st->childlist.prev, &st->childlist, st->childlist.next);
			return(-EINVAL);
		}
	} else
		head = &st->childlist;
	if (!pid)
		return(-EINVAL);
	contr = kmalloc(sizeof(Controller_t), GFP_KERNEL);
	if (!contr)
		return(-ENOMEM);
	memset(contr, 0, sizeof(Controller_t));
	INIT_LIST_HEAD(&contr->Applications);
	INIT_LIST_HEAD(&contr->SSProcesse);
	INIT_LIST_HEAD(&contr->linklist);
	spin_lock_init(&contr->list_lock);
	contr->next_id = 1;
	memcpy(&contr->inst.pid, pid, sizeof(mISDN_pid_t));
#ifndef OLDCAPI_DRIVER_INTERFACE
	if (!(contr->ctrl = kmalloc(sizeof(struct capi_ctr), GFP_KERNEL))) {
		printk(KERN_ERR "no mem for contr->ctrl\n");
		int_error();
		ControllerDestr(contr);
		return -ENOMEM;
	}
	memset(contr->ctrl, 0, sizeof(struct capi_ctr));
#endif
	list_for_each_entry(cst, head, list)
		contr->nr_bc++;
	if (!contr->nr_bc) {
		printk(KERN_ERR "no bchannels\n");
		ControllerDestr(contr);
		return(-EINVAL); // FIXME
	}
	if (contr->nr_bc <= 2)
		contr->maxplci = CAPI_MAXPLCI_BRI;
	else if (contr->nr_bc <= 8)
		contr->maxplci = contr->nr_bc * 2 + 4;
	else
		contr->maxplci = CAPI_MAXPLCI_PRI;
	contr->plcis = kmalloc(contr->maxplci*sizeof(Plci_t), GFP_KERNEL);
	if (!contr->plcis) {
		printk(KERN_ERR "no mem for contr->plcis\n");
		int_error();
		contr->maxplci = 0;
		ControllerDestr(contr);
		return -ENOMEM;
	}
	contr->addr = (st->id >> 8) & 0xff;
	sprintf(contr->inst.name, "CAPI %d", contr->addr);
	mISDN_init_instance(&contr->inst, ocapi, contr, Controller_function);
	if (!mISDN_SetHandledPID(ocapi, &contr->inst.pid)) {
		int_error();
		ControllerDestr(contr);
		return(-ENOPROTOOPT);
	}
	list_for_each_entry(cst, head, list) {
		if (!(plink = kmalloc(sizeof(PLInst_t), GFP_KERNEL))) {
			printk(KERN_ERR "no mem for PLinst\n");
			int_error();
			ControllerDestr(contr);
			return -ENOMEM;
		}
		memset(plink, 0, sizeof(PLInst_t));
		plink->st = cst;
		plink->inst.st = cst;
		mISDN_init_instance(&plink->inst, ocapi, plink, NULL);
		plink->inst.pid.layermask |= ISDN_LAYER(4);
//		plink->inst.down.stat = IF_NOACTIV;
		list_add_tail(&plink->list, &contr->linklist);
	}
	spin_lock_irqsave(&ocapi->lock, flags);
	list_add_tail(&contr->list, &ocapi->ilist);
	spin_unlock_irqrestore(&ocapi->lock, flags);
	contr->entity = MISDN_ENTITY_NONE;
	retval = mISDN_ctrl(&contr->inst, MGR_NEWENTITY | REQUEST, NULL);
	if (retval) {
		printk(KERN_WARNING "mISDN %s: MGR_NEWENTITY REQUEST failed err(%d)\n",
			__FUNCTION__, retval);
	}
	retval = 0;
#ifdef OLDCAPI_DRIVER_INTERFACE
	{
		char	tmp[10];

		sprintf(tmp, "mISDN%d", (st->id >> 8) & 0xff);
		contr->ctrl = cdrv_if->attach_ctr(&mISDN_driver, tmp, contr);
		if (!contr->ctrl)
			retval = -ENODEV;
	}
#else
	contr->ctrl->owner = THIS_MODULE;
	sprintf(contr->ctrl->name, "mISDN%d", contr->addr);
	contr->ctrl->driver_name = "mISDN";
	contr->ctrl->driverdata = contr;
	contr->ctrl->register_appl = RegisterApplication;
	contr->ctrl->release_appl = ReleaseApplication;
	contr->ctrl->send_message = SendMessage;
	contr->ctrl->load_firmware = LoadFirmware;
	contr->ctrl->reset_ctr = ResetController;
	contr->ctrl->procinfo = procinfo;
	contr->ctrl->ctr_read_proc = read_proc;
	retval = attach_capi_ctr(contr->ctrl);
#endif
	if (!retval) {
		printk(KERN_DEBUG "contr->addr(%02x) cnr(%02x) st(%08x)\n",
			contr->addr, contr->ctrl->cnr, st->id);
		contr->addr = contr->ctrl->cnr;
		plciInit(contr);
		mISDN_ctrl(st, MGR_REGLAYER | INDICATION, &contr->inst);
//		contr->inst.up.stat = IF_DOWN;
		*contr_p = contr;
	} else {
		ControllerDestr(contr);
	}
	return retval;
}

PLInst_t *
ControllerSelChannel(Controller_t *contr, u_int channel)
{ 
	mISDNstack_t	*cst;
	PLInst_t	*plink;
	channel_info_t	ci;
	int		ret;

	if (list_empty(&contr->linklist)) {
		int_errtxt("no linklist for controller(%x)", contr->addr);
		return(NULL);
	}
	ci.channel = channel;
	ci.st.p = NULL;
	ret = mISDN_ctrl(contr->inst.st, MGR_SELCHANNEL | REQUEST, &ci);
	if (ret) {
		int_errtxt("MGR_SELCHANNEL ret(%d)", ret);
		return(NULL);
	}
	cst = ci.st.p;
	list_for_each_entry(plink, &contr->linklist, list) {
		if (cst == plink->st)
			return(plink);
	}
	return(NULL);
}

int
ControllerNextId(Controller_t *contr)
{
	int	id;

	id = contr->next_id++;
	if (id == 0x7fff)
		contr->next_id = 1;
	id |= (contr->entity << 16);
	return(id);
}

#if 0
static void
d2_listener(struct IsdnCardState *cs, u_char *buf, int len)
{
	Controller_t *contr = cs->contr;

	if (!contr) {
		int_error();
		return;
	}

	ControllerD2Trace(contr, buf, len);
}
#endif
