/* $Id: capi.h,v 1.1 2001/11/14 10:41:26 kkeil Exp $
 *
 */

#ifndef __HISAX_CAPI_H__
#define __HISAX_CAPI_H__

#include <linux/hisaxif.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include "../avmb1/capiutil.h"
#include "../avmb1/capicmd.h"
#include "../avmb1/capilli.h"
#include "asn1.h"
#include "fsm.h"
#ifdef MEMDBG
#include "memdbg.h"
#endif

// ---------------------------------------------------------------------------
// common stuff

#define LL_DEB_INFO		0x0001
#define LL_DEB_STATE		0x0002
#define LL_DEB_BUFFERING	0x0004
#define LL_DEB_WARN		0x0008

extern struct capi_driver_interface *cdrv_if;                  
extern struct capi_driver hisax_driver;

void init_listen(void);
void init_cplci(void);
void init_ncci(void);
void free_listen(void);
void free_cplci(void);
void free_ncci(void);
void capidebug(int, char *, ...);

#define SuppServiceCF          0x00000010
#define SuppServiceTP          0x00000002
#define HiSaxSupportedServices (SuppServiceCF | SuppServiceTP)

#define CAPIMSG_REQ_DATAHANDLE(m)	(m[18] | (m[19]<<8))
#define CAPIMSG_RESP_DATAHANDLE(m)	(m[12] | (m[13]<<8))

#define CMSGCMD(cmsg) CAPICMD((cmsg)->Command, (cmsg)->Subcommand)

#define CAPI_MAXPLCI 5
#define CAPI_MAXDUMMYPCS 16

struct Bprotocol {
	__u16 B1protocol;
	__u16 B2protocol;
	__u16 B3protocol;
};

__u16 q931CIPValue(SETUP_t *);

typedef struct _DummyProcess {
	__u16	invokeId;
	__u16	Function;  
	__u32	Handle;
	__u32	adrDummy;
	__u16	ApplId;
	struct _Contr *contr;
	struct timer_list tl;
	__u8	buf[128];
} DummyProcess_t;

void dummyPcConstr(DummyProcess_t *dummy_pc, struct _Contr *contr, __u16 invokeId);
void dummyPcDestr(DummyProcess_t *dummy_pc);
void dummyPcAddTimer(DummyProcess_t *dummy_pc, int msec);

int capiEncodeFacIndSuspend(__u8 *dest, __u16  SupplementaryServiceReason);

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

// ---------------------------------------------------------------------------
// struct Contr

typedef struct _BInst {
	struct _BInst	*prev;
	struct _BInst   *next;
	hisaxstack_t	*bst;
	hisaxinstance_t	inst;
} BInst_t;

typedef struct _Contr {
	struct _Contr	*prev;
	struct _Contr	*next;
	hisaxinstance_t	inst;
	BInst_t		*binst;
	struct capi_ctr	*ctrl;
	__u32		adrController;
	int		b3_mode;
	char		infobuf[128];
	char		msgbuf[128];
	struct _Plci	*plcis[CAPI_MAXPLCI];
	struct _Appl	*appls[CAPI_MAXAPPL];
	DummyProcess_t	*dummy_pcs[CAPI_MAXDUMMYPCS];
	__u16		lastInvokeId;
} Contr_t;

