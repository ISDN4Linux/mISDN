/*___________________________________________________________________________________*/
/*                                                                                   */
/*  (C) Copyright Cologne Chip AG, 2005                                              */
/*___________________________________________________________________________________*/
/*                                                                                   */

/*                                                                                   */
/*  File name:     hfcsmcc.h                                                         */
/*  File content:  This file contains the HFC-S mini register definitions.           */
/*  Creation date: 24.10.2005 10:45                                                  */
/*  Creator:       Genero 3.2                                                        */
/*  Data base:     HFC XML 1.6 for HFC-S mini and HFC-S USB (unreleased)             */
/*  Address range: 0x00 - 0xFC                                                       */
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


#ifndef _HFCSMCC_H_
#define _HFCSMCC_H_


typedef unsigned char BYTE;

typedef BYTE REGWORD;       // maximum register length (standard)
typedef BYTE REGADDR;       // address width

/*___________________________________________________________________________________*/
/*                                                                                   */
/*  The following definitions are only used for multi-register access and can be     */
/*  switched off. Define MULTIREG below if you want to use multi-register accesses.  */
/*___________________________________________________________________________________*/
/*                                                                                   */
#define MULTIREG
/*___________________________________________________________________________________*/

#ifdef MULTIREG
	typedef unsigned short REGWORD16; // multi-register access, 2 registers
	typedef unsigned int REGWORD32;   // multi-register access, 4 registers
#endif



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

	#define CHIP_NAME		"HFC-S mini"
	#define CHIP_TITLE		"ISDN HDLC FIFO controller with S/T interface and integrated FIFOs"
	#define CHIP_MANUFACTURER	"Cologne Chip"
	#define CHIP_ID			0x05
	#define CHIP_REGISTER_COUNT	71
	#define CHIP_DATABASE		"Version HFC-XMLHFC XML 1.6 for HFC-S mini and HFC-S USB (unreleased) -GeneroGenero 3.2 "





/*___________________________________________________________________________________*/
/*                                                                                   */
/*  Begin of HFC-S mini register definitions.                                        */
/*___________________________________________________________________________________*/
/*                                                                                   */

#define R_CIRM 0x00 // register access
	#define M_SRES 0x08 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD reserved_28:3;
		REGWORD v_sres:1;
		REGWORD reserved_29:4;
	} bit_r_cirm; // register and bitmap data
	typedef union {REGWORD reg; bit_r_cirm bit;} reg_r_cirm; // register and bitmap access


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


#define R_RAM_ADDR0 0x08 // register access
	#define M_RAM_ADDR0 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ram_addr0:8;
	} bit_r_ram_addr0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_addr0 bit;} reg_r_ram_addr0; // register and bitmap access


#define R_RAM_ADDR1 0x09 // register access
	#define M_RAM_ADDR1 0x07 // bitmap mask (3bit)
		#define M1_RAM_ADDR1 0x01
	#define M_ADDR_RES 0x40 // bitmap mask (1bit)
	#define M_ADDR_INC 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ram_addr1:3;
		REGWORD reserved_0:3;
		REGWORD v_addr_res:1;
		REGWORD v_addr_inc:1;
	} bit_r_ram_addr1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_addr1 bit;} reg_r_ram_addr1; // register and bitmap access


#define R_FIFO_REV 0x0B // register access
	#define M_FIFO0_TX_REV 0x01 // bitmap mask (1bit)
	#define M_FIFO0_RX_REV 0x02 // bitmap mask (1bit)
	#define M_FIFO1_TX_REV 0x04 // bitmap mask (1bit)
	#define M_FIFO1_RX_REV 0x08 // bitmap mask (1bit)
	#define M_FIFO2_TX_REV 0x10 // bitmap mask (1bit)
	#define M_FIFO2_RX_REV 0x20 // bitmap mask (1bit)
	#define M_FIFO3_TX_REV 0x40 // bitmap mask (1bit)
	#define M_FIFO3_RX_REV 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo0_tx_rev:1;
		REGWORD v_fifo0_rx_rev:1;
		REGWORD v_fifo1_tx_rev:1;
		REGWORD v_fifo1_rx_rev:1;
		REGWORD v_fifo2_tx_rev:1;
		REGWORD v_fifo2_rx_rev:1;
		REGWORD v_fifo3_tx_rev:1;
		REGWORD v_fifo3_rx_rev:1;
	} bit_r_fifo_rev; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_rev bit;} reg_r_fifo_rev; // register and bitmap access


