/* $Id: asn1.h,v 1.2 2003/11/09 09:12:28 keil Exp $
 *
 */

#include <linux/mISDNif.h>
#include "helper.h"

#ifndef __ASN1_H__
#define __ASN1_H__

typedef enum {
	invoke       = 1,
	returnResult = 2,
	returnError  = 3,
	reject       = 4,
} asn1Component;

struct PublicPartyNumber {
	int publicTypeOfNumber;
	char numberDigits[30];
};

struct PartyNumber {
	int type;
	union {
		char unknown[30];
		struct PublicPartyNumber publicPartyNumber;
	} p;
};

struct Address {
	struct PartyNumber partyNumber;
	char partySubaddress[30];
};

struct ServedUserNr {
	int all;
	struct PartyNumber partyNumber;
};

struct ActDivNotification {
	int procedure;
	int basicService;
	struct ServedUserNr servedUserNr;
	struct Address address;
};

struct DeactDivNotification {
	int procedure;
	int basicService;
	struct ServedUserNr servedUserNr;
};

struct ServedUserNumberList {
	struct PartyNumber partyNumber[10];
};

struct IntResult {
	struct ServedUserNr servedUserNr;
	int procedure;
	int basicService;
	struct Address address;
};

struct IntResultList {
	struct IntResult intResult[10];
};

struct asn1Invoke {
	__u16 invokeId;
	__u16 operationValue;
	union {
		struct ActDivNotification actNot;
		struct DeactDivNotification deactNot;
	} o;
};

struct asn1ReturnResult {
	__u16 invokeId;
	union {
		struct ServedUserNumberList list;
		struct IntResultList resultList;
	} o;
};

struct asn1ReturnError {
	__u16 invokeId;
	__u16 errorValue;
};

struct asn1_parm {
	asn1Component comp;
	union {
		struct asn1Invoke       inv;
		struct asn1ReturnResult retResult;
		struct asn1ReturnError  retError;
	} u;
};


#undef ASN1_DEBUG

#ifdef ASN1_DEBUG
#define print_asn1msg(dummy, fmt, args...) printk(fmt, ## args)
#else
#define print_asn1msg(dummy, fmt, args...) 
#endif

int ParseASN1(u_char *p, u_char *end, int level);

int ParseTag(u_char *p, u_char *end, int *tag);
int ParseLen(u_char *p, u_char *end, int *len);

#define ASN1_TAG_BOOLEAN           (0x01) // is that true?
#define ASN1_TAG_INTEGER           (0x02)
#define ASN1_TAG_BIT_STRING        (0x03)
#define ASN1_TAG_OCTET_STRING      (0x04)
#define ASN1_TAG_NULL              (0x05)
#define ASN1_TAG_OBJECT_IDENTIFIER (0x06)
#define ASN1_TAG_ENUM              (0x0a)
#define ASN1_TAG_SEQUENCE          (0x30)
#define ASN1_TAG_SET               (0x31)
#define ASN1_TAG_NUMERIC_STRING    (0x12)
#define ASN1_TAG_PRINTABLE_STRING  (0x13)
#define ASN1_TAG_IA5_STRING        (0x16)
#define ASN1_TAG_UTC_TIME          (0x17)

#define ASN1_TAG_CONSTRUCTED       (0x20)
#define ASN1_TAG_CONTEXT_SPECIFIC  (0x80)

#define ASN1_TAG_EXPLICIT          (0x100)
#define ASN1_TAG_OPT               (0x200)
#define ASN1_NOT_TAGGED            (0x400)

#define CallASN1(ret, p, end, todo) do { \
        ret = todo; \
	if (ret < 0) { \
                int_error(); \
                return -1; \
        } \
        p += ret; \
} while (0)

#define INIT \
	int tag, len; \
	int ret; \
	u_char *beg; \
        \
        print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> %s\n", __FUNCTION__); \
	beg = p; \
	CallASN1(ret, p, end, ParseTag(p, end, &tag)); \
	CallASN1(ret, p, end, ParseLen(p, end, &len)); \
        if (len >= 0) { \
                if (p + len > end) \
                        return -1; \
                end = p + len; \
        }

