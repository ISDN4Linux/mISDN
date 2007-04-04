/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\

File:  octvqe_linux.c

Copyright (c) 2006 Octasic Inc. All rights reserved.

Description:  This file contains the linux kernel module functions for the
              OCTVQE software echo canceller.

This file is part of the OCTVQE GPL kernel module. The OCTVQE GPL kernel module 
is free software; you can redistribute it and/or modify it under the terms of 
the GNU General Public License as published by the Free Software Foundation; 
either version 2 of the License, or (at your option) any later version.

The OCTVQE GPL module is distributed in the hope that it will be useful, but 
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
for more details. 

You should have received a copy of the GNU General Public License 
along with the OCTVQE GPL module; if not, write to the Free Software 
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.

$Octasic_Release: OCTVQE8-01.01.02-PR $

$Octasic_Revision: 21 $

\*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>

/* user<-> kernel space access functions */
#include <asm/uaccess.h>

#include "octvqe_linux.h"
#include "octvqe_ioctl.h"

#define OCTVQE_MODULE_VERSION "OCTVQE-MOD-01.01.02-PR"

/*
 *
 * Pick your _DEFAULT_ echo canceller.
 *
 * The _DEFAULT_ echo canceller is the echo canceller that will be used when
 * the OCTVQE service is not listening (connected) to the OCTVQE module.
 * Also, this default echo canceller will be used if all the licensed
 * channels available for cancellation are already used.  These echo cancellers
 * are the ones found in the Zaptel source.
 * 
 */ 
/* #define ECHO_CAN_STEVE */
/* #define ECHO_CAN_STEVE2 */
/* #define ECHO_CAN_MARK */
/* #define ECHO_CAN_MARK2 */
/* #define ECHO_CAN_MARK3 */
/* #define ECHO_CAN_KB1 */
/* This is the new latest and greatest */
#define ECHO_CAN_MG2

#ifdef AGGRESSIVE_SUPPRESSOR
#define ZAPTEL_ECHO_AGGRESSIVE " (aggressive)"
#else
#define ZAPTEL_ECHO_AGGRESSIVE ""
#endif

#if defined(ECHO_CAN_STEVE)
#include "../sec.h"
#define ZAPTEL_ECHO_NAME "STEVE"
#elif defined(ECHO_CAN_STEVE2)
#include "../sec-2.h"
#define ZAPTEL_ECHO_NAME "STEVE2"
#elif defined(ECHO_CAN_MARK)
#include "../mec.h"
#define ZAPTEL_ECHO_NAME "MARK"
#elif defined(ECHO_CAN_MARK2)
#include "../mec2.h"
#define ZAPTEL_ECHO_NAME "MARK2"
#elif defined(ECHO_CAN_KB1)
#include "../kb1ec.h"
#define ZAPTEL_ECHO_NAME "KB1"
#elif defined(ECHO_CAN_MG2)
#include "../dsp_mg2ec.h"
#define ZAPTEL_ECHO_NAME "MG2"
#else
#include "../mec3.h"
#define ZAPTEL_ECHO_NAME "MARK3"
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define LINUX26
#endif

/* Maximum number of concurrent echo channels. */
#define MAX_NUM_SUPPORTED_CHANNELS 8

#define DEV_NAME "octvqe"
#define SUCCESS 0
#define FAIL -1

#define BUFFER_SIZE (8 * 20) /* 20 ms. */
#define NUM_BUFFERS (2)

typedef struct _OCTVQE_CHAN_INSTANCE_
{
    unsigned long           ulChannelIndex;
    void *                  pvZapEcChan; /* Reverse lookup to upper layer. */

    short                   asRinSamples[ NUM_BUFFERS ][ BUFFER_SIZE ];
    short                   asSinSamples[ NUM_BUFFERS ][ BUFFER_SIZE ];
    short                   asSoutSamples[ NUM_BUFFERS ][ BUFFER_SIZE ];
    unsigned long           ulXinReadPtr;
    unsigned long           ulSoutReadPtr;
    unsigned long           ulXinWritePtr;
    unsigned long           ulSoutWritePtr;
    unsigned long           ulReceivedSamples;

    unsigned long           ulProcessedBuf;
    int                     fChanOk; 
	int						fChanPrintErr;

    unsigned long           ulLongestCpuTime;
    unsigned long long      ullCpuTimeSumUs;

    unsigned long           ulTimestampIn;
    unsigned long           ulTimestampOut;

    int                     fUsed; 
    int                     fOpened; 

    wait_queue_head_t       ReadWaitQueue;
    wait_queue_head_t       WriteWaitQueue;
    wait_queue_head_t       SelectWaitQueue;

    int                     iXinWriteBuf;
    int                     iXinReadBuf;

    int                     iSoutWriteBuf;
    int                     iSoutReadBuf;
    int                     fChanReady;
    
	int                     iTailLength;
	int                     iTailDisplacement;
	int                     iDefaultErl;
	int                     fRinAnr;
	int                     fSoutAnr;
	int                     iRinCurrentEnergy;
	int                     iSinCurrentEnergy;
	int                     iRinAverageEnergy;
	int                     iSinAverageEnergy;
	int                     iAverageTimeUs;
	int                     iMaxTimeUs;
	
    void *                  pvDefaultEchoCanContext;

    spinlock_t				Lock;

} tOCTVQE_CHAN_INSTANCE, *tPOCTVQE_CHAN_INSTANCE;

tPOCTVQE_CHAN_INSTANCE g_apEchoChanInst[ MAX_NUM_SUPPORTED_CHANNELS ] = { 0 };

/* Exported symbols. */
EXPORT_SYMBOL( ZapOctVqeApiEcChannelInitialize );
EXPORT_SYMBOL( ZapOctVqeApiEcChannelProcess );
EXPORT_SYMBOL( ZapOctVqeApiEcChannelFree );
EXPORT_SYMBOL( ZapOctVqeApiEcChannelTrainTap );