#define A_F1 0x0C // register access
	#define M_F1 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_f1:8;
	} bit_a_f1; // register and bitmap data
	typedef union {REGWORD reg; bit_a_f1 bit;} reg_a_f1; // register and bitmap access


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


#define A_F2 0x0D // register access
	#define M_F2 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_f2:8;
	} bit_a_f2; // register and bitmap data
	typedef union {REGWORD reg; bit_a_f2 bit;} reg_a_f2; // register and bitmap access


#define R_DF_MD 0x0D // register access
	#define M_CSM 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD reserved_1:7;
		REGWORD v_csm:1;
	} bit_r_df_md; // register and bitmap data
	typedef union {REGWORD reg; bit_r_df_md bit;} reg_r_df_md; // register and bitmap access


#define A_INC_RES_FIFO 0x0E // register access
	#define M_INC_F 0x01 // bitmap mask (1bit)
	#define M_RES_FIFO 0x02 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_inc_f:1;
		REGWORD v_res_fifo:1;
		REGWORD reserved_2:6;
	} bit_a_inc_res_fifo; // register and bitmap data
	typedef union {REGWORD reg; bit_a_inc_res_fifo bit;} reg_a_inc_res_fifo; // register and bitmap access


#define R_FIFO 0x0F // register access
	#define M_FIFO_DIR 0x01 // bitmap mask (1bit)
	#define M_FIFO_NUM 0x06 // bitmap mask (2bit)
		#define M1_FIFO_NUM 0x02

	typedef struct // bitmap construction
	{
		REGWORD v_fifo_dir:1;
		REGWORD v_fifo_num:2;
		REGWORD reserved_3:5;
	} bit_r_fifo; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo bit;} reg_r_fifo; // register and bitmap access


#define R_FIFO_IRQ 0x10 // register access
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
	} bit_r_fifo_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_irq bit;} reg_r_fifo_irq; // register and bitmap access


#define R_MISC_IRQ 0x11 // register access
	#define M_ST_IRQ 0x01 // bitmap mask (1bit)
	#define M_TI_IRQ 0x02 // bitmap mask (1bit)
	#define M_PROC_IRQ 0x04 // bitmap mask (1bit)
	#define M_CI_IRQ 0x08 // bitmap mask (1bit)
	#define M_MON_RX_IRQ 0x10 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_irq:1;
		REGWORD v_ti_irq:1;
		REGWORD v_proc_irq:1;
		REGWORD v_ci_irq:1;
		REGWORD v_mon_rx_irq:1;
		REGWORD reserved_31:3;
	} bit_r_misc_irq; // register and bitmap data
	typedef union {REGWORD reg; bit_r_misc_irq bit;} reg_r_misc_irq; // register and bitmap access


#define R_PCM_MD0 0x14 // register access
	#define M_PCM_MD 0x01 // bitmap mask (1bit)
	#define M_C4_POL 0x02 // bitmap mask (1bit)
	#define M_F0_NEG 0x04 // bitmap mask (1bit)
	#define M_F0_LEN 0x08 // bitmap mask (1bit)
	#define M_SL_CODECA 0x30 // bitmap mask (2bit)
		#define M1_SL_CODECA 0x10
	#define M_SL_CODECB 0xC0 // bitmap mask (2bit)
		#define M1_SL_CODECB 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_pcm_md:1;
		REGWORD v_c4_pol:1;
		REGWORD v_f0_neg:1;
		REGWORD v_f0_len:1;
		REGWORD v_sl_codeca:2;
		REGWORD v_sl_codecb:2;
	} bit_r_pcm_md0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_md0 bit;} reg_r_pcm_md0; // register and bitmap access


