/*___________________________________________________________________________________*/
/*                                                                                   */
/*  (C) Copyright Cologne Chip AG, 2006                                              */
/*___________________________________________________________________________________*/
/*                                                                                   */

/*                                                                                   */
/*  File name:     xhfc24succ.h                                                      */
/*  File content:  This file contains the XHFC-2S4U / XHFC-4SU register definitions. */
/*  Creation date: 22.02.2006 13:14                                                  */
/*  Creator:       Genero 3.4                                                        */
/*  Data base:     HFC XML 1.6 for XHFC-1SU, XHFC-2SU, XHFC-2S4U and XHFC-4SU        */
/*  Address range: 0x00 - 0xFF                                                       */
/*                                                                                   */
/*  The information presented can not be considered as assured characteristics.      */
/*  Data can change without notice. Please check version numbers in case of doubt.   */
/*                                                                                   */
/*  For further information or questions please contact support@CologneChip.com      */
/*                                                                                   */
/*                                                                                   */
/*___________________________________________________________________________________*/
/*                                                                                   */
/*  WARNING: This file has been generated automatically and should not be            */
/*           changed to maintain compatibility with later versions.                  */
/*___________________________________________________________________________________*/
/*                                                                                   */


#ifndef _XHFC24SUCC_H_
#define _XHFC24SUCC_H_


typedef unsigned char BYTE;

typedef BYTE REGWORD;       // maximum register length (standard)
typedef BYTE REGADDR;       // address width



typedef enum {no=0, yes} REGBOOL;


typedef enum
{
	// register and bitmap access modes:
	writeonly=0,		// write only
	readonly,		// read only
	readwrite,		// read/write
	// following modes only for mixed mode registers:
	readwrite_write,	// read/write and write only
	readwrite_read,		// read/write and read only
	write_read,		// write only and read only
	readwrite_write_read	// read/write, write only and read only
} ACCESSMODE;



/*___________________________________________________________________________________*/
/*                                                                                   */
/* common chip information:                                                          */
/*___________________________________________________________________________________*/

	#define CHIP_NAME_2S4U		"XHFC-2S4U"
	#define CHIP_NAME_4SU		"XHFC-4SU"
	#define CHIP_TITLE_2S4U		"ISDN HDLC FIFO controller 2 S/T interfaces combined with 4 Up interfaces (Universal ISDN Ports)"
	#define CHIP_TITLE_4SU		"ISDN HDLC FIFO controller with 4 S/T interfaces combined with 4 Up interfaces (Universal ISDN Ports)"
	#define CHIP_MANUFACTURER	"Cologne Chip"
	#define CHIP_ID_2S4U		0x62
	#define CHIP_ID_4SU		0x63
	#define CHIP_REGISTER_COUNT	122
	#define CHIP_DATABASE		"Version HFC-XMLHFC XML 1.6 for XHFC-1SU, XHFC-2SU, XHFC-2S4U and XHFC-4SU - GeneroGenero 3.4 "

// This register file can also be used for XHFC-2SU and XHFC-1SU programming.
// For this reason these chip names, IDs and titles are defined here as well:

	#define CHIP_NAME_2SU		"XHFC-2SU"
	#define CHIP_TITLE_2SU		"ISDN HDLC FIFO controller with 2 combined S/T and Up Interfaces"
	#define CHIP_ID_2SU		0x61

	#define CHIP_NAME_1SU		"XHFC-1SU"
	#define CHIP_TITLE_1SU		"ISDN HDLC FIFO controller with a combined S/T and Up Interface"
	#define CHIP_ID_1SU		0x60




/*___________________________________________________________________________________*/
/*                                                                                   */
/*  Begin of XHFC-2S4U / XHFC-4SU register definitions.                              */
/*___________________________________________________________________________________*/
/*                                                                                   */

#define R_CIRM 0x00 // register access
	#define M_CLK_OFF 0x01 // bitmap mask (1bit)
	#define M_WAIT_PROC 0x02 // bitmap mask (1bit)
	#define M_WAIT_REG 0x04 // bitmap mask (1bit)
	#define M_SRES 0x08 // bitmap mask (1bit)
	#define M_HFC_RES 0x10 // bitmap mask (1bit)
	#define M_PCM_RES 0x20 // bitmap mask (1bit)
	#define M_SU_RES 0x40 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_clk_off:1;
		REGWORD v_wait_proc:1;
		REGWORD v_wait_reg:1;
		REGWORD v_sres:1;
		REGWORD v_hfc_res:1;
		REGWORD v_pcm_res:1;
		REGWORD v_su_res:1;
		REGWORD reserved_0:1;
	} bit_r_cirm; // register and bitmap data
	typedef union {REGWORD reg; bit_r_cirm bit;} reg_r_cirm; // register and bitmap access


#define R_CTRL 0x01 // register access
	#define M_FIFO_LPRIO 0x02 // bitmap mask (1bit)
	#define M_NT_SYNC 0x08 // bitmap mask (1bit)
	#define M_OSC_OFF 0x20 // bitmap mask (1bit)
	#define M_SU_CLK 0xC0 // bitmap mask (2bit)
		#define M1_SU_CLK 0x40

	typedef struct // bitmap construction
	{
		REGWORD reserved_1:1;
		REGWORD v_fifo_lprio:1;
		REGWORD reserved_2:1;
		REGWORD v_nt_sync:1;
		REGWORD reserved_3:1;
		REGWORD v_osc_off:1;
		REGWORD v_su_clk:2;
	} bit_r_ctrl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ctrl bit;} reg_r_ctrl; // register and bitmap access


#define R_CLK_CFG 0x02 // register access
	#define M_CLK_PLL 0x01 // bitmap mask (1bit)
	#define M_CLKO_HI 0x02 // bitmap mask (1bit)
	#define M_CLKO_PLL 0x04 // bitmap mask (1bit)
	#define M_PCM_CLK 0x20 // bitmap mask (1bit)
	#define M_CLKO_OFF 0x40 // bitmap mask (1bit)
	#define M_CLK_F1 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_clk_pll:1;
		REGWORD v_clko_hi:1;
		REGWORD v_clko_pll:1;
		REGWORD reserved_4:2;
		REGWORD v_pcm_clk:1;
		REGWORD v_clko_off:1;
		REGWORD v_clk_f1:1;
	} bit_r_clk_cfg; // register and bitmap data
	typedef union {REGWORD reg; bit_r_clk_cfg bit;} reg_r_clk_cfg; // register and bitmap access


#define A_Z1 0x04 // register access
	#define M_Z1 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_z1:8;
	} bit_a_z1; // register and bitmap data
	typedef union {REGWORD reg; bit_a_z1 bit;} reg_a_z1; // register and bitmap access


#define A_Z2 0x06 // register access
	#define M_Z2 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_z2:8;
	} bit_a_z2; // register and bitmap data
	typedef union {REGWORD reg; bit_a_z2 bit;} reg_a_z2; // register and bitmap access


#define R_RAM_ADDR 0x08 // register access
	#define M_RAM_ADDR0 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ram_addr0:8;
	} bit_r_ram_addr; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_addr bit;} reg_r_ram_addr; // register and bitmap access


#define R_RAM_CTRL 0x09 // register access
	#define M_RAM_ADDR1 0x0F // bitmap mask (4bit)
		#define M1_RAM_ADDR1 0x01
	#define M_ADDR_RES 0x40 // bitmap mask (1bit)
	#define M_ADDR_INC 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ram_addr1:4;
		REGWORD reserved_5:2;
		REGWORD v_addr_res:1;
		REGWORD v_addr_inc:1;
	} bit_r_ram_ctrl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_ctrl bit;} reg_r_ram_ctrl; // register and bitmap access


#define R_FIRST_FIFO 0x0B // register access
	#define M_FIRST_FIFO_DIR 0x01 // bitmap mask (1bit)
	#define M_FIRST_FIFO_NUM 0x1E // bitmap mask (4bit)
		#define M1_FIRST_FIFO_NUM 0x02

	typedef struct // bitmap construction
	{
		REGWORD v_first_fifo_dir:1;
		REGWORD v_first_fifo_num:4;
		REGWORD reserved_6:3;
	} bit_r_first_fifo; // register and bitmap data
	typedef union {REGWORD reg; bit_r_first_fifo bit;} reg_r_first_fifo; // register and bitmap access


#define R_FIFO_THRES 0x0C // register access
	#define M_THRES_TX 0x0F // bitmap mask (4bit)
		#define M1_THRES_TX 0x01
	#define M_THRES_RX 0xF0 // bitmap mask (4bit)
		#define M1_THRES_RX 0x10

	typedef struct // bitmap construction
	{
		REGWORD v_thres_tx:4;
		REGWORD v_thres_rx:4;
	} bit_r_fifo_thres; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_thres bit;} reg_r_fifo_thres; // register and bitmap access


#define A_F1 0x0C // register access
	#define M_F1 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_f1:8;
	} bit_a_f1; // register and bitmap data
	typedef union {REGWORD reg; bit_a_f1 bit;} reg_a_f1; // register and bitmap access


#define R_FIFO_MD 0x0D // register access
	#define M_FIFO_MD 0x03 // bitmap mask (2bit)
		#define M1_FIFO_MD 0x01
	#define M_DF_MD 0x0C // bitmap mask (2bit)
		#define M1_DF_MD 0x04
	#define M_UNIDIR_MD 0x10 // bitmap mask (1bit)
	#define M_UNIDIR_RX 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_md:2;
		REGWORD v_df_md:2;
		REGWORD v_unidir_md:1;
		REGWORD v_unidir_rx:1;
		REGWORD reserved_7:2;
	} bit_r_fifo_md; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_md bit;} reg_r_fifo_md; // register and bitmap access


#define A_F2 0x0D // register access
	#define M_F2 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_f2:8;
	} bit_a_f2; // register and bitmap data
	typedef union {REGWORD reg; bit_a_f2 bit;} reg_a_f2; // register and bitmap access


