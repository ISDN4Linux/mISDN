/* $Id: asn1_address.c,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 */

#include "asn1.h"
#include "asn1_generic.h"
#include "asn1_address.h"

void buildnumber(char *num, int oc3, int oc3a, char *result, int version,
		 int *provider, int *sondernummer, int *intern, int *local,
		 int dir, int who);


// ======================================================================
// Address Types EN 300 196-1 D.3

int ParsePresentationRestricted(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	int ret;

	ret = ParseNull(pc, p, end, -1);
	if (ret < 0)
		return ret;
	strcpy(str, "(presentation restricted)");
	return ret;
}

int ParseNotAvailInterworking(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	int ret;

	ret = ParseNull(pc, p, end, -1);
	if (ret < 0)
		return ret;
	strcpy(str, "(not available)");
	return ret;
}

int ParsePresentedAddressScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	INIT;

	XCHOICE_1(ParseAddressScreened, ASN1_TAG_SEQUENCE, 0, str);
	XCHOICE_1(ParsePresentationRestricted, ASN1_TAG_NULL, 1, str);
	XCHOICE_1(ParseNotAvailInterworking, ASN1_TAG_NULL, 2, str);
	XCHOICE_1(ParseAddressScreened, ASN1_TAG_NULL, 3, str);
	XCHOICE_DEFAULT;
}

int ParsePresentedNumberScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	INIT;

	XCHOICE_1(ParseNumberScreened, ASN1_TAG_SEQUENCE, 0, str);
	XCHOICE_1(ParsePresentationRestricted, ASN1_TAG_NULL, 1, str);
	XCHOICE_1(ParseNotAvailInterworking, ASN1_TAG_NULL, 2, str);
	XCHOICE_1(ParseNumberScreened, ASN1_TAG_NULL, 3, str);
	XCHOICE_DEFAULT;
}

int ParsePresentedNumberUnscreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	struct PartyNumber partyNumber;
	INIT;

	XCHOICE_1(ParsePartyNumber, ASN1_TAG_SEQUENCE, 0, &partyNumber); // FIXME EXP
	XCHOICE_1(ParsePresentationRestricted, ASN1_TAG_NULL, 1, str);
	XCHOICE_1(ParseNotAvailInterworking, ASN1_TAG_NULL, 2, str);
	XCHOICE_1(ParsePartyNumber, ASN1_TAG_SEQUENCE, 3, &partyNumber); // FIXME EXP
	XCHOICE_DEFAULT;
}

int ParseNumberScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	struct PartyNumber partyNumber;
	char screeningIndicator[30];
	INIT;

	XSEQUENCE_1(ParsePartyNumber, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &partyNumber);
	XSEQUENCE_1(ParseScreeningIndicator, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, screeningIndicator);

//	str += sprintf(str, "%s", partyNumber);

	return p - beg;
}

int ParseAddressScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	struct PartyNumber partyNumber;
	char partySubaddress[30] = { 0, };
	char screeningIndicator[30];
	INIT;

	XSEQUENCE_1(ParsePartyNumber, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &partyNumber);
	XSEQUENCE_1(ParseScreeningIndicator, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, screeningIndicator);
	XSEQUENCE_OPT_1(ParsePartySubaddress, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, partySubaddress);

//	str += sprintf(str, "%s", partyNumber);
	if (strlen(partySubaddress))
		str += sprintf(str, ".%s", partySubaddress);

	return p - beg;
}

int ParseAddress(struct asn1_parm *pc, u_char *p, u_char *end, struct Address *address)
{
	INIT;

	address->partySubaddress[0] = 0;
	XSEQUENCE_1(ParsePartyNumber, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, &address->partyNumber);
	
	XSEQUENCE_OPT_1(ParsePartySubaddress, ASN1_NOT_TAGGED, ASN1_NOT_TAGGED, address->partySubaddress);

	return p - beg;
}