#define R_PCM_MD1 0x15 // register access
	#define M_AUX1_MIR 0x01 // bitmap mask (1bit)
	#define M_AUX2_MIR 0x02 // bitmap mask (1bit)
	#define M_PLL_ADJ 0x0C // bitmap mask (2bit)
		#define M1_PLL_ADJ 0x04
	#define M_PCM_DR 0x30 // bitmap mask (2bit)
		#define M1_PCM_DR 0x10
	#define M_PCM_LOOP 0x40 // bitmap mask (1bit)
	#define M_GCI_EN 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_aux1_mir:1;
		REGWORD v_aux2_mir:1;
		REGWORD v_pll_adj:2;
		REGWORD v_pcm_dr:2;
		REGWORD v_pcm_loop:1;
		REGWORD v_gci_en:1;
	} bit_r_pcm_md1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_md1 bit;} reg_r_pcm_md1; // register and bitmap access


#define R_PCM_MD2 0x16 // register access
	#define M_OKI_CODECA 0x01 // bitmap mask (1bit)
	#define M_OKI_CODECB 0x02 // bitmap mask (1bit)
	#define M_SYNC_SRC 0x04 // bitmap mask (1bit)
	#define M_SYNC_OUT 0x08 // bitmap mask (1bit)
	#define M_SL_BL 0x30 // bitmap mask (2bit)
		#define M1_SL_BL 0x10
	#define M_PLL_ICR 0x40 // bitmap mask (1bit)
	#define M_PLL_MAN 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_oki_codeca:1;
		REGWORD v_oki_codecb:1;
		REGWORD v_sync_src:1;
		REGWORD v_sync_out:1;
		REGWORD v_sl_bl:2;
		REGWORD v_pll_icr:1;
		REGWORD v_pll_man:1;
	} bit_r_pcm_md2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_md2 bit;} reg_r_pcm_md2; // register and bitmap access


#define R_CHIP_ID 0x16 // register access
	#define M_CHIP_ID 0xF0 // bitmap mask (4bit)
		#define M1_CHIP_ID 0x10

	typedef struct // bitmap construction
	{
		REGWORD reserved_22:4;
		REGWORD v_chip_id:4;
	} bit_r_chip_id; // register and bitmap data
	typedef union {REGWORD reg; bit_r_chip_id bit;} reg_r_chip_id; // register and bitmap access


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


#define A_USAGE 0x1A // register access
	#define M_USAGE 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_usage:8;
	} bit_a_usage; // register and bitmap data
	typedef union {REGWORD reg; bit_a_usage bit;} reg_a_usage; // register and bitmap access


#define R_FIFO_IRQMSK 0x1A // register access
	#define M_FIFO0_TX_IRQMSK 0x01 // bitmap mask (1bit)
	#define M_FIFO0_RX_IRQMSK 0x02 // bitmap mask (1bit)
	#define M_FIFO1_TX_IRQMSK 0x04 // bitmap mask (1bit)
	#define M_FIFO1_RX_IRQMSK 0x08 // bitmap mask (1bit)
	#define M_FIFO2_TX_IRQMSK 0x10 // bitmap mask (1bit)
	#define M_FIFO2_RX_IRQMSK 0x20 // bitmap mask (1bit)
	#define M_FIFO3_TX_IRQMSK 0x40 // bitmap mask (1bit)
	#define M_FIFO3_RX_IRQMSK 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo0_tx_irqmsk:1;
		REGWORD v_fifo0_rx_irqmsk:1;
		REGWORD v_fifo1_tx_irqmsk:1;
		REGWORD v_fifo1_rx_irqmsk:1;
		REGWORD v_fifo2_tx_irqmsk:1;
		REGWORD v_fifo2_rx_irqmsk:1;
		REGWORD v_fifo3_tx_irqmsk:1;
		REGWORD v_fifo3_rx_irqmsk:1;
	} bit_r_fifo_irqmsk; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fifo_irqmsk bit;} reg_r_fifo_irqmsk; // register and bitmap access


