/* $Id: asn1_comp.c,v 1.2 2006/03/06 12:52:07 keil Exp $
 *
 */

#include "asn1.h"
#include "asn1_comp.h"
#include "asn1_generic.h"
#include "asn1_aoc.h"
#include "asn1_diversion.h"

// ======================================================================
// Component EN 300 196-1 D.1

int
ParseInvokeId(struct asn1_parm *pc, u_char *p, u_char *end, int *invokeId)
{
	return ParseInteger(pc, p, end, invokeId);
}

int
ParseErrorValue(struct asn1_parm *pc, u_char *p, u_char *end, int *errorValue)
{
	return ParseInteger(pc, p, end, errorValue);
}

int
ParseOperationValue(struct asn1_parm *pc, u_char *p, u_char *end, int *operationValue)
{
	return ParseInteger(pc, p, end, operationValue);
}

int
ParseInvokeComponent(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int invokeId, operationValue;
	INIT;

	pc->comp = invoke;
	XSEQUENCE_1(ParseInvokeId, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &invokeId);
//	XSEQUENCE_OPT(ParseLinkedId, ASN1_TAG_INTEGER, 0);
	XSEQUENCE_1(ParseOperationValue, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &operationValue);
	pc->u.inv.invokeId = invokeId;
	pc->u.inv.operationValue = operationValue;
	switch (operationValue) {
#if 0
	case 7:	 XSEQUENCE(ParseARGActivationDiversion, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
	case 8:	 XSEQUENCE(ParseARGDeactivationDiversion, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
#endif
 	case 9:	 XSEQUENCE_1(ParseARGActivationStatusNotificationDiv, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, &pc->u.inv.o.actNot); break;
 	case 10: XSEQUENCE_1(ParseARGDeactivationStatusNotificationDiv, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, &pc->u.inv.o.deactNot); break;
#if 0
 	case 11: XSEQUENCE(ParseARGInterrogationDiversion, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
 	case 12: XSEQUENCE(ParseARGDiversionInformation, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
 	case 17: XSEQUENCE(ParseARGInterrogateServedUserNumbers, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
//	case 30: XSEQUENCE(ParseChargingRequest, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
//	case 31: XSEQUENCE(ParseAOCSCurrency, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
//	case 32: XSEQUENCE(ParseAOCSSpecialArr, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
	case 33: XSEQUENCE(ParseAOCDCurrency, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
	case 34: XSEQUENCE(ParseAOCDChargingUnit, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
	case 35: XSEQUENCE(ParseAOCECurrency, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
	case 36: XSEQUENCE(ParseAOCEChargingUnit, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED); break;
#endif
	default:
		return -1;
	}

	return p - beg;
}

int
ParseReturnResultComponentSequence(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int operationValue;
	INIT;

	XSEQUENCE_1(ParseOperationValue, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &operationValue);
	switch (operationValue) {
 	case 11: XSEQUENCE(ParseRESInterrogationDiversion, ASN1_TAG_SET, ASN1_NOT_TAGGED); break;
 	case 17: XSEQUENCE(ParseRESInterrogateServedUserNumbers, ASN1_TAG_SET, ASN1_NOT_TAGGED); break;
	default: return -1;
	}
		
	return p - beg;
}

int
ParseReturnResultComponent(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int invokeId;
	INIT;

	pc->comp = returnResult;
	XSEQUENCE_1(ParseInvokeId, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &invokeId);
	XSEQUENCE_OPT(ParseReturnResultComponentSequence, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED);
	pc->u.retResult.invokeId = invokeId;

	return p - beg;
}

int
ParseReturnErrorComponent(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
        int invokeId;
        int errorValue;
        char error[80];
        INIT;

	pc->comp = returnError;

	XSEQUENCE_1(ParseInvokeId, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &invokeId);
	XSEQUENCE_1(ParseErrorValue, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &errorValue);

	pc->u.retError.invokeId = invokeId;
	pc->u.retError.errorValue = errorValue;

	switch (errorValue) {
		case 0: sprintf(error, "not subscribed"); break;
		case 3: sprintf(error, "not available"); break;
		case 4: sprintf(error, "not implemented"); break;
		case 6: sprintf(error, "invalid served user nr"); break;
		case 7: sprintf(error, "invalid call state"); break;
		case 8: sprintf(error, "basic service not provided"); break;
		case 9: sprintf(error, "not incoming call"); break;
		case 10: sprintf(error, "supplementary service interaction not allowed"); break;
		case 11: sprintf(error, "resource unavailable"); break;
		case 12: sprintf(error, "invalid diverted-to nr"); break;
		case 14: sprintf(error, "special service nr"); break;
		case 15: sprintf(error, "diversion to served user nr"); break;
		case 23: sprintf(error, "incoming call accepted"); break;
		case 24: sprintf(error, "number of diversions exceeded"); break;
		case 46: sprintf(error, "not activated"); break;
		case 48: sprintf(error, "request already accepted"); break;
		default: sprintf(error, "(%d)", errorValue); break;
	}
	print_asn1msg(PRT_DEBUG_DECODE, "ReturnError: %s\n", error);

	return p - beg;
}

int
ParseProblemValue(struct asn1_parm *pc, u_char *p, u_char *end, asn1Problem prob)
{
	INIT;
	
	pc->u.reject.problem = prob;
	
	print_asn1msg(PRT_DEBUG_DECODE, "ParseProblemValue: %d %d\n", prob, *p);
	pc->u.reject.problemValue = *p++;
	return p - beg;
}

int
ParseRejectProblem(struct asn1_parm *pc, u_char *p, u_char *end)
{
        INIT;

	XCHOICE_1(ParseProblemValue, ASN1_TAG_CONTEXT_SPECIFIC, 0, GeneralP);
	XCHOICE_1(ParseProblemValue, ASN1_TAG_CONTEXT_SPECIFIC, 1, InvokeP);
	XCHOICE_1(ParseProblemValue, ASN1_TAG_CONTEXT_SPECIFIC, 2, ReturnResultP);
	XCHOICE_1(ParseProblemValue, ASN1_TAG_CONTEXT_SPECIFIC, 3, ReturnErrorP);
	XCHOICE_DEFAULT;
}

int
ParseRejectComponent(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
        int invokeId = -1;
        int rval;
        INIT;

	pc->comp = reject;

	XSEQUENCE_OPT_1(ParseInvokeId, ASN1_TAG_INTEGER, ASN1_NOT_TAGGED, &invokeId);
	XSEQUENCE_OPT(ParseNull, ASN1_TAG_NULL, ASN1_NOT_TAGGED);
	
	print_asn1msg(PRT_DEBUG_DECODE, "ParseRejectComponent: invokeId %d\n", invokeId);

	pc->u.reject.invokeId = invokeId;
	
	rval = ParseRejectProblem(pc, p, end);

	print_asn1msg(PRT_DEBUG_DECODE, "ParseRejectComponent: rval %d\n", rval);

	if (rval > 0)
		p += rval;
	else
		return(-1);

	return p - beg;
}

int
ParseUnknownComponent(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	int invokeId;
	INIT;
	
	pc->comp = tag;
	return end - beg;
}

int
ParseComponent(struct asn1_parm *pc, u_char *p, u_char *end)
{
        INIT;

	XCHOICE(ParseInvokeComponent, ASN1_TAG_SEQUENCE, 1);
	XCHOICE(ParseReturnResultComponent, ASN1_TAG_SEQUENCE, 2);
	XCHOICE(ParseReturnErrorComponent, ASN1_TAG_SEQUENCE, 3);
	XCHOICE(ParseRejectComponent, ASN1_TAG_SEQUENCE, 4);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 5);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 6);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 7);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 8);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 9);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 10);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 11);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 12);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 13);
	XCHOICE(ParseUnknownComponent, ASN1_TAG_SEQUENCE, 14);
	XCHOICE_DEFAULT;
}

