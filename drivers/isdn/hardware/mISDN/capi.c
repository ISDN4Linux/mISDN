/* $Id: capi.c,v 0.6 2001/03/04 17:08:32 kkeil Exp $
 *
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

const char *capi_revision = "$Revision: 0.6 $";

static int debug = 0;
static hisaxobject_t capi_obj;


static char MName[] = "HiSax Capi 2.0";

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
#define Capi20Init init_module
#endif

static char deb_buf[256];

void capidebug(int level, char *fmt, ...)
{
	va_list args;

	if (debug & level) {
		va_start(args, fmt);
		vsprintf(deb_buf, fmt, args);
		printk(KERN_DEBUG "%s\n", deb_buf);
		va_end(args);
	}
}

// ---------------------------------------------------------------------------
// registration to kernelcapi

int hisax_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	Contr_t *contr = ctrl->driverdata;
	u_char *tmp;
	int retval;

	printk(KERN_INFO __FUNCTION__ " firm user(%d) len(%d)\n",
		data->firmware.user, data->firmware.len);
	printk(KERN_INFO __FUNCTION__ "  cfg user(%d) len(%d)\n",
		data->configuration.user, data->configuration.len);
	if (data->firmware.user) {
		tmp = vmalloc(data->firmware.len);
		if (!tmp)
			return(-ENOMEM);
		retval = copy_from_user(tmp, data->firmware.data,
			data->firmware.len);
		if (retval)
			return(retval);
	} else
		tmp = data->firmware.data;
	contrLoadFirmware(contr, data->firmware.len, tmp);
	if (data->firmware.user)
		vfree(tmp);
	return 0;
}

void hisax_reset_ctr(struct capi_ctr *ctrl)
{
	Contr_t *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrReset(contr);
}

void hisax_remove_ctr(struct capi_ctr *ctrl)
{
	printk(KERN_INFO __FUNCTION__ "\n");
//	int_error();
}

static char *hisax_procinfo(struct capi_ctr *ctrl)
{
	Contr_t *contr = (ctrl->driverdata);

	printk(KERN_INFO __FUNCTION__ "\n");
	if (!contr)
		return "";
	sprintf(contr->infobuf, "-");
	return contr->infobuf;
}

void hisax_register_appl(struct capi_ctr *ctrl,
			 __u16 ApplId, capi_register_params *rp)
{
	Contr_t *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrRegisterAppl(contr, ApplId, rp);
}

void hisax_release_appl(struct capi_ctr *ctrl, __u16 ApplId)
{
	Contr_t *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrReleaseAppl(contr, ApplId);
}

void hisax_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	Contr_t *contr = ctrl->driverdata;

	contrSendMessage(contr, skb);
}

static int hisax_read_proc(char *page, char **start, off_t off,
		int count, int *eof, struct capi_ctr *ctrl)
{
       int len = 0;

       len += sprintf(page+len, "hisax_read_proc\n");
       if (off+count >= len)
          *eof = 1;
       if (len < off)
           return 0;
       *start = page + off;
       return ((count < len-off) ? count : len-off);
};

struct capi_driver_interface *cdrv_if;                  

struct capi_driver hisax_driver = {
       "hisax",
       "0.01",
       hisax_load_firmware,
       hisax_reset_ctr,
       hisax_remove_ctr,
       hisax_register_appl,
       hisax_release_appl,
       hisax_send_message,
       hisax_procinfo,
       hisax_read_proc,
       0,
       0,
};

int CapiNew(void)
{
	char tmp[64];

	strcpy(tmp, capi_revision);
	printk(KERN_INFO "HiSax: CAPI Revision %s\n", HiSax_getrev(tmp));

	cdrv_if = attach_capi_driver(&hisax_driver);
	
	if (!cdrv_if) {
		printk(KERN_ERR "hisax: failed to attach capi_driver\n");
		return -EIO;
	}
	init_listen();
	init_cplci();
	init_ncci();
	return 0;
}

static int
add_if_contr(Contr_t *ctrl, hisaxinstance_t *inst, hisaxif_t *hif) {
	int err;

	printk(KERN_DEBUG "capi add_if lay %x/%x prot %x\n", hif->layermask,
		hif->stat, hif->protocol);
	if (IF_TYPE(hif) == IF_UP) {
		printk(KERN_WARNING "capi add_if here is no UP interface\n");
	} else if (IF_TYPE(hif) == IF_DOWN) {
		if (inst == &ctrl->inst) {
			hif->fdata = ctrl;
			hif->func = contrL3L4;
		} else {
			hif->fdata = inst->data;
			ncciSetInterface(hif);
		}
		if (inst->down.stat == IF_NOACTIV) {
			inst->down.stat = IF_UP;
			inst->down.layermask = get_down_layer(hif->layermask);
			inst->down.protocol = get_protocol(inst->st,
				inst->down.layermask);
			err = capi_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
			if (err)
				inst->down.stat = IF_NOACTIV;
		}
	} else
		return(-EINVAL);
	return(0);
}

static int
del_if(hisaxinstance_t *inst, hisaxif_t *hif) {
	int err;

	printk(KERN_DEBUG "capi del_if lay %x/%x %p/%p\n", hif->layermask,
		hif->stat, hif->func, hif->fdata);
	if ((hif->func == inst->up.func) && (hif->fdata == inst->up.fdata)) {
		inst->up.stat = IF_NOACTIV;
		inst->up.protocol = ISDN_PID_NONE;
		err = capi_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->up);
	} else if ((hif->func == inst->down.func) && (hif->fdata == inst->down.fdata)) {
		inst->down.stat = IF_NOACTIV;
		inst->down.protocol = ISDN_PID_NONE;
		err = capi_obj.ctrl(inst->st, MGR_ADDIF | REQUEST, &inst->down);
	} else {
		printk(KERN_DEBUG "capi del_if no if found\n");
		return(-EINVAL);
	}
	return(0);
}


static int
capi20_manager(void *data, u_int prim, void *arg) {
	hisaxstack_t *st = data;
	hisaxinstance_t *inst = NULL;
	BInst_t *binst;
	Contr_t *ctrl = (Contr_t *)capi_obj.ilist;

	printk(KERN_DEBUG "capi20_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	while(ctrl) {
		if (ctrl->inst.st == st) {
			inst = &ctrl->inst;
			break;
		}
		binst = ctrl->binst;
		while(binst) {
			if (binst->inst.st == st) {
				inst = &binst->inst;
				break;
			}
		}
		if (inst)
			break;
		ctrl = ctrl->next;
	}
	switch(prim) {
	    case MGR_ADDIF | REQUEST:
		if (!ctrl) {
			ctrl = newContr(&capi_obj, st, arg);
			if (!ctrl) {
				printk(KERN_WARNING "capi20_manager create_ctrl failed\n");
				return(-EINVAL);
			} else {
				inst = &ctrl->inst;
			}
		}
		return(add_if_contr(ctrl, inst, arg));
		break;
	    case MGR_DELIF | REQUEST:
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager delif no instance\n");
			return(-EINVAL);
		}
		return(del_if(inst, arg));
		break;
	    case MGR_DELLAYER | REQUEST:
		if (inst) {
			if (ctrl->inst.st == st) {
				DelIF(inst, &inst->down, contrL3L4, ctrl);
			} else {
				hisaxif_t hif;
				ncciSetInterface(&hif);
				DelIF(inst, &inst->down, hif.func, inst->data);
			}
		} else {
			printk(KERN_WARNING "capi20_manager DELLAYER no instance\n");
			return(-EINVAL);
		}
		capi_obj.ctrl(st, MGR_DELLAYER | REQUEST, inst);
		break;
	    case MGR_RELEASE | INDICATION:
	    	if (ctrl) {
			printk(KERN_DEBUG "release_capi20 id %x\n", ctrl->inst.st->id);
			contrDestr(ctrl);
			kfree(ctrl);
	    	} else 
	    		printk(KERN_WARNING "capi20_manager release no instance\n");
	    	break;
	    default:
		printk(KERN_WARNING "capi20_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

int Capi20Init(void)
{
	int err;

	capi_obj.name = MName;
	capi_obj.DPROTO.protocol[4] = ISDN_PID_L4_CAPI20;
	capi_obj.BPROTO.protocol[4] = ISDN_PID_L4_B_CAPI20;
	capi_obj.BPROTO.protocol[3] = ISDN_PID_L3_B_TRANS;
	capi_obj.own_ctrl = capi20_manager;
	capi_obj.prev = NULL;
	capi_obj.next = NULL;
	capi_obj.ilist = NULL;
	if ((err = CapiNew()))
		return(err);
	if ((err = HiSax_register(&capi_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
		detach_capi_driver(&hisax_driver);
		free_listen();
		free_cplci();
		free_ncci();
	}
	return(err);
}

#ifdef MODULE
void cleanup_module(void)
{
	int err;
	Contr_t *contr;

	if ((err = HiSax_unregister(&capi_obj))) {
		printk(KERN_ERR "Can't unregister User DSS1 error(%d)\n", err);
	}
	if (capi_obj.ilist) {
		printk(KERN_WARNING "hisaxl3 contrlist not empty\n");
		while((contr = capi_obj.ilist)) {
			contrDestr(contr);
			kfree(contr);
		}
	}
	detach_capi_driver(&hisax_driver);
	free_listen();
	free_cplci();
	free_ncci();
}
#endif