#define R_FILL 0x1B // register access
	#define M_FIFO0_TX_FILL 0x01 // bitmap mask (1bit)
	#define M_FIFO0_RX_FILL 0x02 // bitmap mask (1bit)
	#define M_FIFO1_TX_FILL 0x04 // bitmap mask (1bit)
	#define M_FIFO1_RX_FILL 0x08 // bitmap mask (1bit)
	#define M_FIFO2_TX_FILL 0x10 // bitmap mask (1bit)
	#define M_FIFO2_RX_FILL 0x20 // bitmap mask (1bit)
	#define M_FIFO3_TX_FILL 0x40 // bitmap mask (1bit)
	#define M_FIFO3_RX_FILL 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_fifo0_tx_fill:1;
		REGWORD v_fifo0_rx_fill:1;
		REGWORD v_fifo1_tx_fill:1;
		REGWORD v_fifo1_rx_fill:1;
		REGWORD v_fifo2_tx_fill:1;
		REGWORD v_fifo2_rx_fill:1;
		REGWORD v_fifo3_tx_fill:1;
		REGWORD v_fifo3_rx_fill:1;
	} bit_r_fill; // register and bitmap data
	typedef union {REGWORD reg; bit_r_fill bit;} reg_r_fill; // register and bitmap access


#define R_MISC_IRQMSK 0x1B // register access
	#define M_ST_IRQMSK 0x01 // bitmap mask (1bit)
	#define M_TI_IRQMSK 0x02 // bitmap mask (1bit)
	#define M_PROC_IRQMSK 0x04 // bitmap mask (1bit)
	#define M_CI_IRQMSK 0x08 // bitmap mask (1bit)
	#define M_MON_IRQMSK 0x10 // bitmap mask (1bit)
	#define M_IRQ_REV 0x40 // bitmap mask (1bit)
	#define M_IRQ_EN 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_irqmsk:1;
		REGWORD v_ti_irqmsk:1;
		REGWORD v_proc_irqmsk:1;
		REGWORD v_ci_irqmsk:1;
		REGWORD v_mon_irqmsk:1;
		REGWORD reserved_4:1;
		REGWORD v_irq_rev:1;
		REGWORD v_irq_en:1;
	} bit_r_misc_irqmsk; // register and bitmap data
	typedef union {REGWORD reg; bit_r_misc_irqmsk bit;} reg_r_misc_irqmsk; // register and bitmap access


#define R_TI 0x1C // register access
	#define M_EV_TS 0x0F // bitmap mask (4bit)
		#define M1_EV_TS 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_ev_ts:4;
		REGWORD reserved_30:4;
	} bit_r_ti; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ti bit;} reg_r_ti; // register and bitmap access


#define R_STATUS 0x1C // register access
	#define M_BUSY 0x01 // bitmap mask (1bit)
	#define M_PROC 0x02 // bitmap mask (1bit)
	#define M_AWAKE_IN 0x08 // bitmap mask (1bit)
	#define M_SYNC_IN 0x10 // bitmap mask (1bit)
	#define M_MISC_IRQSTA 0x40 // bitmap mask (1bit)
	#define M_FIFO_IRQSTA 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_busy:1;
		REGWORD v_proc:1;
		REGWORD reserved_32:1;
		REGWORD v_awake_in:1;
		REGWORD v_sync_in:1;
		REGWORD reserved_33:1;
		REGWORD v_misc_irqsta:1;
		REGWORD v_fifo_irqsta:1;
	} bit_r_status; // register and bitmap data
	typedef union {REGWORD reg; bit_r_status bit;} reg_r_status; // register and bitmap access


#define R_B1_TX_SL 0x20 // register access
	#define M_B1_TX_SL 0x1F // bitmap mask (5bit)
		#define M1_B1_TX_SL 0x01
	#define M_B1_TX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_B1_TX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_b1_tx_sl:5;
		REGWORD reserved_5:1;
		REGWORD v_b1_tx_rout:2;
	} bit_r_b1_tx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b1_tx_sl bit;} reg_r_b1_tx_sl; // register and bitmap access


#define R_B2_TX_SL 0x21 // register access
	#define M_B2_TX_SL 0x1F // bitmap mask (5bit)
		#define M1_B2_TX_SL 0x01
	#define M_B2_TX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_B2_TX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_b2_tx_sl:5;
		REGWORD reserved_6:1;
		REGWORD v_b2_tx_rout:2;
	} bit_r_b2_tx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b2_tx_sl bit;} reg_r_b2_tx_sl; // register and bitmap access