static int iMajor;
static int verbose = 0;
module_param( verbose, int, 0 );

/* Taken from the original __printk_ratelimit, which was not implemented in the 2.4 series. */
int octvqe_ratelimit( void )
{
    static spinlock_t ratelimit_lock = SPIN_LOCK_UNLOCKED;
    static unsigned long toks = 10 * 5 * HZ;
    static unsigned long last_msg;
    static int missed;
    unsigned long flags;
    unsigned long now = jiffies;
    int ratelimit_burst = 10;
    int ratelimit_jiffies = 5 * HZ;

    spin_lock_irqsave( &ratelimit_lock, flags );
    toks += now - last_msg;
    last_msg = now;
    if ( toks > ( ratelimit_burst * ratelimit_jiffies ) )
    	toks = ratelimit_burst * ratelimit_jiffies;
    	
	if ( toks >= ratelimit_jiffies )
	{
		int lost = missed;

        missed = 0;
        toks -= ratelimit_jiffies;
        spin_unlock_irqrestore(&ratelimit_lock, flags);
        if ( lost )
    	    printk( KERN_WARNING "%s: %d messages suppressed.\n", DEV_NAME, lost );
    	    
        return 1;
    }
    
    missed ++;
    spin_unlock_irqrestore( &ratelimit_lock, flags );
    return 0;
}

void * ZapOctVqeApiEcChannelInitialize( int f_iLen, int f_iAdaptionMode )
{
    tPOCTVQE_CHAN_INSTANCE pChan = NULL;
    int i; 
    unsigned long ulFlags;
    void * pvDefaultEchoCanContext;

    /* Check if we can open this channel. */
    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
		spin_lock_irqsave( &g_apEchoChanInst[ i ]->Lock, ulFlags );

        if ( g_apEchoChanInst[ i ]->fUsed == 0 )
            break;

		spin_unlock_irqrestore( &g_apEchoChanInst[ i ]->Lock, ulFlags );
    }

    /* Check if we found an available slot. */
    if ( i == MAX_NUM_SUPPORTED_CHANNELS )
    {
        /* Nope we did not find any available slot, revert back to the default Zaptel canceller. */
        pvDefaultEchoCanContext = echo_can_create( f_iLen, f_iAdaptionMode );
        if ( pvDefaultEchoCanContext == NULL )
        {
            printk( KERN_WARNING "%s: Could not create default echo canceller!\n", DEV_NAME );
            return NULL;
        }
    	
        /* printk( KERN_WARNING "%s: All channels (%d) opened!\n", DEV_NAME, MAX_NUM_SUPPORTED_CHANNELS ); */
        return pvDefaultEchoCanContext;
    }
    
 	/* Set driver channel information. */
    pChan = g_apEchoChanInst[ i ];
    
    /* Call the DEFAULT echo canceller create function. */
    
    pChan->pvDefaultEchoCanContext = echo_can_create( f_iLen, f_iAdaptionMode );
    if ( pChan->pvDefaultEchoCanContext == NULL )
    {
    	printk( KERN_WARNING "%s: Could not create default echo canceller!\n", DEV_NAME );
    	return NULL;
    }

	/* pChan->fChanPrintErr = 0; */
	pChan->ulReceivedSamples = 0;

	/* Identify channel with upper layer. */
	pChan->pvZapEcChan = pChan;

    /* Remember the that this channel is used. */
    pChan->fUsed = 1;

    /* Remember which channel index we are. */
    pChan->ulChannelIndex = i;

    /* Initialize working variables. */
 	pChan->ulSoutReadPtr = 0;
	pChan->ulSoutWritePtr = 0;
	pChan->ulXinWritePtr = 0;
	pChan->ulXinReadPtr = 0;
    pChan->fChanReady = 0;

	pChan->iXinWriteBuf = 0;
	pChan->iXinReadBuf = -1;
	pChan->iSoutWriteBuf = 0;
	pChan->iSoutReadBuf = -1;

    /* Channel "opened up" correctly. */
    pChan->fChanOk = 1;

	spin_unlock_irqrestore( &pChan->Lock, ulFlags );

    /* Start with some silence on the ports. */
    memset( pChan->asRinSamples, 0, sizeof( pChan->asRinSamples ) );
	memset( pChan->asSinSamples, 0, sizeof( pChan->asSinSamples ) );
	memset( pChan->asSoutSamples, 0, sizeof( pChan->asSoutSamples ) );

    return pChan;
}

void ZapOctVqeApiEcChannelFree( void * f_pvEcChan )
{
    int i;
    unsigned long ulFlags;
    tPOCTVQE_CHAN_INSTANCE pChan = NULL;
    void * pvDefaultEchoCanContext = NULL;

	if ( f_pvEcChan == NULL )
	{
        printk( KERN_ERR "%s: Cannot free NULL channel!\n", DEV_NAME );
        return;
	}

	/* Find the channel to be freed in our instance list. */
    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
		spin_lock_irqsave( &g_apEchoChanInst[ i ]->Lock, ulFlags );

        if ( g_apEchoChanInst[ i ]->pvZapEcChan == f_pvEcChan )
        {
            pChan = g_apEchoChanInst[ i ];
            break;
        }

		spin_unlock_irqrestore( &g_apEchoChanInst[ i ]->Lock, ulFlags );
    }

	/* Check if we found the channel to be freed. */
    if ( pChan == NULL )
    {
		/* It must probably be the default echo canceller.  Free it. */
		echo_can_free( f_pvEcChan );
		
        /* printk( KERN_ERR "%s: Trying to close an invalid channel!\n", DEV_NAME ); */
        return;
    }

    /* Close this channel. */
    pChan->pvZapEcChan = NULL;
    pChan->fChanOk = 0;
	pChan->fUsed = 0;
	
	if ( pChan->pvDefaultEchoCanContext != NULL )
	{
		pvDefaultEchoCanContext = pChan->pvDefaultEchoCanContext;
		pChan->pvDefaultEchoCanContext = NULL;
	}
		
    spin_unlock_irqrestore( &pChan->Lock, ulFlags );
    
    if ( pvDefaultEchoCanContext != NULL )
    	echo_can_free( pvDefaultEchoCanContext );

	/* We're done here. */
}

