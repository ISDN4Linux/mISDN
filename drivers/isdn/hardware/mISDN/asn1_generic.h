/* $Id: asn1_generic.h,v 1.0 2001/11/02 23:42:26 kkeil Exp $
 *
 */

#include "asn1.h"

// ======================================================================
// general ASN.1

int ParseBoolean(struct asn1_parm *pc, u_char *p, u_char *end, int *i);
int ParseNull(struct asn1_parm *pc, u_char *p, u_char *end, int dummy);
int ParseInteger(struct asn1_parm *pc, u_char *p, u_char *end, int *i);
int ParseEnum(struct asn1_parm *pc, u_char *p, u_char *end, int *i);
int ParseIA5String(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseNumericString(struct asn1_parm *pc, u_char *p, u_char *end, char *str);
int ParseOctetString(struct asn1_parm *pc, u_char *p, u_char *end, char *str);

