/* $Id: asn1_address.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 */

// ======================================================================
// Address Types EN 300 196-1 D.3

int ParsePresentedAddressScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParsePresentedNumberScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParsePresentedNumberUnscreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseAddressScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseNumberScreened(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseAddress(struct asn1_parm *pc, u_char *p, u_char *end, struct Address *address);
int ParsePartyNumber(struct asn1_parm *pc, u_char *p, u_char *end, struct PartyNumber *partyNumber);
int ParsePublicPartyNumber(struct asn1_parm *pc, u_char *p, u_char *end, struct PublicPartyNumber *publicPartyNumber);
int ParsePrivatePartyNumber(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParsePublicTypeOfNumber(struct asn1_parm *pc, u_char *p, u_char *end, int *publicTypeOfNumber);
int ParsePrivateTypeOfNumber(struct asn1_parm *pc, u_char *p, u_char *end, int *privateTypeOfNumber);
int ParsePartySubaddress(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseUserSpecifiedSubaddress(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseNSAPSubaddress(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseSubaddressInformation(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseScreeningIndicator(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseNumberDigits(struct asn1_parm *pc, u_char *p, u_char *end, char *str);