#define A_INC_RES_FIFO 0x0E // register access
	#define M_INC_F 0x01 // bitmap mask (1bit)
	#define M_RES_FIFO 0x02 // bitmap mask (1bit)
	#define M_RES_LOST 0x04 // bitmap mask (1bit)
	#define M_RES_FIFO_ERR 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_inc_f:1;
		REGWORD v_res_fifo:1;
		REGWORD v_res_lost:1;
		REGWORD v_res_fifo_err:1;
		REGWORD reserved_8:4;
	} bit_a_inc_res_fifo; // register and bitmap data
	typedef union {REGWORD reg; bit_a_inc_res_fifo bit;} reg_a_inc_res_fifo; // register and bitmap access


#define A_FIFO_STA 0x0E // register access
	#define M_FIFO_ERR 0x01 // bitmap mask (1bit)
	#define M_ABO_DONE 0x10 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_err:1;
		REGWORD reserved_11:3;
		REGWORD v_abo_done:1;
		REGWORD reserved_12:3;
	} bit_a_fifo_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_a_fifo_sta bit;} reg_a_fifo_sta; // register and bitmap access


#define R_FSM_IDX 0x0F // register access
	#define M_IDX 0x1F // bitmap mask (5bit)
		#define M1_IDX 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_idx:5;
		REGWORD reserved_10:3;
	} bit_r_fsm_idx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fsm_idx bit;} reg_r_fsm_idx; // register and bitmap access
	#define IDX_FSM_IDX 0x01 // index value selecting this multi-register


#define R_FIFO 0x0F // register access
	#define M_FIFO_DIR 0x01 // bitmap mask (1bit)
	#define M_FIFO_NUM 0x1E // bitmap mask (4bit)
		#define M1_FIFO_NUM 0x02
	#define M_REV 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_dir:1;
		REGWORD v_fifo_num:4;
		REGWORD reserved_9:2;
		REGWORD v_rev:1;
	} bit_r_fifo; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo bit;} reg_r_fifo; // register and bitmap access
	#define IDX_FIFO 0x00 // index value selecting this multi-register


#define R_SLOT 0x10 // register access
	#define M_SL_DIR 0x01 // bitmap mask (1bit)
	#define M_SL_NUM 0xFE // bitmap mask (7bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sl_dir:1;
		REGWORD v_sl_num:7;
	} bit_r_slot; // register and bitmap data
	typedef union {REGWORD reg; bit_r_slot bit;} reg_r_slot; // register and bitmap access


#define R_IRQ_OVIEW 0x10 // register access
	#define M_FIFO_BL0_IRQ 0x01 // bitmap mask (1bit)
	#define M_FIFO_BL1_IRQ 0x02 // bitmap mask (1bit)
	#define M_FIFO_BL2_IRQ 0x04 // bitmap mask (1bit)
	#define M_FIFO_BL3_IRQ 0x08 // bitmap mask (1bit)
	#define M_MISC_IRQ 0x10 // bitmap mask (1bit)
	#define M_CH_IRQ 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_bl0_irq:1;
		REGWORD v_fifo_bl1_irq:1;
		REGWORD v_fifo_bl2_irq:1;
		REGWORD v_fifo_bl3_irq:1;
		REGWORD v_misc_irq:1;
		REGWORD v_ch_irq:1;
		REGWORD reserved_19:2;
	} bit_r_irq_oview; // register and bitmap data
	typedef union {REGWORD reg; bit_r_irq_oview bit;} reg_r_irq_oview; // register and bitmap access


#define R_MISC_IRQMSK 0x11 // register access
	#define M_SLIP_IRQMSK 0x01 // bitmap mask (1bit)
	#define M_TI_IRQMSK 0x02 // bitmap mask (1bit)
	#define M_PROC_IRQMSK 0x04 // bitmap mask (1bit)
	#define M_CI_IRQMSK 0x10 // bitmap mask (1bit)
	#define M_WAK_IRQMSK 0x20 // bitmap mask (1bit)
	#define M_MON_TX_IRQMSK 0x40 // bitmap mask (1bit)
	#define M_MON_RX_IRQMSK 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_slip_irqmsk:1;
		REGWORD v_ti_irqmsk:1;
		REGWORD v_proc_irqmsk:1;
		REGWORD reserved_13:1;
		REGWORD v_ci_irqmsk:1;
		REGWORD v_wak_irqmsk:1;
		REGWORD v_mon_tx_irqmsk:1;
		REGWORD v_mon_rx_irqmsk:1;
	} bit_r_misc_irqmsk; // register and bitmap data
	typedef union {REGWORD reg; bit_r_misc_irqmsk bit;} reg_r_misc_irqmsk; // register and bitmap access


#define R_MISC_IRQ 0x11 // register access
	#define M_SLIP_IRQ 0x01 // bitmap mask (1bit)
	#define M_TI_IRQ 0x02 // bitmap mask (1bit)
	#define M_PROC_IRQ 0x04 // bitmap mask (1bit)
	#define M_CI_IRQ 0x10 // bitmap mask (1bit)
	#define M_WAK_IRQ 0x20 // bitmap mask (1bit)
	#define M_MON_TX_IRQ 0x40 // bitmap mask (1bit)
	#define M_MON_RX_IRQ 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_slip_irq:1;
		REGWORD v_ti_irq:1;
		REGWORD v_proc_irq:1;
		REGWORD reserved_20:1;
		REGWORD v_ci_irq:1;
		REGWORD v_wak_irq:1;
		REGWORD v_mon_tx_irq:1;
		REGWORD v_mon_rx_irq:1;
	} bit_r_misc_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_misc_irq bit;} reg_r_misc_irq; // register and bitmap access


#define R_SU_IRQ 0x12 // register access
	#define M_SU0_IRQ 0x01 // bitmap mask (1bit)
	#define M_SU1_IRQ 0x02 // bitmap mask (1bit)
	#define M_SU2_IRQ 0x04 // bitmap mask (1bit)
	#define M_SU3_IRQ 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_su0_irq:1;
		REGWORD v_su1_irq:1;
		REGWORD v_su2_irq:1;
		REGWORD v_su3_irq:1;
		REGWORD reserved_27:4;
	} bit_r_su_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_su_irq bit;} reg_r_su_irq; // register and bitmap access


#define R_SU_IRQMSK 0x12 // register access
	#define M_SU0_IRQMSK 0x01 // bitmap mask (1bit)
	#define M_SU1_IRQMSK 0x02 // bitmap mask (1bit)
	#define M_SU2_IRQMSK 0x04 // bitmap mask (1bit)
	#define M_SU3_IRQMSK 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_su0_irqmsk:1;
		REGWORD v_su1_irqmsk:1;
		REGWORD v_su2_irqmsk:1;
		REGWORD v_su3_irqmsk:1;
		REGWORD reserved_29:4;
	} bit_r_su_irqmsk; // register and bitmap data
	typedef union {REGWORD reg; bit_r_su_irqmsk bit;} reg_r_su_irqmsk; // register and bitmap access


#define R_AF0_OVIEW 0x13 // register access
	#define M_SU0_AF0 0x01 // bitmap mask (1bit)
	#define M_SU1_AF0 0x02 // bitmap mask (1bit)
	#define M_SU2_AF0 0x04 // bitmap mask (1bit)
	#define M_SU3_AF0 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_su0_af0:1;
		REGWORD v_su1_af0:1;
		REGWORD v_su2_af0:1;
		REGWORD v_su3_af0:1;
		REGWORD reserved_28:4;
	} bit_r_af0_oview; // register and bitmap data
	typedef union {REGWORD reg; bit_r_af0_oview bit;} reg_r_af0_oview; // register and bitmap access


#define R_IRQ_CTRL 0x13 // register access
	#define M_FIFO_IRQ_EN 0x01 // bitmap mask (1bit)
	#define M_GLOB_IRQ_EN 0x08 // bitmap mask (1bit)
	#define M_IRQ_POL 0x10 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_irq_en:1;
		REGWORD reserved_14:2;
		REGWORD v_glob_irq_en:1;
		REGWORD v_irq_pol:1;
		REGWORD reserved_15:3;
	} bit_r_irq_ctrl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_irq_ctrl bit;} reg_r_irq_ctrl; // register and bitmap access


#define A_USAGE 0x14 // register access
	#define M_USAGE 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_usage:8;
	} bit_a_usage; // register and bitmap data
	typedef union {REGWORD reg; bit_a_usage bit;} reg_a_usage; // register and bitmap access


#define R_PCM_MD0 0x14 // register access
	#define M_PCM_MD 0x01 // bitmap mask (1bit)
	#define M_C4_POL 0x02 // bitmap mask (1bit)
	#define M_F0_NEG 0x04 // bitmap mask (1bit)
	#define M_F0_LEN 0x08 // bitmap mask (1bit)
	#define M_PCM_IDX 0xF0 // bitmap mask (4bit)
		#define M1_PCM_IDX 0x10

	typedef struct // bitmap construction
	{
		REGWORD v_pcm_md:1;
		REGWORD v_c4_pol:1;
		REGWORD v_f0_neg:1;
		REGWORD v_f0_len:1;
		REGWORD v_pcm_idx:4;
	} bit_r_pcm_md0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_md0 bit;} reg_r_pcm_md0; // register and bitmap access


#define R_SL_SEL0 0x15 // register access
	#define M_SL_SEL0 0x7F // bitmap mask (7bit)
	#define M_SH_SEL0 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sl_sel0:7;
		REGWORD v_sh_sel0:1;
	} bit_r_sl_sel0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sl_sel0 bit;} reg_r_sl_sel0; // register and bitmap access
	#define IDX_SL_SEL0 0x00 // index value selecting this multi-register


#define R_SL_SEL1 0x15 // register access
	#define M_SL_SEL1 0x7F // bitmap mask (7bit)
	#define M_SH_SEL1 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sl_sel1:7;
		REGWORD v_sh_sel1:1;
	} bit_r_sl_sel1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sl_sel1 bit;} reg_r_sl_sel1; // register and bitmap access
	#define IDX_SL_SEL1 0x01 // index value selecting this multi-register


#define R_SL_SEL7 0x15 // register access
	#define M_SL_SEL7 0x7F // bitmap mask (7bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sl_sel7:7;
		REGWORD reserved_30:1;
	} bit_r_sl_sel7; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sl_sel7 bit;} reg_r_sl_sel7; // register and bitmap access
	#define IDX_SL_SEL7 0x07 // index value selecting this multi-register


