/* $Id: asn1_diversion.c,v 1.1 2003/11/09 09:12:28 keil Exp $
 *
 */

#include "asn1.h"
#include "asn1_generic.h"
#include "asn1_address.h"
#include "asn1_basic_service.h"
#include "asn1_diversion.h"

// ======================================================================
// Diversion Supplementary Services ETS 300 207-1 Table 3

#if 0
int
ParseARGActivationDiversion(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int procedure, basicService;
	struct ServedUserNr servedUserNr;
	struct Address address;
	INIT;

	XSEQUENCE_1(ParseProcedure, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &procedure);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &basicService);
	XSEQUENCE_1(ParseAddress, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, &address);
	XSEQUENCE_1(ParseServedUserNr, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &servedUserNr);

	return p - beg;
}

int
ParseARGDeactivationDiversion(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int procedure, basicService;
	struct ServedUserNr servedUserNr;
	INIT;

	XSEQUENCE_1(ParseProcedure, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &procedure);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &basicService);
	XSEQUENCE_1(ParseServedUserNr, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &servedUserNr);

	print_asn1msg(PRT_SHOWNUMBERS, "Deactivation Diversion %d (%d), \n",
		  procedure, basicService);
	return p - beg;
}
#endif

int
ParseARGActivationStatusNotificationDiv(struct asn1_parm *pc, u_char *p, u_char *end, struct ActDivNotification *actNot)
{
	INIT;

	XSEQUENCE_1(ParseProcedure, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &actNot->procedure);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &actNot->basicService);
	XSEQUENCE_1(ParseAddress, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, &actNot->address);
	XSEQUENCE_1(ParseServedUserNr, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &actNot->servedUserNr);

	return p - beg;
}

int
ParseARGDeactivationStatusNotificationDiv(struct asn1_parm *pc, u_char *p, u_char *end, struct DeactDivNotification *deactNot)
{
	INIT;

	XSEQUENCE_1(ParseProcedure, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &deactNot->procedure);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &deactNot->basicService);
	XSEQUENCE_1(ParseServedUserNr, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &deactNot->servedUserNr);

	return p - beg;
}

#if 0
int 
ParseARGInterrogationDiversion(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int procedure, basicService;
	struct ServedUserNr servedUserNr;
	INIT;

	XSEQUENCE_1(ParseProcedure, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &procedure);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &basicService);
	XSEQUENCE_1(ParseServedUserNr, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &servedUserNr);

	print_asn1msg(PRT_SHOWNUMBERS, "Interrogation Diversion %d (%d), \n",
		procedure, basicService);
	return p - beg;
}
#endif

int 
ParseRESInterrogationDiversion(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	print_asn1msg(PRT_SHOWNUMBERS, "Interrogation Diversion Result\n");
	return ParseIntResultList(pc, p,  end, &pc->u.retResult.o.resultList);
}

#if 0
int 
ParseARGInterrogateServedUserNumbers(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	print_asn1msg(PRT_SHOWNUMBERS, "Interrogate Served User Numbers\n");
	return 0;
}
#endif

int 
ParseRESInterrogateServedUserNumbers(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int ret;

	ret = ParseServedUserNumberList(pc, p, end, &pc->u.retResult.o.list);
	if (ret < 0)
		return ret;

	print_asn1msg(PRT_SHOWNUMBERS, "Interrogate Served User Numbers:\n");
	
	return ret;
}

