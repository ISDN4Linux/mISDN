/* $Id: plci.c,v 0.1 2001/02/21 19:22:35 kkeil Exp $
 *
 */

#include "hisax_capi.h"
#include "helper.h"
#include "debug.h"

#if 0
#define plciDebug(plci, lev, fmt, args...) \
        debug(lev, plci->contr->cs, "", fmt, ## args)

static void l4pc_l3l4(struct l4_process *l4_pc, int pr, void *arg)
{
	Plci_t *plci = l4_pc->priv;

	plci_l3l4(plci, pr, arg);
}

#endif

void plciConstr(Plci_t *plci, Contr_t *contr, __u32 adrPLCI)
{
	memset(plci, 0, sizeof(Plci_t));
	plci->adrPLCI = adrPLCI;
	plci->contr = contr;
}

void plciDestr(Plci_t *plci)
{
#if 0
	if (plci->l4_pc.l3pc) {
		// FIXME: we need to kill l3_process, actually
		plci->l4_pc.l3pc->l4pc = 0;
	}
#endif
}

void plciHandleSetupInd(Plci_t *plci, int pr, SETUP_t *setup)
{
	int ApplId;
	__u16 CIPValue;
	Appl_t *appl;
	Cplci_t *cplci;

	for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
		appl = contrId2appl(plci->contr, ApplId);
		if (appl) {
			CIPValue = q931CIPValue(setup);
			if (listenHandle(&appl->listen, CIPValue)) {
				cplci = applNewCplci(appl, plci);
				if (!cplci) {
					int_error();
					break;
				}
				cplci_l3l4(cplci, pr, setup);
			}
		}
	}
	if (plci->nAppl == 0) {
		RELEASE_COMPLETE_t relcmpl;
		u_char cause[3] = {2,0x80,0xd8}; /* incompatible destination */

		memset(&relcmpl, 0, sizeof(RELEASE_COMPLETE_t));
		relcmpl.CAUSE = cause;
		p_L4L3(plci, CC_RELEASE_COMPLETE | REQUEST, &relcmpl);
	}
}

void plci_l3l4(Plci_t *plci, int pr, void *arg)
{
	__u16 applId;
	Cplci_t *cplci;

	switch (pr) {
	case CC_SETUP | INDICATION:
		plciHandleSetupInd(plci, pr, arg);
		break;
	case CC_RELEASE_CR | INDICATION:
		if (plci->nAppl == 0) {
			contrDelPlci(plci->contr, plci);
		}
		break;
	default:
		for (applId = 1; applId <= CAPI_MAXAPPL; applId++) {
			cplci = plci->cplcis[applId - 1];
			if (cplci) 
				cplci_l3l4(cplci, pr, arg);
		}
	}
}

void plciAttachCplci(Plci_t *plci, Cplci_t *cplci)
{
	__u16 applId = cplci->appl->ApplId;

	if (plci->cplcis[applId - 1]) {
		int_error();
		return;
	}
	plci->cplcis[applId - 1] = cplci;
	plci->nAppl++;
}

void plciDetachCplci(Plci_t *plci, Cplci_t *cplci)
{
	__u16 applId = cplci->appl->ApplId;

	if (plci->cplcis[applId - 1] != cplci) {
		int_error();
		return;
	}
	cplci->plci = 0;
	plci->cplcis[applId - 1] = 0;
	plci->nAppl--;
	if (plci->nAppl == 0) {
		contrDelPlci(plci->contr, plci);
	}
}

#if 0
void plciNewCrInd(Plci_t *plci, struct l3_process *l3_pc)
{
	l3_pc->l4pc = &plci->l4_pc; 
	l3_pc->l4pc->l3pc = l3_pc;
}
#endif

void plciNewCrReq(Plci_t *plci)
{
	L4L3(plci->contr, CC_NEW_CR | REQUEST, &plci->adrPLCI);
}
