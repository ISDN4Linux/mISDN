/*
  oslec_wrap.c
  David Rowe
  7 Feb 2007

  Wrapper for OSLEC to turn it into a kernel module compatable with Zaptel.

  patched to make it work with mISDN
  Peter Schlaile
  23 Aug 2007

  The /proc/oslec interface points to the first echo canceller
  instance created. Zaptel appears to create/destroy e/c on a call by
  call basis, and with the current echo can function interface it is
  difficult to tell which channel is assigned to which e/c.  So to
  simply the /proc interface (at least in this first implementation)
  we limit it to the first echo canceller created.

  So if you only have one call up on a system, /proc/oslec will refer
  to that.  That should be sufficient for debugging the echo canceller
  algorithm, we can extend it to handle multiple simultaneous channels
  later.
*/

/*
  Copyright (C) 2007 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2, as
  published by the Free Software Foundation.
   This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>


#define malloc(a) kmalloc((a), GFP_ATOMIC)
#define free(a) kfree(a)

#include "oslec.h"
#include "oslec_echo.h"

/* constants for isr cycle averaging */

#define LTC   5   /* base 2 log of TC */

/* number of cycles we are using per call */

struct echo_can_state *oslec_echo_can_create(int len, int adaption_mode)
{
  struct echo_can_state *ec;

  ec = (struct echo_can_state *)malloc(sizeof(struct echo_can_state));
  ec->ec = (void *)echo_can_create(len,   ECHO_CAN_USE_ADAPTION
					| ECHO_CAN_USE_NLP
					| ECHO_CAN_USE_CLIP
					| ECHO_CAN_USE_TX_HPF
					| ECHO_CAN_USE_RX_HPF);
  return ec;
}

void oslec_echo_can_free(struct echo_can_state *ec)
{
  echo_can_free((struct echo_can_state_s *)(ec->ec));
  free(ec);
}

short oslec_echo_can_update(struct echo_can_state *ec, short iref, short isig)
{
    short clean;

    clean = echo_can_update((struct echo_can_state_s *)(ec->ec), iref, isig);

    /*
      Simple IIR averager:

		   -LTC           -LTC
      y(n) = (1 - 2    )y(n-1) + 2    x(n)

    */

    return clean;
}

int oslec_echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{
	return 0;
}