int ZapOctVqeApiEcChannelTrainTap( void * f_pvEcChan, int f_iPos, short f_sVal )
{
    int                    i;
    tPOCTVQE_CHAN_INSTANCE pChan = NULL;
    
    if ( f_pvEcChan == NULL )
    {
        printk( KERN_ERR "%s: Cannot train NULL channel!\n", DEV_NAME );
        return 1;
    }
	
    /* Find the given channel.  No need to SpinLock here since we will not modify the pointer. */
    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
        if ( g_apEchoChanInst[ i ]->pvZapEcChan == f_pvEcChan )
        {
            pChan = g_apEchoChanInst[ i ];
            break;
        }
    }

    /* Check if it is one of our channels. */
    if ( pChan == NULL )
    {
    	/* Call the default echo canceller training method. */
    	return echo_can_traintap( f_pvEcChan, f_iPos, f_sVal );
    }
    
    /* Check if the OCTVQE service is listening on this channel. */
    if ( pChan->fOpened != 1 )
    {
        /* Fallback to the default echo canceller training method. */
        return echo_can_traintap( pChan->pvDefaultEchoCanContext, f_iPos, f_sVal );
    }

    /* OCTVQE does not need to be trained.  It will "auto-tune" */
    return 1;	
}

short ZapOctVqeApiEcChannelProcess( void * f_pvEcChan, short f_sRin, short f_sSin )
{
    int                    i;
	int                    iOldXinWriteBuf;
	int                    iOldSoutReadBuf;
	int                    fXinOverflow = 0;
	int                    fSoutUnderflow = 0;
	int                    fWakeUpReader = 0;
	int                    fWakeUpWrite = 0;
	short                  sSout;
	unsigned long          ulFlags;
    tPOCTVQE_CHAN_INSTANCE pChan = NULL;

	if ( f_pvEcChan == NULL )
	{
        printk( KERN_ERR "%s: Cannot process NULL channel!\n", DEV_NAME );
        return f_sSin;
	}

	/* Find the given channel.  No need to SpinLock here since we will not modify the pointer. */
    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
        if ( g_apEchoChanInst[ i ]->pvZapEcChan == f_pvEcChan )
        {
            pChan = g_apEchoChanInst[ i ];
            break;
        }
    }

	/* Check if it is one of our channels.  Otherwise fallback to the Zaptel echo canceller. */
    if ( pChan == NULL )
    {
        return echo_can_update( f_pvEcChan, f_sRin, f_sSin );
    }

	/* Check if the user thread is listening on this channel. */
	if ( pChan->fOpened != 1 )
	{
		if ( pChan->fChanPrintErr == 0 )
		{
			printk( KERN_WARNING "%s: OCTVQE service deactivated on zaptel channel #%d!\n", DEV_NAME, i+1 );
			pChan->fChanPrintErr = 1;
		}
		
        /* Call the DEFAULT echo canceller process function, if available. */
        if ( pChan->pvDefaultEchoCanContext != NULL )
        {
            return echo_can_update( pChan->pvDefaultEchoCanContext, f_sRin, f_sSin );
        }
        else
        {
            /* Cannot "echo cancel" this. */
            return f_sSin;
        }
	}
	
	pChan->fChanPrintErr = 0;

    if ( pChan->fChanOk )
	{
		spin_lock_irqsave( &pChan->Lock, ulFlags );

		/* If space to receive samples. */
		if ( pChan->iXinWriteBuf > -1 )
		{
			/* Accumulate samples until enough for processing. */
			pChan->asRinSamples[ pChan->iXinWriteBuf ][ pChan->ulXinWritePtr ] = f_sRin;
			pChan->asSinSamples[ pChan->iXinWriteBuf ][ pChan->ulXinWritePtr ] = f_sSin;
			pChan->ulXinWritePtr++;
			if ( pChan->ulXinWritePtr == BUFFER_SIZE )
			{
				iOldXinWriteBuf = pChan->iXinWriteBuf;
				pChan->iXinWriteBuf = ( pChan->iXinWriteBuf + 1 ) % NUM_BUFFERS;

				/* Reset write index. */
				pChan->ulXinWritePtr = 0;

				/* Check if about to overflow. */
				if ( pChan->iXinWriteBuf == pChan->iXinReadBuf ) 
				{
					/* Whoops, we're full, and have no where else
					to store into for the next samples.  We'll drop stuff
					until there's a buffer available */
							
					pChan->iXinWriteBuf = -1;
				}

				/* Check if just started receiving. */
				if ( pChan->iXinReadBuf < 0 ) 
				{ 
					/* Start out buffer if not already */
					pChan->iXinReadBuf = iOldXinWriteBuf;
				}

				/* Notify a blocked reader that there is data available
				to be read, unless we're waiting for it to be full */
				/*printk("Notifying reader data in buf %d\n", iOldXinWriteBuf); */
				fWakeUpReader = 1;
			}
		}
		else
		{
			/* No more space to put the samples in! */
			fXinOverflow = 1;

			/* Let the user know! */
			fWakeUpReader = 1;
		}

		/* If Sout buffer contains results. */
		if ( pChan->iSoutReadBuf > -1 )
		{
			/* We have at least received something from the OCTVQE service. */
			pChan->fChanReady = 1;

			/* Read in pending Sout sample. */
			sSout = pChan->asSoutSamples[ pChan->iSoutReadBuf ][ pChan->ulSoutReadPtr ];

			/* --- Check buffer status. */

			/* Increment Sout read pointer to remember this sample has been read. */
			pChan->ulSoutReadPtr++;
			if ( pChan->ulSoutReadPtr == BUFFER_SIZE )
			{
				/* We've reached the end of our buffer.  Go to the next. */
		
				iOldSoutReadBuf = pChan->iSoutReadBuf;
				pChan->iSoutReadBuf = ( pChan->iSoutReadBuf + 1 ) % NUM_BUFFERS;

				/* Reset read index. */
				pChan->ulSoutReadPtr = 0;

				/* Check if about to underflow. */
				if ( pChan->iSoutReadBuf == pChan->iSoutWriteBuf ) 
				{
					/* Whoops, we ran out of buffers.  Mark ours
					as -1 and wait for the filler to notify us that there 
					is something to write */

					pChan->iSoutReadBuf = -1;
				}

				/* Check if have just freed enough memory for the writer to wake up. */
				if ( pChan->iSoutWriteBuf < 0 ) 
				{ 
					/* Start out buffer if not already */
					pChan->iSoutWriteBuf = iOldSoutReadBuf;
				}

				/* Wake up the write thread, in case it is sleeping! */
				fWakeUpWrite = 1;
			}
		}
		else
		{
			/* Running out of samples on the Sout side! */
			/* The user thread is probably choking.. */
					
			if ( pChan->fChanReady == 1)
			{
				fSoutUnderflow = 1;
			}
					
			/* Wake up the writer! */
			fWakeUpWrite = 1;
			
			/* Send Silence  */
			sSout = 0; /* f_sSin; */
		}

		spin_unlock_irqrestore( &pChan->Lock, ulFlags );

		/* Wake up any waiting process -- If needed. */
		if ( fWakeUpReader == 1 ) /* wake_up_interruptible waiting on read */
		{
			wake_up_interruptible( &pChan->ReadWaitQueue );

      		/* Retrieve CPU time before processing. */
     		rdtscl( pChan->ulTimestampIn ); 
		}

		if ( fWakeUpWrite == 1 ) /* wake_up_interruptible waiting on write */
			wake_up_interruptible( &pChan->WriteWaitQueue );

		if ( ( fWakeUpReader == 1 ) || ( fWakeUpWrite == 1 ) ) /* wake_up_interruptible waiting on select */
			wake_up_interruptible( &pChan->SelectWaitQueue );

		/* Check if we are about to run out of sout samples. */
		if ( fXinOverflow == 1 )
		{
			/* Buffer overrun!  The host needs to wake up! */
			if ( verbose == 1 )
			{
				if ( octvqe_ratelimit() == 0 )
					printk( KERN_WARNING "%s: Xin buffer overrun on channel #%d\n", DEV_NAME, i+1 );
			}
		}

		if ( fSoutUnderflow == 1 )
		{
			/* Buffer underrun!  We really need some samples! */
			if ( verbose == 1 )
			{
				if ( octvqe_ratelimit() == 0 )
					printk( KERN_WARNING "%s: Sout buffer underrun on channel #%d\n", DEV_NAME, i+1 );
			}
		}
		pChan->ulReceivedSamples++;
	}
    
    if ( pChan->fChanOk == 0 )
    {
        /* If something is wrong with the echo canceller, return Sin. */
        sSout = f_sSin;
    }
	
    return sSout;
}