#define R_AUX1_TX_SL 0x22 // register access
	#define M_AUX1_TX_SL 0x1F // bitmap mask (5bit)
		#define M1_AUX1_TX_SL 0x01
	#define M_AUX1_TX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_AUX1_TX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_aux1_tx_sl:5;
		REGWORD reserved_7:1;
		REGWORD v_aux1_tx_rout:2;
	} bit_r_aux1_tx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux1_tx_sl bit;} reg_r_aux1_tx_sl; // register and bitmap access


#define R_AUX2_TX_SL 0x23 // register access
	#define M_AUX2_TX_SL 0x1F // bitmap mask (5bit)
		#define M1_AUX2_TX_SL 0x01
	#define M_AUX2_TX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_AUX2_TX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_aux2_tx_sl:5;
		REGWORD reserved_8:1;
		REGWORD v_aux2_tx_rout:2;
	} bit_r_aux2_tx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux2_tx_sl bit;} reg_r_aux2_tx_sl; // register and bitmap access


#define R_B1_RX_SL 0x24 // register access
	#define M_B1_RX_SL 0x1F // bitmap mask (5bit)
		#define M1_B1_RX_SL 0x01
	#define M_B1_RX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_B1_RX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_b1_rx_sl:5;
		REGWORD reserved_9:1;
		REGWORD v_b1_rx_rout:2;
	} bit_r_b1_rx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b1_rx_sl bit;} reg_r_b1_rx_sl; // register and bitmap access


#define R_B2_RX_SL 0x25 // register access
	#define M_B2_RX_SL 0x1F // bitmap mask (5bit)
		#define M1_B2_RX_SL 0x01
	#define M_B2_RX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_B2_RX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_b2_rx_sl:5;
		REGWORD reserved_10:1;
		REGWORD v_b2_rx_rout:2;
	} bit_r_b2_rx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b2_rx_sl bit;} reg_r_b2_rx_sl; // register and bitmap access


#define R_AUX1_RX_SL 0x26 // register access
	#define M_AUX1_RX_SL 0x1F // bitmap mask (5bit)
		#define M1_AUX1_RX_SL 0x01
	#define M_AUX1_RX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_AUX1_RX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_aux1_rx_sl:5;
		REGWORD reserved_11:1;
		REGWORD v_aux1_rx_rout:2;
	} bit_r_aux1_rx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux1_rx_sl bit;} reg_r_aux1_rx_sl; // register and bitmap access


#define R_AUX2_RX_SL 0x27 // register access
	#define M_AUX2_RX_SL 0x1F // bitmap mask (5bit)
		#define M1_AUX2_RX_SL 0x01
	#define M_AUX2_RX_ROUT 0xC0 // bitmap mask (2bit)
		#define M1_AUX2_RX_ROUT 0x40

	typedef struct // bitmap construction
	{
		REGWORD v_aux2_rx_sl:5;
		REGWORD reserved_12:1;
		REGWORD v_aux2_rx_rout:2;
	} bit_r_aux2_rx_sl; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux2_rx_sl bit;} reg_r_aux2_rx_sl; // register and bitmap access


#define R_CI_RX 0x28 // register access
	#define M_GCI_I 0x0F // bitmap mask (4bit)
		#define M1_GCI_I 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_gci_i:4;
		REGWORD reserved_23:4;
	} bit_r_ci_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ci_rx bit;} reg_r_ci_rx; // register and bitmap access


#define R_CI_TX 0x28 // register access
	#define M_GCI_C 0x0F // bitmap mask (4bit)
		#define M1_GCI_C 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_gci_c:4;
		REGWORD reserved_13:4;
	} bit_r_ci_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ci_tx bit;} reg_r_ci_tx; // register and bitmap access


#define R_PCM_GCI_STA 0x29 // register access
	#define M_MON_RXR 0x01 // bitmap mask (1bit)
	#define M_MON_TXR 0x02 // bitmap mask (1bit)
	#define M_STIO2_IN 0x40 // bitmap mask (1bit)
	#define M_STIO1_IN 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon_rxr:1;
		REGWORD v_mon_txr:1;
		REGWORD reserved_24:4;
		REGWORD v_stio2_in:1;
		REGWORD v_stio1_in:1;
	} bit_r_pcm_gci_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_r_pcm_gci_sta bit;} reg_r_pcm_gci_sta; // register and bitmap access


