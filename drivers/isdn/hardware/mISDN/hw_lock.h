/* $Id: hw_lock.h,v 1.1 2003/06/21 21:39:54 kkeil Exp $
 *
 * hw_lock.h  HArdware locking inline routines
 *
 * Author Karsten Keil (keil@isdn4linux.de)
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
*/


#ifndef __hw_lock__
#define __hw_lock__

typedef struct _hisax_HWlock {
	u_long			flags;
	spinlock_t		lock;
#ifdef SPIN_DEBUG
	void			*spin_adr;
	void			*busy_adr;
#endif
	volatile u_int		state;
#ifdef LOCK_STATISTIC
	u_int			try_ok;
	u_int			try_wait;
	u_int			try_inirq;
	u_int			try_mult;
	u_int			irq_ok;
	u_int			irq_fail;
#endif
} hisax_HWlock_t;

#define STATE_FLAG_BUSY		1
#define STATE_FLAG_INIRQ	2


static inline void lock_HW(hisax_HWlock_t *lock)
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
		/* delay 1 jiffie is enought */
		if (in_interrupt()) {
#ifdef LOCK_STATISTIC
			lock->try_inirq++;
#endif
			printk(KERN_ERR "lock_HW: try to schedule in IRQ state(%x)\n",
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
} 

static inline void unlock_HW(hisax_HWlock_t *lock)
{
	if (!test_and_clear_bit(STATE_FLAG_BUSY, &lock->state)) {
		printk(KERN_ERR "lock_HW: STATE_FLAG_BUSY not locked state(%x)\n",
			lock->state);
	}
#ifdef SPIN_DEBUG
	lock->busy_adr = NULL;
	lock->spin_adr = NULL;
#endif
	spin_unlock_irqrestore(&lock->lock, lock->flags);
}

static inline void lock_HW_init(hisax_HWlock_t *lock)
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
