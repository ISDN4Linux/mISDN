/* $Id: capi.c,v 1.14 2004/06/17 12:31:11 keil Exp $
 *
 */

#include <linux/module.h>
#include "m_capi.h"
#include "helper.h"
#include "debug.h"

static char *capi_revision = "$Revision: 1.14 $";

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

kmem_cache_t	*mISDN_cmsg_cp;
kmem_cache_t	*mISDN_AppPlci_cp;
kmem_cache_t	*mISDN_ncci_cp;
kmem_cache_t	*mISDN_sspc_cp;

#ifdef MISDN_KMEM_DEBUG
static struct list_head mISDN_kmem_garbage = LIST_HEAD_INIT(mISDN_kmem_garbage);

_cmsg *
_kd_cmsg_alloc(char *fn, int line)
{
	_kd_cmsg_t	*ki = kmem_cache_alloc(mISDN_cmsg_cp, GFP_ATOMIC);

	if (!ki)
		return(NULL);
	ki->kdi.typ = KM_DBG_TYP_CM;
	INIT_LIST_HEAD(&ki->kdi.head);
	ki->kdi.line = line;
	ki->kdi.file = fn;
	list_add_tail(&ki->kdi.head, &mISDN_kmem_garbage);
	return(&ki->cm);
}

void
cmsg_free(_cmsg *cm)
{
	km_dbg_item_t	*kdi;

	if (!cm) {
		int_errtxt("zero pointer free at %p", __builtin_return_address(0));
		return;
	}
	kdi = KDB_GET_KDI(cm);
	list_del(&kdi->head);
	kmem_cache_free(mISDN_cmsg_cp, kdi);
}

AppPlci_t *
_kd_AppPlci_alloc(char *fn, int line)
{
	_kd_AppPlci_t	*ki = kmem_cache_alloc(mISDN_AppPlci_cp, GFP_ATOMIC);

	if (!ki)
		return(NULL);
	ki->kdi.typ = KM_DBG_TYP_AP;
	INIT_LIST_HEAD(&ki->kdi.head);
	ki->kdi.line = line;
	ki->kdi.file = fn;
	list_add_tail(&ki->kdi.head, &mISDN_kmem_garbage);
	return(&ki->ap);
}

void
AppPlci_free(AppPlci_t *ap)
{
	km_dbg_item_t	*kdi;

	if (!ap) {
		int_errtxt("zero pointer free at %p", __builtin_return_address(0));
		return;
	}
	kdi = KDB_GET_KDI(ap);
	list_del(&kdi->head);
	kmem_cache_free(mISDN_AppPlci_cp, kdi);
}

Ncci_t *
_kd_ncci_alloc(char *fn, int line)
{
	_kd_Ncci_t	*ki = kmem_cache_alloc(mISDN_ncci_cp, GFP_ATOMIC);

	if (!ki)
		return(NULL);
	ki->kdi.typ = KM_DBG_TYP_NI;
	INIT_LIST_HEAD(&ki->kdi.head);
	ki->kdi.line = line;
	ki->kdi.file = fn;
	list_add_tail(&ki->kdi.head, &mISDN_kmem_garbage);
	return(&ki->ni);
}

void
ncci_free(Ncci_t *ni)
{
	km_dbg_item_t	*kdi;

	if (!ni) {
		int_errtxt("zero pointer free at %p", __builtin_return_address(0));
		return;
	}
	kdi = KDB_GET_KDI(ni);
	list_del(&kdi->head);
	kmem_cache_free(mISDN_ncci_cp, kdi);
}

SSProcess_t *
_kd_SSProcess_alloc(char *fn, int line)
{
	_kd_SSProcess_t	*ki = kmem_cache_alloc(mISDN_sspc_cp, GFP_ATOMIC);

	if (!ki)
		return(NULL);
	ki->kdi.typ = KM_DBG_TYP_SP;
	INIT_LIST_HEAD(&ki->kdi.head);
	ki->kdi.line = line;
	ki->kdi.file = fn;
	list_add_tail(&ki->kdi.head, &mISDN_kmem_garbage);
	return(&ki->sp);
}

void
SSProcess_free(SSProcess_t *sp)
{
	km_dbg_item_t	*kdi;

	if (!sp) {
		int_errtxt("zero pointer free at %p", __builtin_return_address(0));
		return;
	}
	kdi = KDB_GET_KDI(sp);
	list_del(&kdi->head);
	kmem_cache_free(mISDN_sspc_cp, kdi);
}

