/* $Id: m_capi.h,v 1.11 2004/06/17 12:31:12 keil Exp $
 *
 * Rewritten CAPI Layer (Layer4 in mISDN)
 * 
 * The CAPI layer knows following basic Objects
 *
 *  - Controller_t  : the contoller instance
 *  - Application_t : applications object
 *  - Plci_t        : PLCI object
 *  - AppPlci_t     : per application PLCI object
 *  - Ncci_t	    : NCCI object
 *
 * For Supplementary Services
 *  - SSProcess_t   : a process handling a service request
 *
 *  The controller is a Layer4 (D-channel) stack instance of
 *  mISDN.
 *
 *  Applications are owned by the controller and only
 *  handle this controller, multiplexing multiple
 *  controller with one application is done in the higher
 *  driver independ CAPI driver. The application contain
 *  the Listen state machine.
 *
 *  Plcis are owned by the controller and are static (allocated
 *  together with the controller). They maybe in use or inactiv.
 *  Currently 8 PLCIs are available on a BRI (2 B-channel) controller
 *  and 40 on a PRI (30 B-channel). They have a list of the associated
 *  application PLCIs.
 *  
 *  AppPlcis are owned by the application and are
 *  instance of the PLCI per application. They contain the
 *  CAPI2.0 PCLI state machine
 * 
 *  Nccis are owned by the application Plcis. In the first version
 *  this driver supports only one NCCI per PLCI.
 *
 *
 */

#ifndef __mISDN_CAPI_H__
#define __mISDN_CAPI_H__

#include <linux/mISDNif.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#ifdef OLDCAPI_DRIVER_INTERFACE
#include "../avmb1/capiutil.h"
#include "../avmb1/capicmd.h"
#include "../avmb1/capilli.h"
#else
#include <linux/list.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capilli.h>
#endif
#include "asn1.h"
#include "fsm.h"
#ifdef MISDN_MEMDEBUG
#include "memdbg.h"
#define MISDN_KMEM_DEBUG		1
#endif

// ---------------------------------------------------------------------------
// common stuff
//     debuging levels and functions
// ---------------------------------------------------------------------------

#define CAPI_DBG_WARN		0x00000001
#define CAPI_DBG_INFO		0x00000004
#define CAPI_DBG_APPL		0x00000010
#define CAPI_DBG_APPL_INFO	0x00000040
#define CAPI_DBG_APPL_MSG	0x00000080
#define CAPI_DBG_LISTEN		0x00000100
#define CAPI_DBG_LISTEN_STATE	0x00000200
#define CAPI_DBG_LISTEN_INFO	0x00000400
#define CAPI_DBG_CONTR		0x00010000
#define CAPI_DBG_CONTR_INFO	0x00040000
#define CAPI_DBG_CONTR_MSG	0x00080000
#define CAPI_DBG_PLCI		0x00100000
#define CAPI_DBG_PLCI_STATE	0x00200000
#define CAPI_DBG_PLCI_INFO	0x00400000
#define CAPI_DBG_PLCI_L3	0x00800000
#define CAPI_DBG_NCCI		0x01000000
#define CAPI_DBG_NCCI_STATE	0x02000000
#define CAPI_DBG_NCCI_INFO	0x04000000
#define CAPI_DBG_NCCI_L3	0x08000000
void capidebug(int, char *, ...);

#ifdef OLDCAPI_DRIVER_INTERFACE
extern struct capi_driver_interface *cdrv_if;                  
extern struct capi_driver mISDN_driver;
#endif

// ---------------------------------------------------------------------------
// Init/Exit functions
// ---------------------------------------------------------------------------

void init_listen(void);
void init_AppPlci(void);
void init_ncci(void);
void free_Application(void);
void free_listen(void);
void free_AppPlci(void);
void free_ncci(void);

// ---------------------------------------------------------------------------
// More CAPI defines
// ---------------------------------------------------------------------------

/* we implement 64 bit extentions */
#define CAPI_B3_DATA_IND_HEADER_SIZE	30
#define CAPI_MSG_DEFAULT_LEN		256

#define CAPIMSG_REQ_DATAHANDLE(m)	(m[18] | (m[19]<<8))
#define CAPIMSG_RESP_DATAHANDLE(m)	(m[12] | (m[13]<<8))

#define CMSGCMD(cmsg)			CAPICMD((cmsg)->Command, (cmsg)->Subcommand)

#define CAPI_MAXPLCI_BRI		8
#define CAPI_MAXPLCI_PRI		40

__u16 q931CIPValue(Q931_info_t *);

// ---------------------------------------------------------------------------
// Basic CAPI types
// ---------------------------------------------------------------------------

