/* $Id: asn1_aoc.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 */

// ======================================================================
// AOC EN 300 182-1 V1.3.3

int ParseAOCDCurrency(struct asn1_parm *pc, u_char *p, u_char *end, int dummy);
int ParseAOCDChargingUnit(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseAOCECurrency(struct asn1_parm *pc, u_char *p, u_char *end, int dummy);
int ParseAOCEChargingUnit(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseAOCDCurrencyInfo(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseAOCDChargingUnitInfo(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseRecordedCurrency(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseRecordedUnitsList(struct asn1_parm *pc,u_char *p, u_char *end, int *recordedUnits);
int ParseTypeOfChargingInfo(struct asn1_parm *pc,u_char *p, u_char *end, int *typeOfChargingInfo);
int ParseRecordedUnits(struct asn1_parm *pc,u_char *p, u_char *end, int *recordedUnits);
int ParseAOCDBillingId(struct asn1_parm *pc, u_char *p, u_char *end, int *billingId);
int ParseAOCECurrencyInfo(struct asn1_parm *pc, u_char *p, u_char *end, int dummy);
int ParseAOCEChargingUnitInfo(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseAOCEBillingId(struct asn1_parm *pc,u_char *p, u_char *end, int *billingId);
int ParseCurrency(struct asn1_parm *pc,u_char *p, u_char *end, char *currency);
int ParseAmount(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseCurrencyAmount(struct asn1_parm *pc,u_char *p, u_char *end, int *currencyAmount);
int ParseMultiplier(struct asn1_parm *pc,u_char *p, u_char *end, int *multiplier);
int ParseTypeOfUnit(struct asn1_parm *pc,u_char *p, u_char *end, int *typeOfUnit);
int ParseNumberOfUnits(struct asn1_parm *pc,u_char *p, u_char *end, int *numberOfUnits);
int ParseChargingAssociation(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);
int ParseChargeIdentifier(struct asn1_parm *pc,u_char *p, u_char *end, int dummy);

