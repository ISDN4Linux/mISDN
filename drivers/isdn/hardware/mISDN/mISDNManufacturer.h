/* $Id: mISDNManufacturer.h,v 1.1 2003/11/11 20:31:34 keil Exp $
 *
 * definition of mISDN own Manufacturer functions
 *
 */

#ifndef mISDNManufacturer_H
#define mISDNManufacturer_H

#define mISDN_MANUFACTURER_ID	0x44534963	/* "mISD" */

/* mISDN_MANUFACTURER message layout
 *
 * Controller		dword	Controller Address
 * ManuID		dword	mISDN_MANUFACTURER_ID
 * Class		dword   Function Class
 * Function		dword	Function Identifier
 * Function specific	struct	Data for this Function
 *
 * in a CONF the Function specific struct contain at least
 * a word which is coded as Capi Info word (error code)
 */
 
/*
 * HANDSET special functions
 *
 */
#define mISDN_MF_CLASS_HANDSET		1

#define mISDN_MF_HANDSET_ENABLE		1	/* no function specific data */
#define mISDN_MF_HANDSET_DISABLE	2	/* no function specific data */
#define mISDN_MF_HANDSET_SETMICVOLUME	3	/* word volume value */
#define mISDN_MF_HANDSET_SETSPKVOLUME	4	/* word volume value */
#define mISDN_MF_HANDSET_GETMICVOLUME	5	/* CONF: Info, word volume value */
#define mISDN_MF_HANDSET_GETSPKVOLUME	6	/* CONF: Info, word volume value */

#endif /* mISDNManufactor_H */
