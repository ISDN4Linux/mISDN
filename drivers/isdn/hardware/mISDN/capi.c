/* $Id: capi.c,v 1.7 2003/07/28 12:05:47 kkeil Exp $
 *
 */

#include <linux/module.h>
#include "capi.h"
#include "helper.h"
#include "debug.h"

static char *capi_revision = "$Revision: 1.7 $";

static int debug = 0;
static mISDNobject_t capi_obj;

static char MName[] = "mISDN Capi 2.0";

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
MODULE_PARM(debug, "1i");
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

#ifdef OLDCAPI_DRIVER_INTERFACE
struct capi_driver_interface *cdrv_if;
#endif

int CapiNew(void)
{
#ifdef OLDCAPI_DRIVER_INTERFACE
	cdrv_if = attach_capi_driver(&mISDN_driver);
	if (!cdrv_if) {
		printk(KERN_ERR "mISDN: failed to attach capi_driver\n");
		return -EIO;
	}
#endif
	init_listen();
	init_cplci();
	init_ncci();
	return 0;
}

static int
capi20_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t	*inst = data;
	int		found=0;
	BInst_t		*binst = NULL;
	Contr_t		*ctrl = (Contr_t *)capi_obj.ilist;

	if (CAPI_DBG_INFO & debug)
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
	    	else
	    		ctrl->debug = debug;
	        break;
	    case MGR_CONNECT | REQUEST:
		if (!ctrl) {
			if (CAPI_DBG_WARN & debug)
				printk(KERN_WARNING "capi20_manager connect no instance\n");
			return(-EINVAL);
		}
		return(ConnectIF(inst, arg));
		break;
	    case MGR_SETIF | INDICATION:
	    case MGR_SETIF | REQUEST:
		if (!ctrl) {
			if (CAPI_DBG_WARN & debug)
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
			if (CAPI_DBG_WARN & debug)
				printk(KERN_WARNING "capi20_manager disconnect no instance\n");
			return(-EINVAL);
		}
		return(DisConnectIF(inst, arg));
		break;
	    case MGR_RELEASE | INDICATION:
	    	if (ctrl) {
	    		if (CAPI_DBG_INFO & debug)
				printk(KERN_DEBUG "release_capi20 id %x\n", ctrl->inst.st->id);
			contrDestr(ctrl);
			kfree(ctrl);
	    	} else if (CAPI_DBG_WARN & debug)
	    		printk(KERN_WARNING "capi20_manager release no instance\n");
	    	break;
	    case MGR_UNREGLAYER | REQUEST:
		if (!ctrl) {
			if (CAPI_DBG_WARN & debug)
				printk(KERN_WARNING "capi20_manager unreglayer no instance\n");
			return(-EINVAL);
		}
		if (binst) {
			capi_obj.ctrl(binst->inst.down.peer, MGR_DISCONNECT | REQUEST,
				&binst->inst.down);
			capi_obj.ctrl(&binst->inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		break;
	    case MGR_CTRLREADY | INDICATION:
		if (ctrl) {
			if (CAPI_DBG_INFO & debug)
				printk(KERN_DEBUG "ctrl %x ready\n", ctrl->inst.st->id);
			contrRun(ctrl);
		} else {
			if (CAPI_DBG_WARN & debug)
				printk(KERN_WARNING "ctrl ready no instance\n");
			return(-EINVAL);
		}
		break;
	    default:
	    	if (CAPI_DBG_WARN & debug)
			printk(KERN_WARNING "capi20_manager prim %x not handled\n", prim);
		return(-EINVAL);
	}
	return(0);
}

int Capi20Init(void)
{
	int err;

	printk(KERN_INFO "%s driver file version %s\n", MName, mISDN_getrev(capi_revision));
	SET_MODULE_OWNER(&capi_obj);
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
	if ((err = mISDN_register(&capi_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
#ifdef OLDCAPI_DRIVER_INTERFACE
		detach_capi_driver(&mISDN_driver);
#endif
		free_listen();
		free_cplci();
		free_ncci();
	}
	return(err);
}

#ifdef MODULE
static void Capi20cleanup(void)
{
	int err;
	Contr_t *contr;

	if ((err = mISDN_unregister(&capi_obj))) {
		printk(KERN_ERR "Can't unregister User DSS1 error(%d)\n", err);
	}
	if (capi_obj.ilist) {
		printk(KERN_WARNING "mISDNl3 contrlist not empty\n");
		while((contr = capi_obj.ilist)) {
			contrDestr(contr);
			kfree(contr);
		}
	}
#ifdef OLDCAPI_DRIVER_INTERFACE
	detach_capi_driver(&mISDN_driver);
#endif
	free_listen();
	free_cplci();
	free_ncci();
}

module_init(Capi20Init);
module_exit(Capi20cleanup);
#endif