static int octdev_open(struct inode *inode, struct file *file) 
{
    unsigned int iMinor;
	unsigned long ulFlags;

    /* Retrieve the iNode minor which will give us the echo channel "index" */
#ifdef LINUX26
    iMinor = iminor( inode );
#else /* LINUX26 */
	iMinor = MINOR( inode->i_rdev );
#endif /* LINUX26 */

    /* Check if we support that many channels. */
    if ( iMinor >= MAX_NUM_SUPPORTED_CHANNELS )
    {
        printk( KERN_WARNING "%s: Cannot open channel #%d, only %d channels supported\n", DEV_NAME, iMinor+1, MAX_NUM_SUPPORTED_CHANNELS );
        return -EINVAL;
    }

    /* Check if the channel is already opened. */
    if ( g_apEchoChanInst[ iMinor ]->fOpened != 0 )
    {
        printk( KERN_WARNING "%s: Echo cancellation channel #%d is already opened\n", DEV_NAME, iMinor+1 );
        return -EINVAL;
    }

#ifdef LINUX26
	try_module_get( THIS_MODULE );
#else /* LINUX26 */
	/* Increment module usage count. */
	MOD_INC_USE_COUNT;
#endif /* LINUX26 */	

    /* Remember which channel index we are trying to retrieve data for. */
    file->private_data = g_apEchoChanInst[ iMinor ];

	spin_lock_irqsave( &g_apEchoChanInst[ iMinor ]->Lock, ulFlags );

	/* Remember that this channel is opened. */
	g_apEchoChanInst[ iMinor ]->fOpened = 1;

	spin_unlock_irqrestore( &g_apEchoChanInst[ iMinor ]->Lock, ulFlags );

    return SUCCESS;
}