#define R_MSS0 0x15 // register access
	#define M_MSS_MOD 0x01 // bitmap mask (1bit)
	#define M_MSS_MOD_REP 0x02 // bitmap mask (1bit)
	#define M_MSS_SRC_EN 0x04 // bitmap mask (1bit)
	#define M_MSS_SRC_GRD 0x08 // bitmap mask (1bit)
	#define M_MSS_OUT_EN 0x10 // bitmap mask (1bit)
	#define M_MSS_OUT_REP 0x20 // bitmap mask (1bit)
	#define M_MSS_SRC 0xC0 // bitmap mask (2bit)
		#define M1_MSS_SRC 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_mss_mod:1;
		REGWORD v_mss_mod_rep:1;
		REGWORD v_mss_src_en:1;
		REGWORD v_mss_src_grd:1;
		REGWORD v_mss_out_en:1;
		REGWORD v_mss_out_rep:1;
		REGWORD v_mss_src:2;
	} bit_r_mss0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mss0 bit;} reg_r_mss0; // register and bitmap access
	#define IDX_MSS0 0x08 // index value selecting this multi-register


#define R_PCM_MD1 0x15 // register access
	#define M_PCM_OD 0x02 // bitmap mask (1bit)
	#define M_PLL_ADJ 0x0C // bitmap mask (2bit)
		#define M1_PLL_ADJ 0x04
	#define M_PCM_DR 0x30 // bitmap mask (2bit)
		#define M1_PCM_DR 0x10
	#define M_PCM_LOOP 0x40 // bitmap mask (1bit)
	#define M_PCM_SMPL 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD reserved_31:1;
		REGWORD v_pcm_od:1;
		REGWORD v_pll_adj:2;
		REGWORD v_pcm_dr:2;
		REGWORD v_pcm_loop:1;
		REGWORD v_pcm_smpl:1;
	} bit_r_pcm_md1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_md1 bit;} reg_r_pcm_md1; // register and bitmap access
	#define IDX_PCM_MD1 0x09 // index value selecting this multi-register


#define R_PCM_MD2 0x15 // register access
	#define M_SYNC_OUT1 0x02 // bitmap mask (1bit)
	#define M_SYNC_SRC 0x04 // bitmap mask (1bit)
	#define M_SYNC_OUT2 0x08 // bitmap mask (1bit)
	#define M_C2O_EN 0x10 // bitmap mask (1bit)
	#define M_C2I_EN 0x20 // bitmap mask (1bit)
	#define M_PLL_ICR 0x40 // bitmap mask (1bit)
	#define M_PLL_MAN 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD reserved_32:1;
		REGWORD v_sync_out1:1;
		REGWORD v_sync_src:1;
		REGWORD v_sync_out2:1;
		REGWORD v_c2o_en:1;
		REGWORD v_c2i_en:1;
		REGWORD v_pll_icr:1;
		REGWORD v_pll_man:1;
	} bit_r_pcm_md2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_md2 bit;} reg_r_pcm_md2; // register and bitmap access
	#define IDX_PCM_MD2 0x0A // index value selecting this multi-register


#define R_MSS1 0x15 // register access
	#define M_MSS_OFFS 0x07 // bitmap mask (3bit)
		#define M1_MSS_OFFS 0x01
	#define M_MS_SSYNC1 0x08 // bitmap mask (1bit)
	#define M_MSS_DLY 0xF0 // bitmap mask (4bit)
		#define M1_MSS_DLY 0x10

	typedef struct // bitmap construction
	{
		REGWORD v_mss_offs:3;
		REGWORD v_ms_ssync1:1;
		REGWORD v_mss_dly:4;
	} bit_r_mss1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mss1 bit;} reg_r_mss1; // register and bitmap access
	#define IDX_MSS1 0x0B // index value selecting this multi-register


#define R_SH0L 0x15 // register access
	#define M_SH0L 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sh0l:8;
	} bit_r_sh0l; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sh0l bit;} reg_r_sh0l; // register and bitmap access
	#define IDX_SH0L 0x0C // index value selecting this multi-register


#define R_SH0H 0x15 // register access
	#define M_SH0H 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sh0h:8;
	} bit_r_sh0h; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sh0h bit;} reg_r_sh0h; // register and bitmap access
	#define IDX_SH0H 0x0D // index value selecting this multi-register


#define R_SH1L 0x15 // register access
	#define M_SH1L 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sh1l:8;
	} bit_r_sh1l; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sh1l bit;} reg_r_sh1l; // register and bitmap access
	#define IDX_SH1L 0x0E // index value selecting this multi-register


#define R_SH1H 0x15 // register access
	#define M_SH1H 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sh1h:8;
	} bit_r_sh1h; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sh1h bit;} reg_r_sh1h; // register and bitmap access
	#define IDX_SH1H 0x0F // index value selecting this multi-register


#define R_RAM_USE 0x15 // register access
	#define M_SRAM_USE 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sram_use:8;
	} bit_r_ram_use; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_use bit;} reg_r_ram_use; // register and bitmap access


#define R_SU_SEL 0x16 // register access
	#define M_SU_SEL 0x03 // bitmap mask (2bit)
		#define M1_SU_SEL 0x01
	#define M_MULT_SU 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_su_sel:2;
		REGWORD reserved_25:1;
		REGWORD v_mult_su:1;
		REGWORD reserved_26:4;
	} bit_r_su_sel; // register and bitmap data
	typedef union {REGWORD reg; bit_r_su_sel bit;} reg_r_su_sel; // register and bitmap access


#define R_CHIP_ID 0x16 // register access
	#define M_CHIP_ID 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_chip_id:8;
	} bit_r_chip_id; // register and bitmap data
	typedef union {REGWORD reg; bit_r_chip_id bit;} reg_r_chip_id; // register and bitmap access


#define R_SU_SYNC 0x17 // register access
	#define M_SYNC_SEL 0x07 // bitmap mask (3bit)
		#define M1_SYNC_SEL 0x01
	#define M_MAN_SYNC 0x08 // bitmap mask (1bit)
	#define M_AUTO_SYNCI 0x10 // bitmap mask (1bit)
	#define M_D_MERGE_TX 0x20 // bitmap mask (1bit)
	#define M_E_MERGE_RX 0x40 // bitmap mask (1bit)
	#define M_D_MERGE_RX 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sync_sel:3;
		REGWORD v_man_sync:1;
		REGWORD v_auto_synci:1;
		REGWORD v_d_merge_tx:1;
		REGWORD v_e_merge_rx:1;
		REGWORD v_d_merge_rx:1;
	} bit_r_su_sync; // register and bitmap data
	typedef union {REGWORD reg; bit_r_su_sync bit;} reg_r_su_sync; // register and bitmap access


#define R_BERT_STA 0x17 // register access
	#define M_RD_SYNC_SRC 0x07 // bitmap mask (3bit)
		#define M1_RD_SYNC_SRC 0x01
	#define M_BERT_SYNC 0x10 // bitmap mask (1bit)
	#define M_BERT_INV_DATA 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_rd_sync_src:3;
		REGWORD reserved_21:1;
		REGWORD v_bert_sync:1;
		REGWORD v_bert_inv_data:1;
		REGWORD reserved_22:2;
	} bit_r_bert_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_r_bert_sta bit;} reg_r_bert_sta; // register and bitmap access


#define R_F0_CNTL 0x18 // register access
	#define M_F0_CNTL 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_f0_cntl:8;
	} bit_r_f0_cntl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_f0_cntl bit;} reg_r_f0_cntl; // register and bitmap access


#define R_F0_CNTH 0x19 // register access
	#define M_F0_CNTH 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_f0_cnth:8;
	} bit_r_f0_cnth; // register and bitmap data
	typedef union {REGWORD reg; bit_r_f0_cnth bit;} reg_r_f0_cnth; // register and bitmap access


#define R_BERT_ECL 0x1A // register access
	#define M_BERT_ECL 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_bert_ecl:8;
	} bit_r_bert_ecl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_bert_ecl bit;} reg_r_bert_ecl; // register and bitmap access


#define R_TI_WD 0x1A // register access
	#define M_EV_TS 0x0F // bitmap mask (4bit)
		#define M1_EV_TS 0x01
	#define M_WD_TS 0xF0 // bitmap mask (4bit)
		#define M1_WD_TS 0x10

	typedef struct // bitmap construction
	{
		REGWORD v_ev_ts:4;
		REGWORD v_wd_ts:4;
	} bit_r_ti_wd; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ti_wd bit;} reg_r_ti_wd; // register and bitmap access


#define R_BERT_ECH 0x1B // register access
	#define M_BERT_ECH 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_bert_ech:8;
	} bit_r_bert_ech; // register and bitmap data
	typedef union {REGWORD reg; bit_r_bert_ech bit;} reg_r_bert_ech; // register and bitmap access


#define R_BERT_WD_MD 0x1B // register access
	#define M_PAT_SEQ 0x07 // bitmap mask (3bit)
		#define M1_PAT_SEQ 0x01
	#define M_BERT_ERR 0x08 // bitmap mask (1bit)
	#define M_AUTO_WD_RES 0x20 // bitmap mask (1bit)
	#define M_WD_RES 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_pat_seq:3;
		REGWORD v_bert_err:1;
		REGWORD reserved_16:1;
		REGWORD v_auto_wd_res:1;
		REGWORD reserved_17:1;
		REGWORD v_wd_res:1;
	} bit_r_bert_wd_md; // register and bitmap data
	typedef union {REGWORD reg; bit_r_bert_wd_md bit;} reg_r_bert_wd_md; // register and bitmap access


#define R_STATUS 0x1C // register access
	#define M_BUSY 0x01 // bitmap mask (1bit)
	#define M_PROC 0x02 // bitmap mask (1bit)
	#define M_LOST_STA 0x08 // bitmap mask (1bit)
	#define M_PCM_INIT 0x10 // bitmap mask (1bit)
	#define M_WAK_STA 0x20 // bitmap mask (1bit)
	#define M_MISC_IRQSTA 0x40 // bitmap mask (1bit)
	#define M_FR_IRQSTA 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_busy:1;
		REGWORD v_proc:1;
		REGWORD reserved_23:1;
		REGWORD v_lost_sta:1;
		REGWORD v_pcm_init:1;
		REGWORD v_wak_sta:1;
		REGWORD v_misc_irqsta:1;
		REGWORD v_fr_irqsta:1;
	} bit_r_status; // register and bitmap data
	typedef union {REGWORD reg; bit_r_status bit;} reg_r_status; // register and bitmap access


