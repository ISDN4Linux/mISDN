/* $Id: plci.c,v 1.5 2003/07/21 12:44:46 kkeil Exp $
 *
 */

#include "capi.h"
#include "dss1.h"
#include "helper.h"
#include "debug.h"

#define plciDebug(plci, lev, fmt, args...) \
        capidebug(lev, fmt, ## args)


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

void plciHandleSetupInd(Plci_t *plci, int pr, Q931_info_t *qi)
{
	int	ApplId;
	__u16	CIPValue;
	Appl_t	*appl;
	Cplci_t	*cplci;

	if (!qi) {
		int_error();
		return;
	}
	for (ApplId = 1; ApplId <= CAPI_MAXAPPL; ApplId++) {
		appl = contrId2appl(plci->contr, ApplId);
		if (appl) {
			CIPValue = q931CIPValue(qi);
			if (listenHandle(&appl->listen, CIPValue)) {
				cplci = applNewCplci(appl, plci);
				if (!cplci) {
					int_error();
					break;
				}
				cplci_l3l4(cplci, pr, qi);
			}
		}
	}
	if (plci->nAppl == 0) {
		struct sk_buff *skb = alloc_l3msg(10, MT_RELEASE_COMPLETE);
		u_char cause[4] = {IE_CAUSE,2,0x80,0xd8}; /* incompatible destination */

		if (skb) {
			AddvarIE(skb,cause);
			plciL4L3(plci, CC_RELEASE_COMPLETE | REQUEST, skb);
		}
	}
}

int plci_l3l4(Plci_t *plci, int pr, struct sk_buff *skb)
{
	__u16		applId;
	Cplci_t		*cplci;
	Q931_info_t	*qi;

	if (skb->len)
		qi = (Q931_info_t *)skb->data;
	else
		qi = NULL;
	switch (pr) {
		case CC_SETUP | INDICATION:
			plciHandleSetupInd(plci, pr, qi);
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
					cplci_l3l4(cplci, pr, qi);
			}
			break;
	}
	dev_kfree_skb(skb);
	return(0);
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
	plciL4L3(plci, CC_NEW_CR | REQUEST, NULL);
}

int plciL4L3(Plci_t *plci, __u32 prim, struct sk_buff *skb)
{
#define	MY_RESERVE	8
	int	err;

	if (!skb) {
		if (!(skb = alloc_skb(MY_RESERVE, GFP_ATOMIC))) {
			printk(KERN_WARNING "%s: no skb size %d\n",
				__FUNCTION__, MY_RESERVE);
			return(-ENOMEM);
		} else
			skb_reserve(skb, MY_RESERVE);
	}
	err = contrL4L3(plci->contr, prim, plci->adrPLCI, skb);
	if (err)
		dev_kfree_skb(skb);
	return(err);
}