/* This function is called when application closes access to device */ 
static int octdev_release( struct inode *inode, struct file *file )
{
    unsigned int iMinor;
	unsigned long ulFlags;

	/* Decrement module usage count. */
#ifdef LINUX26
	module_put( THIS_MODULE );
#else /* LINUX26 */
	MOD_DEC_USE_COUNT;
#endif /* LINUX26 */

    /* Retrieve the iNode minor which will give us the echo channel "index" */
#ifdef LINUX26
    iMinor = iminor( inode );
#else /* LINUX26 */
	iMinor = MINOR( inode->i_rdev );
#endif /* LINUX26 */

    /* Check if we support that many channels. */
    if ( iMinor >= MAX_NUM_SUPPORTED_CHANNELS )
    {
        printk( KERN_WARNING "%s: Cannot close channel #%d, only %d channels supported\n", DEV_NAME, iMinor+1, MAX_NUM_SUPPORTED_CHANNELS );
        return -EINVAL;
    }

	spin_lock_irqsave( &g_apEchoChanInst[ iMinor ]->Lock, ulFlags );

    /* Reset read index pointer. */
    g_apEchoChanInst[ iMinor ]->fOpened = 0;

	spin_unlock_irqrestore( &g_apEchoChanInst[ iMinor ]->Lock, ulFlags );

    return SUCCESS;
}

/* Called when a process, which already opened the dev file, attempts to */
/* read from it.  In our case, we return the pending bytes on Rin/Sin. */

static ssize_t octdev_read(
                       struct file *filp,
                       char *f_pUserBuf,    /* The buffer to fill with data     */
                       size_t f_Length,   /* The length of the buffer         */
                       loff_t *offset ) /* Our offset in the file, not used */
{
    /* Number of bytes actually written to the buffer */
    int iBytesRead = 0;
	int iXinReadBuf;
	int iOldXinReadBuf;
    unsigned long ulRc = 0;
    tPOCTVQE_CHAN_INSTANCE pChan;
    unsigned long ulFlags;

    /* Retrieve channel index. */
    
    pChan = (tPOCTVQE_CHAN_INSTANCE)filp->private_data;
    if ( pChan == NULL )
    {
        printk( KERN_ERR "%s: Invalid private data (NULL)\n", DEV_NAME );
        return -EINVAL;
    }

	/* Check if the channel has been opened (fopen??) */
    if ( pChan->fOpened == 0 )
    {
        printk( KERN_WARNING "%s: Channel #%lu not opened\n", DEV_NAME, pChan->ulChannelIndex+1 );
        return -EINVAL;
    }

	/* Check if we can at least fit buffer sample of both Rin+Sin. */
	if ( f_Length < (2*2*BUFFER_SIZE) /* Rin+Sin */ )
    {
        printk( KERN_WARNING "%s: Channel #%lu byte space provided less then %d bytes\n", DEV_NAME, pChan->ulChannelIndex+1, 2*2*BUFFER_SIZE );
        return -EINVAL;
    }

	/* Spin until there is something to read, unless non-blocking. */
	for ( ;; ) 
	{
		spin_lock_irqsave( &pChan->Lock, ulFlags );

		iXinReadBuf = pChan->iXinReadBuf;

		spin_unlock_irqrestore( &pChan->Lock, ulFlags );

		/* Check if there's something to read. */
		if ( iXinReadBuf >= 0 ) 
			break;

		/* Nope..  Check if the user requested to block here. */
		if ( filp->f_flags & O_NONBLOCK )
			return -EAGAIN;

		/* User requested to block here until a buffer was available.  No */
		/* problem with that. */

		/* Sleep in user space until woken up. */
		{
			DECLARE_WAITQUEUE( WaitQ, current );

			add_wait_queue( &pChan->ReadWaitQueue, &WaitQ );

			current->state = TASK_INTERRUPTIBLE;

			if ( !signal_pending( current ) ) 
				schedule();

			current->state = TASK_RUNNING;
			remove_wait_queue( &pChan->ReadWaitQueue, &WaitQ );

			if ( signal_pending( current ) ) 
				return -ERESTARTSYS;
			
			/* All's good, carry on. */
		}
	}

    /* Some signal left for transferring. */
    /* Put the data into the user buffer */

    /* The buffer is in the user data segment, not the kernel segment; */
    /* assignment won't work.  We have to use copy_to_user which copies data from */
    /* the kernel data segment to the user data segment. */

    ulRc = copy_to_user( &f_pUserBuf[ iBytesRead ], 
                         pChan->asRinSamples[ iXinReadBuf ],
                         2 * BUFFER_SIZE );
    if ( ulRc == 0 )
	{
		iBytesRead += 2 * BUFFER_SIZE;

		ulRc = copy_to_user( &f_pUserBuf[ iBytesRead ], 
							 &pChan->asSinSamples[ iXinReadBuf ], 
							 2 * BUFFER_SIZE );

		if ( ulRc == 0 )
			iBytesRead += 2 * BUFFER_SIZE;
	}

	/* Update pointers. */
	spin_lock_irqsave( &pChan->Lock, ulFlags );
	
	iOldXinReadBuf = pChan->iXinReadBuf;
	pChan->iXinReadBuf = ( pChan->iXinReadBuf + 1 ) % NUM_BUFFERS;

	/* Check if we read everything, most probably.  */
	if ( pChan->iXinReadBuf == pChan->iXinWriteBuf ) 
	{
		/* Out of stuff for now, let the interrupt guy know. */
		pChan->iXinReadBuf = -1;
	}
	
	/* Check if our Xin buffers were full. */
	if ( pChan->iXinWriteBuf < 0 ) 
	{ 
		/* A buffer is cleared for the interrupt, ready to be filled!!! */
		pChan->iXinWriteBuf = iOldXinReadBuf;
	}

    spin_unlock_irqrestore( &pChan->Lock, ulFlags );

    /* If something went wrong, return an error to the upper space. */
    if ( ulRc != 0 )
    {
        printk( KERN_ERR "%s: copy_to_user failed with error %lu\n", DEV_NAME, ulRc );
        return -EFAULT;
    }

    /* Return the number of bytes put into the buffer */
    return iBytesRead;
}