typedef struct _Controller	Controller_t;
typedef struct _Application	Application_t;
typedef struct _Ncci 		Ncci_t;
typedef struct _Plci		Plci_t;
typedef struct _AppPlci		AppPlci_t;
typedef struct _SSProcess	SSProcess_t;

// some helper types
typedef struct _ConfQueue	ConfQueue_t;
typedef struct _PLInst		PLInst_t;

// Facility types
typedef struct FacReqParm	FacReqParm_t;
typedef struct FacConfParm	FacConfParm_t;

// ---------------------------------------------------------------------------
// Helper structs
// ---------------------------------------------------------------------------

struct _PLInst {
	struct list_head	list;
	u_int			state;
	mISDNstack_t		*st;
	mISDNinstance_t		inst;
};

struct _ConfQueue { 
	__u32	PktId; 
	__u16	DataHandle;
	__u16	MsgId; 
};

struct Bprotocol {
	__u16	B1;
	__u16	B2;
	__u16	B3;
	__u8	B1cfg[16];
	__u8	B2cfg[16];
	__u8	B3cfg[80];
};

// ---------------------------------------------------------------------------
// struct Controller
// ---------------------------------------------------------------------------

struct _Controller {
	struct list_head	list;
	mISDNinstance_t		inst;
	int			nr_bc;
	struct list_head	linklist;
	struct capi_ctr		*ctrl;
	__u32			addr;
	int			entity;
	int			next_id;
	spinlock_t		id_lock;
	u_int			debug;
	int			maxplci;
	Plci_t			*plcis;
	struct list_head	Applications;
	struct list_head	SSProcesse;
	spinlock_t		list_lock;
	__u32			NotificationMask;
	__u16			LastInvokeId;
	char			infobuf[128];
};

// ---------------------------------------------------------------------------
// struct Application
// ---------------------------------------------------------------------------

struct _Application {
	struct list_head	head;
	Controller_t		*contr;
	__u16			ApplId;
	__u16			MsgId;
	__u32			InfoMask;
	__u32			CIPmask;
	__u32			CIPmask2;
	__u32			NotificationMask;
	u_long			state;
	struct FsmInst		listen_m;
	int			maxplci;
	AppPlci_t		**AppPlcis;
	capi_register_params	reg_params;
};

#define APPL_STATE_ACTIV	1
#define APPL_STATE_RELEASE	2
#define APPL_STATE_LISTEN	3
#define APPL_STATE_DESTRUCTOR	4
#define APPL_STATE_D2TRACE	8

// ---------------------------------------------------------------------------
// struct Plci
// ---------------------------------------------------------------------------

struct _Plci {
	Controller_t		*contr;
	__u32			addr;
	__u32			l3id;
	u_long			state;
	int			nAppl;
	struct list_head	AppPlcis;
};

#define PLCI_STATE_ACTIV	1
#define PLCI_STATE_ALERTING	2
#define PLCI_STATE_OUTGOING	3
#define PLCI_STATE_STACKREADY	4
#define PLCI_STATE_SENDDELAYED	5

// ---------------------------------------------------------------------------
// struct AppPlci
// ---------------------------------------------------------------------------

struct _AppPlci {
	struct list_head	head;
	__u32			addr;
	Plci_t			*plci;
	Application_t		*appl;
	Controller_t		*contr;
	PLInst_t		*link;
	struct sk_buff_head	delayedq;
	struct list_head	Nccis;
	struct FsmInst		plci_m;
	u_char			cause[4];
	int			channel;
	struct Bprotocol	Bprotocol;
};

// ---------------------------------------------------------------------------
// struct Ncci
// ---------------------------------------------------------------------------

struct _Ncci {
	struct list_head	head;
	__u32			addr;
	PLInst_t		*link;
	Controller_t		*contr;
	AppPlci_t		*AppPlci;
	Application_t		*appl;
	struct FsmInst		ncci_m;
	int			savedstate;
	int			window;
	u_long			state;
	ConfQueue_t		xmit_skb_handles[CAPI_MAXDATAWINDOW];
	struct sk_buff		*recv_skb_handles[CAPI_MAXDATAWINDOW];
	struct sk_buff_head	squeue;
};

#define NCCI_STATE_FCTRL	1
#define NCCI_STATE_BUSY		2
#define NCCI_STATE_L3TRANS	3
#define	NCCI_STATE_APPLRELEASED	4

// ---------------------------------------------------------------------------
// struct SSProcess_t
// ---------------------------------------------------------------------------