static void
free_garbage(void)
{
	struct list_head	*item, *next;
	_kd_all_t		*kda;

	list_for_each_safe(item, next, &mISDN_kmem_garbage) {
		kda = (_kd_all_t *)item;
		printk(KERN_DEBUG "garbage item found (%p <- %p -> %p) type%ld allocated at %s:%d\n",
			kda->kdi.head.prev, item, kda->kdi.head.next, kda->kdi.typ, kda->kdi.file, kda->kdi.line);
		list_del(item);
		switch(kda->kdi.typ) {
			case KM_DBG_TYP_CM:
				printk(KERN_DEBUG "cmsg cmd(%x,%x) appl(%x) addr(%x) nr(%d)\n",
					kda->a.cm.Command,
					kda->a.cm.Subcommand,
					kda->a.cm.ApplId,
					kda->a.cm.adr.adrController,
					kda->a.cm.Messagenumber);
				kmem_cache_free(mISDN_cmsg_cp, item);
				break;
			case KM_DBG_TYP_AP:
				printk(KERN_DEBUG "AppPlci: PLCI(%x) m.state(%x) appl(%p)\n",
					kda->a.ap.addr,
					kda->a.ap.plci_m.state,
					kda->a.ap.appl);
				kmem_cache_free(mISDN_AppPlci_cp, item);
				break;
			case KM_DBG_TYP_NI:
				printk(KERN_DEBUG "Ncci: NCCI(%x) state(%lx) m.state(%x) aplci(%p)\n",
					kda->a.ni.addr,
					kda->a.ni.state,
					kda->a.ni.ncci_m.state,
					kda->a.ni.AppPlci);
				kmem_cache_free(mISDN_ncci_cp, item);
				break;
			case KM_DBG_TYP_SP:
				printk(KERN_DEBUG "SSPc: addr(%x) id(%x) apid(%x) func(%x)\n",
					kda->a.sp.addr,
					kda->a.sp.invokeId,
					kda->a.sp.ApplId,
					kda->a.sp.Function);
				kmem_cache_free(mISDN_sspc_cp, item);
				break;
			default:
				printk(KERN_DEBUG "unknown garbage item(%p) type %ld\n",
					item, kda->kdi.typ);
				break; 
		}
	}
}

#endif

static void CapiCachesFree(void)
{
#ifdef MISDN_KMEM_DEBUG
	free_garbage();
#endif
	if (mISDN_cmsg_cp) {
		kmem_cache_destroy(mISDN_cmsg_cp);
		mISDN_cmsg_cp = NULL;
	}
	if (mISDN_AppPlci_cp) {
		kmem_cache_destroy(mISDN_AppPlci_cp);
		mISDN_AppPlci_cp = NULL;
	}
	if (mISDN_ncci_cp) {
		kmem_cache_destroy(mISDN_ncci_cp);
		mISDN_ncci_cp = NULL;
	}
	if (mISDN_sspc_cp) {
		kmem_cache_destroy(mISDN_sspc_cp);
		mISDN_sspc_cp = NULL;
	}
}

static int CapiNew(void)
{
	mISDN_cmsg_cp = NULL;
	mISDN_AppPlci_cp = NULL;
	mISDN_ncci_cp = NULL;
	mISDN_sspc_cp = NULL;
	mISDN_cmsg_cp = kmem_cache_create("mISDN_cmesg",
#ifdef MISDN_KMEM_DEBUG
				sizeof(_kd_cmsg_t),
#else
				sizeof(_cmsg),
#endif
				0, 0, NULL, NULL);
	if (!mISDN_cmsg_cp) {
		CapiCachesFree();
		return(-ENOMEM);
	}
	mISDN_AppPlci_cp = kmem_cache_create("mISDN_AppPlci",
#ifdef MISDN_KMEM_DEBUG
				sizeof(_kd_AppPlci_t),
#else
				sizeof(AppPlci_t),
#endif
				0, 0, NULL, NULL);
	if (!mISDN_AppPlci_cp) {
		CapiCachesFree();
		return(-ENOMEM);
	}
	mISDN_ncci_cp = kmem_cache_create("mISDN_Ncci",
#ifdef MISDN_KMEM_DEBUG
				sizeof(_kd_Ncci_t),
#else
				sizeof(Ncci_t),
#endif
				0, 0, NULL, NULL);
	if (!mISDN_ncci_cp) {
		CapiCachesFree();
		return(-ENOMEM);
	}
	mISDN_sspc_cp = kmem_cache_create("mISDN_SSProc",
#ifdef MISDN_KMEM_DEBUG
				sizeof(_kd_SSProcess_t),
#else
				sizeof(SSProcess_t),
#endif
				0, 0, NULL, NULL);
	if (!mISDN_sspc_cp) {
		CapiCachesFree();
		return(-ENOMEM);
	}
#ifdef OLDCAPI_DRIVER_INTERFACE
	cdrv_if = attach_capi_driver(&mISDN_driver);
	if (!cdrv_if) {
		CapiCachesFree();
		printk(KERN_ERR "mISDN: failed to attach capi_driver\n");
		return -EIO;
	}
#endif
	init_listen();
	init_AppPlci();
	init_ncci();
	return 0;
}