static ssize_t octdev_write(
                       struct file *filp,
                       const char *f_pUserBuf,  /* The buffer containing data to be written */
                       size_t f_Length,   /* The length of the buffer                 */
                       loff_t *offset )   /* Our offset in the file, not used         */
{
    /* Number of bytes actually written to the buffer */
    int iBytesWritten = 0;
	int iSoutWriteBuf;
	int iOldSoutWriteBuf;
    unsigned long ulRc = 0;
    unsigned long ulFlags;
	int fSoutBufferOverflow = 0;
    tPOCTVQE_CHAN_INSTANCE pChan;

    /* Retrieve channel index. */
    pChan = (tPOCTVQE_CHAN_INSTANCE)filp->private_data;
    if ( pChan == NULL )
    {
        printk( KERN_ERR "%s: Invalid private data channel (NULL)\n", DEV_NAME );
        return -EINVAL;
    }

	/* Check if the channel has been opened (fopen??) */
    if ( pChan->fOpened == 0 )
    {
        printk( KERN_WARNING "%s: Channel #%lu not opened\n", DEV_NAME, pChan->ulChannelIndex+1 );
        return -EINVAL;
    }

    /* Check passed length. */
    if ( f_Length != BUFFER_SIZE * 2 )
	{
        printk( KERN_WARNING "%s: written data for chan #%lu must be the size of a buffer (%d)\n", DEV_NAME, pChan->ulChannelIndex+1, BUFFER_SIZE );
        return -EINVAL;
	}

	/* Spin until there is place to write, unless non-blocking. */
	for ( ;; ) 
	{
		spin_lock_irqsave( &pChan->Lock, ulFlags );

		iSoutWriteBuf = pChan->iSoutWriteBuf;

		spin_unlock_irqrestore( &pChan->Lock, ulFlags );

		/* Check if there's some place to write. */
		if ( iSoutWriteBuf >= 0 ) 
			break;

		/* Nope..  Check if the user requested to block here. */
		if ( filp->f_flags & O_NONBLOCK )
			return -EAGAIN;

		/* User requested to block here until a buffer was available.  No */
		/* problem with that. */

		/* Sleep in user space until woken up. */
		{
			DECLARE_WAITQUEUE( WaitQ, current );

			add_wait_queue( &pChan->WriteWaitQueue, &WaitQ );

			current->state = TASK_INTERRUPTIBLE;

			if ( !signal_pending( current ) ) 
				schedule();

			current->state = TASK_RUNNING;
			remove_wait_queue( &pChan->WriteWaitQueue, &WaitQ );

			if ( signal_pending( current ) ) 
				return -ERESTARTSYS;
			
			/* All's good, carry on. */
		}
	}

	/* Ok, some space available. */

	/* The buffer is in the user data segment, not the kernel segment; */
	/* assignment won't work.  We have to use copy_from_user which copies data from */
	/* the user data segment to the kernel data segment. */

	ulRc = copy_from_user( pChan->asSoutSamples[ iSoutWriteBuf ],
						 &f_pUserBuf[ iBytesWritten ], 
						 BUFFER_SIZE * 2 );
	if ( ulRc == 0 )
		iBytesWritten += BUFFER_SIZE * 2;

	iOldSoutWriteBuf = iSoutWriteBuf;
	pChan->ulProcessedBuf++;

	spin_lock_irqsave( &pChan->Lock, ulFlags );

    /* Be extra careful with the locks and the variables we touch. */
	pChan->iSoutWriteBuf = ( iSoutWriteBuf + 1 ) % NUM_BUFFERS;

	/* Check if about to overflow. */
	if ( pChan->iSoutWriteBuf == pChan->iSoutReadBuf ) 
	{
		/* Whoops, we're full, and have no space left
		to store into for the next buffer.  We'll have to wait
		until there's a buffer available, the next time we are called. */
		
		pChan->iSoutWriteBuf = -1;
		
		fSoutBufferOverflow = 1;
	}

	/* Check if just started receiving. */
	if ( pChan->iSoutReadBuf < 0 ) 
	{ 
		/* Start out buffer if not already */
		pChan->iSoutReadBuf = iOldSoutWriteBuf;
	}
	
    spin_unlock_irqrestore( &pChan->Lock, ulFlags );

	/* Check if the Sout buffer overflowed. */
	if ( fSoutBufferOverflow == 1 )
	{
		if ( verbose == 1 )
		{
			/* CPMODIF:  I do not think this is a problem..  No need to log here. */
			/* if ( octvqe_ratelimit() == 0 )
				printk( KERN_WARNING "%s: Sout write Q is full for channel #%d\n", DEV_NAME, pChan->ulChannelIndex+1 ); */
		}
	}

    /* If something went wrong, return an error to the upper space. */
    if ( ulRc != 0 )
    {
        printk( KERN_ERR "%s: copy_from_user failed with error %lu\n", DEV_NAME, ulRc );
        return -EINVAL;
    }

    /* Return the number of bytes put into the buffer */
    return iBytesWritten;
}

/* unsigned int (*poll) (struct file *, struct poll_table_struct *);
The poll method is the back end of three system calls: poll, epoll, and select, all of
which are used to query whether a read or write to one or more file descriptors
would block. The poll method should return a bit mask indicating whether nonblocking
reads or writes are possible, and, possibly, provide the kernel with
information that can be used to put the calling process to sleep until I/O
becomes possible. If a driver leaves its poll method NULL, the device is assumed to
be both readable and writable without blocking. */
static unsigned int octdev_poll(
                       struct file *filp,
                       struct poll_table_struct * f_pPollStruct )
{
    tPOCTVQE_CHAN_INSTANCE pChan;
    unsigned int iMask = 0;
    unsigned long ulFlags;