#define R_SL_MAX 0x1D // register access
	#define M_SL_MAX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_sl_max:8;
	} bit_r_sl_max; // register and bitmap data
	typedef union {REGWORD reg; bit_r_sl_max bit;} reg_r_sl_max; // register and bitmap access


#define R_PWM_CFG 0x1E // register access
	#define M_PWM0_16KHZ 0x10 // bitmap mask (1bit)
	#define M_PWM1_16KHZ 0x20 // bitmap mask (1bit)
	#define M_PWM_FRQ 0xC0 // bitmap mask (2bit)
		#define M1_PWM_FRQ 0x40

	typedef struct // bitmap construction
	{
		REGWORD reserved_18:4;
		REGWORD v_pwm0_16khz:1;
		REGWORD v_pwm1_16khz:1;
		REGWORD v_pwm_frq:2;
	} bit_r_pwm_cfg; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pwm_cfg bit;} reg_r_pwm_cfg; // register and bitmap access


#define R_CHIP_RV 0x1F // register access
	#define M_CHIP_RV 0x0F // bitmap mask (4bit)
		#define M1_CHIP_RV 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_chip_rv:4;
		REGWORD reserved_24:4;
	} bit_r_chip_rv; // register and bitmap data
	typedef union {REGWORD reg; bit_r_chip_rv bit;} reg_r_chip_rv; // register and bitmap access


#define R_FIFO_BL0_IRQ 0x20 // register access
	#define M_FIFO0_TX_IRQ 0x01 // bitmap mask (1bit)
	#define M_FIFO0_RX_IRQ 0x02 // bitmap mask (1bit)
	#define M_FIFO1_TX_IRQ 0x04 // bitmap mask (1bit)
	#define M_FIFO1_RX_IRQ 0x08 // bitmap mask (1bit)
	#define M_FIFO2_TX_IRQ 0x10 // bitmap mask (1bit)
	#define M_FIFO2_RX_IRQ 0x20 // bitmap mask (1bit)
	#define M_FIFO3_TX_IRQ 0x40 // bitmap mask (1bit)
	#define M_FIFO3_RX_IRQ 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo0_tx_irq:1;
		REGWORD v_fifo0_rx_irq:1;
		REGWORD v_fifo1_tx_irq:1;
		REGWORD v_fifo1_rx_irq:1;
		REGWORD v_fifo2_tx_irq:1;
		REGWORD v_fifo2_rx_irq:1;
		REGWORD v_fifo3_tx_irq:1;
		REGWORD v_fifo3_rx_irq:1;
	} bit_r_fifo_bl0_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_bl0_irq bit;} reg_r_fifo_bl0_irq; // register and bitmap access


#define R_FIFO_BL1_IRQ 0x21 // register access
	#define M_FIFO4_TX_IRQ 0x01 // bitmap mask (1bit)
	#define M_FIFO4_RX_IRQ 0x02 // bitmap mask (1bit)
	#define M_FIFO5_TX_IRQ 0x04 // bitmap mask (1bit)
	#define M_FIFO5_RX_IRQ 0x08 // bitmap mask (1bit)
	#define M_FIFO6_TX_IRQ 0x10 // bitmap mask (1bit)
	#define M_FIFO6_RX_IRQ 0x20 // bitmap mask (1bit)
	#define M_FIFO7_TX_IRQ 0x40 // bitmap mask (1bit)
	#define M_FIFO7_RX_IRQ 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo4_tx_irq:1;
		REGWORD v_fifo4_rx_irq:1;
		REGWORD v_fifo5_tx_irq:1;
		REGWORD v_fifo5_rx_irq:1;
		REGWORD v_fifo6_tx_irq:1;
		REGWORD v_fifo6_rx_irq:1;
		REGWORD v_fifo7_tx_irq:1;
		REGWORD v_fifo7_rx_irq:1;
	} bit_r_fifo_bl1_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_bl1_irq bit;} reg_r_fifo_bl1_irq; // register and bitmap access


#define R_FIFO_BL2_IRQ 0x22 // register access
	#define M_FIFO8_TX_IRQ 0x01 // bitmap mask (1bit)
	#define M_FIFO8_RX_IRQ 0x02 // bitmap mask (1bit)
	#define M_FIFO9_TX_IRQ 0x04 // bitmap mask (1bit)
	#define M_FIFO9_RX_IRQ 0x08 // bitmap mask (1bit)
	#define M_FIFO10_TX_IRQ 0x10 // bitmap mask (1bit)
	#define M_FIFO10_RX_IRQ 0x20 // bitmap mask (1bit)
	#define M_FIFO11_TX_IRQ 0x40 // bitmap mask (1bit)
	#define M_FIFO11_RX_IRQ 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo8_tx_irq:1;
		REGWORD v_fifo8_rx_irq:1;
		REGWORD v_fifo9_tx_irq:1;
		REGWORD v_fifo9_rx_irq:1;
		REGWORD v_fifo10_tx_irq:1;
		REGWORD v_fifo10_rx_irq:1;
		REGWORD v_fifo11_tx_irq:1;
		REGWORD v_fifo11_rx_irq:1;
	} bit_r_fifo_bl2_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_bl2_irq bit;} reg_r_fifo_bl2_irq; // register and bitmap access


#define R_FIFO_BL3_IRQ 0x23 // register access
	#define M_FIFO12_TX_IRQ 0x01 // bitmap mask (1bit)
	#define M_FIFO12_RX_IRQ 0x02 // bitmap mask (1bit)
	#define M_FIFO13_TX_IRQ 0x04 // bitmap mask (1bit)
	#define M_FIFO13_RX_IRQ 0x08 // bitmap mask (1bit)
	#define M_FIFO14_TX_IRQ 0x10 // bitmap mask (1bit)
	#define M_FIFO14_RX_IRQ 0x20 // bitmap mask (1bit)
	#define M_FIFO15_TX_IRQ 0x40 // bitmap mask (1bit)
	#define M_FIFO15_RX_IRQ 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo12_tx_irq:1;
		REGWORD v_fifo12_rx_irq:1;
		REGWORD v_fifo13_tx_irq:1;
		REGWORD v_fifo13_rx_irq:1;
		REGWORD v_fifo14_tx_irq:1;
		REGWORD v_fifo14_rx_irq:1;
		REGWORD v_fifo15_tx_irq:1;
		REGWORD v_fifo15_rx_irq:1;
	} bit_r_fifo_bl3_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_bl3_irq bit;} reg_r_fifo_bl3_irq; // register and bitmap access


#define R_FILL_BL0 0x24 // register access
	#define M_FILL_FIFO0_TX 0x01 // bitmap mask (1bit)
	#define M_FILL_FIFO0_RX 0x02 // bitmap mask (1bit)
	#define M_FILL_FIFO1_TX 0x04 // bitmap mask (1bit)
	#define M_FILL_FIFO1_RX 0x08 // bitmap mask (1bit)
	#define M_FILL_FIFO2_TX 0x10 // bitmap mask (1bit)
	#define M_FILL_FIFO2_RX 0x20 // bitmap mask (1bit)
	#define M_FILL_FIFO3_TX 0x40 // bitmap mask (1bit)
	#define M_FILL_FIFO3_RX 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fill_fifo0_tx:1;
		REGWORD v_fill_fifo0_rx:1;
		REGWORD v_fill_fifo1_tx:1;
		REGWORD v_fill_fifo1_rx:1;
		REGWORD v_fill_fifo2_tx:1;
		REGWORD v_fill_fifo2_rx:1;
		REGWORD v_fill_fifo3_tx:1;
		REGWORD v_fill_fifo3_rx:1;
	} bit_r_fill_bl0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fill_bl0 bit;} reg_r_fill_bl0; // register and bitmap access


#define R_FILL_BL1 0x25 // register access
	#define M_FILL_FIFO4_TX 0x01 // bitmap mask (1bit)
	#define M_FILL_FIFO4_RX 0x02 // bitmap mask (1bit)
	#define M_FILL_FIFO5_TX 0x04 // bitmap mask (1bit)
	#define M_FILL_FIFO5_RX 0x08 // bitmap mask (1bit)
	#define M_FILL_FIFO6_TX 0x10 // bitmap mask (1bit)
	#define M_FILL_FIFO6_RX 0x20 // bitmap mask (1bit)
	#define M_FILL_FIFO7_TX 0x40 // bitmap mask (1bit)
	#define M_FILL_FIFO7_RX 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fill_fifo4_tx:1;
		REGWORD v_fill_fifo4_rx:1;
		REGWORD v_fill_fifo5_tx:1;
		REGWORD v_fill_fifo5_rx:1;
		REGWORD v_fill_fifo6_tx:1;
		REGWORD v_fill_fifo6_rx:1;
		REGWORD v_fill_fifo7_tx:1;
		REGWORD v_fill_fifo7_rx:1;
	} bit_r_fill_bl1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fill_bl1 bit;} reg_r_fill_bl1; // register and bitmap access


#define R_FILL_BL2 0x26 // register access
	#define M_FILL_FIFO8_TX 0x01 // bitmap mask (1bit)
	#define M_FILL_FIFO8_RX 0x02 // bitmap mask (1bit)
	#define M_FILL_FIFO9_TX 0x04 // bitmap mask (1bit)
	#define M_FILL_FIFO9_RX 0x08 // bitmap mask (1bit)
	#define M_FILL_FIFO10_TX 0x10 // bitmap mask (1bit)
	#define M_FILL_FIFO10_RX 0x20 // bitmap mask (1bit)
	#define M_FILL_FIFO11_TX 0x40 // bitmap mask (1bit)
	#define M_FILL_FIFO11_RX 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fill_fifo8_tx:1;
		REGWORD v_fill_fifo8_rx:1;
		REGWORD v_fill_fifo9_tx:1;
		REGWORD v_fill_fifo9_rx:1;
		REGWORD v_fill_fifo10_tx:1;
		REGWORD v_fill_fifo10_rx:1;
		REGWORD v_fill_fifo11_tx:1;
		REGWORD v_fill_fifo11_rx:1;
	} bit_r_fill_bl2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fill_bl2 bit;} reg_r_fill_bl2; // register and bitmap access