struct _SSProcess {
	struct list_head	head;
	__u16			invokeId;
	__u16			Function;  
	__u32			Handle;
	__u32			addr;
	__u16			ApplId;
	Controller_t		*contr;
	struct timer_list	tl;
	__u8			buf[128];
};

// ---------------------------------------------------------------------------
// FUNCTION prototypes
//
// Controller prototypes
// ---------------------------------------------------------------------------

int		ControllerConstr(Controller_t **, mISDNstack_t *, mISDN_pid_t *, mISDNobject_t *);
void		ControllerDestr(Controller_t *);
void		ControllerRun(Controller_t *);
void		ControllerDebug(Controller_t *, __u32, char *, ...);
int		ControllerNewPlci(Controller_t *, Plci_t  **, u_int);
int		ControllerReleasePlci(Plci_t *);
Application_t	*getApplication4Id(Controller_t *, __u16);
Plci_t		*getPlci4Addr(Controller_t *, __u32);
int		ControllerL4L3(Controller_t *, u_int, int, struct sk_buff *);
int		ControllerL3L4(mISDNif_t *, struct sk_buff *);
PLInst_t	*ControllerSelChannel(Controller_t *, u_int);
void		ControllerAddSSProcess(Controller_t *, SSProcess_t *);
SSProcess_t	*getSSProcess4Id(Controller_t *, __u16);
int		ControllerNextId(Controller_t *);

// ---------------------------------------------------------------------------
// Application prototypes
// ---------------------------------------------------------------------------

int		ApplicationConstr(Controller_t *, __u16, capi_register_params *);
int		ApplicationDestr(Application_t *, int);
void		ApplicationDebug(Application_t *appl, __u32 level, char *fmt, ...);
__u16		ApplicationSendMessage(Application_t *appl, struct sk_buff *skb);
void		SendCmsg2Application(Application_t *, _cmsg *);
void		SendCmsgAnswer2Application(Application_t *, _cmsg *, __u16);
void		AnswerMessage2Application(Application_t *, struct sk_buff *, __u16);
void		applManufacturerReq(Application_t *appl, struct sk_buff *skb);
void		applD2Trace(Application_t *appl, u_char *buf, int len);
AppPlci_t	*ApplicationNewAppPlci(Application_t *, Plci_t *);
AppPlci_t	*getAppPlci4addr(Application_t *, __u32);
void		ApplicationDelAppPlci(Application_t *, AppPlci_t *);

void		listenConstr(Application_t *);
void		listenDestr(Application_t *);
__u16		listenSendMessage(Application_t *, struct sk_buff *);
int		listenHandle(Application_t *, __u16);

// ---------------------------------------------------------------------------
// PLCI prototypes
// ---------------------------------------------------------------------------

void	plciInit(Controller_t *);
void	plciDebug(Plci_t *, __u32, char *, ...);
int	plci_l3l4(Plci_t *, int, struct sk_buff *);
void	plciAttachAppPlci(Plci_t *, AppPlci_t *);
void	plciDetachAppPlci(Plci_t *, AppPlci_t *);
void	plciNewCrInd(Plci_t *, void *);
void	plciNewCrReq(Plci_t *);
int	plciL4L3(Plci_t *, __u32, struct sk_buff *);

// ---------------------------------------------------------------------------
// AppPLCI prototypes
// ---------------------------------------------------------------------------

int	AppPlciConstr(AppPlci_t **, Application_t *, Plci_t *);
void	AppPlciDestr(AppPlci_t *);
void 	AppPlciDelNCCI(Ncci_t *);
void 	AppPlci_l3l4(AppPlci_t *, int, void *);
__u16 	AppPlciSendMessage(AppPlci_t *, struct sk_buff *);
void	AppPlciRelease(AppPlci_t *);
int	AppPlciFacSuspendReq(AppPlci_t *, FacReqParm_t *, FacConfParm_t *);
int	AppPlciFacResumeReq(AppPlci_t *, FacReqParm_t *, FacConfParm_t *);
void	AppPlciGetCmsg(AppPlci_t *, _cmsg *);
Ncci_t	*getNCCI4addr(AppPlci_t *, __u32, int);
int	ConnectB3Request(AppPlci_t *, struct sk_buff *);
int	AppPlcimISDN_SetIF(AppPlci_t *, u_int, void *);

#define	GET_NCCI_EXACT		1
#define GET_NCCI_ONLY_PLCI	2
#define GET_NCCI_PLCI		3

// ---------------------------------------------------------------------------
// NCCI prototypes
// ---------------------------------------------------------------------------