    /* Retrieve channel index. */
    pChan = (tPOCTVQE_CHAN_INSTANCE)filp->private_data;
    if ( pChan == NULL )
    {
        printk( KERN_ERR "%s: Invalid private data channel (NULL)\n", DEV_NAME );
        return iMask;
    }

	poll_wait( filp, &pChan->SelectWaitQueue, f_pPollStruct );

	spin_lock_irqsave( &pChan->Lock, ulFlags );

	if ( pChan->iXinReadBuf > -1 )
		iMask |= POLLIN | POLLRDNORM; /* readable */
		
	spin_unlock_irqrestore( &pChan->Lock, ulFlags );

	return iMask;
}

static int octdev_ioctl( struct inode * inode , struct file * filp, unsigned int cmd , unsigned long arg )
{
    tPOCTVQE_CHAN_INSTANCE pChan;

    /* Retrieve channel index. */
    pChan = (tPOCTVQE_CHAN_INSTANCE)filp->private_data;
    if ( pChan == NULL )
    {
        printk( KERN_ERR "%s: Invalid private data channel (NULL)\n", DEV_NAME );
        return -EINVAL;
    }

	switch( cmd ) 
	{
	case OCTDEV_IOCTL_TAIL_LENGTH:
		if ( copy_from_user( &pChan->iTailLength, (int *)arg, sizeof( pChan->iTailLength ) ) )
			return -EFAULT;
		break;
	case OCTDEV_IOCTL_TAIL_DISPLACEMENT:
		if ( copy_from_user( &pChan->iTailDisplacement, (int *)arg, sizeof( pChan->iTailDisplacement ) ) )
			return -EFAULT;
		break;
	case OCTDEV_IOCTL_ERL:
		if ( copy_from_user( &pChan->iDefaultErl, (int *)arg, sizeof( pChan->iDefaultErl ) ) )
			return -EFAULT;
		break;		
	case OCTDEV_IOCTL_RIN_ANR:
		if ( copy_from_user( &pChan->fRinAnr, (int *)arg, sizeof( pChan->fRinAnr ) ) )
			return -EFAULT;
		break;
	case OCTDEV_IOCTL_SOUT_ANR:
		if ( copy_from_user( &pChan->fSoutAnr, (int *)arg, sizeof( pChan->fSoutAnr ) ) )
			return -EFAULT;
		break;		
	case OCTDEV_IOCTL_RIN_CURRENT_ENERGY:
		if ( copy_from_user( &pChan->iRinCurrentEnergy, (int *)arg, sizeof( pChan->iRinCurrentEnergy ) ) )
			return -EFAULT;
		break;
		
	case OCTDEV_IOCTL_SIN_CURRENT_ENERGY:
		if ( copy_from_user( &pChan->iSinCurrentEnergy, (int *)arg, sizeof( pChan->iSinCurrentEnergy ) ) )
			return -EFAULT;
		break;		
	case OCTDEV_IOCTL_RIN_AVERAGE_ENERGY:
		if ( copy_from_user( &pChan->iRinAverageEnergy, (int *)arg, sizeof( pChan->iRinAverageEnergy ) ) )
			return -EFAULT;
		break;
	case OCTDEV_IOCTL_SIN_AVERAGE_ENERGY:
		if ( copy_from_user( &pChan->iSinAverageEnergy, (int *)arg, sizeof( pChan->iSinAverageEnergy ) ) )
			return -EFAULT;
		break;
	case OCTDEV_IOCTL_AVERAGE_TIME_US:
		if ( copy_from_user( &pChan->iAverageTimeUs, (int *)arg, sizeof( pChan->iAverageTimeUs ) ) )
			return -EFAULT;
		break;
	case OCTDEV_IOCTL_MAX_TIME_US:
		if ( copy_from_user( &pChan->iMaxTimeUs, (int *)arg, sizeof( pChan->iMaxTimeUs ) ) )
			return -EFAULT;
		break;		
	default:
		return -ENOTTY;
	}
		
	return 0;
}

void * octdev_seq_start( struct seq_file *sfile, loff_t *pos )
{
    if ( *pos >= MAX_NUM_SUPPORTED_CHANNELS )
        return NULL; /* No more to read */
    return g_apEchoChanInst[ *pos ];	
}

void * octdev_seq_next( struct seq_file *s, void *v, loff_t *pos )
{
    (*pos)++;
    if ( *pos >= MAX_NUM_SUPPORTED_CHANNELS )
        return NULL;
    return g_apEchoChanInst[ *pos ];
}

void octdev_seq_stop( struct seq_file *sfile, void *v )
{
    /* Nothing to cleanup. */
}

int octdev_seq_show( struct seq_file *s, void *v )
{
    int i = 0;
    
    tPOCTVQE_CHAN_INSTANCE pChan = (tPOCTVQE_CHAN_INSTANCE)v;

    if ( pChan == g_apEchoChanInst[ 0 ] )
    {
        /* Retrieve API version and print it out. */
#ifndef OCTVQE_TRIAL_VERSION
        seq_printf( s, "%s, built on %s %s\n", OCTVQE_MODULE_VERSION, __DATE__, __TIME__ );
#else /* OCTVQE_TRIAL_VERSION */
        seq_printf( s, "*** TRIAL *** %s, built on %s %s\n", OCTVQE_MODULE_VERSION, __DATE__, __TIME__ );
#endif /* OCTVQE_TRIAL_VERSION */
    }

    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
    	if ( g_apEchoChanInst[ i ] == pChan )
    	    break;
    }

	if ( pChan )
	{
		/* critical section ... */
		seq_printf( s,"Channel %i (%s) (%s)\n", 
				i+1, 
				pChan->fChanOk ? "Active" : "Inactive",
				pChan->fOpened ? "Octvqed Connected" : "NO Octvqed Connected" );

		if ( pChan->fOpened != 0 )
		{
 			seq_printf( s, "  Tail Length (%d ms), Default ERL (%d dB)\n", 
				pChan->iTailLength, 
				pChan->iDefaultErl  );
				
			seq_printf( s, "  Rin/Sin Current Energy (%d dB, %d dB), Av. Energy (%d dB, %d dB)\n", 
				pChan->iRinCurrentEnergy, 
				pChan->iSinCurrentEnergy,
				pChan->iRinAverageEnergy, 
				pChan->iSinAverageEnergy );
				
			seq_printf( s, "  Number Buffers (%d), Buffer Size (%d ms), Av./Max CPU Time (%d us, %d us)\n", 
				(int)pChan->ulProcessedBuf, 
				(int)(BUFFER_SIZE / 8),
				pChan->iAverageTimeUs,
				pChan->iMaxTimeUs );
		}
	}
       
    return 0;
}

