/* $Id: listen.c,v 0.3 2001/02/27 17:45:44 kkeil Exp $
 *
 */

#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

#define listenDebug(listen, lev, fmt, args...) \
        capidebug(lev, fmt, ## args)

// --------------------------------------------------------------------
// LISTEN state machine

enum {
	ST_LISTEN_L_0,
	ST_LISTEN_L_0_1,
	ST_LISTEN_L_1,
	ST_LISTEN_L_1_1,
}

const ST_LISTEN_COUNT = ST_LISTEN_L_1_1 + 1;

static char *str_st_listen[] = {
	"ST_LISTEN_L_0",
	"ST_LISTEN_L_0_1",
	"ST_LISTEN_L_1",
	"ST_LISTEN_L_1_1",
};

enum {
	EV_LISTEN_REQ,
	EV_LISTEN_CONF,
}

const EV_LISTEN_COUNT = EV_LISTEN_CONF + 1;

static char* str_ev_listen[] = {
	"EV_LISTEN_REQ",
	"EV_LISTEN_CONF",
};

static struct Fsm listen_fsm =
{ 0, 0, 0, 0, 0 };

static void listen_debug(struct FsmInst *fi, char *fmt, ...)
{
	char tmp[128];
	char *p = tmp;
	va_list args;
	Listen_t *listen = fi->userdata;
	
	if (!fi->debug)
		return;
	va_start(args, fmt);
	p += sprintf(p, "Controller 0x%x ApplId %d listen ",
		     listen->contr->adrController, listen->ApplId);
	p += vsprintf(p, fmt, args);
	*p = 0;
	listenDebug(listen, LL_DEB_STATE, tmp);
	va_end(args);
}

static void listen_req_l_x(struct FsmInst *fi, int event, void *arg, int state)
{
	Listen_t *listen = fi->userdata;
	_cmsg *cmsg = arg;

	FsmChangeState(fi, state);

	listen->InfoMask = cmsg->InfoMask;
	listen->CIPmask = cmsg->CIPmask;
	listen->CIPmask2 = cmsg->CIPmask2;
	listenDebug(listen, LL_DEB_INFO, "set InfoMask to 0x%x", listen->InfoMask);
	listenDebug(listen, LL_DEB_INFO, "set CIP to 0x%x,0x%x", listen->CIPmask,
		    listen->CIPmask2);

	capi_cmsg_answer(cmsg);
	cmsg->Info = CAPI_NOERROR;

	FsmEvent(&listen->listen_m, EV_LISTEN_CONF, cmsg);
	contrRecvCmsg(listen->contr, cmsg);
}

static void listen_req_l_0(struct FsmInst *fi, int event, void *arg)
{
	listen_req_l_x(fi, event, arg, ST_LISTEN_L_0_1);
}

static void listen_req_l_1(struct FsmInst *fi, int event, void *arg)
{
	listen_req_l_x(fi, event, arg, ST_LISTEN_L_1_1);
}

static void listen_conf_l_x_1(struct FsmInst *fi, int event, void *arg,
			      int state)
{
	Listen_t *listen = fi->userdata;
	_cmsg *cmsg = arg;

	if (cmsg->Info != CAPI_NOERROR) {
		FsmChangeState(fi, state);
	} else { // Info == 0
		if (listen->CIPmask == 0)
			FsmChangeState(fi, ST_LISTEN_L_0);
		else
			FsmChangeState(fi, ST_LISTEN_L_1);
	}
}

static void listen_conf_l_0_1(struct FsmInst *fi, int event, void *arg)
{
	listen_conf_l_x_1(fi, event, arg, ST_LISTEN_L_0);
}

static void listen_conf_l_1_1(struct FsmInst *fi, int event, void *arg)
{
	listen_conf_l_x_1(fi, event, arg, ST_LISTEN_L_1);
}

static struct FsmNode fn_listen_list[] =
{
	{ST_LISTEN_L_0,		EV_LISTEN_REQ,	listen_req_l_0},
	{ST_LISTEN_L_0_1,	EV_LISTEN_CONF,	listen_conf_l_0_1},
	{ST_LISTEN_L_1,		EV_LISTEN_REQ,	listen_req_l_1},
	{ST_LISTEN_L_1_1,	EV_LISTEN_CONF,	listen_conf_l_1_1},
};

const int FN_LISTEN_COUNT = sizeof(fn_listen_list)/sizeof(struct FsmNode);

// ----------------------------------------------------------------------
// Methods


void listenConstr(Listen_t *listen, Contr_t *contr, __u16 ApplId)
{
	listen->listen_m.fsm = &listen_fsm;
	listen->listen_m.state = ST_LISTEN_L_0;
	listen->listen_m.debug = 1;
	listen->listen_m.userdata = listen;
	listen->listen_m.printdebug = listen_debug;

	listen->contr = contr;
	listen->ApplId = ApplId;
	listen->InfoMask = 0;
	listen->CIPmask = 0;
	listen->CIPmask2 = 0;
}

void listenDestr(Listen_t *listen)
{
}

void listenSendMessage(Listen_t *listen, struct sk_buff *skb)
{
       _cmsg cmsg;
       capi_message2cmsg(&cmsg, skb->data);
       
       switch (CMSGCMD(&cmsg)) {
       case CAPI_LISTEN_REQ:
               FsmEvent(&listen->listen_m, EV_LISTEN_REQ, &cmsg);
               break;
       default:
	       int_error();
       }
       dev_kfree_skb(skb);
}
 
int listenHandle(Listen_t *listen, __u16 CIPValue)
{
	if ((listen->CIPmask & 1) || 
	    (listen -> CIPmask & (1 << CIPValue)))
		return 1;
	return 0;
}

// ---------------------------------------------------------------------
//

void init_listen(void)
{
	listen_fsm.state_count = ST_LISTEN_COUNT;
	listen_fsm.event_count = EV_LISTEN_COUNT;
	listen_fsm.strEvent = str_ev_listen;
	listen_fsm.strState = str_st_listen;
	
	FsmNew(&listen_fsm, fn_listen_list, FN_LISTEN_COUNT);
}

void free_listen(void)
{
	FsmFree(&listen_fsm);
}