#define R_FILL_BL3 0x27 // register access
	#define M_FILL_FIFO12_TX 0x01 // bitmap mask (1bit)
	#define M_FILL_FIFO12_RX 0x02 // bitmap mask (1bit)
	#define M_FILL_FIFO13_TX 0x04 // bitmap mask (1bit)
	#define M_FILL_FIFO13_RX 0x08 // bitmap mask (1bit)
	#define M_FILL_FIFO14_TX 0x10 // bitmap mask (1bit)
	#define M_FILL_FIFO14_RX 0x20 // bitmap mask (1bit)
	#define M_FILL_FIFO15_TX 0x40 // bitmap mask (1bit)
	#define M_FILL_FIFO15_RX 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fill_fifo12_tx:1;
		REGWORD v_fill_fifo12_rx:1;
		REGWORD v_fill_fifo13_tx:1;
		REGWORD v_fill_fifo13_rx:1;
		REGWORD v_fill_fifo14_tx:1;
		REGWORD v_fill_fifo14_rx:1;
		REGWORD v_fill_fifo15_tx:1;
		REGWORD v_fill_fifo15_rx:1;
	} bit_r_fill_bl3; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fill_bl3 bit;} reg_r_fill_bl3; // register and bitmap access


#define R_CI_TX 0x28 // register access
	#define M_GCI_C 0x3F // bitmap mask (6bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gci_c:6;
		REGWORD reserved_33:2;
	} bit_r_ci_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ci_tx bit;} reg_r_ci_tx; // register and bitmap access


#define R_CI_RX 0x28 // register access
	#define M_GCI_I 0x3F // bitmap mask (6bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gci_i:6;
		REGWORD reserved_35:2;
	} bit_r_ci_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ci_rx bit;} reg_r_ci_rx; // register and bitmap access


#define R_GCI_CFG0 0x29 // register access
	#define M_MON_END 0x01 // bitmap mask (1bit)
	#define M_MON_SLOW 0x02 // bitmap mask (1bit)
	#define M_MON_DLL 0x04 // bitmap mask (1bit)
	#define M_MON_CI6 0x08 // bitmap mask (1bit)
	#define M_GCI_SWAP_TXHS 0x10 // bitmap mask (1bit)
	#define M_GCI_SWAP_RXHS 0x20 // bitmap mask (1bit)
	#define M_GCI_SWAP_STIO 0x40 // bitmap mask (1bit)
	#define M_GCI_EN 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon_end:1;
		REGWORD v_mon_slow:1;
		REGWORD v_mon_dll:1;
		REGWORD v_mon_ci6:1;
		REGWORD v_gci_swap_txhs:1;
		REGWORD v_gci_swap_rxhs:1;
		REGWORD v_gci_swap_stio:1;
		REGWORD v_gci_en:1;
	} bit_r_gci_cfg0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gci_cfg0 bit;} reg_r_gci_cfg0; // register and bitmap access


#define R_GCI_STA 0x29 // register access
	#define M_MON_RXR 0x01 // bitmap mask (1bit)
	#define M_MON_TXR 0x02 // bitmap mask (1bit)
	#define M_GCI_MX 0x04 // bitmap mask (1bit)
	#define M_GCI_MR 0x08 // bitmap mask (1bit)
	#define M_GCI_RX 0x10 // bitmap mask (1bit)
	#define M_GCI_ABO 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon_rxr:1;
		REGWORD v_mon_txr:1;
		REGWORD v_gci_mx:1;
		REGWORD v_gci_mr:1;
		REGWORD v_gci_rx:1;
		REGWORD v_gci_abo:1;
		REGWORD reserved_36:2;
	} bit_r_gci_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gci_sta bit;} reg_r_gci_sta; // register and bitmap access


#define R_GCI_CFG1 0x2A // register access
	#define M_GCI_SL 0x1F // bitmap mask (5bit)
		#define M1_GCI_SL 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_gci_sl:5;
		REGWORD reserved_34:3;
	} bit_r_gci_cfg1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gci_cfg1 bit;} reg_r_gci_cfg1; // register and bitmap access


#define R_MON_RX 0x2A // register access
	#define M_MON_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon_rx:8;
	} bit_r_mon_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mon_rx bit;} reg_r_mon_rx; // register and bitmap access


#define R_MON_TX 0x2B // register access
	#define M_MON_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon_tx:8;
	} bit_r_mon_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mon_tx bit;} reg_r_mon_tx; // register and bitmap access


#define A_SU_WR_STA 0x30 // register access
	#define M_SU_SET_STA 0x0F // bitmap mask (4bit)
		#define M1_SU_SET_STA 0x01
	#define M_SU_LD_STA 0x10 // bitmap mask (1bit)
	#define M_SU_ACT 0x60 // bitmap mask (2bit)
		#define M1_SU_ACT 0x20
	#define M_SU_SET_G2_G3 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_su_set_sta:4;
		REGWORD v_su_ld_sta:1;
		REGWORD v_su_act:2;
		REGWORD v_su_set_g2_g3:1;
	} bit_a_su_wr_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_wr_sta bit;} reg_a_su_wr_sta; // register and bitmap access


#define A_SU_RD_STA 0x30 // register access
	#define M_SU_STA 0x0F // bitmap mask (4bit)
		#define M1_SU_STA 0x01
	#define M_SU_FR_SYNC 0x10 // bitmap mask (1bit)
	#define M_SU_T2_EXP 0x20 // bitmap mask (1bit)
	#define M_SU_INFO0 0x40 // bitmap mask (1bit)
	#define M_G2_G3 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_su_sta:4;
		REGWORD v_su_fr_sync:1;
		REGWORD v_su_t2_exp:1;
		REGWORD v_su_info0:1;
		REGWORD v_g2_g3:1;
	} bit_a_su_rd_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_rd_sta bit;} reg_a_su_rd_sta; // register and bitmap access


#define A_SU_CTRL0 0x31 // register access
	#define M_B1_TX_EN 0x01 // bitmap mask (1bit)
	#define M_B2_TX_EN 0x02 // bitmap mask (1bit)
	#define M_SU_MD 0x04 // bitmap mask (1bit)
	#define M_ST_D_LPRIO 0x08 // bitmap mask (1bit)
	#define M_ST_SQ_EN 0x10 // bitmap mask (1bit)
	#define M_SU_TST_SIG 0x20 // bitmap mask (1bit)
	#define M_ST_PU_CTRL 0x40 // bitmap mask (1bit)
	#define M_SU_STOP 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_tx_en:1;
		REGWORD v_b2_tx_en:1;
		REGWORD v_su_md:1;
		REGWORD v_st_d_lprio:1;
		REGWORD v_st_sq_en:1;
		REGWORD v_su_tst_sig:1;
		REGWORD v_st_pu_ctrl:1;
		REGWORD v_su_stop:1;
	} bit_a_su_ctrl0; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_ctrl0 bit;} reg_a_su_ctrl0; // register and bitmap access


#define A_SU_DLYL 0x31 // register access
	#define M_SU_DLYL 0x1F // bitmap mask (5bit)
		#define M1_SU_DLYL 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_su_dlyl:5;
		REGWORD reserved_46:3;
	} bit_a_su_dlyl; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_dlyl bit;} reg_a_su_dlyl; // register and bitmap access


#define A_SU_CTRL1 0x32 // register access
	#define M_G2_G3_EN 0x01 // bitmap mask (1bit)
	#define M_D_RES 0x04 // bitmap mask (1bit)
	#define M_ST_E_IGNO 0x08 // bitmap mask (1bit)
	#define M_ST_E_LO 0x10 // bitmap mask (1bit)
	#define M_BAC_D 0x40 // bitmap mask (1bit)
	#define M_B12_SWAP 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_g2_g3_en:1;
		REGWORD reserved_37:1;
		REGWORD v_d_res:1;
		REGWORD v_st_e_igno:1;
		REGWORD v_st_e_lo:1;
		REGWORD reserved_38:1;
		REGWORD v_bac_d:1;
		REGWORD v_b12_swap:1;
	} bit_a_su_ctrl1; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_ctrl1 bit;} reg_a_su_ctrl1; // register and bitmap access


#define A_SU_DLYH 0x32 // register access
	#define M_SU_DLYH 0x1F // bitmap mask (5bit)
		#define M1_SU_DLYH 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_su_dlyh:5;
		REGWORD reserved_47:3;
	} bit_a_su_dlyh; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_dlyh bit;} reg_a_su_dlyh; // register and bitmap access


#define A_SU_CTRL2 0x33 // register access
	#define M_B1_RX_EN 0x01 // bitmap mask (1bit)
	#define M_B2_RX_EN 0x02 // bitmap mask (1bit)
	#define M_MS_SSYNC2 0x04 // bitmap mask (1bit)
	#define M_BAC_S_SEL 0x08 // bitmap mask (1bit)
	#define M_SU_SYNC_NT 0x10 // bitmap mask (1bit)
	#define M_SU_2KHZ 0x20 // bitmap mask (1bit)
	#define M_SU_TRI 0x40 // bitmap mask (1bit)
	#define M_SU_EXCHG 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_rx_en:1;
		REGWORD v_b2_rx_en:1;
		REGWORD v_ms_ssync2:1;
		REGWORD v_bac_s_sel:1;
		REGWORD v_su_sync_nt:1;
		REGWORD v_su_2khz:1;
		REGWORD v_su_tri:1;
		REGWORD v_su_exchg:1;
	} bit_a_su_ctrl2; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_ctrl2 bit;} reg_a_su_ctrl2; // register and bitmap access


#define A_MS_TX 0x34 // register access
	#define M_MS_TX 0x0F // bitmap mask (4bit)
		#define M1_MS_TX 0x01
	#define M_UP_S_TX 0x40 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ms_tx:4;
		REGWORD reserved_39:2;
		REGWORD v_up_s_tx:1;
		REGWORD reserved_40:1;
	} bit_a_ms_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_ms_tx bit;} reg_a_ms_tx; // register and bitmap access