#define R_MON1_RX 0x2A // register access
	#define M_MON1_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon1_rx:8;
	} bit_r_mon1_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mon1_rx bit;} reg_r_mon1_rx; // register and bitmap access


#define R_MON1_TX 0x2A // register access
	#define M_MON1_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon1_tx:8;
	} bit_r_mon1_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mon1_tx bit;} reg_r_mon1_tx; // register and bitmap access


#define R_MON2_RX 0x2B // register access
	#define M_MON2_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon2_rx:8;
	} bit_r_mon2_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mon2_rx bit;} reg_r_mon2_rx; // register and bitmap access


#define R_MON2_TX 0x2B // register access
	#define M_MON2_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_mon2_tx:8;
	} bit_r_mon2_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_mon2_tx bit;} reg_r_mon2_tx; // register and bitmap access


#define R_B1_RX 0x2C // register access
	#define M_B1_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_rx:8;
	} bit_r_b1_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b1_rx bit;} reg_r_b1_rx; // register and bitmap access


#define R_B1_TX 0x2C // register access
	#define M_B1_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_tx:8;
	} bit_r_b1_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b1_tx bit;} reg_r_b1_tx; // register and bitmap access


#define R_B2_RX 0x2D // register access
	#define M_B2_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b2_rx:8;
	} bit_r_b2_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b2_rx bit;} reg_r_b2_rx; // register and bitmap access


#define R_B2_TX 0x2D // register access
	#define M_B2_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b2_tx:8;
	} bit_r_b2_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_b2_tx bit;} reg_r_b2_tx; // register and bitmap access


#define R_AUX1_RX 0x2E // register access
	#define M_AUX1_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_aux1_rx:8;
	} bit_r_aux1_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux1_rx bit;} reg_r_aux1_rx; // register and bitmap access


#define R_AUX1_TX 0x2E // register access
	#define M_AUX1_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_aux1_tx:8;
	} bit_r_aux1_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux1_tx bit;} reg_r_aux1_tx; // register and bitmap access


#define R_AUX2_RX 0x2F // register access
	#define M_AUX2_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_aux2_rx:8;
	} bit_r_aux2_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux2_rx bit;} reg_r_aux2_rx; // register and bitmap access


#define R_AUX2_TX 0x2F // register access
	#define M_AUX2_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_aux2_tx:8;
	} bit_r_aux2_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_aux2_tx bit;} reg_r_aux2_tx; // register and bitmap access


#define R_ST_RD_STA 0x30 // register access
	#define M_ST_STA 0x0F // bitmap mask (4bit)
		#define M1_ST_STA 0x01
	#define M_FR_SYNC 0x10 // bitmap mask (1bit)
	#define M_T2_EXP 0x20 // bitmap mask (1bit)
	#define M_INFO0 0x40 // bitmap mask (1bit)
	#define M_G2_G3 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_sta:4;
		REGWORD v_fr_sync:1;
		REGWORD v_t2_exp:1;
		REGWORD v_info0:1;
		REGWORD v_g2_g3:1;
	} bit_r_st_rd_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_rd_sta bit;} reg_r_st_rd_sta; // register and bitmap access


#define R_ST_WR_STA 0x30 // register access
	#define M_ST_SET_STA 0x0F // bitmap mask (4bit)
		#define M1_ST_SET_STA 0x01
	#define M_ST_LD_STA 0x10 // bitmap mask (1bit)
	#define M_ST_ACT 0x60 // bitmap mask (2bit)
		#define M1_ST_ACT 0x20
	#define M_SET_G2_G3 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_set_sta:4;
		REGWORD v_st_ld_sta:1;
		REGWORD v_st_act:2;
		REGWORD v_set_g2_g3:1;
	} bit_r_st_wr_sta; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_wr_sta bit;} reg_r_st_wr_sta; // register and bitmap access