int octdev_proc_open( struct inode *inode, struct file *file );

static struct file_operations octdev_proc_ops = {
    .owner = THIS_MODULE,
    .open = octdev_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release
};

static struct seq_operations octdev_seq_ops = {
    .start = octdev_seq_start,
    .next = octdev_seq_next,
    .stop = octdev_seq_stop,
    .show = octdev_seq_show
};

int octdev_proc_open( struct inode *inode, struct file *file )
{
    return seq_open( file, &octdev_seq_ops );
}

static struct file_operations octdev_fops = {
    owner:            THIS_MODULE,
    open:             octdev_open,
    read:             octdev_read,
    write:            octdev_write,
    release:          octdev_release,
    poll:             octdev_poll,
    ioctl:            octdev_ioctl,
};

static int octvqe_init(void)
{
    int				          iInstanceSize;
    int                       i;
    struct proc_dir_entry *   proc_entry;

    /* Assign a default major number to the character device. */
    /* Note that minor numbers from 0 to 255 are reserved for us after this point. */
    iMajor = register_chrdev( 0, DEV_NAME, &octdev_fops );
    if ( iMajor < 0 ) 
    {
        printk( KERN_ERR "Registering the dev %s failed with %d\n", DEV_NAME, iMajor );
        return iMajor;
    }

    /* The instance size is the basic structure size, plus the size of the diagnostic buffer, if requested. */
    iInstanceSize = sizeof( tOCTVQE_CHAN_INSTANCE );

    /* Allocate channel working structures. */
    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
        g_apEchoChanInst[ i ] = (tPOCTVQE_CHAN_INSTANCE)kmalloc( iInstanceSize, GFP_KERNEL );
        if ( g_apEchoChanInst[ i ] == NULL )
        {
            printk( KERN_ERR "Allocating %d bytes of memory for dev %s failed\n", iInstanceSize, DEV_NAME );
            break;
        }
        else
        {
            /* Clear this memory out. */
            memset( g_apEchoChanInst[ i ], 0x0, iInstanceSize );
        }
    }

    /* Check if could allocate memory correctly. */
    if ( i != MAX_NUM_SUPPORTED_CHANNELS )
    {
        for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
        {
            if ( g_apEchoChanInst[ i ] != NULL )
            {
                kfree( g_apEchoChanInst[ i ] );
                g_apEchoChanInst[ i ] = NULL;
            }
        }

        /* Unregister character device since it will not be used due to lack of memory. */
        unregister_chrdev( iMajor, DEV_NAME );
        return FAIL;
    }

    /* Initialize the wait queues. */
	for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
	{
		init_waitqueue_head( &g_apEchoChanInst[ i ]->ReadWaitQueue );
		init_waitqueue_head( &g_apEchoChanInst[ i ]->WriteWaitQueue );
		init_waitqueue_head( &g_apEchoChanInst[ i ]->SelectWaitQueue );

		spin_lock_init( &g_apEchoChanInst[ i ]->Lock );

		g_apEchoChanInst[ i ]->iXinReadBuf = -1;
		g_apEchoChanInst[ i ]->iSoutReadBuf = -1;
	}

    /* Try to create a proc entry under the /proc directory. */
    proc_entry = create_proc_entry( DEV_NAME, 0, NULL );
    if ( proc_entry )
        proc_entry->proc_fops = &octdev_proc_ops;
	
    printk( KERN_INFO "%s: %s. Using major %d, Default EC \"%s%s\" (Built on %s %s).\n", 
        DEV_NAME, OCTVQE_MODULE_VERSION, iMajor, ZAPTEL_ECHO_NAME, 
        ZAPTEL_ECHO_AGGRESSIVE, __DATE__, __TIME__ );
        
    return 0;
}

static void octvqe_exit( void )
{
    int iStatus; 
    int i;

    /* Remove device name proc entry. */
    remove_proc_entry( DEV_NAME, NULL );
	
    for ( i=0; i<MAX_NUM_SUPPORTED_CHANNELS; i++ )
    {
        if ( g_apEchoChanInst[ i ] != NULL )
        {
            kfree( g_apEchoChanInst[ i ] );
            g_apEchoChanInst[ i ] = NULL;
        }
    }

    iStatus = unregister_chrdev( iMajor, DEV_NAME );
    if ( iStatus < 0 )
        printk( KERN_WARNING "%s: Error %d unregister_chrdev\n", DEV_NAME, iStatus );

    printk( KERN_INFO "%s: Echo cancellation support unloaded\n", DEV_NAME );
}

module_init( octvqe_init );
module_exit( octvqe_exit );

MODULE_AUTHOR( "Octasic Inc." );
MODULE_DESCRIPTION( "OCTVQE echo canceller" );
MODULE_LICENSE( "GPL" );
MODULE_SUPPORTED_DEVICE( DEV_NAME );
