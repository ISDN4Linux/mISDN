/* $Id: asn1_enc.c,v 1.3 2003/11/21 22:29:41 keil Exp $
 *
 */

#include "m_capi.h"
#include "helper.h"
#include "asn1_enc.h"

int encodeNull(__u8 *dest)
{
	dest[0] = 0x05;  // null
	dest[1] = 0;     // length
	return 2;
}

int encodeInt(__u8 *dest, __u32 i)
{
	__u8 *p;

	dest[0] = 0x02;  // integer
	dest[1] = 0;     // length
	p = &dest[2];
	do {
		*p++ = i;
		i >>= 8;
	} while (i);

	dest[1] = p - &dest[2];
	return p - dest;
}

int encodeEnum(__u8 *dest, __u32 i)
{
	__u8 *p;

	dest[0] = 0x0a;  // integer
	dest[1] = 0;     // length
	p = &dest[2];
	do {
		*p++ = i;
		i >>= 8;
	} while (i);

	dest[1] = p - &dest[2];
	return p - dest;
}

int encodeNumberDigits(__u8 *dest, __u8 *nd, __u8 len)
{
	__u8 *p;
	int i;

	dest[0] = 0x12;    // numeric string
	dest[1] = 0x0;     // length
	p = &dest[2];
	for (i = 0; i < len; i++)
		*p++ = *nd++;

	dest[1] = p - &dest[2];
	return p - dest;
}

int encodePublicPartyNumber(__u8 *dest, __u8 *facilityPartyNumber)
{
	__u8 *p;

	dest[0] = 0x20;  // sequence
	dest[1] = 0;     // length
	p = &dest[2];
	p += encodeEnum(p, (facilityPartyNumber[2] & 0x70) >> 4);
	p += encodeNumberDigits(p, &facilityPartyNumber[4], facilityPartyNumber[0] - 3);

	dest[1] = p - &dest[2];
	return p - dest;
}

int encodePartyNumber(__u8 *dest, __u8 *facilityPartyNumber)
{
	__u8 *p = dest;

	p = dest;
	switch (facilityPartyNumber[1]) {
	case 0: // unknown
		p += encodeNumberDigits(p, &facilityPartyNumber[4], facilityPartyNumber[0] - 3);
		dest[0] &= 0x20;
		dest[0] |= 0x81;
		break;
	case 1: // publicPartyNumber
		p += encodePublicPartyNumber(p, facilityPartyNumber);
		dest[0] &= 0x20;
		dest[0] |= 0x81;
		break;
	default:
		int_error();
		return -1;
	}
	return p - dest;
}

int encodeServedUserNumber(__u8 *dest, __u8 *servedUserNumber)
{
	if (servedUserNumber[0])
		return encodePartyNumber(dest, servedUserNumber);
        else
		return encodeNull(dest);
}

int encodeAddress(__u8 *dest, __u8 *facilityPartyNumber, __u8 *calledPartySubaddress)
{
	__u8 *p = dest;

	dest[0] = 0x30;  // invoke id tag, integer
	dest[1] = 0;     // length
	p = &dest[2];

	p += encodePartyNumber(p, facilityPartyNumber);
#if 0 // FIXME
	if (calledPartySubaddress[0])
		p += encodePartySubaddress(p, calledPartySubaddress);
#endif
	dest[1] = p - &dest[2];
	return p - dest;
}

int encodeActivationDiversion(__u8 *dest, struct FacReqCFActivate *CFActivate)
{
	__u8 *p;

	dest[0] = 0x30;  // sequence
	dest[1] = 0;     // length
	p = &dest[2];

	p += encodeEnum(p, CFActivate->Procedure);
	p += encodeEnum(p, CFActivate->BasicService);
	p += encodeAddress(p, CFActivate->ForwardedToNumber, CFActivate->ForwardedToSubaddress);
	p += encodeServedUserNumber(p, CFActivate->ServedUserNumber);

	dest[1] = p - &dest[2];
	return p - dest;
}

int encodeDeactivationDiversion(__u8 *dest, struct FacReqCFDeactivate *CFDeactivate)
{
	__u8 *p;

	dest[0] = 0x30;  // sequence
	dest[1] = 0;     // length
	p = &dest[2];

	p += encodeEnum(p, CFDeactivate->Procedure);
	p += encodeEnum(p, CFDeactivate->BasicService);
	p += encodeServedUserNumber(p, CFDeactivate->ServedUserNumber);

	dest[1] = p - &dest[2];
	return p - dest;
}

int encodeInterrogationDiversion(__u8 *dest, struct FacReqCFInterrogateParameters *params)
{
	__u8 *p;

	dest[0] = 0x30;  // sequence
	dest[1] = 0;     // length
	p = &dest[2];

	p += encodeEnum(p, params->Procedure);
#if 0
	if (basicService == 0)
		p += encodeNull(p);
	else
#endif
	p += encodeEnum(p, params->BasicService);
	p += encodeServedUserNumber(p, params->ServedUserNumber);

	dest[1] = p - &dest[2];
	return p - dest;
}

