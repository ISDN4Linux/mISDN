/* $Id: asn1_comp.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 */

#include "asn1.h"

int ParseInvokeId(struct asn1_parm *parm, u_char *p, u_char *end, int *invokeId);
int ParseOperationValue(struct asn1_parm *parm, u_char *p, u_char *end, int *operationValue);
int ParseInvokeComponent(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseReturnResultComponent(struct asn1_parm *parm, u_char *p, u_char *end, int dummy);
int ParseComponent(struct asn1_parm *parm, u_char *p, u_char *end);
int XParseComponent(struct asn1_parm *parm, u_char *p, u_char *end);