#define A_MS_RX 0x34 // register access
	#define M_MS_RX 0x0F // bitmap mask (4bit)
		#define M1_MS_RX 0x01
	#define M_MS_RX_RDY 0x10 // bitmap mask (1bit)
	#define M_UP_S_RX 0x40 // bitmap mask (1bit)
	#define M_MS_TX_RDY 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ms_rx:4;
		REGWORD v_ms_rx_rdy:1;
		REGWORD reserved_48:1;
		REGWORD v_up_s_rx:1;
		REGWORD v_ms_tx_rdy:1;
	} bit_a_ms_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_ms_rx bit;} reg_a_ms_rx; // register and bitmap access


#define A_ST_CTRL3 0x35 // register access
	#define M_ST_SEL 0x01 // bitmap mask (1bit)
	#define M_ST_PULSE 0xFE // bitmap mask (7bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_sel:1;
		REGWORD v_st_pulse:7;
	} bit_a_st_ctrl3; // register and bitmap data
	typedef union {REGWORD reg; bit_a_st_ctrl3 bit;} reg_a_st_ctrl3; // register and bitmap access
	#define IDX_ST_CTRL3 0x00 // index value selecting this multi-register


#define A_UP_CTRL3 0x35 // register access
	#define M_UP_SEL 0x01 // bitmap mask (1bit)
	#define M_UP_VIO 0x02 // bitmap mask (1bit)
	#define M_UP_DC_STR 0x04 // bitmap mask (1bit)
	#define M_UP_DC_OFF 0x08 // bitmap mask (1bit)
	#define M_UP_RPT_PAT 0x10 // bitmap mask (1bit)
	#define M_UP_SCRM_MD 0x20 // bitmap mask (1bit)
	#define M_UP_SCRM_TX_OFF 0x40 // bitmap mask (1bit)
	#define M_UP_SCRM_RX_OFF 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_up_sel:1;
		REGWORD v_up_vio:1;
		REGWORD v_up_dc_str:1;
		REGWORD v_up_dc_off:1;
		REGWORD v_up_rpt_pat:1;
		REGWORD v_up_scrm_md:1;
		REGWORD v_up_scrm_tx_off:1;
		REGWORD v_up_scrm_rx_off:1;
	} bit_a_up_ctrl3; // register and bitmap data
	typedef union {REGWORD reg; bit_a_up_ctrl3 bit;} reg_a_up_ctrl3; // register and bitmap access
	#define IDX_UP_CTRL3 0x01 // index value selecting this multi-register


#define A_SU_STA 0x35 // register access
	#define M_ST_D_HPRIO9 0x01 // bitmap mask (1bit)
	#define M_ST_D_LPRIO11 0x02 // bitmap mask (1bit)
	#define M_ST_D_CONT 0x04 // bitmap mask (1bit)
	#define M_ST_D_ACT 0x08 // bitmap mask (1bit)
	#define M_SU_AF0 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_d_hprio9:1;
		REGWORD v_st_d_lprio11:1;
		REGWORD v_st_d_cont:1;
		REGWORD v_st_d_act:1;
		REGWORD reserved_49:3;
		REGWORD v_su_af0:1;
	} bit_a_su_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_sta bit;} reg_a_su_sta; // register and bitmap access


#define A_MS_DF 0x36 // register access
	#define M_BAC_NINV 0x01 // bitmap mask (1bit)
	#define M_SG_AB_INV 0x02 // bitmap mask (1bit)
	#define M_SQ_T_SRC 0x04 // bitmap mask (1bit)
	#define M_M_S_SRC 0x08 // bitmap mask (1bit)
	#define M_SQ_T_DST 0x10 // bitmap mask (1bit)
	#define M_SU_RX_VAL 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_bac_ninv:1;
		REGWORD v_sg_ab_inv:1;
		REGWORD v_sq_t_src:1;
		REGWORD v_m_s_src:1;
		REGWORD v_sq_t_dst:1;
		REGWORD v_su_rx_val:1;
		REGWORD reserved_41:2;
	} bit_a_ms_df; // register and bitmap data
	typedef union {REGWORD reg; bit_a_ms_df bit;} reg_a_ms_df; // register and bitmap access


#define A_SU_CLK_DLY 0x37 // register access
	#define M_SU_CLK_DLY 0x0F // bitmap mask (4bit)
		#define M1_SU_CLK_DLY 0x01
	#define M_ST_SMPL 0x70 // bitmap mask (3bit)
		#define M1_ST_SMPL 0x10

	typedef struct // bitmap construction
	{
		REGWORD v_su_clk_dly:4;
		REGWORD v_st_smpl:3;
		REGWORD reserved_42:1;
	} bit_a_su_clk_dly; // register and bitmap data
	typedef union {REGWORD reg; bit_a_su_clk_dly bit;} reg_a_su_clk_dly; // register and bitmap access


#define R_PWM0 0x38 // register access
	#define M_PWM0 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_pwm0:8;
	} bit_r_pwm0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pwm0 bit;} reg_r_pwm0; // register and bitmap access


#define R_PWM1 0x39 // register access
	#define M_PWM1 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_pwm1:8;
	} bit_r_pwm1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pwm1 bit;} reg_r_pwm1; // register and bitmap access


#define A_B1_TX 0x3C // register access
	#define M_B1_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_tx:8;
	} bit_a_b1_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_b1_tx bit;} reg_a_b1_tx; // register and bitmap access


#define A_B1_RX 0x3C // register access
	#define M_B1_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_rx:8;
	} bit_a_b1_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_b1_rx bit;} reg_a_b1_rx; // register and bitmap access


#define A_B2_TX 0x3D // register access
	#define M_B2_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b2_tx:8;
	} bit_a_b2_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_b2_tx bit;} reg_a_b2_tx; // register and bitmap access


#define A_B2_RX 0x3D // register access
	#define M_B2_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b2_rx:8;
	} bit_a_b2_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_b2_rx bit;} reg_a_b2_rx; // register and bitmap access


#define A_D_TX 0x3E // register access
	#define M_D_TX_S 0x01 // bitmap mask (1bit)
	#define M_D_TX_BAC 0x20 // bitmap mask (1bit)
	#define M_D_TX 0xC0 // bitmap mask (2bit)
		#define M1_D_TX 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_d_tx_s:1;
		REGWORD reserved_43:4;
		REGWORD v_d_tx_bac:1;
		REGWORD v_d_tx:2;
	} bit_a_d_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_d_tx bit;} reg_a_d_tx; // register and bitmap access


#define A_D_RX 0x3E // register access
	#define M_D_RX_S 0x01 // bitmap mask (1bit)
	#define M_D_RX_AB 0x10 // bitmap mask (1bit)
	#define M_D_RX_SG 0x20 // bitmap mask (1bit)
	#define M_D_RX 0xC0 // bitmap mask (2bit)
		#define M1_D_RX 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_d_rx_s:1;
		REGWORD reserved_50:3;
		REGWORD v_d_rx_ab:1;
		REGWORD v_d_rx_sg:1;
		REGWORD v_d_rx:2;
	} bit_a_d_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_d_rx bit;} reg_a_d_rx; // register and bitmap access


#define A_E_RX 0x3F // register access
	#define M_E_RX_S 0x01 // bitmap mask (1bit)
	#define M_E_RX_AB 0x10 // bitmap mask (1bit)
	#define M_E_RX_SG 0x20 // bitmap mask (1bit)
	#define M_E_RX 0xC0 // bitmap mask (2bit)
		#define M1_E_RX 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_e_rx_s:1;
		REGWORD reserved_51:3;
		REGWORD v_e_rx_ab:1;
		REGWORD v_e_rx_sg:1;
		REGWORD v_e_rx:2;
	} bit_a_e_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_e_rx bit;} reg_a_e_rx; // register and bitmap access


#define A_BAC_S_TX 0x3F // register access
	#define M_S_TX 0x01 // bitmap mask (1bit)
	#define M_BAC_TX 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_s_tx:1;
		REGWORD reserved_44:4;
		REGWORD v_bac_tx:1;
		REGWORD reserved_45:2;
	} bit_a_bac_s_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_a_bac_s_tx bit;} reg_a_bac_s_tx; // register and bitmap access


#define R_GPIO_OUT1 0x40 // register access
	#define M_GPIO_OUT8 0x01 // bitmap mask (1bit)
	#define M_GPIO_OUT9 0x02 // bitmap mask (1bit)
	#define M_GPIO_OUT10 0x04 // bitmap mask (1bit)
	#define M_GPIO_OUT11 0x08 // bitmap mask (1bit)
	#define M_GPIO_OUT12 0x10 // bitmap mask (1bit)
	#define M_GPIO_OUT13 0x20 // bitmap mask (1bit)
	#define M_GPIO_OUT14 0x40 // bitmap mask (1bit)
	#define M_GPIO_OUT15 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_out8:1;
		REGWORD v_gpio_out9:1;
		REGWORD v_gpio_out10:1;
		REGWORD v_gpio_out11:1;
		REGWORD v_gpio_out12:1;
		REGWORD v_gpio_out13:1;
		REGWORD v_gpio_out14:1;
		REGWORD v_gpio_out15:1;
	} bit_r_gpio_out1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_out1 bit;} reg_r_gpio_out1; // register and bitmap access


#define R_GPIO_IN1 0x40 // register access
	#define M_GPIO_IN8 0x01 // bitmap mask (1bit)
	#define M_GPIO_IN9 0x02 // bitmap mask (1bit)
	#define M_GPIO_IN10 0x04 // bitmap mask (1bit)
	#define M_GPIO_IN11 0x08 // bitmap mask (1bit)
	#define M_GPIO_IN12 0x10 // bitmap mask (1bit)
	#define M_GPIO_IN13 0x20 // bitmap mask (1bit)
	#define M_GPIO_IN14 0x40 // bitmap mask (1bit)
	#define M_GPIO_IN15 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_in8:1;
		REGWORD v_gpio_in9:1;
		REGWORD v_gpio_in10:1;
		REGWORD v_gpio_in11:1;
		REGWORD v_gpio_in12:1;
		REGWORD v_gpio_in13:1;
		REGWORD v_gpio_in14:1;
		REGWORD v_gpio_in15:1;
	} bit_r_gpio_in1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_in1 bit;} reg_r_gpio_in1; // register and bitmap access


