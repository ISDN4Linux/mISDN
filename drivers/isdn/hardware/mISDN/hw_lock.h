/* $Id: hw_lock.h,v 1.5 2003/07/27 11:14:19 kkeil Exp $
 *
 * hw_lock.h  Hardware locking inline routines
 *
 * Author Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
*/

/* Description of the locking mechanism
 * 
 * The locking must grant serialisized and atomic
 * access to the ISDN hardware registers, if the lock
 * is aquired no other process or IRQ is alloed to
 * access ISDN hardware registers.
 *
 * In general here are 3 possible entry points:
 *    1. the ISDN interrupt routine
 *    2. ISDN timer routines in the hardware module
 *    3. messages that came from upper layers
 *
 * Since most work must be do in the interrupt routine
 * (to grant minimum IRQ latency) and only few things with
 * need direct HW access must be done for messages from upper
 * layers, we should allow other IRQs in our IRQ routines and
 * only block our own routines in this case. Since the common IRQ
 * routines allready mask the same IRQ, we only need to protect us
 * from timer and uper layers. The disadvantage is, that we need to
 * disable local IRQ for the 2. and 3. points, but since the routines
 * which need hardware access are well known and small, the impact
 * is very small.
 *
 * We have a two stage locking to make this working:
 * A spinlock which protect the state LOCK Flag (STATE_FLAG_BUSY) and
 * also protect us from local IRQs from the entry points 2 and 3.
 *
 * In the hardware IRQ we aquire the spinlock, set the STATE_FLAG_BUSY
 * LOCK Flag and then release the spinlock. It can never happen that
 * the STATE_FLAG_BUSY is allready set in this case, see later.
 *
 * In the other cases (from timer or upper layers) we aquire the spinlock
 * test_and_set the STATE_FLAG_BUSY LOCK Flag, if it was allready set
 * (a ISDN IRQ is running on the other CPU) we schedule timeout or add a other
 * small timeout.
 * If it was not set, we have the lock and we don't release the spinlock until we have
 * done the harware work.
 *
 * To avoid any kind of deadlocking, it is important that we release the lock
 * before we call functions that deliver to upper layers.
 * To leave the impact of disabled local IRQ small, it is important to only protect
 * small areas where hardware is accessed.
 *
 * The following routines handle the lock in the entry point from upper layers and other
 * none IRQ cases (module init/exit stuff).
 *
 * They never called directly, but via the wrappers assigned to the instance
 * inst.lock / inst.unlock pointers.
 *
 * Here are two defines which can be used for DEBUGING and PROFILING
 * SPIN_DEBUG and LOCK_STATISTIC
 *
 */
#ifndef __hw_lock__
#define __hw_lock__

typedef struct _mISDN_HWlock {
	u_long			flags;
	spinlock_t		lock;
#ifdef SPIN_DEBUG
	void			*spin_adr;
	void			*busy_adr;
#endif
	volatile u_long		state;
#ifdef LOCK_STATISTIC
	u_int			try_ok;
	u_int			try_wait;
	u_int			try_inirq;
	u_int			try_mult;
	u_int			irq_ok;
	u_int			irq_fail;
#endif
} mISDN_HWlock_t;

#define STATE_FLAG_BUSY		1
#define STATE_FLAG_INIRQ	2


/*
 * returns 0 if the lock was aquired
 * returns 1 if nowait != 0 and the lock is not aquired 
 */
static inline int lock_HW(mISDN_HWlock_t *lock, int nowait)
{
	register u_long	flags;
#ifdef LOCK_STATISTIC
	int	wait = 0;
#endif

	spin_lock_irqsave(&lock->lock, flags);
#ifdef SPIN_DEBUG
	lock->spin_adr = __builtin_return_address(0);
#endif
	while (test_and_set_bit(STATE_FLAG_BUSY, &lock->state)) {
		/* allready busy so we delay */
		spin_unlock_irqrestore(&lock->lock, flags);
#ifdef SPIN_DEBUG
		lock->spin_adr = NULL;
#endif
		if (nowait) {
#ifdef LOCK_STATISTIC
			lock->try_wait++;
#endif 
			return(1);
		}
		/* delay 1 jiffie is enought */
		if (in_interrupt()) {
			/* Should never happen */
#ifdef LOCK_STATISTIC
			lock->try_inirq++;
#endif
			printk(KERN_ERR "lock_HW: try to schedule in IRQ state(%lx)\n",
				lock->state);
			mdelay(1);
		} else {
#ifdef LOCK_STATISTIC
			if (wait++)
				lock->try_mult++;
			else
				lock->try_wait++;
#endif
			schedule_timeout(1);
		}
		spin_lock_irqsave(&lock->lock, flags);
#ifdef SPIN_DEBUG
		lock->spin_adr = __builtin_return_address(0);
#endif
	}	
	/* get the LOCK */
	lock->flags = flags;
#ifdef SPIN_DEBUG
	lock->busy_adr = __builtin_return_address(0);
#endif
#ifdef LOCK_STATISTIC
	if (!wait)
		lock->try_ok++;
#endif
	return(0);
} 

static inline void unlock_HW(mISDN_HWlock_t *lock)
{
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &lock->state)) {
		printk(KERN_ERR "lock_HW: STATE_FLAG_BUSY not locked state(%lx)\n",
			lock->state);
	}
#ifdef SPIN_DEBUG
	lock->busy_adr = NULL;
	lock->spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&lock->lock, lock->flags);
}

static inline void lock_HW_init(mISDN_HWlock_t *lock)
{
	spin_lock_init(&lock->lock);
	lock->state = 0;
#ifdef SPIN_DEBUG
	lock->busy_adr = NULL;
	lock->spin_adr = NULL;
#endif
#ifdef LOCK_STATISTIC
	lock->try_ok = 0;
	lock->try_wait = 0;
	lock->try_inirq = 0;
	lock->try_mult = 0;
	lock->irq_ok = 0;
	lock->irq_fail = 0;
#endif
}

#endif
