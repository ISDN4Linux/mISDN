/* $Id: capi.c,v 0.1 2001/02/21 19:22:35 kkeil Exp $
 *
 */

#include <linux/module.h>
#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

const char *capi_revision = "$Revision: 0.1 $";

static int debug = 0;
static hisaxobject_t capi_obj;


static char MName[] = "HiSax Capi 2.0";

static int Capi20Protocols[] = { ISDN_PID_CAPI20
};
#define PROTOCOLCNT	(sizeof(Capi20Protocols)/sizeof(int))

#ifdef MODULE
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(debug, "1i");
#define Capi20Init init_module
#endif

// ---------------------------------------------------------------------------
// registration to kernelcapi

int hisax_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	Contr_t *contr = ctrl->driverdata;

	printk(KERN_INFO __FUNCTION__ "\n");
	contrLoadFirmware(contr);
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
	int_error();
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
	hisaxstack_t *st = data;
	Contr_t *ctrl = (Contr_t *)capi_obj.ilist;

//	printk(KERN_DEBUG "capi20_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	while(ctrl) {
		if (ctrl->inst.st == st)
			break;
		ctrl = ctrl->next;
	}
	switch(prim) {
	    case MGR_ADDIF | REQUEST:
		if (!ctrl)
			ctrl = newContr(&capi_obj, st, arg);
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager create_ctrl failed\n");
			return(-EINVAL);
		}
		return(add_if(ctrl, arg));
		break;
	    case MGR_DELIF | REQUEST:
		if (!ctrl) {
			printk(KERN_WARNING "capi20_manager delif no instance\n");
			return(-EINVAL);
		}
		return(del_if(ctrl, arg));
		break;
	    case MGR_RELEASE | INDICATION:
	    	if (ctrl) {
			printk(KERN_DEBUG "release_capi20 id %x\n", ctrl->inst.st->id);
	    		release_udss1(ctrl);
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
	capi_obj.protocols = Capi20Protocols;
	capi_obj.protcnt = PROTOCOLCNT;
	capi_obj.own_ctrl = capi20_manager;
	capi_obj.prev = NULL;
	capi_obj.next = NULL;
	capi_obj.ilist = NULL;
	capi_obj.layer = 4;
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
	Contr_t *contrlist = (Contr_t *)capi_obj.ilist;

	if ((err = HiSax_unregister(&capi_obj))) {
		printk(KERN_ERR "Can't unregister User DSS1 error(%d)\n", err);
	}
	if (contrlist) {
		printk(KERN_WARNING "hisaxl3 contrlist not empty\n");
		while(contrlist)
			release_contr(contrlist);
		capi_obj.ilist = NULL;
	}
	detach_capi_driver(&hisax_driver);
	free_listen();
	free_cplci();
	free_ncci();
}
#endif