#define R_GPIO_OUT3 0x41 // register access
	#define M_GPIO_OUT24 0x01 // bitmap mask (1bit)
	#define M_GPIO_OUT25 0x02 // bitmap mask (1bit)
	#define M_GPIO_OUT26 0x04 // bitmap mask (1bit)
	#define M_GPIO_OUT27 0x08 // bitmap mask (1bit)
	#define M_GPIO_OUT28 0x10 // bitmap mask (1bit)
	#define M_GPIO_OUT29 0x20 // bitmap mask (1bit)
	#define M_GPIO_OUT30 0x40 // bitmap mask (1bit)
	#define M_GPIO_OUT31 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_out24:1;
		REGWORD v_gpio_out25:1;
		REGWORD v_gpio_out26:1;
		REGWORD v_gpio_out27:1;
		REGWORD v_gpio_out28:1;
		REGWORD v_gpio_out29:1;
		REGWORD v_gpio_out30:1;
		REGWORD v_gpio_out31:1;
	} bit_r_gpio_out3; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_out3 bit;} reg_r_gpio_out3; // register and bitmap access


#define R_GPIO_IN3 0x41 // register access
	#define M_GPIO_IN24 0x01 // bitmap mask (1bit)
	#define M_GPIO_IN25 0x02 // bitmap mask (1bit)
	#define M_GPIO_IN26 0x04 // bitmap mask (1bit)
	#define M_GPIO_IN27 0x08 // bitmap mask (1bit)
	#define M_GPIO_IN28 0x10 // bitmap mask (1bit)
	#define M_GPIO_IN29 0x20 // bitmap mask (1bit)
	#define M_GPIO_IN30 0x40 // bitmap mask (1bit)
	#define M_GPIO_IN31 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_in24:1;
		REGWORD v_gpio_in25:1;
		REGWORD v_gpio_in26:1;
		REGWORD v_gpio_in27:1;
		REGWORD v_gpio_in28:1;
		REGWORD v_gpio_in29:1;
		REGWORD v_gpio_in30:1;
		REGWORD v_gpio_in31:1;
	} bit_r_gpio_in3; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_in3 bit;} reg_r_gpio_in3; // register and bitmap access


#define R_GPIO_EN1 0x42 // register access
	#define M_GPIO_EN8 0x01 // bitmap mask (1bit)
	#define M_GPIO_EN9 0x02 // bitmap mask (1bit)
	#define M_GPIO_EN10 0x04 // bitmap mask (1bit)
	#define M_GPIO_EN11 0x08 // bitmap mask (1bit)
	#define M_GPIO_EN12 0x10 // bitmap mask (1bit)
	#define M_GPIO_EN13 0x20 // bitmap mask (1bit)
	#define M_GPIO_EN14 0x40 // bitmap mask (1bit)
	#define M_GPIO_EN15 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_en8:1;
		REGWORD v_gpio_en9:1;
		REGWORD v_gpio_en10:1;
		REGWORD v_gpio_en11:1;
		REGWORD v_gpio_en12:1;
		REGWORD v_gpio_en13:1;
		REGWORD v_gpio_en14:1;
		REGWORD v_gpio_en15:1;
	} bit_r_gpio_en1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_en1 bit;} reg_r_gpio_en1; // register and bitmap access


#define R_GPIO_EN3 0x43 // register access
	#define M_GPIO_EN24 0x01 // bitmap mask (1bit)
	#define M_GPIO_EN25 0x02 // bitmap mask (1bit)
	#define M_GPIO_EN26 0x04 // bitmap mask (1bit)
	#define M_GPIO_EN27 0x08 // bitmap mask (1bit)
	#define M_GPIO_EN28 0x10 // bitmap mask (1bit)
	#define M_GPIO_EN29 0x20 // bitmap mask (1bit)
	#define M_GPIO_EN30 0x40 // bitmap mask (1bit)
	#define M_GPIO_EN31 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_en24:1;
		REGWORD v_gpio_en25:1;
		REGWORD v_gpio_en26:1;
		REGWORD v_gpio_en27:1;
		REGWORD v_gpio_en28:1;
		REGWORD v_gpio_en29:1;
		REGWORD v_gpio_en30:1;
		REGWORD v_gpio_en31:1;
	} bit_r_gpio_en3; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_en3 bit;} reg_r_gpio_en3; // register and bitmap access


#define R_GPIO_SEL_BL 0x44 // register access
	#define M_GPIO_BL0 0x01 // bitmap mask (1bit)
	#define M_GPIO_BL1 0x02 // bitmap mask (1bit)
	#define M_GPIO_BL2 0x04 // bitmap mask (1bit)
	#define M_GPIO_BL3 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_bl0:1;
		REGWORD v_gpio_bl1:1;
		REGWORD v_gpio_bl2:1;
		REGWORD v_gpio_bl3:1;
		REGWORD reserved_54:4;
	} bit_r_gpio_sel_bl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_sel_bl bit;} reg_r_gpio_sel_bl; // register and bitmap access


#define R_GPIO_OUT2 0x45 // register access
	#define M_GPIO_OUT16 0x01 // bitmap mask (1bit)
	#define M_GPIO_OUT17 0x02 // bitmap mask (1bit)
	#define M_GPIO_OUT18 0x04 // bitmap mask (1bit)
	#define M_GPIO_OUT19 0x08 // bitmap mask (1bit)
	#define M_GPIO_OUT20 0x10 // bitmap mask (1bit)
	#define M_GPIO_OUT21 0x20 // bitmap mask (1bit)
	#define M_GPIO_OUT22 0x40 // bitmap mask (1bit)
	#define M_GPIO_OUT23 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_out16:1;
		REGWORD v_gpio_out17:1;
		REGWORD v_gpio_out18:1;
		REGWORD v_gpio_out19:1;
		REGWORD v_gpio_out20:1;
		REGWORD v_gpio_out21:1;
		REGWORD v_gpio_out22:1;
		REGWORD v_gpio_out23:1;
	} bit_r_gpio_out2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_out2 bit;} reg_r_gpio_out2; // register and bitmap access


#define R_GPIO_IN2 0x45 // register access
	#define M_GPIO_IN16 0x01 // bitmap mask (1bit)
	#define M_GPIO_IN17 0x02 // bitmap mask (1bit)
	#define M_GPIO_IN18 0x04 // bitmap mask (1bit)
	#define M_GPIO_IN19 0x08 // bitmap mask (1bit)
	#define M_GPIO_IN20 0x10 // bitmap mask (1bit)
	#define M_GPIO_IN21 0x20 // bitmap mask (1bit)
	#define M_GPIO_IN22 0x40 // bitmap mask (1bit)
	#define M_GPIO_IN23 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_in16:1;
		REGWORD v_gpio_in17:1;
		REGWORD v_gpio_in18:1;
		REGWORD v_gpio_in19:1;
		REGWORD v_gpio_in20:1;
		REGWORD v_gpio_in21:1;
		REGWORD v_gpio_in22:1;
		REGWORD v_gpio_in23:1;
	} bit_r_gpio_in2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_in2 bit;} reg_r_gpio_in2; // register and bitmap access


#define R_PWM_MD 0x46 // register access
	#define M_WAK_EN 0x02 // bitmap mask (1bit)
	#define M_PWM0_MD 0x30 // bitmap mask (2bit)
		#define M1_PWM0_MD 0x10
	#define M_PWM1_MD 0xC0 // bitmap mask (2bit)
		#define M1_PWM1_MD 0x40

	typedef struct // bitmap construction
	{
		REGWORD reserved_52:1;
		REGWORD v_wak_en:1;
		REGWORD reserved_53:2;
		REGWORD v_pwm0_md:2;
		REGWORD v_pwm1_md:2;
	} bit_r_pwm_md; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pwm_md bit;} reg_r_pwm_md; // register and bitmap access


#define R_GPIO_EN2 0x47 // register access
	#define M_GPIO_EN16 0x01 // bitmap mask (1bit)
	#define M_GPIO_EN17 0x02 // bitmap mask (1bit)
	#define M_GPIO_EN18 0x04 // bitmap mask (1bit)
	#define M_GPIO_EN19 0x08 // bitmap mask (1bit)
	#define M_GPIO_EN20 0x10 // bitmap mask (1bit)
	#define M_GPIO_EN21 0x20 // bitmap mask (1bit)
	#define M_GPIO_EN22 0x40 // bitmap mask (1bit)
	#define M_GPIO_EN23 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_en16:1;
		REGWORD v_gpio_en17:1;
		REGWORD v_gpio_en18:1;
		REGWORD v_gpio_en19:1;
		REGWORD v_gpio_en20:1;
		REGWORD v_gpio_en21:1;
		REGWORD v_gpio_en22:1;
		REGWORD v_gpio_en23:1;
	} bit_r_gpio_en2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_en2 bit;} reg_r_gpio_en2; // register and bitmap access


#define R_GPIO_IN0 0x48 // register access
	#define M_GPIO_IN0 0x01 // bitmap mask (1bit)
	#define M_GPIO_IN1 0x02 // bitmap mask (1bit)
	#define M_GPIO_IN2 0x04 // bitmap mask (1bit)
	#define M_GPIO_IN3 0x08 // bitmap mask (1bit)
	#define M_GPIO_IN4 0x10 // bitmap mask (1bit)
	#define M_GPIO_IN5 0x20 // bitmap mask (1bit)
	#define M_GPIO_IN6 0x40 // bitmap mask (1bit)
	#define M_GPIO_IN7 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_in0:1;
		REGWORD v_gpio_in1:1;
		REGWORD v_gpio_in2:1;
		REGWORD v_gpio_in3:1;
		REGWORD v_gpio_in4:1;
		REGWORD v_gpio_in5:1;
		REGWORD v_gpio_in6:1;
		REGWORD v_gpio_in7:1;
	} bit_r_gpio_in0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_in0 bit;} reg_r_gpio_in0; // register and bitmap access


#define R_GPIO_OUT0 0x48 // register access
	#define M_GPIO_OUT0 0x01 // bitmap mask (1bit)
	#define M_GPIO_OUT1 0x02 // bitmap mask (1bit)
	#define M_GPIO_OUT2 0x04 // bitmap mask (1bit)
	#define M_GPIO_OUT3 0x08 // bitmap mask (1bit)
	#define M_GPIO_OUT4 0x10 // bitmap mask (1bit)
	#define M_GPIO_OUT5 0x20 // bitmap mask (1bit)
	#define M_GPIO_OUT6 0x40 // bitmap mask (1bit)
	#define M_GPIO_OUT7 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_out0:1;
		REGWORD v_gpio_out1:1;
		REGWORD v_gpio_out2:1;
		REGWORD v_gpio_out3:1;
		REGWORD v_gpio_out4:1;
		REGWORD v_gpio_out5:1;
		REGWORD v_gpio_out6:1;
		REGWORD v_gpio_out7:1;
	} bit_r_gpio_out0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_out0 bit;} reg_r_gpio_out0; // register and bitmap access