#define XSEQUENCE_1(todo, act_tag, the_tag, arg1) do { \
	if (p < end) { \
  	        if (((the_tag) &~ ASN1_TAG_OPT) == ASN1_NOT_TAGGED) { \
		        if (((u_char)act_tag == *p) || ((act_tag) == ASN1_NOT_TAGGED)) { \
			        CallASN1(ret, p, end, todo(pc, p, end, arg1)); \
                        } else { \
                                if (!((the_tag) & ASN1_TAG_OPT)) { \
                                        print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> err 1 %s:%d\n", __FUNCTION__, __LINE__); \
                	    	        return -1; \
                                } \
                        } \
	        } else { \
                        if ((the_tag) & ASN1_TAG_EXPLICIT) { \
		                if ((u_char)(((the_tag) & 0xff) | (ASN1_TAG_CONTEXT_SPECIFIC | ASN1_TAG_CONSTRUCTED)) == *p) { \
                                        int xtag, xlen; \
	                                CallASN1(ret, p, end, ParseTag(p, end, &xtag)); \
			                CallASN1(ret, p, end, ParseLen(p, end, &xlen)); \
  	                                CallASN1(ret, p, end, todo(pc, p, end, arg1)); \
                                } else { \
                                        if (!(the_tag) & ASN1_TAG_OPT) { \
                                                print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> err 2 %s:%d\n", __FUNCTION__, __LINE__); \
                        	    	        return -1; \
                                        } \
                                } \
                        } else { \
		                if ((u_char)(((the_tag) & 0xff) | (ASN1_TAG_CONTEXT_SPECIFIC | (act_tag & ASN1_TAG_CONSTRUCTED))) == *p) { \
  	                                CallASN1(ret, p, end, todo(pc, p, end, arg1)); \
                                } else { \
                                        if (!(the_tag) & ASN1_TAG_OPT) { \
                                                print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> err 3 %s:%d\n", __FUNCTION__, __LINE__); \
                        	    	        return -1; \
                                        } \
                                } \
		        } \
		} \
        } else { \
                if (!(the_tag) & ASN1_TAG_OPT) { \
                        print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> err 4 %s:%d\n", __FUNCTION__, __LINE__); \
	    	        return -1; \
                } \
        } \
} while (0)

#define XSEQUENCE_OPT_1(todo, act_tag, the_tag, arg1) \
        XSEQUENCE_1(todo, act_tag, (the_tag | ASN1_TAG_OPT), arg1)

#define XSEQUENCE(todo, act_tag, the_tag) XSEQUENCE_1(todo, act_tag, the_tag, -1)
#define XSEQUENCE_OPT(todo, act_tag, the_tag) XSEQUENCE_OPT_1(todo, act_tag, the_tag, -1)

#define XCHOICE_1(todo, act_tag, the_tag, arg1) \
	if (act_tag == ASN1_NOT_TAGGED) { \
		return todo(pc, beg, end, arg1); \
        } \
        if (the_tag == ASN1_NOT_TAGGED) { \
		  if (act_tag == tag) { \
                            return todo(pc, beg, end, arg1); \
                  } \
         } else { \
		  if ((the_tag | (0x80 | (act_tag & 0x20))) == tag) { \
                            return todo(pc, beg, end, arg1); \
                  } \
	 }

#define XCHOICE(todo, act_tag, the_tag) XCHOICE_1(todo, act_tag, the_tag, -1)

#define XCHOICE_DEFAULT do {\
          print_asn1msg(PRT_DEBUG_DECODE, " DEBUG> err 5 %s:%d\n", __FUNCTION__, __LINE__); \
          return -1; \
	  } while (0)

#define CHECK_P do { \
        if (p >= end) \
                 return -1; \
        } while (0) 


#endif