#define R_ST_CTRL0 0x31 // register access
	#define M_B1_EN 0x01 // bitmap mask (1bit)
	#define M_B2_EN 0x02 // bitmap mask (1bit)
	#define M_ST_MD 0x04 // bitmap mask (1bit)
	#define M_D_PRIO 0x08 // bitmap mask (1bit)
	#define M_SQ_EN 0x10 // bitmap mask (1bit)
	#define M_96KHZ 0x20 // bitmap mask (1bit)
	#define M_TX_LI 0x40 // bitmap mask (1bit)
	#define M_ST_STOP 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_en:1;
		REGWORD v_b2_en:1;
		REGWORD v_st_md:1;
		REGWORD v_d_prio:1;
		REGWORD v_sq_en:1;
		REGWORD v_96khz:1;
		REGWORD v_tx_li:1;
		REGWORD v_st_stop:1;
	} bit_r_st_ctrl0; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_ctrl0 bit;} reg_r_st_ctrl0; // register and bitmap access


#define R_ST_CTRL1 0x32 // register access
	#define M_G2_G3_EN 0x01 // bitmap mask (1bit)
	#define M_D_RES 0x04 // bitmap mask (1bit)
	#define M_E_IGNO 0x08 // bitmap mask (1bit)
	#define M_E_LO 0x10 // bitmap mask (1bit)
	#define M_B12_SWAP 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_g2_g3_en:1;
		REGWORD reserved_14:1;
		REGWORD v_d_res:1;
		REGWORD v_e_igno:1;
		REGWORD v_e_lo:1;
		REGWORD reserved_15:2;
		REGWORD v_b12_swap:1;
	} bit_r_st_ctrl1; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_ctrl1 bit;} reg_r_st_ctrl1; // register and bitmap access


#define R_ST_CTRL2 0x33 // register access
	#define M_B1_RX_EN 0x01 // bitmap mask (1bit)
	#define M_B2_RX_EN 0x02 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_b1_rx_en:1;
		REGWORD v_b2_rx_en:1;
		REGWORD reserved_16:6;
	} bit_r_st_ctrl2; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_ctrl2 bit;} reg_r_st_ctrl2; // register and bitmap access


#define R_ST_SQ_RD 0x34 // register access
	#define M_ST_SQ_RD 0x0F // bitmap mask (4bit)
		#define M1_ST_SQ_RD 0x01
	#define M_MF_RX_RDY 0x10 // bitmap mask (1bit)
	#define M_MF_TX_RDY 0x80 // bitmap mask (1bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_sq_rd:4;
		REGWORD v_mf_rx_rdy:1;
		REGWORD reserved_25:2;
		REGWORD v_mf_tx_rdy:1;
	} bit_r_st_sq_rd; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_sq_rd bit;} reg_r_st_sq_rd; // register and bitmap access


#define R_ST_SQ_WR 0x34 // register access
	#define M_ST_SQ_WR 0x0F // bitmap mask (4bit)
		#define M1_ST_SQ_WR 0x01

	typedef struct // bitmap construction
	{
		REGWORD v_st_sq_wr:4;
		REGWORD reserved_17:4;
	} bit_r_st_sq_wr; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_sq_wr bit;} reg_r_st_sq_wr; // register and bitmap access


#define R_ST_CLK_DLY 0x37 // register access
	#define M_ST_CLK_DLY 0x0F // bitmap mask (4bit)
		#define M1_ST_CLK_DLY 0x01
	#define M_ST_SMPL 0x70 // bitmap mask (3bit)
		#define M1_ST_SMPL 0x10

	typedef struct // bitmap construction
	{
		REGWORD v_st_clk_dly:4;
		REGWORD v_st_smpl:3;
		REGWORD reserved_18:1;
	} bit_r_st_clk_dly; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_clk_dly bit;} reg_r_st_clk_dly; // register and bitmap access


#define R_ST_B1_RX 0x3C // register access
	#define M_ST_B1_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_b1_rx:8;
	} bit_r_st_b1_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_b1_rx bit;} reg_r_st_b1_rx; // register and bitmap access