#define R_GPIO_EN0 0x4A // register access
	#define M_GPIO_EN0 0x01 // bitmap mask (1bit)
	#define M_GPIO_EN1 0x02 // bitmap mask (1bit)
	#define M_GPIO_EN2 0x04 // bitmap mask (1bit)
	#define M_GPIO_EN3 0x08 // bitmap mask (1bit)
	#define M_GPIO_EN4 0x10 // bitmap mask (1bit)
	#define M_GPIO_EN5 0x20 // bitmap mask (1bit)
	#define M_GPIO_EN6 0x40 // bitmap mask (1bit)
	#define M_GPIO_EN7 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_en0:1;
		REGWORD v_gpio_en1:1;
		REGWORD v_gpio_en2:1;
		REGWORD v_gpio_en3:1;
		REGWORD v_gpio_en4:1;
		REGWORD v_gpio_en5:1;
		REGWORD v_gpio_en6:1;
		REGWORD v_gpio_en7:1;
	} bit_r_gpio_en0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_en0 bit;} reg_r_gpio_en0; // register and bitmap access


#define R_GPIO_SEL 0x4C // register access
	#define M_GPIO_SEL0 0x01 // bitmap mask (1bit)
	#define M_GPIO_SEL1 0x02 // bitmap mask (1bit)
	#define M_GPIO_SEL2 0x04 // bitmap mask (1bit)
	#define M_GPIO_SEL3 0x08 // bitmap mask (1bit)
	#define M_GPIO_SEL4 0x10 // bitmap mask (1bit)
	#define M_GPIO_SEL5 0x20 // bitmap mask (1bit)
	#define M_GPIO_SEL6 0x40 // bitmap mask (1bit)
	#define M_GPIO_SEL7 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_gpio_sel0:1;
		REGWORD v_gpio_sel1:1;
		REGWORD v_gpio_sel2:1;
		REGWORD v_gpio_sel3:1;
		REGWORD v_gpio_sel4:1;
		REGWORD v_gpio_sel5:1;
		REGWORD v_gpio_sel6:1;
		REGWORD v_gpio_sel7:1;
	} bit_r_gpio_sel; // register and bitmap data
	typedef union {REGWORD reg; bit_r_gpio_sel bit;} reg_r_gpio_sel; // register and bitmap access


#define R_PLL_STA 0x50 // register access
	#define M_PLL_LOCK 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD reserved_56:7;
		REGWORD v_pll_lock:1;
	} bit_r_pll_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pll_sta bit;} reg_r_pll_sta; // register and bitmap access


#define R_PLL_CTRL 0x50 // register access
	#define M_PLL_NRES 0x01 // bitmap mask (1bit)
	#define M_PLL_TST 0x02 // bitmap mask (1bit)
	#define M_PLL_FREEZE 0x20 // bitmap mask (1bit)
	#define M_PLL_M 0xC0 // bitmap mask (2bit)
		#define M1_PLL_M 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_pll_nres:1;
		REGWORD v_pll_tst:1;
		REGWORD reserved_55:3;
		REGWORD v_pll_freeze:1;
		REGWORD v_pll_m:2;
	} bit_r_pll_ctrl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pll_ctrl bit;} reg_r_pll_ctrl; // register and bitmap access


#define R_PLL_P 0x51 // register access
	#define M_PLL_P 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_pll_p:8;
	} bit_r_pll_p; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pll_p bit;} reg_r_pll_p; // register and bitmap access


#define R_PLL_N 0x52 // register access
	#define M_PLL_N 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_pll_n:8;
	} bit_r_pll_n; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pll_n bit;} reg_r_pll_n; // register and bitmap access


#define R_PLL_S 0x53 // register access
	#define M_PLL_S 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_pll_s:8;
	} bit_r_pll_s; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pll_s bit;} reg_r_pll_s; // register and bitmap access


#define A_FIFO_DATA 0x80 // register access
	#define M_FIFO_DATA 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_data:8;
	} bit_a_fifo_data; // register and bitmap data
	typedef union {REGWORD reg; bit_a_fifo_data bit;} reg_a_fifo_data; // register and bitmap access


#define A_FIFO_DATA_NOINC 0x84 // register access
	#define M_FIFO_DATA_NOINC 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_data_noinc:8;
	} bit_a_fifo_data_noinc; // register and bitmap data
	typedef union {REGWORD reg; bit_a_fifo_data_noinc bit;} reg_a_fifo_data_noinc; // register and bitmap access


#define R_INT_DATA 0x88 // register access
	#define M_INT_DATA 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_int_data:8;
	} bit_r_int_data; // register and bitmap data
	typedef union {REGWORD reg; bit_r_int_data bit;} reg_r_int_data; // register and bitmap access


#define R_RAM_DATA 0xC0 // register access
	#define M_RAM_DATA 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ram_data:8;
	} bit_r_ram_data; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_data bit;} reg_r_ram_data; // register and bitmap access


#define A_SL_CFG 0xD0 // register access
	#define M_CH_SDIR 0x01 // bitmap mask (1bit)
	#define M_CH_SNUM 0x3E // bitmap mask (5bit)
		#define M1_CH_SNUM 0x02
	#define M_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_ch_sdir:1;
		REGWORD v_ch_snum:5;
		REGWORD v_rout:2;
	} bit_a_sl_cfg; // register and bitmap data
	typedef union {REGWORD reg; bit_a_sl_cfg bit;} reg_a_sl_cfg; // register and bitmap access


#define A_CH_MSK 0xF4 // register access
	#define M_CH_MSK 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ch_msk:8;
	} bit_a_ch_msk; // register and bitmap data
	typedef union {REGWORD reg; bit_a_ch_msk bit;} reg_a_ch_msk; // register and bitmap access


#define A_CON_HDLC 0xFA // register access
	#define M_IFF 0x01 // bitmap mask (1bit)
	#define M_HDLC_TRP 0x02 // bitmap mask (1bit)
	#define M_FIFO_IRQ 0x1C // bitmap mask (3bit)
		#define M1_FIFO_IRQ 0x04
	#define M_DATA_FLOW 0xE0 // bitmap mask (3bit)
		#define M1_DATA_FLOW 0x20

	typedef struct // bitmap construction
	{
		REGWORD v_iff:1;
		REGWORD v_hdlc_trp:1;
		REGWORD v_fifo_irq:3;
		REGWORD v_data_flow:3;
	} bit_a_con_hdlc; // register and bitmap data
	typedef union {REGWORD reg; bit_a_con_hdlc bit;} reg_a_con_hdlc; // register and bitmap access


#define A_SUBCH_CFG 0xFB // register access
	#define M_BIT_CNT 0x07 // bitmap mask (3bit)
		#define M1_BIT_CNT 0x01
	#define M_START_BIT 0x38 // bitmap mask (3bit)
		#define M1_START_BIT 0x08
	#define M_LOOP_FIFO 0x40 // bitmap mask (1bit)
	#define M_INV_DATA 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_bit_cnt:3;
		REGWORD v_start_bit:3;
		REGWORD v_loop_fifo:1;
		REGWORD v_inv_data:1;
	} bit_a_subch_cfg; // register and bitmap data
	typedef union {REGWORD reg; bit_a_subch_cfg bit;} reg_a_subch_cfg; // register and bitmap access


#define A_CHANNEL 0xFC // register access
	#define M_CH_FDIR 0x01 // bitmap mask (1bit)
	#define M_CH_FNUM 0x1E // bitmap mask (4bit)
		#define M1_CH_FNUM 0x02

	typedef struct // bitmap construction
	{
		REGWORD v_ch_fdir:1;
		REGWORD v_ch_fnum:4;
		REGWORD reserved_57:3;
	} bit_a_channel; // register and bitmap data
	typedef union {REGWORD reg; bit_a_channel bit;} reg_a_channel; // register and bitmap access


#define A_FIFO_SEQ 0xFD // register access
	#define M_NEXT_FIFO_DIR 0x01 // bitmap mask (1bit)
	#define M_NEXT_FIFO_NUM 0x1E // bitmap mask (4bit)
		#define M1_NEXT_FIFO_NUM 0x02
	#define M_SEQ_END 0x40 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_next_fifo_dir:1;
		REGWORD v_next_fifo_num:4;
		REGWORD reserved_58:1;
		REGWORD v_seq_end:1;
		REGWORD reserved_59:1;
	} bit_a_fifo_seq; // register and bitmap data
	typedef union {REGWORD reg; bit_a_fifo_seq bit;} reg_a_fifo_seq; // register and bitmap access


#define A_FIFO_CTRL 0xFF // register access
	#define M_FIFO_IRQMSK 0x01 // bitmap mask (1bit)
	#define M_BERT_EN 0x02 // bitmap mask (1bit)
	#define M_MIX_IRQ 0x04 // bitmap mask (1bit)
	#define M_FR_ABO 0x08 // bitmap mask (1bit)
	#define M_NO_CRC 0x10 // bitmap mask (1bit)
	#define M_NO_REP 0x20 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_irqmsk:1;
		REGWORD v_bert_en:1;
		REGWORD v_mix_irq:1;
		REGWORD v_fr_abo:1;
		REGWORD v_no_crc:1;
		REGWORD v_no_rep:1;
		REGWORD reserved_60:2;
	} bit_a_fifo_ctrl; // register and bitmap data
	typedef union {REGWORD reg; bit_a_fifo_ctrl bit;} reg_a_fifo_ctrl; // register and bitmap access


#endif /* _XHFC24SUCC_H_ */

/*___________________________________________________________________________________*/
/*                                                                                   */
/*  End of XHFC-2S4U / XHFC-4SU register definitions.                                */
/*                                                                                   */
/*  Total number of registers processed: 122 of 122                                  */
/*  Total number of bitmaps processed  : 523                                         */
/*                                                                                   */
/*___________________________________________________________________________________*/
/*                                                                                   */
