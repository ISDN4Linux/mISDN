/* $Id: listen.c,v 1.8 2004/01/26 22:21:30 keil Exp $
 *
 */

#include "m_capi.h"
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

static void
listen_debug(struct FsmInst *fi, char *fmt, ...)
{
	char tmp[128];
	char *p = tmp;
	va_list args;
	Application_t *app = fi->userdata;
	
	if (!fi->debug)
		return;
	va_start(args, fmt);
	p += sprintf(p, "Controller 0x%x ApplId %d listen ",
		     app->contr->addr, app->ApplId);
	p += vsprintf(p, fmt, args);
	*p = 0;
	listenDebug(app, CAPI_DBG_LISTEN_STATE, tmp);
	va_end(args);
}

static void
listen_req_l_x(struct FsmInst *fi, int event, void *arg, int state)
{
	Application_t	*app = fi->userdata;
	_cmsg		*cmsg = arg;

	mISDN_FsmChangeState(fi, state);

	app->InfoMask = cmsg->InfoMask;
	app->CIPmask = cmsg->CIPmask;
	app->CIPmask2 = cmsg->CIPmask2;
	listenDebug(app, CAPI_DBG_LISTEN_INFO, "set InfoMask to 0x%x", app->InfoMask);
	listenDebug(app, CAPI_DBG_LISTEN_INFO, "set CIP to 0x%x,0x%x", app->CIPmask,
		app->CIPmask2);

	capi_cmsg_answer(cmsg);
	cmsg->Info = CAPI_NOERROR;

	if (mISDN_FsmEvent(&app->listen_m, EV_LISTEN_CONF, cmsg))
		cmsg_free(cmsg);
}

static void
listen_req_l_0(struct FsmInst *fi, int event, void *arg)
{
	listen_req_l_x(fi, event, arg, ST_LISTEN_L_0_1);
}

static void
listen_req_l_1(struct FsmInst *fi, int event, void *arg)
{
	listen_req_l_x(fi, event, arg, ST_LISTEN_L_1_1);
}

static void
listen_conf_l_x_1(struct FsmInst *fi, int event, void *arg, int state)
{
	Application_t	*app = fi->userdata;
	_cmsg		*cmsg = arg;

	if (cmsg->Info != CAPI_NOERROR) {
		mISDN_FsmChangeState(fi, state);
	} else { // Info == 0
		if (app->CIPmask == 0) {
			test_and_clear_bit(APPL_STATE_LISTEN, &app->state);
			mISDN_FsmChangeState(fi, ST_LISTEN_L_0);
		} else {
			test_and_set_bit(APPL_STATE_LISTEN, &app->state);
			mISDN_FsmChangeState(fi, ST_LISTEN_L_1);
		}
	}
	SendCmsg2Application(app, cmsg);
}

static void
listen_conf_l_0_1(struct FsmInst *fi, int event, void *arg)
{
	listen_conf_l_x_1(fi, event, arg, ST_LISTEN_L_0);
}

static void
listen_conf_l_1_1(struct FsmInst *fi, int event, void *arg)
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

void listenConstr(Application_t *app)
{
	app->listen_m.fsm = &listen_fsm;
	app->listen_m.state = ST_LISTEN_L_0;
	app->listen_m.debug = app->contr->debug & CAPI_DBG_LISTEN_STATE;
	app->listen_m.userdata = app;
	app->listen_m.printdebug = listen_debug;
	app->InfoMask = 0;
	app->CIPmask = 0;
	app->CIPmask2 = 0;
}

void listenDestr(Application_t *app)
{
	test_and_clear_bit(APPL_STATE_LISTEN, &app->state);
	listenDebug(app, CAPI_DBG_LISTEN, "%s", __FUNCTION__);
}

__u16
listenSendMessage(Application_t *app, struct sk_buff *skb)
{
	_cmsg	*cmsg;

	cmsg = cmsg_alloc();
	if (!cmsg) {
		int_error();
		return (CAPI_MSGOSRESOURCEERR);
	}
	capi_message2cmsg(cmsg, skb->data);
	switch (CMSGCMD(cmsg)) {
		case CAPI_LISTEN_REQ:
			if (mISDN_FsmEvent(&app->listen_m, EV_LISTEN_REQ, cmsg))
				cmsg_free(cmsg);
			break;
		default:
			int_error();
			cmsg_free(cmsg);
	}
	dev_kfree_skb(skb);
	return(CAPI_NOERROR);
}
 
int listenHandle(Application_t *app, __u16 CIPValue)
{
	if ((app->CIPmask & 1) || 
	    (app->CIPmask & (1 << CIPValue)))
		return 1;
	return 0;
}

void init_listen(void)
{
	listen_fsm.state_count = ST_LISTEN_COUNT;
	listen_fsm.event_count = EV_LISTEN_COUNT;
	listen_fsm.strEvent = str_ev_listen;
	listen_fsm.strState = str_st_listen;
	
	mISDN_FsmNew(&listen_fsm, fn_listen_list, FN_LISTEN_COUNT);
}

void free_listen(void)
{
	mISDN_FsmFree(&listen_fsm);
}