Ncci_t	*ncciConstr(AppPlci_t *);
void	ncciDestr(Ncci_t *);
void	ncciApplRelease(Ncci_t *);
void	ncciDelAppPlci(Ncci_t *);
__u16	ncciSendMessage(Ncci_t *, struct sk_buff *);
int	ncci_l3l4(Ncci_t *, mISDN_head_t *, struct sk_buff *);
void	ncciGetCmsg(Ncci_t *, _cmsg *);
int	ncci_l3l4_direct(Ncci_t *, mISDN_head_t *, struct sk_buff *);
void	ncciReleaseLink(Ncci_t *);

// ---------------------------------------------------------------------------
//  SSProcess prototypes
// ---------------------------------------------------------------------------

SSProcess_t	*SSProcessConstr(Application_t *, __u16, __u32);
void		SSProcessDestr(SSProcess_t *);
int		Supplementary_l3l4(Controller_t *, __u32, struct sk_buff *);
void		SupplementaryFacilityReq(Application_t *, _cmsg *);

// ---------------------------------------------------------------------------
// INFOMASK defines (LISTEN commands)
// ---------------------------------------------------------------------------

#define CAPI_INFOMASK_CAUSE     (0x0001)
#define CAPI_INFOMASK_DATETIME  (0x0002)
#define CAPI_INFOMASK_DISPLAY   (0x0004)
#define CAPI_INFOMASK_USERUSER  (0x0008)
#define CAPI_INFOMASK_PROGRESS  (0x0010)
#define CAPI_INFOMASK_FACILITY  (0x0020)
//#define CAPI_INFOMASK_CHARGE    (0x0040)
//#define CAPI_INFOMASK_CALLEDPN  (0x0080)
#define CAPI_INFOMASK_CHANNELID (0x0100)
#define CAPI_INFOMASK_EARLYB3   (0x0200)
//#define CAPI_INFOMASK_REDIRECT  (0x0400)

// ---------------------------------------------------------------------------
// Supplementary Services
// ---------------------------------------------------------------------------

#define SuppServiceTP			0x00000002
#define SuppServiceCF			0x00000010
#define mISDNSupportedServices		(SuppServiceCF | SuppServiceTP)

// ---------------------------------------------------------------------------
// structs for Facillity requests
// ---------------------------------------------------------------------------

struct FacReqListen {
	__u32 NotificationMask;
};

struct FacReqSuspend {
	__u8 *CallIdentity;
};

struct FacReqResume {
	__u8 *CallIdentity;
};

struct FacReqCFActivate {
	__u32 Handle;
	__u16 Procedure;
	__u16 BasicService;
	__u8  *ServedUserNumber;
	__u8  *ForwardedToNumber;
	__u8  *ForwardedToSubaddress;
};

struct FacReqCFDeactivate {
	__u32 Handle;
	__u16 Procedure;
	__u16 BasicService;
	__u8  *ServedUserNumber;
};

#define FacReqCFInterrogateParameters FacReqCFDeactivate

struct FacReqCFInterrogateNumbers {
	__u32 Handle;
};

struct FacReqParm {
	__u16 Function;
	union {
		struct FacReqListen Listen;
		struct FacReqSuspend Suspend;
		struct FacReqResume Resume;
		struct FacReqCFActivate CFActivate;
		struct FacReqCFDeactivate CFDeactivate;
		struct FacReqCFInterrogateParameters CFInterrogateParameters;
		struct FacReqCFInterrogateNumbers CFInterrogateNumbers;
	} u;
};

// ---------------------------------------------------------------------------
// structs for Facillity confirms
// ---------------------------------------------------------------------------

struct FacConfGetSupportedServices {
	__u16 SupplementaryServiceInfo;
	__u32 SupportedServices;
};

struct FacConfInfo {
	__u16 SupplementaryServiceInfo;
};

struct FacConfParm {
	__u16 Function;
	union {
		struct FacConfGetSupportedServices GetSupportedServices;
		struct FacConfInfo Info;
	} u;
};

int capiEncodeWord(__u8 *dest, __u16 i);
int capiEncodeDWord(__u8 *dest, __u32 i);
int capiEncodeFacIndCFact(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle);
int capiEncodeFacIndCFdeact(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle);
int capiEncodeFacIndCFNotAct(__u8 *dest, struct ActDivNotification *actNot);
int capiEncodeFacIndCFNotDeact(__u8 *dest, struct DeactDivNotification *deactNot);
int capiEncodeFacIndCFinterParameters(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle, 
				      struct IntResultList *intResultList);
int capiEncodeFacIndCFinterNumbers(__u8 *dest, __u16 SupplementaryServiceReason, __u32 Handle, 
				   struct ServedUserNumberList *list);
int capiEncodeFacConfParm(__u8 *dest, struct FacConfParm *facConfParm);