static int
capi20_manager(void *data, u_int prim, void *arg) {
	mISDNinstance_t	*inst = data;
	int		found=0;
	PLInst_t	*plink = NULL;
	Controller_t	*ctrl;

	if (CAPI_DBG_INFO & debug)
		printk(KERN_DEBUG "capi20_manager data:%p prim:%x arg:%p\n", data, prim, arg);
	if (!data)
		return(-EINVAL);
	list_for_each_entry(ctrl, &capi_obj.ilist, list) {
		if (&ctrl->inst == inst) {
			found++;
			break;
		}
		list_for_each_entry(plink, &ctrl->linklist, list) {
			if (&plink->inst == inst) {
				found++;
				break;
			}
		}
		if (found)
			break;
		plink = NULL;
	}
	if (&ctrl->list == &capi_obj.ilist)
		ctrl = NULL;
	if (prim == (MGR_NEWLAYER | REQUEST)) {
		int ret = ControllerConstr(&ctrl, data, arg, &capi_obj);
		if (!ret)
			ctrl->debug = debug;
		return(ret);
	}
	if (!ctrl) {
		if (CAPI_DBG_WARN & debug)
			printk(KERN_WARNING "capi20_manager setif no instance\n");
		return(-EINVAL);
	}
	switch(prim) {
	    case MGR_NEWENTITY | CONFIRM:
		ctrl->entity = (int)arg;
		break;
	    case MGR_CONNECT | REQUEST:
		return(mISDN_ConnectIF(inst, arg));
	    case MGR_SETIF | INDICATION:
	    case MGR_SETIF | REQUEST:
		if (&ctrl->inst == inst)
			return(mISDN_SetIF(inst, arg, prim, NULL, ControllerL3L4, ctrl));
		else
			return(AppPlcimISDN_SetIF(inst->data, prim, arg));
	    case MGR_DISCONNECT | REQUEST:
	    case MGR_DISCONNECT | INDICATION:
		return(mISDN_DisConnectIF(inst, arg));
	    case MGR_RELEASE | INDICATION:
		if (CAPI_DBG_INFO & debug)
			printk(KERN_DEBUG "release_capi20 id %x\n", ctrl->inst.st->id);
		ControllerDestr(ctrl);
	    	break;
	    case MGR_UNREGLAYER | REQUEST:
		if (plink) {
			capi_obj.ctrl(plink->inst.down.peer, MGR_DISCONNECT | REQUEST,
				&plink->inst.down);
			capi_obj.ctrl(&plink->inst, MGR_UNREGLAYER | REQUEST, NULL);
		}
		break;
	    case MGR_CTRLREADY | INDICATION:
		if (CAPI_DBG_INFO & debug)
			printk(KERN_DEBUG "ctrl %x ready\n", ctrl->inst.st->id);
		ControllerRun(ctrl);
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
#ifdef MODULE
	capi_obj.owner = THIS_MODULE;
#endif
	capi_obj.name = MName;
	capi_obj.DPROTO.protocol[4] = ISDN_PID_L4_CAPI20;
	capi_obj.BPROTO.protocol[4] = ISDN_PID_L4_B_CAPI20;
	capi_obj.BPROTO.protocol[3] = ISDN_PID_L3_B_TRANS;
	capi_obj.own_ctrl = capi20_manager;
	INIT_LIST_HEAD(&capi_obj.ilist);
	if ((err = CapiNew()))
		return(err);
	if ((err = mISDN_register(&capi_obj))) {
		printk(KERN_ERR "Can't register %s error(%d)\n", MName, err);
#ifdef OLDCAPI_DRIVER_INTERFACE
		detach_capi_driver(&mISDN_driver);
#endif
		CapiCachesFree();
		free_listen();
		free_AppPlci();
		free_ncci();
		free_Application();
	}
	return(err);
}

#ifdef MODULE
static void Capi20cleanup(void)
{
	int		err;
	Controller_t	*contr, *next;

	if ((err = mISDN_unregister(&capi_obj))) {
		printk(KERN_ERR "Can't unregister CAPI20 error(%d)\n", err);
	}
	if (!list_empty(&capi_obj.ilist)) {
		printk(KERN_WARNING "mISDN controller list not empty\n");
		list_for_each_entry_safe(contr, next, &capi_obj.ilist, list)
			ControllerDestr(contr);
	}
#ifdef OLDCAPI_DRIVER_INTERFACE
	detach_capi_driver(&mISDN_driver);
#endif
	free_Application();
	CapiCachesFree();
	free_listen();
	free_AppPlci();
	free_ncci();
}

module_init(Capi20Init);
module_exit(Capi20cleanup);
#endif
