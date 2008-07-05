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

Description:  Zaptel Linux OCTVQE module header

$Octasic_Release: OCTVQE8-01.01.02-PR $

$Octasic_Revision: 5 $

\*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/

#ifndef __OCTVQE_LINUX_H__
#define __OCTVQE_LINUX_H__

/*****************************  FUNCTIONS  ***********************************/

void *ZapOctVqeApiEcChannelInitialize(int f_iLen, int f_iAdaptionMode);

int ZapOctVqeApiEcChannelTrainTap(void *f_pvEcChan, int f_iPos, short f_sVal);

short ZapOctVqeApiEcChannelProcess(void *f_pvEcChan, short f_sRin,
	short f_sSin);

void ZapOctVqeApiEcChannelFree(void *f_pvEcChan);

#endif /* __OCTVQE_LINUX_H__ */