#define R_ST_B1_TX 0x3C // register access
	#define M_ST_B1_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_b1_tx:8;
	} bit_r_st_b1_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_b1_tx bit;} reg_r_st_b1_tx; // register and bitmap access


#define R_ST_B2_RX 0x3D // register access
	#define M_ST_B2_RX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_b2_rx:8;
	} bit_r_st_b2_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_b2_rx bit;} reg_r_st_b2_rx; // register and bitmap access


#define R_ST_B2_TX 0x3D // register access
	#define M_ST_B2_TX 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_st_b2_tx:8;
	} bit_r_st_b2_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_b2_tx bit;} reg_r_st_b2_tx; // register and bitmap access


#define R_ST_D_RX 0x3E // register access
	#define M_ST_D_RX 0xC0 // bitmap mask (2bit)
		#define M1_ST_D_RX 0x40

	typedef struct // bitmap construction
	{
		REGWORD reserved_26:6;
		REGWORD v_st_d_rx:2;
	} bit_r_st_d_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_d_rx bit;} reg_r_st_d_rx; // register and bitmap access


#define R_ST_D_TX 0x3E // register access
	#define M_ST_D_TX 0xC0 // bitmap mask (2bit)
		#define M1_ST_D_TX 0x40

	typedef struct // bitmap construction
	{
		REGWORD reserved_19:6;
		REGWORD v_st_d_tx:2;
	} bit_r_st_d_tx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_d_tx bit;} reg_r_st_d_tx; // register and bitmap access


#define R_ST_E_RX 0x3F // register access
	#define M_ST_E_RX 0xC0 // bitmap mask (2bit)
		#define M1_ST_E_RX 0x40

	typedef struct // bitmap construction
	{
		REGWORD reserved_27:6;
		REGWORD v_st_e_rx:2;
	} bit_r_st_e_rx; // register and bitmap data
	typedef union {REGWORD reg; bit_r_st_e_rx bit;} reg_r_st_e_rx; // register and bitmap access


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


#define R_RAM_DATA 0xC0 // register access
	#define M_RAM_DATA 0xFF // bitmap mask (8bit)

	typedef struct // bitmap construction
	{
		REGWORD v_ram_data:8;
	} bit_r_ram_data; // register and bitmap data
	typedef union {REGWORD reg; bit_r_ram_data bit;} reg_r_ram_data; // register and bitmap access


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
	#define M_TRP_IRQ 0x0C // bitmap mask (2bit)
		#define M1_TRP_IRQ 0x04
	#define M_DATA_FLOW 0xE0 // bitmap mask (3bit)
		#define M1_DATA_FLOW 0x20

	typedef struct // bitmap construction
	{
		REGWORD v_iff:1;
		REGWORD v_hdlc_trp:1;
		REGWORD v_trp_irq:2;
		REGWORD reserved_20:1;
		REGWORD v_data_flow:3;
	} bit_a_con_hdlc; // register and bitmap data
	typedef union {REGWORD reg; bit_a_con_hdlc bit;} reg_a_con_hdlc; // register and bitmap access


#define A_HDLC_PAR 0xFB // register access
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
	} bit_a_hdlc_par; // register and bitmap data
	typedef union {REGWORD reg; bit_a_hdlc_par bit;} reg_a_hdlc_par; // register and bitmap access


#define A_CHANNEL 0xFC // register access
	#define M_CH_DIR 0x01 // bitmap mask (1bit)
	#define M_CH_NUM 0x06 // bitmap mask (2bit)
		#define M1_CH_NUM 0x02

	typedef struct // bitmap construction
	{
		REGWORD v_ch_dir:1;
		REGWORD v_ch_num:2;
		REGWORD reserved_21:5;
	} bit_a_channel; // register and bitmap data
	typedef union {REGWORD reg; bit_a_channel bit;} reg_a_channel; // register and bitmap access


#endif /* _HFCSMCC_H_ */

/*___________________________________________________________________________________*/
/*                                                                                   */
/*  End of HFC-S mini register definitions.                                          */
/*                                                                                   */
/*  Total number of registers processed: 71 of 71                                    */
/*  Total number of bitmaps processed  : 209                                         */
/*                                                                                   */
/*___________________________________________________________________________________*/
/*                                                                                   */
