/* $Id: asn1_generic.c,v 1.1 2003/11/09 09:12:28 keil Exp $
 *
 */

#include "asn1.h"
#include "asn1_generic.h"

// ======================================================================
// general ASN.1

int
ParseBoolean(struct asn1_parm *pc, u_char *p, u_char *end, int *i)
{
	INIT;

	*i = 0;
	while (len--) {
		CHECK_P;
		*i = (*i >> 8) + *p;
		p++;
	}
	print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> BOOL = %d %#x\n", *i, *i);
	return p - beg;
}

int
ParseNull(struct asn1_parm *pc, u_char *p, u_char *end, int dummy)
{
	INIT;

	return p - beg;
}

int
ParseInteger(struct asn1_parm *pc, u_char *p, u_char *end, int *i)
{
	INIT;

	*i = 0;
	while (len--) {
		CHECK_P;
		*i = (*i << 8) + *p;
		p++;
	}
	print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> INT = %d %#x\n", *i, *i);
	return p - beg;
}

int
ParseEnum(struct asn1_parm *pc, u_char *p, u_char *end, int *i)
{
	INIT;

	*i = 0;
	while (len--) {
		CHECK_P;
		*i = (*i << 8) + *p;
		p++;
	}
	print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> ENUM = %d %#x\n", *i, *i);
	return p - beg;
}

#if 0
int
ParseIA5String(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	INIT;

	print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> IA5 = ");
	while (len--) {
		CHECK_P;
		print_asn1msg(PRT_DEBUG_DECODE, "%c", *p);
		*str++ = *p;
		p++;
	}
	print_asn1msg(PRT_DEBUG_DECODE, "\n");
	*str = 0;
	return p - beg;
}
#endif

int
ParseNumericString(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	INIT;

	print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> NumStr = ");
	while (len--) {
		CHECK_P;
		print_asn1msg(PRT_DEBUG_DECODE, "%c", *p);
		*str++ = *p;
		p++;
	}
	print_asn1msg(PRT_DEBUG_DECODE, "\n");
	*str = 0;
	return p - beg;
}

int
ParseOctetString(struct asn1_parm *pc, u_char *p, u_char *end, char *str)
{
	INIT;

	print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> Octets = ");
	while (len--) {
		CHECK_P;
		print_asn1msg(PRT_DEBUG_DECODE, " %02x", *p);
		*str++ = *p;
		p++;
	}
	print_asn1msg(PRT_DEBUG_DECODE, "\n");
	*str = 0;
	return p - beg;
}
