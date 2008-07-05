/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\

File: octvqe_linux.h

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

Description:  Zaptel OCTVQE module I/O control defines

$Octasic_Release: OCTVQE8-01.01.02-PR $

$Octasic_Revision: 4 $

\*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

#ifndef __OCTVQE_IOCTL_H__
#define __OCTVQE_IOCTL_H__


/* Use 'o' as magic number */
#define OCTDEV_IOCTL_MAGIC 'o'
#define OCTDEV_IOCTL_TAIL_LENGTH _IOW(OCTDEV_IOCTL_MAGIC, 0, int)
#define OCTDEV_IOCTL_TAIL_DISPLACEMENT _IOW(OCTDEV_IOCTL_MAGIC, 1, int)
#define OCTDEV_IOCTL_ERL _IOW(OCTDEV_IOCTL_MAGIC, 2, int)
#define OCTDEV_IOCTL_RIN_ANR _IOW(OCTDEV_IOCTL_MAGIC, 3, int)
#define OCTDEV_IOCTL_SOUT_ANR _IOW(OCTDEV_IOCTL_MAGIC, 4, int)
#define OCTDEV_IOCTL_RIN_CURRENT_ENERGY _IOW(OCTDEV_IOCTL_MAGIC, 5, int)
#define OCTDEV_IOCTL_SIN_CURRENT_ENERGY _IOW(OCTDEV_IOCTL_MAGIC, 6, int)
#define OCTDEV_IOCTL_RIN_AVERAGE_ENERGY _IOW(OCTDEV_IOCTL_MAGIC, 7, int)
#define OCTDEV_IOCTL_SIN_AVERAGE_ENERGY _IOW(OCTDEV_IOCTL_MAGIC, 8, int)
#define OCTDEV_IOCTL_AVERAGE_TIME_US _IOW(OCTDEV_IOCTL_MAGIC, 9, int)
#define OCTDEV_IOCTL_MAX_TIME_US _IOW(OCTDEV_IOCTL_MAGIC, 10, int)
#define OCTDEV_IOCTL_MAXNR 10

#endif /* __OCTVQE_IOCTL_H__ */
