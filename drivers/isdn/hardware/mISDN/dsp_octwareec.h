/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\

File:  octasicec14.h

Copyright (c) 2007 Octasic Inc. All rights reserved.

Description: 

    This file contains the functions used as the entry point to Octasic's
    echo cancellation software.

\*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

#ifndef __OCTWARE_EC_H__
#define __OCTWARE_EC_H__

#include <linux/kernel.h>

#include "octvqe/octvqe_linux.h"
#define EC_TYPE "OCTWARE"

#if 0
static void echo_can_init(void)
{
	printk( "Zaptel Echo Canceller: OCTVQE\n" );
}

static void echo_can_shutdown(void)
{
}
#endif

struct echo_can_state
{
    void * pvOctvqeEchoCanceller;
};

static inline struct echo_can_state *echo_can_create(int len, int adaption_mode)
{
    struct echo_can_state *pEchoCanceller = NULL;
	
    /* Allocate echo canceller state structure. */
    pEchoCanceller = (struct echo_can_state *) kmalloc(sizeof(struct echo_can_state), GFP_KERNEL);
    if (pEchoCanceller != NULL)
    {
        pEchoCanceller->pvOctvqeEchoCanceller = ZapOctVqeApiEcChannelInitialize(len, adaption_mode);
        if (pEchoCanceller->pvOctvqeEchoCanceller == NULL)
        {
            printk( KERN_ERR "ZapOctVqeApiEcChannelInitialize failure!\n" );

            /* Cleanup. */
            kfree(pEchoCanceller);
            pEchoCanceller = NULL;
        }
    }
    else
    {
        printk( KERN_ERR "echo_can_create error: kmalloc failed (requested %u bytes)\n", 
                sizeof(struct echo_can_state) );
    }
	
	return pEchoCanceller;
}

static inline short echo_can_update(struct echo_can_state *ec, short iref, short isig)
{
    return ZapOctVqeApiEcChannelProcess(ec->pvOctvqeEchoCanceller, iref, isig);
}

static inline void echo_can_free(struct echo_can_state *ec)
{
	/* Tell OCTVQE module that this channel is not used anymore. */
    ZapOctVqeApiEcChannelFree(ec->pvOctvqeEchoCanceller);
    kfree(ec);
}

static inline int echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{	
	return ZapOctVqeApiEcChannelTrainTap(ec->pvOctvqeEchoCanceller, pos, val);
}
 
#endif /* __OCTWARE_EC_H__ */