int ParsePartyNumber(struct asn1_parm *pc, u_char *p, u_char *end, struct PartyNumber *partyNumber)
{
	INIT;

	partyNumber->type = 0;
	XCHOICE_1(ParseNumberDigits, ASN1_TAG_NUMERIC_STRING, 0, partyNumber->p.unknown); // unknownPartyNumber
	partyNumber->type = 1;
	XCHOICE_1(ParsePublicPartyNumber, ASN1_TAG_SEQUENCE, 1, &partyNumber->p.publicPartyNumber); 
#if 0
	XCHOICE_1(ParseNumberDigits, ASN1_TAG_NUMERIC_STRING, 3, str); // dataPartyNumber
	XCHOICE_1(ParseNumberDigits, ASN1_TAG_NUMERIC_STRING, 4, str); // telexPartyNumber
	XCHOICE_1(ParsePrivatePartyNumber, ASN1_TAG_SEQUENCE, 5, str);
	XCHOICE_1(ParseNumberDigits, ASN1_TAG_NUMERIC_STRING, 8, str); // nationalStandardPartyNumber
#endif
	XCHOICE_DEFAULT;
}

int ParsePublicPartyNumber(struct asn1_parm *pc, u_char *p, u_char *end, struct PublicPartyNumber *publicPartyNumber)
{
	INIT;

	XSEQUENCE_1(ParsePublicTypeOfNumber, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &publicPartyNumber->publicTypeOfNumber);
	XSEQUENCE_1(ParseNumberDigits, ASN1_TAG_NUMERIC_STRING, ASN1_NOT_TAGGED, publicPartyNumber->numberDigits);

	return p - beg;
}

#if 0
int ParsePrivatePartyNumber(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	int privateTypeOfNumber;
	char numberDigits[20];
	INIT;

	XSEQUENCE_1(ParsePrivateTypeOfNumber, ASN1_TAG_ENUM, ASN1_NOT_TAGGED, &privateTypeOfNumber); 
	XSEQUENCE_1(ParseNumberDigits, ASN1_TAG_NUMERIC_STRING, ASN1_NOT_TAGGED, numberDigits); 

	switch (privateTypeOfNumber) {
	case 0: str += sprintf(str, "(unknown)"); break;
	case 1: str += sprintf(str, "(regional2)"); break;
	case 2: str += sprintf(str, "(regional1)"); break;
	case 3: str += sprintf(str, "(ptn)"); break;
	case 4: str += sprintf(str, "(local)"); break;
	case 6: str += sprintf(str, "(abbrev)"); break;
	}
	str += sprintf(str, numberDigits);

	return p - beg;
}
#endif

int ParsePublicTypeOfNumber(struct asn1_parm *pc, u_char *p, u_char *end, int *publicTypeOfNumber)
{
	return ParseEnum(pc, p, end, publicTypeOfNumber);
}

#if 0
int ParsePrivateTypeOfNumber(struct asn1_parm *pc, u_char *p, u_char *end, int *privateTypeOfNumber)
{
	return ParseEnum(pc, p, end, privateTypeOfNumber);
}
#endif

int ParsePartySubaddress(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	INIT;

	XCHOICE_1(ParseUserSpecifiedSubaddress, ASN1_TAG_SEQUENCE, ASN1_NOT_TAGGED, str);
	XCHOICE_1(ParseNSAPSubaddress, ASN1_TAG_OCTET_STRING, ASN1_NOT_TAGGED, str);
	XCHOICE_DEFAULT;
}

int ParseUserSpecifiedSubaddress(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	int oddCountIndicator;
	INIT;

	XSEQUENCE_1(ParseSubaddressInformation, ASN1_TAG_OCTET_STRING, ASN1_NOT_TAGGED, str);
	XSEQUENCE_OPT_1(ParseBoolean, ASN1_TAG_BOOLEAN, ASN1_NOT_TAGGED, &oddCountIndicator);
	
	return p - beg;
}

int ParseNSAPSubaddress(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	return ParseOctetString(pc, p, end, str);
}

int ParseSubaddressInformation(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	return ParseOctetString(pc, p, end, str);
}

int ParseScreeningIndicator(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	int ret;
	int screeningIndicator;

	ret = ParseEnum(pc, p, end, &screeningIndicator);
	if (ret < 0)
		return ret;
	
	switch (screeningIndicator) {
	case 0: sprintf(str, "user provided, not screened"); break;
	case 1: sprintf(str, "user provided, passed"); break;
	case 2: sprintf(str, "user provided, failed"); break;
	case 3: sprintf(str, "network provided"); break;
	default: sprintf(str, "(%d)", screeningIndicator); break;
	}

	return ret;
}

int ParseNumberDigits(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	return ParseNumericString(pc, p, end, str);
}
