/* $Id: capi.c,v 0.9 2001/10/31 23:04:42 kkeil Exp $
 *
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

const char *capi_revision = "$Revision: 0.9 $";

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
capi20_manager(void *data, u_int prim, void *arg) {
	hisaxinstance_t *inst = data;
	int	found=0;
	BInst_t *binst = NULL;
	Contr_t *ctrl = (Contr_t *)capi_obj.ilist;

	printk(KERN_DEBUG "capi20_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	while(ctrl) {
		if (&ctrl->inst == inst) {
			found++;
			break;
		}
		binst = ctrl->binst;
		while(binst) {
			if (&binst->inst == inst) {
				found++;
				break;
			}
			binst = binst->next;
		}
		if (found)
			break;
		ctrl = ctrl->next;
	}
	switch(prim) {
	    case MGR_NEWLAYER | REQUEST:
	    	if (!(ctrl = newContr(&capi_obj, data, arg)))
	    		return(-EINVAL);
	        break;
	    case MGR_CONNECT | REQUEST:
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager connect no instance\n");
			return(-EINVAL);
		}
		return(ConnectIF(inst, arg));
		break;
	    case MGR_SETIF | INDICATION:
	    case MGR_SETIF | REQUEST:
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager setif no instance\n");
			return(-EINVAL);
		}
		if (&ctrl->inst == inst)
			return(SetIF(inst, arg, prim, NULL, contrL3L4, ctrl));
		else
			return(SetIF(inst, arg, prim, NULL, ncci_l3l4, inst->data));
		break;
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager disconnect no instance\n");
			return(-EINVAL);
		}
		return(DisConnectIF(inst, arg));
		break;
	    case MGR_RELEASE | INDICATION:
	    	if (ctrl) {
			printk(KERN_DEBUG "release_capi20 id %x\n", ctrl->inst.st->id);
			contrDestr(ctrl);
			kfree(ctrl);
	    	} else 
	    		printk(KERN_WARNING "capi20_manager release no instance\n");
	    	break;
	    case MGR_UNREGLAYER | REQUEST:
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager unreglayer no instance\n");
			return(-EINVAL);
		}
		if (binst) {
			capi_obj.ctrl(binst->inst.down.peer, MGR_DISCONNECT | REQUEST,
				&binst->inst.down);
			capi_obj.ctrl(&binst->inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
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