int 
ParseARGDiversionInformation(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	char diversionReason[20];
	int basicService;
	char servedUserSubaddress[30];
	char callingAddress[80];
	char originalCalledNr[80];
	char lastDivertingNr[80];
	char lastDivertingReason[20];
	INIT;
	
	servedUserSubaddress[0] = 0;
	callingAddress[0] = 0;
	originalCalledNr[0] = 0;
	lastDivertingNr[0] = 0;
	lastDivertingReason[0] = 0;

	XSEQUENCE_1(ParseDiversionReason, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, diversionReason);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &basicService);
	XSEQUENCE_OPT_1(ParsePartySubaddress, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, servedUserSubaddress);
	XSEQUENCE_OPT_1(ParsePresentedAddressScreened, ASN1_NOT_TAGGED, 0 | ASN1_TAG_EXPLICIT, callingAddress);
	XSEQUENCE_OPT_1(ParsePresentedNumberUnscreened, ASN1_NOT_TAGGED, 1 | ASN1_TAG_EXPLICIT, originalCalledNr);
	XSEQUENCE_OPT_1(ParsePresentedNumberUnscreened, ASN1_NOT_TAGGED, 2 | ASN1_TAG_EXPLICIT, lastDivertingNr);
	XSEQUENCE_OPT_1(ParseDiversionReason, ASN1_TAG_ENUM, 3 | ASN1_TAG_EXPLICIT, lastDivertingReason);
//	XSEQUENCE_OPT_1(ParseQ931InformationElement, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, userInfo);
	print_asn1msg(PRT_SHOWNUMBERS, "Diversion Information %s(%d) %s\n"
		  "  callingAddress %s originalCalled Nr %s\n"
		  "  lastDivertingNr %s lastDiverting Reason %s\n",
		  diversionReason, basicService, servedUserSubaddress, callingAddress,
		  originalCalledNr, lastDivertingNr, lastDivertingReason);
	return p - beg;
}

int 
ParseIntResultList(struct asn1_parm *pc, u_char *p, u_char *end, struct IntResultList *intResultList)
{
	int i;
	INIT;

	for (i = 0; i < 10; i++) {
		intResultList->intResult[i].basicService = -1;
		XSEQUENCE_OPT_1(ParseIntResult, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, 
				&intResultList->intResult[i] );
	}

	return p - beg;
}

int 
ParseIntResult(struct asn1_parm *pc, u_char *p, u_char *end, struct IntResult *intResult)
{
	INIT;

	XSEQUENCE_1(ParseServedUserNr, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &intResult->servedUserNr);
	XSEQUENCE_1(ParseBasicService, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &intResult->basicService);
	XSEQUENCE_1(ParseProcedure, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &intResult->procedure);
	XSEQUENCE_1(ParseAddress, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, &intResult->address);

	return p - beg;
}

int
ParseServedUserNrAll(struct asn1_parm *pc, u_char *p, u_char *end, struct ServedUserNr *servedUserNr)
{
	int ret;

	ret = ParseNull(pc, p, end, 0);
	if (ret < 0)
		return ret;
	servedUserNr->all = 1;

	return ret;
}

int
ParseServedUserNr(struct asn1_parm *pc, u_char *p, u_char *end, struct ServedUserNr *servedUserNr)
{
	INIT;

	servedUserNr->all = 0;
	XCHOICE_1(ParseServedUserNrAll, ASN1_TAG_NULL, ASN1_NOT_TAGGED, servedUserNr);
	XCHOICE_1(ParsePartyNumber, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &servedUserNr->partyNumber);
	XCHOICE_DEFAULT;
}

int
ParseProcedure(struct asn1_parm *pc, u_char *p, u_char *end, int *procedure)
{
	return ParseEnum(pc, p, end, procedure);
}

int ParseServedUserNumberList(struct asn1_parm *pc, u_char *p, u_char *end, struct ServedUserNumberList *list)
{
	int i;
	INIT;

	for (i = 0; i < 10; i++) {
		list->partyNumber[i].type = -1;
		XSEQUENCE_OPT_1(ParsePartyNumber, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &list->partyNumber[i]);
	}

	return p - beg;
}

int
ParseDiversionReason(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	int ret;
	int diversionReason;

	ret = ParseEnum(pc, p, end, &diversionReason);
	if (ret < 0)
		return ret;
	
	switch (diversionReason) {
	case 0: sprintf(str, "unknown"); break;
	case 1: sprintf(str, "CFU"); break;
	case 2: sprintf(str, "CFB"); break;
	case 3: sprintf(str, "CFNR"); break;
	case 4: sprintf(str, "CD (Alerting)"); break;
	case 5: sprintf(str, "CD (Immediate)"); break;
	default: sprintf(str, "(%d)", diversionReason); break;
	}

	return ret;
}

