/* $Id: asn1_diversion.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 */

#if 0
int ParseARGActivationDiversion(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseARGDeactivationDiversion(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
#endif
int ParseARGActivationStatusNotificationDiv(struct asn1_parm *pc, u_char *p, u_char *end, struct ActDivNotification *actNot);
int ParseARGDeactivationStatusNotificationDiv(struct asn1_parm *pc, u_char *p, u_char *end, struct DeactDivNotification *deactNot);
int ParseARGInterrogationDiversion(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseRESInterrogationDiversion(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseARGInterrogateServedUserNumbers(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseRESInterrogateServedUserNumbers(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseARGDiversionInformation(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseIntResult(struct asn1_parm *parm, u_char *p, u_char *end, struct IntResult *intResult);
int ParseIntResultList(struct asn1_parm *parm, u_char *p, u_char *end, struct IntResultList *intResultList);
int ParseServedUserNr(struct asn1_parm *parm, u_char *p, u_char *end, struct ServedUserNr *servedUserNr);
int ParseProcedure(struct asn1_parm *pc, u_char *p, u_char *end, int *procedure);
int ParseServedUserNumberList(struct asn1_parm *parm, u_char *p, u_char *end, struct ServedUserNumberList *list);
int ParseDiversionReason(struct asn1_parm *parm, u_char *p, u_char *end, char *str);