Contr_t		*newContr(hisaxobject_t *, hisaxstack_t *, hisax_pid_t *);
int		contrConstr(Contr_t *, hisaxstack_t *, hisax_pid_t *, hisaxobject_t *);
void		contrDestr(Contr_t *contr);
void		contrDebug(Contr_t *contr, __u32 level, char *fmt, ...);
void		contrRegisterAppl(Contr_t *contr, __u16 ApplId, capi_register_params *rp);
void		contrReleaseAppl(Contr_t *contr, __u16 ApplId);
void		contrSendMessage(Contr_t *contr, struct sk_buff *skb);
void		contrLoadFirmware(Contr_t *, int, void *);
void		contrReset(Contr_t *contr);
void		contrRecvCmsg(Contr_t *contr, _cmsg *cmsg);
void		contrAnswerCmsg(Contr_t *contr, _cmsg *cmsg, __u16 Info);
void		contrAnswerMessage(Contr_t *contr, struct sk_buff *skb, __u16 Info);
struct _Plci	*contrNewPlci(Contr_t *contr);
struct _Appl	*contrId2appl(Contr_t *contr, __u16 ApplId);
struct _Plci	*contrAdr2plci(Contr_t *contr, __u32 adr);
void		contrDelPlci(Contr_t *contr, struct _Plci *plci);
int		contrDummyInd(Contr_t *, __u32, struct sk_buff *);
DummyProcess_t	*contrNewDummyPc(Contr_t *contr);
DummyProcess_t	*contrId2DummyPc(Contr_t *contr, __u16 invokeId);
int		contrL4L3(Contr_t *, u_int, int, struct sk_buff *);
int		contrL3L4(hisaxif_t *, struct sk_buff *);
BInst_t		*contrSelChannel(Contr_t *, u_int);
// ---------------------------------------------------------------------------
// struct Listen

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

typedef struct _Listen {
	Contr_t *contr;
	__u16 ApplId;
	__u32 InfoMask;
	__u32 CIPmask;
	__u32 CIPmask2;
	struct FsmInst listen_m;
} Listen_t;

void listenConstr(Listen_t *listen, Contr_t *contr, __u16 ApplId);
void listenDestr(Listen_t *listen);
void listenDebug(Listen_t *listen, __u32 level, char *fmt, ...);
void listenSendMessage(Listen_t *listen, struct sk_buff *skb);
int listenHandle(Listen_t *listen, __u16 CIPValue);

// ---------------------------------------------------------------------------
// struct Appl

#define APPL_FLAG_D2TRACE 1

typedef struct _Appl {
	Contr_t			*contr;
	__u16			ApplId;
	__u16			MsgId;
	Listen_t		listen;
	struct _Cplci		*cplcis[CAPI_MAXPLCI];
	__u32			NotificationMask;
	int			flags;
	capi_register_params	rp;
} Appl_t;

void applConstr(Appl_t *appl, Contr_t *contr, __u16 ApplId, capi_register_params *rp);
void applDestr(Appl_t *appl);
void applDebug(Appl_t *appl, __u32 level, char *fmt, ...);
void applSendMessage(Appl_t *appl, struct sk_buff *skb);
void applFacilityReq(Appl_t *appl, struct sk_buff *skb);
void applSuppFacilityReq(Appl_t *appl, _cmsg *cmsg);
int applGetSupportedServices(Appl_t *appl, struct FacReqParm *facReqParm, 
			      struct FacConfParm *facConfParm);
int applFacListen(Appl_t *appl, struct FacReqParm *facReqParm,
		   struct FacConfParm *facConfParm);
int applFacCFActivate(Appl_t *appl, struct FacReqParm *facReqParm,
		       struct FacConfParm *facConfParm);
int applFacCFDeactivate(Appl_t *appl, struct FacReqParm *facReqParm,
			 struct FacConfParm *facConfParm);
int applFacCFInterrogateNumbers(Appl_t *appl, struct FacReqParm *facReqParm,
				 struct FacConfParm *facConfParm);
int applFacCFInterrogateParameters(Appl_t *appl, struct FacReqParm *facReqParm,
				    struct FacConfParm *facConfParm);
void applManufacturerReq(Appl_t *appl, struct sk_buff *skb);
void applD2Trace(Appl_t *appl, u_char *buf, int len);
DummyProcess_t *applNewDummyPc(Appl_t *appl, __u16 Function, __u32 Handle);
struct _Cplci *applNewCplci(Appl_t *appl, struct _Plci *plci);
struct _Cplci *applAdr2cplci(Appl_t *appl, __u32 adr);
void applDelCplci(Appl_t *appl, struct _Cplci *cplci);

// ---------------------------------------------------------------------------
// struct Plci

#define PLCI_FLAG_ALERTING 1
#define PLCI_FLAG_OUTGOING 2

typedef struct _Plci {
	Contr_t	*contr;
	__u32	adrPLCI;
	int	flags;
	int	nAppl;
	struct _Cplci *cplcis[CAPI_MAXAPPL];
} Plci_t;

