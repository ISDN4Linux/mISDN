/* $Id: core.h,v 2.0 2007/06/06 15:25:06 kkeil Exp $
 *
 * This file is (c) under GPL version 2
 *
 */

#ifndef mISDN_CORE_H
#define mISDN_CORE_H

extern struct mISDNdevice	*get_mdevice(u_int);
extern int			get_mdevice_count(void);

/* stack status flag */
#define mISDN_STACK_ACTION_MASK		0x0000ffff
#define mISDN_STACK_COMMAND_MASK	0x000f0000
#define mISDN_STACK_STATUS_MASK		0xfff00000
/* action bits 0-15 */
#define mISDN_STACK_WORK	0
#define mISDN_STACK_SETUP	1
#define mISDN_STACK_CLEARING	2
#define mISDN_STACK_RESTART	3
#define mISDN_STACK_WAKEUP	4
#define mISDN_STACK_ABORT	15
/* command bits 16-19 */
#define mISDN_STACK_STOPPED	16
#define mISDN_STACK_INIT	17
#define mISDN_STACK_THREADSTART	18
/* status bits 20-31 */
#define mISDN_STACK_BCHANNEL	20
#define mISDN_STACK_ACTIVE      29
#define mISDN_STACK_RUNNING     30
#define mISDN_STACK_KILLED      31


/* manager options */
#define MGR_OPT_USER		24
#define MGR_OPT_NETWORK		25

extern int	create_data_stack(struct mISDNchannel *, u_int,
			struct sockaddr_mISDN *);
extern int	connect_data_stack(struct mISDNchannel *, u_int,
			struct sockaddr_mISDN *);
extern int	create_mgr_stack(struct mISDNdevice *);
extern void 	set_mgr_channel(struct mISDNchannel *, struct mISDNstack *);
extern int	open_mgr_channel(struct mISDNstack *, u_int);
extern void	close_mgr_channel(struct mISDNstack *);
extern int	delete_stack(struct mISDNstack *);
extern int	register_stack(struct mISDNstack *, struct mISDNdevice *);
extern void	mISDN_initstack(u_int *);
#endif