int capiEncodeFacIndSuspend(__u8 *dest, __u16  SupplementaryServiceReason);

// ---------------------------------------------------------------------------
// mISDN kmem cache managment functions
// ---------------------------------------------------------------------------

/* kmem caches */
extern kmem_cache_t	*mISDN_cmsg_cp;
extern kmem_cache_t	*mISDN_AppPlci_cp;
extern kmem_cache_t	*mISDN_ncci_cp;
extern kmem_cache_t	*mISDN_sspc_cp;

#ifdef MISDN_KMEM_DEBUG
typedef struct _kd_cmsg		_kd_cmsg_t;
typedef struct _kd_Ncci		_kd_Ncci_t;
typedef struct _kd_AppPlci	_kd_AppPlci_t;
typedef struct _kd_SSProcess	_kd_SSProcess_t;
typedef struct _kd_all		_kd_all_t;

typedef	struct __km_dbg_item {
	struct list_head	head;
	long			typ;
	char			*file;
	u_int			line;
} km_dbg_item_t;

struct _kd_cmsg {
	km_dbg_item_t		kdi;
	_cmsg			cm;
};

struct _kd_AppPlci {
	km_dbg_item_t		kdi;
	AppPlci_t		ap;
};

struct _kd_Ncci {
	km_dbg_item_t		kdi;
	Ncci_t			ni;
};

struct _kd_SSProcess {
	km_dbg_item_t		kdi;
	SSProcess_t		sp;
};

struct _kd_all {
	km_dbg_item_t		kdi;
	union {
		_cmsg		cm;
		AppPlci_t	ap;
		Ncci_t		ni;
		SSProcess_t	sp;
	} a;
};

#define KDB_GET_KDI(kd)		((km_dbg_item_t *)(((u_char *)kd) - sizeof(km_dbg_item_t)))
#define KDB_GET_KDALL(kd)	((_kd_all_t *)(((u_char *)kd) - sizeof(km_dbg_item_t)))

#define KM_DBG_TYP_CM		1
#define KM_DBG_TYP_AP		2
#define KM_DBG_TYP_NI		3
#define KM_DBG_TYP_SP		4

#define cmsg_alloc()		_kd_cmsg_alloc(__FILE__, __LINE__)
extern _cmsg			*_kd_cmsg_alloc(char *, int);
extern void			cmsg_free(_cmsg *cm);

#define AppPlci_alloc()		_kd_AppPlci_alloc(__FILE__, __LINE__)
extern AppPlci_t		*_kd_AppPlci_alloc(char *, int);
extern void			AppPlci_free(AppPlci_t *ap);

#define ncci_alloc()		_kd_ncci_alloc(__FILE__, __LINE__)
extern Ncci_t			*_kd_ncci_alloc(char *, int);
extern void			ncci_free(Ncci_t *ni);

#define SSProcess_alloc()	_kd_SSProcess_alloc(__FILE__, __LINE__)
extern SSProcess_t		*_kd_SSProcess_alloc(char *, int);
extern void			SSProcess_free(SSProcess_t *sp);

#else /* ! MISDN_KMEM_DEBUG */

static __inline__ _cmsg *cmsg_alloc(void)
{
	return(kmem_cache_alloc(mISDN_cmsg_cp, GFP_ATOMIC));
}

static __inline__ void cmsg_free(_cmsg *cm)
{
	kmem_cache_free(mISDN_cmsg_cp, cm);
}

static __inline__ AppPlci_t *AppPlci_alloc(void)
{
	return(kmem_cache_alloc(mISDN_AppPlci_cp, GFP_ATOMIC));
}

static __inline__ void AppPlci_free(AppPlci_t *ap)
{
	kmem_cache_free(mISDN_AppPlci_cp, ap);
}

static __inline__ Ncci_t *ncci_alloc(void)
{
	return(kmem_cache_alloc(mISDN_ncci_cp, GFP_ATOMIC));
}

static __inline__ void ncci_free(Ncci_t *ni)
{
	kmem_cache_free(mISDN_ncci_cp, ni);
}

static __inline__ SSProcess_t *SSProcess_alloc(void)
{
	return(kmem_cache_alloc(mISDN_sspc_cp, GFP_ATOMIC));
}

static __inline__ void SSProcess_free(SSProcess_t *sp)
{
	kmem_cache_free(mISDN_sspc_cp, sp);
}

#endif /* MISDN_KMEM_DEBUG */
// cmsg_alloc with error handling for void functions
#define CMSG_ALLOC(cm)	if (!(cm = cmsg_alloc())) {int_error();return;}

#endif