void plciConstr(Plci_t *plci, Contr_t *contr, __u32 adrPLCI);
void plciDestr(Plci_t *plci);
void plciDebug(Plci_t *plci, __u32 level, char *fmt, ...);
int  plci_l3l4(Plci_t *, int, struct sk_buff *);
void plciAttachCplci(Plci_t *plci, struct _Cplci *cplci);
void plciDetachCplci(Plci_t *plci, struct _Cplci *cplci);
void plciNewCrInd(Plci_t *plci, void *);
void plciNewCrReq(Plci_t *plci);
int  plciL4L3(Plci_t *, __u32, int, void *);

// ---------------------------------------------------------------------------
// struct Cplci

typedef struct _Cplci {
	__u32	adrPLCI;
	Plci_t	*plci;
	Appl_t	*appl;
	struct	_Ncci *ncci;
	Contr_t	*contr;
	struct	FsmInst plci_m;
	u_char	cause[4]; // we may get a cause from l3 DISCONNECT message
			  // which we'll need send in DISCONNECT_IND caused by
			  // l3 RELEASE message
	u_int	bchannel;
	struct Bprotocol Bprotocol;
} Cplci_t;

void cplciConstr(Cplci_t *cplci, Appl_t *appl, Plci_t *plci);
void cplciDestr(Cplci_t *cplci);
void cplciDebug(Cplci_t *cplci, __u32 level, char *fmt, ...);
void cplci_l3l4(Cplci_t *cplci, int pr, void *arg);
void cplciSendMessage(Cplci_t *cplci, struct sk_buff *skb);
void cplciClearOtherApps(Cplci_t *cplci);
void cplciInfoIndMsg(Cplci_t *,  __u32, unsigned char);
void cplciInfoIndIE(Cplci_t *, unsigned char, __u32, unsigned char *);
void cplciRecvCmsg(Cplci_t *cplci, _cmsg *cmsg);
void cplciCmsgHeader(Cplci_t *cplci, _cmsg *cmsg, __u8 cmd, __u8 subcmd);
void cplciLinkUp(Cplci_t *cplci);
void cplciLinkDown(Cplci_t *cplci);
int  cplciFacSuspendReq(Cplci_t *cplci, struct FacReqParm *facReqParm,
		       struct FacConfParm *facConfParm);
int  cplciFacResumeReq(Cplci_t *cplci, struct FacReqParm *facReqParm,
		      struct FacConfParm *facConfParm);

// ---------------------------------------------------------------------------
// Ncci_t

typedef struct _Ncci {
	BInst_t		*binst;
	__u32		adrNCCI;
	Contr_t		*contr;
	Cplci_t		*cplci;
	Appl_t		*appl;
	struct FsmInst	ncci_m;
	int		window;
	u_long		Flags;
	struct { 
		struct sk_buff *skb; 
		__u16	DataHandle;
		__u16	MsgId;
	} xmit_skb_handles[CAPI_MAXDATAWINDOW];
	struct sk_buff *recv_skb_handles[CAPI_MAXDATAWINDOW];
	struct sk_buff_head squeue;
	_cmsg		tmpmsg;
} Ncci_t;

#define NCCI_FLG_FCTRL	1
#define NCCI_FLG_BUSY	2

void ncciConstr(Ncci_t *ncci, Cplci_t *cplci);
void ncciDestr(Ncci_t *ncci);
void ncciSendMessage(Ncci_t *, struct sk_buff *);
int  ncci_l3l4(hisaxif_t *, struct sk_buff *);
void ncciLinkUp(Ncci_t *ncci);
void ncciLinkDown(Ncci_t *ncci);
void ncciInitSt(Ncci_t *ncci);
void ncciReleaseSt(Ncci_t *ncci);
__u16 ncciSelectBprotocol(Ncci_t *ncci);
void ncciRecvCmsg(Ncci_t *ncci, _cmsg *cmsg);
void ncciCmsgHeader(Ncci_t *ncci, _cmsg *cmsg, __u8 cmd, __u8 subcmd);


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

#endif
