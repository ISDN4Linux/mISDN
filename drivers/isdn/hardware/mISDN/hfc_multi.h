/*

 * see notice in hfc_multi.c
 */

#define DEBUG_HFCMULTI_FIFO	0x0001
#define DEBUG_HFCMULTI_CRC	0x0002
#define DEBUG_HFCMULTI_INIT     0x0004
#define DEBUG_HFCMULTI_MGR	0x0008
#define DEBUG_HFCMULTI_MODE	0x0010
#define DEBUG_HFCMULTI_MSG	0x0020
#define DEBUG_HFCMULTI_STATE	0x0040
#define DEBUG_HFCMULTI_SYNC	0x0100
#define DEBUG_HFCMULTI_DTMF	0x0200
#define DEBUG_HFCMULTI_LOCK	0x8000

#define PCI_ENA_REGIO	0x01
#define PCI_ENA_MEMIO	0x02

/* NOTE: some registers are assigned multiple times due to different modes
         also registers are assigned differen for HFC-4s/8s and HFC-E1
*/

#define MAX_FRAME_SIZE	2048

struct hfc_chan {
	/* dch or bch is set, otherwhise this channel is not used */
	dchannel_t	*dch; /* link if channel is a D-channel */
	bchannel_t	*bch; /* link if channel is a B-channel */
	int		rx_idx; /* for D-channel */
	unsigned char	*rx_buf; /* for D-channel */
	int		port; /* the interface port this channel is associated with */
	int		nt_mode;
	int		nt_timer; /* -1 if off, 0 if elapsed, >0 if running */
	int		los, ais, slip_tx, slip_rx; /* current alarms */
	int		jitter;
	u_long		cfg; /* port configuration */
	int		sync; /* sync state (used by E1) */
	struct sk_buff_head dtmfque;
	unsigned long	protocol; /* current protocol */
	int		slot_tx; /* current pcm slot */
	int		bank_tx; /* current pcm bank */
	int		slot_rx;
	int		bank_rx;
	int		conf; /* conference setting of TX slot */
	int		txpending; /* if there is currently data in the FIFO 0=no, 1=yes, 2=splloop */
	int		e1_state; /* keep track of last state */
};

typedef struct hfc_chan		hfc_chan_t;

struct hfcmulti_hw {
	unsigned char	r_ctrl;	
	unsigned char	r_irq_ctrl;	
	unsigned char	r_cirm;	
	unsigned char	r_ram_sz;
	unsigned char	r_pcm_mo0;
	unsigned char	r_irqmsk_misc;
	unsigned char	r_dtmf;
	unsigned char	r_st_sync;
	unsigned char	r_sci_msk;
	unsigned char	a_st_ctrl0[8];
	timer_t	timer;
};

typedef struct hfcmulti_hw	hfcmulti_hw_t;

/* for each stack these flags are used (cfg) */
#define HFC_CFG_NTMODE		0 /* NT-mode */
#define HFC_CFG_NONCAP_TX	1 /* S/T TX interface has less capacity */
#define HFC_CFG_DIS_ECHANNEL	2 /* disable E-channel processing */
#define HFC_CFG_REG_ECHANNEL	2 /* register E-channel */
#define HFC_CFG_OPTICAL		3 /* the E1 interface is optical */
#define HFC_CFG_REPORT_LOS	4 /* the card should report loss of signal */
#define HFC_CFG_REPORT_AIS	5 /* the card should report alarm ind. sign. */
#define HFC_CFG_REPORT_SLIP	6 /* the card should report bit slips */
#define HFC_CFG_DTMF		7 /* enable DTMF-detection */

#define HFC_CHIP_EXRAM_128	0 /* external ram 128k */
#define HFC_CHIP_EXRAM_512	1 /* external ram 256k */
#define HFC_CHIP_REVISION0	2 /* old fifo handling */
#define HFC_CHIP_PCM_SLAVE	3 /* PCM is slave */
#define HFC_CHIP_DTMF		4 /* DTMF decoding is enabled */
#define HFC_CHIP_ULAW		5 /* ULAW mode */
#define HFC_CHIP_LEDS		5 /* used LEDs */
#define HFC_CHIP_CLOCK2		6 /* double clock mode */

struct hfc_multi {
	struct list_head	list;
	char		name[32];
	int		id;	/* chip number starting with 1 */
	int		pcm;	/* id of pcm bus */
	int		type;

	u_int		irq;	/* irq used by card */
	u_int		irqcnt;
	struct pci_dev	*pci_dev;
#ifdef CONFIG_HFCMULTI_PCIMEM
	unsigned char	*pci_membase;/* PCI memory (MUST BE BYTE POINTER) */
#else
	u_int		pci_iobase;/* PCI IO (MUST BE BYTE POINTER) */
#endif
	hfcmulti_hw_t	hw;	/* remember data of write-only-registers */

	u_long		chip;	/* chip configuration */
	int		masterclk;/* port that provides master clock -1=off */
	int		dtmf;	/* flag that dtmf is currently in process */
	int		Flen;	/* F-buffer size */
	int		Zlen;	/* Z-buffer size (not maximum) */
	int		Zmin;	/* Z-buffer offset */
	int		DTMFbase;/* base address of DTMF coefficients */

	u_int		slots;	/* number of PCM slots */
	u_int		leds;	/* type of leds */
	u_int		ledcount; /* used to animate leds */

	mISDN_HWlock_t  lock;	/* the lock */
#ifdef SPIN_DEBUG
	void		*lock_adr;
#endif

	/* the channel index is counted from 0, regardless where the channel
	 * is located on the hfc-channel.
	 * the bch->channel is equvalent to the hfc-channel
	 */
	hfc_chan_t	chan[32];
	u_char		created[8]; /* what port is created */
	signed char	slot_owner[256]; /* owner channel of slot */
};

typedef struct hfc_multi	hfc_multi_t;


/*********************************************\
|* REGISTER SETTING FOR HFC-4S/8S AND HFC-E1 *|
\*********************************************/

/* write only registers */
#define R_CIRM			0x00
#define R_CTRL			0x01
#define R_BRG_PCM_CFG 		0x02
#define R_RAM_ADDR0		0x08
#define R_RAM_ADDR1		0x09
#define R_RAM_ADDR2		0x0A
#define R_FIRST_FIFO		0x0B
#define R_RAM_SZ		0x0C
#define R_FIFO_MD		0x0D
#define R_INC_RES_FIFO		0x0E
#define R_FSM_IDX		0x0F
#define R_FIFO			0x0F
#define R_SLOT			0x10
#define R_IRQMSK_MISC		0x11
#define R_SCI_MSK		0x12
#define R_IRQ_CTRL		0x13
#define R_PCM_MO0		0x14
#define R_PCM_MO1		0x15
#define R_PCM_MO2		0x15
#define R_SH0H			0x15
#define R_SH1H			0x15
#define R_SH0L			0x15
#define R_SH1L			0x15
#define R_SL_SEL0		0x15
#define R_SL_SEL1		0x15
#define R_SL_SEL2		0x15
#define R_SL_SEL3		0x15
#define R_SL_SEL4		0x15
#define R_SL_SEL5		0x15
#define R_SL_SEL6		0x15
#define R_SL_SEL7		0x15
#define R_ST_SEL		0x16
#define R_ST_SYNC		0x17
#define R_CONF_EN		0x18
#define R_TI_WD			0x1A
#define R_BERT_WD_MD		0x1B
#define R_DTMF			0x1C
#define R_DTMF_N		0x1D
#define R_E1_WR_STA		0x20
#define R_E1_RD_STA		0x20
#define R_LOS0			0x22
#define R_LOS1			0x23
#define R_RX0			0x24
#define R_RX_FR0		0x25
#define R_RX_FR1		0x26
#define R_TX0			0x28
#define R_TX1			0x29
#define R_TX_FR0		0x2C
#define R_TX_FR1		0x2D
#define R_TX_FR2		0x2E
#define R_JATT_ATT		0x2F /* undocumented */
#define A_ST_RD_STATE		0x30
#define A_ST_WR_STATE		0x30
#define R_RX_OFF		0x30
#define A_ST_CTRL0		0x31
#define R_SYNC_OUT		0x21
#define A_ST_CTRL1		0x32
#define A_ST_CTRL2		0x33
#define A_ST_SQ_WR		0x34
#define R_TX_OFF		0x34
#define R_SYNC_CTRL		0x35
#define A_ST_CLK_DLY		0x37
#define R_PWM0			0x38
#define R_PWM1			0x39
#define A_ST_B1_TX		0x3C
#define A_ST_B2_TX		0x3D
#define A_ST_D_TX		0x3E
#define R_GPIO_OUT0		0x40
#define R_GPIO_OUT1		0x41
#define R_GPIO_EN0		0x42
#define R_GPIO_EN1		0x43
#define R_GPIO_SEL		0x44
#define R_BRG_CTRL		0x45
#define R_PWM_MD		0x46
#define R_BRG_MD		0x47
#define R_BRG_TIM0		0x48
#define R_BRG_TIM1		0x49
#define R_BRG_TIM2		0x4A
#define R_BRG_TIM3		0x4B
#define R_BRG_TIM_SEL01		0x4C
#define R_BRG_TIM_SEL23		0x4D
#define R_BRG_TIM_SEL45		0x4E
#define R_BRG_TIM_SEL67		0x4F
#define A_SL_CFG		0xD0
#define A_CONF			0xD1
#define A_CH_MSK		0xF4
#define A_CON_HDLC		0xFA
#define A_SUBCH_CFG		0xFB
#define A_CHANNEL		0xFC
#define A_FIFO_SEQ		0xFD
#define A_IRQ_MSK		0xFF

/* read only registers */
#define A_Z12			0x04
#define A_Z1L			0x04
#define A_Z1			0x04
#define A_Z1H			0x05
#define A_Z2L			0x06
#define A_Z2			0x06
#define A_Z2H			0x07
#define A_F1			0x0C
#define A_F12			0x0C
#define A_F2			0x0D
#define R_IRQ_OVIEW		0x10
#define R_IRQ_MISC		0x11
#define R_IRQ_STATECH		0x12
#define R_CONF_OFLOW		0x14
#define R_RAM_USE		0x15
#define R_CHIP_ID		0x16
#define R_BERT_STA		0x17
#define R_F0_CNTL		0x18
#define R_F0_CNTH		0x19
#define R_BERT_EC		0x1A
#define R_BERT_ECL		0x1A
#define R_BERT_ECH		0x1B
#define R_STATUS		0x1C
#define R_CHIP_RV		0x1F
#define R_STATE			0x20
#define R_RX_STA0		0x24
#define R_RX_STA1		0x25
#define R_RX_STA2		0x26
#define R_RX_STA3		0x27
#define R_JATT_DIR		0x2b /* undocumented */
#define R_SLIP			0x2c
#define A_ST_RD_STA		0x30
#define R_FAS_EC		0x30
#define R_FAS_ECL		0x30
#define R_FAS_ECH		0x31
#define R_VIO_EC		0x32
#define R_VIO_ECL		0x32
#define R_VIO_ECH		0x33
#define A_ST_SQ_RD		0x34
#define R_CRC_EC		0x34
#define R_CRC_ECL		0x34
#define R_CRC_ECH		0x35
#define R_E_EC			0x36
#define R_E_ECL			0x36
#define R_E_ECH			0x37
#define R_SA6_SA13_EC		0x38
#define R_SA6_SA13_ECL		0x38
#define R_SA6_SA13_ECH		0x39
#define R_SA6_SA23_EC		0x3A
#define R_SA6_SA23_ECL		0x3A
#define R_SA6_SA23_ECH		0x3B
#define A_ST_B1_RX		0x3C
#define A_ST_B2_RX		0x3D
#define A_ST_D_RX		0x3E
#define A_ST_E_RX		0x3F
#define R_GPIO_IN0		0x40
#define R_GPIO_IN1		0x41
#define R_GPI_IN0		0x44
#define R_GPI_IN1		0x45
#define R_GPI_IN2		0x46
#define R_GPI_IN3		0x47
#define R_INT_DATA		0x88
#define R_IRQ_FIFO_BL0		0xC8
#define R_IRQ_FIFO_BL1		0xC9
#define R_IRQ_FIFO_BL2		0xCA
#define R_IRQ_FIFO_BL3		0xCB
#define R_IRQ_FIFO_BL4		0xCC
#define R_IRQ_FIFO_BL5		0xCD
#define R_IRQ_FIFO_BL6		0xCE
#define R_IRQ_FIFO_BL7		0xCF

/* read and write registers */
#define A_FIFO_DATA0		0x80
#define A_FIFO_DATA1		0x80
#define A_FIFO_DATA2		0x80
#define A_FIFO_DATA0_NOINC	0x84
#define A_FIFO_DATA1_NOINC	0x84
#define A_FIFO_DATA2_NOINC	0x84
#define R_RAM_DATA		0xC0


/****************************************\
|* BIT SETTING FOR HFC-4S/8S AND HFC-E1 *|
\****************************************/

/* chapter 2: universal bus interface */
/* R_CIRM */
#define V_IRQ_SEL		0x01
#define V_SRES			0x08
#define V_HFCRES		0x10
#define V_PCMRES		0x20
#define V_STRES			0x40
#define V_ETRES			0x40
#define V_RLD_EPR		0x80
/* R_CTRL */
#define V_FIFO_LPRIO		0x02
#define V_SLOW_RD		0x04
#define V_EXT_RAM		0x08
#define V_CLK_OFF		0x20
#define V_ST_CLK		0x40
/* R_RAM_ADDR0 */
#define V_RAM_ADDR2		0x01
#define V_ADDR_RES		0x40
#define V_ADDR_INC		0x80
/* R_RAM_SZ */
#define V_RAM_SZ		0x01
#define V_PWM0_16KHZ		0x10
#define V_PWM1_16KHZ		0x20
#define V_FZ_MD			0x80
/* R_CHIP_ID */
#define V_PNP_IRQ		0x01
#define V_CHIP_ID		0x10

/* chapter 3: data flow */
/* R_FIRST_FIFO */
#define V_FIRST_FIRO_DIR	0x01
#define V_FIRST_FIFO_NUM	0x02
/* R_FIFO_MD */
#define V_FIFO_MD		0x01
#define V_CSM_MD		0x04
#define V_FSM_MD		0x08
#define V_FIFO_SZ		0x10
/* R_FIFO */
#define V_FIFO_DIR		0x01
#define V_FIFO_NUM		0x02
#define V_REV			0x80
/* R_SLOT */
#define V_SL_DIR		0x01
#define V_SL_NUM		0x02
/* A_SL_CFG */
#define V_CH_DIR		0x01
#define V_CH_SEL		0x02
#define V_ROUTING		0x40
/* A_CON_HDLC */
#define V_IFF			0x01
#define V_HDLC_TRP		0x02
#define V_TRP_IRQ		0x04
#define V_DATA_FLOW		0x20
/* A_SUBCH_CFG */
#define V_BIT_CNT		0x01
#define V_START_BIT		0x08
#define V_LOOP_FIFO		0x40
#define V_INV_DATA		0x80
/* A_CHANNEL */
#define V_CH_DIR0		0x01
#define V_CH_NUM0		0x02
/* A_FIFO_SEQ */
#define V_NEXT_FIFO_DIR		0x01
#define V_NEXT_FIFO_NUM		0x02
#define V_SEQ_END		0x40

/* chapter 4: FIFO handling and HDLC controller */
/* R_INC_RES_FIFO */
#define V_INC_F			0x01
#define V_RES_F			0x02
#define V_RES_LOST		0x04

/* chapter 5: S/T interface */
/* R_SCI_MSK */
#define V_SCI_MSK_ST0		0x01
#define V_SCI_MSK_ST1		0x02
#define V_SCI_MSK_ST2		0x04
#define V_SCI_MSK_ST3		0x08
#define V_SCI_MSK_ST4		0x10
#define V_SCI_MSK_ST5		0x20
#define V_SCI_MSK_ST6		0x40
#define V_SCI_MSK_ST7		0x80
/* R_ST_SEL */
#define V_ST_SEL		0x01
#define V_MULT_ST		0x08
/* R_ST_SYNC */
#define V_SYNC_SEL		0x01
#define V_AUTO_SYNC		0x08
/* A_ST_WR_STA */
#define V_ST_SET_STA		0x01
#define V_ST_LD_STA		0x10
#define V_ST_ACT		0x20
#define V_SET_G2_G3		0x80
/* A_ST_CTRL0 */
#define V_B1_EN			0x01
#define V_B2_EN			0x02
#define V_ST_MD			0x04
#define V_D_PRIO		0x08
#define V_SQ_EN			0x10
#define V_96KHZ			0x20
#define V_TX_LI			0x40
#define V_ST_STOP		0x80
/* A_ST_CTRL1 */
#define V_G2_G3_EN		0x01
#define V_D_HI			0x04
#define V_E_IGNO		0x08
#define V_E_LO			0x10
#define V_B12_SWAP		0x80
/* A_ST_CTRL2 */
#define V_B1_RX_EN		0x01
#define V_B2_RX_EN		0x02
#define V_ST_TRIS		0x40
/* A_ST_CLK_DLY */
#define V_ST_CK_DLY		0x01
#define V_ST_SMPL		0x10
/* A_ST_D_TX */
#define V_ST_D_TX		0x40
/* R_IRQ_STATECH */
#define V_SCI_ST0		0x01
#define V_SCI_ST1		0x02
#define V_SCI_ST2		0x04
#define V_SCI_ST3		0x08
#define V_SCI_ST4		0x10
#define V_SCI_ST5		0x20
#define V_SCI_ST6		0x40
#define V_SCI_ST7		0x80
/* A_ST_RD_STA */
#define V_ST_STA		0x01
#define V_FR_SYNC_ST		0x10
#define V_TI2_EXP		0x20
#define V_INFO0			0x40
#define V_G2_G3			0x80
/* A_ST_SQ_RD */
#define V_ST_SQ			0x01
#define V_MF_RX_RDY		0x10
#define V_MF_TX_RDY		0x80
/* A_ST_D_RX */
#define V_ST_D_RX		0x40
/* A_ST_E_RX */
#define V_ST_E_RX		0x40

/* chapter 5: E1 interface */
/* R_E1_WR_STA */
/* R_E1_RD_STA */
#define V_E1_SET_STA		0x01
#define V_E1_LD_STA		0x10
/* R_RX0 */
#define V_RX_CODE		0x01
#define V_RX_FBAUD		0x04
#define V_RX_CMI		0x08
#define V_RX_INV_CMI		0x10
#define V_RX_INV_CLK		0x20
#define V_RX_INV_DATA		0x40
#define V_AIS_ITU		0x80
/* R_RX_FR0 */
#define V_NO_INSYNC		0x01
#define V_AUTO_RESYNC		0x02
#define V_AUTO_RECO		0x04
#define V_SWORD_COND		0x08
#define V_SYNC_LOSS		0x10
#define V_XCRC_SYNC		0x20
#define V_MF_RESYNC		0x40
#define V_RESYNC		0x80
/* R_RX_FR1 */
#define V_RX_MF			0x01
#define V_RX_MF_SYNC		0x02
#define V_RX_SL0_RAM		0x04
#define V_ERR_SIM		0x20
#define V_RES_NMF		0x40
/* R_TX0 */
#define V_TX_CODE		0x01
#define V_TX_FBAUD		0x04
#define V_TX_CMI_CODE		0x08
#define V_TX_INV_CMI_CODE	0x10
#define V_TX_INV_CLK		0x20
#define V_TX_INV_DATA		0x40
#define V_OUT_EN		0x80
/* R_TX1 */
#define V_INV_CLK		0x01
#define V_EXCHG_DATA_LI		0x02
#define V_AIS_OUT		0x04
#define V_ATX			0x20
#define V_NTRI			0x40
#define V_AUTO_ERR_RES		0x80
/* R_TX_FR0 */
#define V_TRP_FAS		0x01
#define V_TRP_NFAS		0x02
#define V_TRP_RAL		0x04
#define V_TRP_SA		0x08
/* R_TX_FR1 */
#define V_TX_FAS		0x01
#define V_TX_NFAS		0x02
#define V_TX_RAL		0x04
#define V_TX_SA			0x08
/* R_TX_FR2 */
#define V_TX_MF			0x01
#define V_TRP_SL0		0x02
#define V_TX_SL0_RAM		0x04
#define V_TX_E			0x10
#define V_NEG_E			0x20
#define V_XS12_ON		0x40
#define V_XS15_ON		0x80
/* R_RX_OFF */
#define V_RX_SZ			0x01
#define V_RX_INIT		0x04
/* R_SYNC_OUT */
#define V_SYNC_E1_RX		0x01
#define V_IPATS0		0x20
#define V_IPATS1		0x40
#define V_IPATS2		0x80
/* R_TX_OFF */
#define V_TX_SZ			0x01
#define V_TX_INIT		0x04
/* R_SYNC_CTRL */
#define V_EXT_CLK_SYNC		0x01
#define V_SYNC_OFFS		0x02
#define V_PCM_SYNC		0x04
#define V_NEG_CLK		0x08
#define V_HCLK			0x10
#define V_JATT_AUTO_DEL		0x20
#define V_JATT_AUTO		0x40
#define V_JATT_EN		0x80
/* R_STATE */
#define V_E1_STA		0x01
#define V_ALT_FR_RX		0x40
#define V_ALT_FR_TX		0x80
/* R_RX_STA0 */
#define V_RX_STA		0x01
#define V_FR_SYNC_E1		0x04
#define V_SIG_LOS		0x08
#define V_MFA_STA		0x10
#define V_AIS			0x40
#define V_NO_MF_SYNC		0x80
/* R_RX_STA1 */
#define V_SI_FAS		0x01
#define V_SI_NFAS		0x02
#define V_A			0x04
#define V_CRC_OK		0x08
#define V_TX_E1			0x10
#define V_TX_E2			0x20
#define V_RX_E1			0x40
#define V_RX_E2			0x80
/* R_SLIP */
#define V_SLIP_RX		0x01
#define V_FOSLIP_RX		0x08
#define V_SLIP_TX		0x10
#define V_FOSLIP_TX		0x80

/* chapter 6: PCM interface */
/* R_PCM_MO0 */
#define V_PCM_MO		0x01
#define V_C4_POL		0x02
#define V_F0_NEG		0x04
#define V_F0_LEN		0x08
#define V_PCM_ADDR		0x10
/* R_SL_SEL0 */
#define V_SL_SEL0		0x01
#define V_SH_SEL0		0x80
/* R_SL_SEL1 */
#define V_SL_SEL1		0x01
#define V_SH_SEL1		0x80
/* R_SL_SEL2 */
#define V_SL_SEL2		0x01
#define V_SH_SEL2		0x80
/* R_SL_SEL3 */
#define V_SL_SEL3		0x01
#define V_SH_SEL3		0x80
/* R_SL_SEL4 */
#define V_SL_SEL4		0x01
#define V_SH_SEL4		0x80
/* R_SL_SEL5 */
#define V_SL_SEL5		0x01
#define V_SH_SEL5		0x80
/* R_SL_SEL6 */
#define V_SL_SEL6		0x01
#define V_SH_SEL6		0x80
/* R_SL_SEL7 */
#define V_SL_SEL7		0x01
#define V_SH_SEL7		0x80
/* R_PCM_MD1 */
#define V_ODEC_CON		0x01
#define V_PLL_ADJ		0x04
#define V_PCM_DR		0x10
#define V_PCM_LOOP		0x40
/* R_PCM_MD2 */
#define V_SYNC_PLL		0x02
#define V_SYCN_SRC		0x04
#define V_SYNC_OUT		0x08
#define V_ICR_FR_TIME		0x40
#define V_EN_PLL		0x80

/* chapter 7: pulse width modulation */
/* R_PWM_MD */
#define V_EXT_IRQ_EN		0x08
#define V_PWM0_MD		0x10
#define V_PWM1_MD		0x40

/* chapter 8: multiparty audio conferences */
/* R_CONF_EN */
#define V_CONF_EN		0x01
#define V_ULAW			0x80
/* A_CONF */
#define V_CONF_NUM		0x01
#define V_NOISE_SUPPR		0x08
#define V_ATT_LEV		0x20
#define V_CONF_SL		0x80
/* R_CONF_OFLOW */
#define V_CONF_OFLOW0		0x01
#define V_CONF_OFLOW1		0x02
#define V_CONF_OFLOW2		0x04
#define V_CONF_OFLOW3		0x08
#define V_CONF_OFLOW4		0x10
#define V_CONF_OFLOW5		0x20
#define V_CONF_OFLOW6		0x40
#define V_CONF_OFLOW7		0x80

/* chapter 9: DTMF contoller */
/* R_DTMF0 */
#define V_DTMF_EN		0x01
#define V_HARM_SEL		0x02
#define V_DTMF_RX_CH		0x04
#define V_DTMF_STOP		0x08
#define V_CHBL_SEL		0x10
#define V_RST_DTMF		0x40
#define V_ULAW_SEL		0x80

/* chapter 10: BERT */
/* R_BERT_WD_MD */
#define V_PAT_SEQ		0x01
#define V_BERT_ERR		0x08
#define V_AUTO_WD_RES		0x20
#define V_WD_RES		0x80
/* R_BERT_STA */
#define V_BERT_SYNC_SRC		0x01
#define V_BERT_SYNC		0x10
#define V_BERT_INV_DATA		0x20

/* chapter 11: auxiliary interface */
/* R_BRG_PCM_CFG */
#define V_BRG_EN		0x01
#define V_BRG_MD		0x02
#define V_PCM_CLK		0x20
#define V_ADDR_WRDLY		0x40
/* R_BRG_CTRL */
#define V_BRG_CS		0x01
#define V_BRG_ADDR		0x08
#define V_BRG_CS_SRC		0x80
/* R_BRG_MD */
#define V_BRG_MD0		0x01
#define V_BRG_MD1		0x02
#define V_BRG_MD2		0x04
#define V_BRG_MD3		0x08
#define V_BRG_MD4		0x10
#define V_BRG_MD5		0x20
#define V_BRG_MD6		0x40
#define V_BRG_MD7		0x80
/* R_BRG_TIM0 */
#define V_BRG_TIM0_IDLE		0x01
#define V_BRG_TIM0_CLK		0x10
/* R_BRG_TIM1 */
#define V_BRG_TIM1_IDLE		0x01
#define V_BRG_TIM1_CLK		0x10
/* R_BRG_TIM2 */
#define V_BRG_TIM2_IDLE		0x01
#define V_BRG_TIM2_CLK		0x10
/* R_BRG_TIM3 */
#define V_BRG_TIM3_IDLE		0x01
#define V_BRG_TIM3_CLK		0x10
/* R_BRG_TIM_SEL01 */
#define V_BRG_WR_SEL0		0x01
#define V_BRG_RD_SEL0		0x04
#define V_BRG_WR_SEL1		0x10
#define V_BRG_RD_SEL1		0x40
/* R_BRG_TIM_SEL23 */
#define V_BRG_WR_SEL2		0x01
#define V_BRG_RD_SEL2		0x04
#define V_BRG_WR_SEL3		0x10
#define V_BRG_RD_SEL3		0x40
/* R_BRG_TIM_SEL45 */
#define V_BRG_WR_SEL4		0x01
#define V_BRG_RD_SEL4		0x04
#define V_BRG_WR_SEL5		0x10
#define V_BRG_RD_SEL5		0x40
/* R_BRG_TIM_SEL67 */
#define V_BRG_WR_SEL6		0x01
#define V_BRG_RD_SEL6		0x04
#define V_BRG_WR_SEL7		0x10
#define V_BRG_RD_SEL7		0x40

/* chapter 12: clock, reset, interrupt, timer and watchdog */
/* R_IRQMSK_MISC */
#define V_STA_IRQMSK		0x01
#define V_TI_IRQMSK		0x02
#define V_PROC_IRQMSK		0x04
#define V_DTMF_IRQMSK		0x08
#define V_IRQ1S_MSK		0x10
#define V_SA6_IRQMSK		0x20
#define V_RX_EOMF_MSK		0x40
#define V_TX_EOMF_MSK		0x80
/* R_IRQ_CTRL */
#define V_FIFO_IRQ		0x01
#define V_GLOB_IRQ_EN		0x08
#define V_IRQ_POL		0x10
/* R_TI_WD */
#define V_EV_TS			0x01
#define V_WD_TS			0x10
/* A_IRQ_MSK */
#define V_IRQ			0x01
#define V_BERT_EN		0x02
#define V_MIX_IRQ		0x04
/* R_IRQ_OVIEW */
#define V_IRQ_FIFO_BL0		0x01
#define V_IRQ_FIFO_BL1		0x02
#define V_IRQ_FIFO_BL2		0x04
#define V_IRQ_FIFO_BL3		0x08
#define V_IRQ_FIFO_BL4		0x10
#define V_IRQ_FIFO_BL5		0x20
#define V_IRQ_FIFO_BL6		0x40
#define V_IRQ_FIFO_BL7		0x80
/* R_IRQ_MISC */
#define V_STA_IRQ		0x01
#define V_TI_IRQ		0x02
#define V_IRQ_PROC		0x04
#define V_DTMF_IRQ		0x08
#define V_IRQ1S			0x10
#define V_SA6_IRQ		0x20
#define V_RX_EOMF		0x40
#define V_TX_EOMF		0x80
/* R_STATUS */
#define V_BUSY			0x01
#define V_PROC			0x02
#define V_DTMF_STA		0x04
#define V_LOST_STA		0x08
#define V_SYNC_IN		0x10
#define V_EXT_IRQSTA		0x20
#define V_MISC_IRQSTA		0x40
#define V_FR_IRQSTA		0x80
/* R_IRQ_FIFO_BL0 */
#define V_IRQ_FIFO0_TX		0x01
#define V_IRQ_FIFO0_RX		0x02
#define V_IRQ_FIFO1_TX		0x04
#define V_IRQ_FIFO1_RX		0x08
#define V_IRQ_FIFO2_TX		0x10
#define V_IRQ_FIFO2_RX		0x20
#define V_IRQ_FIFO3_TX		0x40
#define V_IRQ_FIFO3_RX		0x80
/* R_IRQ_FIFO_BL1 */
#define V_IRQ_FIFO4_TX		0x01
#define V_IRQ_FIFO4_RX		0x02
#define V_IRQ_FIFO5_TX		0x04
#define V_IRQ_FIFO5_RX		0x08
#define V_IRQ_FIFO6_TX		0x10
#define V_IRQ_FIFO6_RX		0x20
#define V_IRQ_FIFO7_TX		0x40
#define V_IRQ_FIFO7_RX		0x80
/* R_IRQ_FIFO_BL2 */
#define V_IRQ_FIFO8_TX		0x01
#define V_IRQ_FIFO8_RX		0x02
#define V_IRQ_FIFO9_TX		0x04
#define V_IRQ_FIFO9_RX		0x08
#define V_IRQ_FIFO10_TX		0x10
#define V_IRQ_FIFO10_RX		0x20
#define V_IRQ_FIFO11_TX		0x40
#define V_IRQ_FIFO11_RX		0x80
/* R_IRQ_FIFO_BL3 */
#define V_IRQ_FIFO12_TX		0x01
#define V_IRQ_FIFO12_RX		0x02
#define V_IRQ_FIFO13_TX		0x04
#define V_IRQ_FIFO13_RX		0x08
#define V_IRQ_FIFO14_TX		0x10
#define V_IRQ_FIFO14_RX		0x20
#define V_IRQ_FIFO15_TX		0x40
#define V_IRQ_FIFO15_RX		0x80
/* R_IRQ_FIFO_BL4 */
#define V_IRQ_FIFO16_TX		0x01
#define V_IRQ_FIFO16_RX		0x02
#define V_IRQ_FIFO17_TX		0x04
#define V_IRQ_FIFO17_RX		0x08
#define V_IRQ_FIFO18_TX		0x10
#define V_IRQ_FIFO18_RX		0x20
#define V_IRQ_FIFO19_TX		0x40
#define V_IRQ_FIFO19_RX		0x80
/* R_IRQ_FIFO_BL5 */
#define V_IRQ_FIFO20_TX		0x01
#define V_IRQ_FIFO20_RX		0x02
#define V_IRQ_FIFO21_TX		0x04
#define V_IRQ_FIFO21_RX		0x08
#define V_IRQ_FIFO22_TX		0x10
#define V_IRQ_FIFO22_RX		0x20
#define V_IRQ_FIFO23_TX		0x40
#define V_IRQ_FIFO23_RX		0x80
/* R_IRQ_FIFO_BL6 */
#define V_IRQ_FIFO24_TX		0x01
#define V_IRQ_FIFO24_RX		0x02
#define V_IRQ_FIFO25_TX		0x04
#define V_IRQ_FIFO25_RX		0x08
#define V_IRQ_FIFO26_TX		0x10
#define V_IRQ_FIFO26_RX		0x20
#define V_IRQ_FIFO27_TX		0x40
#define V_IRQ_FIFO27_RX		0x80
/* R_IRQ_FIFO_BL7 */
#define V_IRQ_FIFO28_TX		0x01
#define V_IRQ_FIFO28_RX		0x02
#define V_IRQ_FIFO29_TX		0x04
#define V_IRQ_FIFO29_RX		0x08
#define V_IRQ_FIFO30_TX		0x10
#define V_IRQ_FIFO30_RX		0x20
#define V_IRQ_FIFO31_TX		0x40
#define V_IRQ_FIFO31_RX		0x80

/* chapter 13: general purpose I/O pins (GPIO) and input pins (GPI) */
/* R_GPIO_OUT0 */
#define V_GPIO_OUT0		0x01
#define V_GPIO_OUT1		0x02
#define V_GPIO_OUT2		0x04
#define V_GPIO_OUT3		0x08
#define V_GPIO_OUT4		0x10
#define V_GPIO_OUT5		0x20
#define V_GPIO_OUT6		0x40
#define V_GPIO_OUT7		0x80
/* R_GPIO_OUT1 */
#define V_GPIO_OUT8		0x01
#define V_GPIO_OUT9		0x02
#define V_GPIO_OUT10		0x04
#define V_GPIO_OUT11		0x08
#define V_GPIO_OUT12		0x10
#define V_GPIO_OUT13		0x20
#define V_GPIO_OUT14		0x40
#define V_GPIO_OUT15		0x80
/* R_GPIO_EN0 */
#define V_GPIO_EN0		0x01
#define V_GPIO_EN1		0x02
#define V_GPIO_EN2		0x04
#define V_GPIO_EN3		0x08
#define V_GPIO_EN4		0x10
#define V_GPIO_EN5		0x20
#define V_GPIO_EN6		0x40
#define V_GPIO_EN7		0x80
/* R_GPIO_EN1 */
#define V_GPIO_EN8		0x01
#define V_GPIO_EN9		0x02
#define V_GPIO_EN10		0x04
#define V_GPIO_EN11		0x08
#define V_GPIO_EN12		0x10
#define V_GPIO_EN13		0x20
#define V_GPIO_EN14		0x40
#define V_GPIO_EN15		0x80
/* R_GPIO_SEL */
#define V_GPIO_SEL0		0x01
#define V_GPIO_SEL1		0x02
#define V_GPIO_SEL2		0x04
#define V_GPIO_SEL3		0x08
#define V_GPIO_SEL4		0x10
#define V_GPIO_SEL5		0x20
#define V_GPIO_SEL6		0x40
#define V_GPIO_SEL7		0x80
/* R_GPIO_IN0 */
#define V_GPIO_IN0		0x01
#define V_GPIO_IN1		0x02
#define V_GPIO_IN2		0x04
#define V_GPIO_IN3		0x08
#define V_GPIO_IN4		0x10
#define V_GPIO_IN5		0x20
#define V_GPIO_IN6		0x40
#define V_GPIO_IN7		0x80
/* R_GPIO_IN1 */
#define V_GPIO_IN8		0x01
#define V_GPIO_IN9		0x02
#define V_GPIO_IN10		0x04
#define V_GPIO_IN11		0x08
#define V_GPIO_IN12		0x10
#define V_GPIO_IN13		0x20
#define V_GPIO_IN14		0x40
#define V_GPIO_IN15		0x80
/* R_GPI_IN0 */
#define V_GPI_IN0		0x01
#define V_GPI_IN1		0x02
#define V_GPI_IN2		0x04
#define V_GPI_IN3		0x08
#define V_GPI_IN4		0x10
#define V_GPI_IN5		0x20
#define V_GPI_IN6		0x40
#define V_GPI_IN7		0x80
/* R_GPI_IN1 */
#define V_GPI_IN8		0x01
#define V_GPI_IN9		0x02
#define V_GPI_IN10		0x04
#define V_GPI_IN11		0x08
#define V_GPI_IN12		0x10
#define V_GPI_IN13		0x20
#define V_GPI_IN14		0x40
#define V_GPI_IN15		0x80
/* R_GPI_IN2 */
#define V_GPI_IN16		0x01
#define V_GPI_IN17		0x02
#define V_GPI_IN18		0x04
#define V_GPI_IN19		0x08
#define V_GPI_IN20		0x10
#define V_GPI_IN21		0x20
#define V_GPI_IN22		0x40
#define V_GPI_IN23		0x80
/* R_GPI_IN3 */
#define V_GPI_IN24		0x01
#define V_GPI_IN25		0x02
#define V_GPI_IN26		0x04
#define V_GPI_IN27		0x08
#define V_GPI_IN28		0x10
#define V_GPI_IN29		0x20
#define V_GPI_IN30		0x40
#define V_GPI_IN31		0x80

/* map of all registers, used for debugging */

#ifdef HFC_REGISTER_MAP
struct hfc_register_names {
	char *name;
	u_char reg;
} hfc_register_names[] = {
	/* write registers */
	{"R_CIRM",		0x00},
	{"R_CTRL",		0x01},
	{"R_BRG_PCM_CFG ",	0x02},
	{"R_RAM_ADDR0",		0x08},
	{"R_RAM_ADDR1",		0x09},
	{"R_RAM_ADDR2",		0x0A},
	{"R_FIRST_FIFO",	0x0B},
	{"R_RAM_SZ",		0x0C},
	{"R_FIFO_MD",		0x0D},
	{"R_INC_RES_FIFO",	0x0E},
	{"R_FIFO / R_FSM_IDX",	0x0F},
	{"R_SLOT",		0x10},
	{"R_IRQMSK_MISC",	0x11},
	{"R_SCI_MSK",		0x12},
	{"R_IRQ_CTRL",		0x13},
	{"R_PCM_MD0",		0x14},
	{"R_0x15",		0x15},
	{"R_ST_SEL",		0x16},
	{"R_ST_SYNC",		0x17},
	{"R_CONF_EN",		0x18},
	{"R_TI_WD",		0x1A},
	{"R_BERT_WD_MD",	0x1B},
	{"R_DTMF",		0x1C},
	{"R_DTMF_N",		0x1D},
	{"R_E1_XX_STA",		0x20},
	{"R_LOS0",		0x22},
	{"R_LOS1",		0x23},
	{"R_RX0",		0x24},
	{"R_RX_FR0",		0x25},
	{"R_RX_FR1",		0x26},
	{"R_TX0",		0x28},
	{"R_TX1",		0x29},
	{"R_TX_FR0",		0x2C},
	{"R_TX_FR1",		0x2D},
	{"R_TX_FR2",		0x2E},
	{"R_JATT_ATT",		0x2F},
	{"A_ST_xx_STA/R_RX_OFF",0x30},
	{"A_ST_CTRL0",		0x31},
	{"R_SYNC_OUT",		0x21},
	{"A_ST_CTRL1",		0x32},
	{"A_ST_CTRL2",		0x33},
	{"A_ST_SQ_WR",		0x34},
	{"R_TX_OFF",		0x34},
	{"R_SYNC_CTRL",		0x35},
	{"A_ST_CLK_DLY",	0x37},
	{"R_PWM0",		0x38},
	{"R_PWM1",		0x39},
	{"A_ST_B1_TX",		0x3C},
	{"A_ST_B2_TX",		0x3D},
	{"A_ST_D_TX",		0x3E},
	{"R_GPIO_OUT0",		0x40},
	{"R_GPIO_OUT1",		0x41},
	{"R_GPIO_EN0",		0x42},
	{"R_GPIO_EN1",		0x43},
	{"R_GPIO_SEL",		0x44},
	{"R_BRG_CTRL",		0x45},
	{"R_PWM_MD",		0x46},
	{"R_BRG_MD",		0x47},
	{"R_BRG_TIM0",		0x48},
	{"R_BRG_TIM1",		0x49},
	{"R_BRG_TIM2",		0x4A},
	{"R_BRG_TIM3",		0x4B},
	{"R_BRG_TIM_SEL01",	0x4C},
	{"R_BRG_TIM_SEL23",	0x4D},
	{"R_BRG_TIM_SEL45",	0x4E},
	{"R_BRG_TIM_SEL67",	0x4F},
	{"A_FIFO_DATA0-2",	0x80},
	{"A_FIFO_DATA0-2_NOINC",0x84},
	{"R_RAM_DATA",		0xC0},
	{"A_SL_CFG",		0xD0},
	{"A_CONF",		0xD1},
	{"A_CH_MSK",		0xF4},
	{"A_CON_HDLC",		0xFA},
	{"A_SUBCH_CFG",		0xFB},
	{"A_CHANNEL",		0xFC},
	{"A_FIFO_SEQ",		0xFD},
	{"A_IRQ_MSK",		0xFF},
	{NULL, 0},

	/* read registers */
	{"A_Z1",		0x04},
	{"A_Z1H",		0x05},
	{"A_Z2",		0x06},
	{"A_Z2H",		0x07},
	{"A_F1",		0x0C},
	{"A_F2",		0x0D},
	{"R_IRQ_OVIEW",		0x10},
	{"R_IRQ_MISC",		0x11},
	{"R_IRQ_STATECH",	0x12},
	{"R_CONF_OFLOW",	0x14},
	{"R_RAM_USE",		0x15},
	{"R_CHIP_ID",		0x16},
	{"R_BERT_STA",		0x17},
	{"R_F0_CNTL",		0x18},
	{"R_F0_CNTH",		0x19},
	{"R_BERT_ECL",		0x1A},
	{"R_BERT_ECH",		0x1B},
	{"R_STATUS",		0x1C},
	{"R_CHIP_RV",		0x1F},
	{"R_STATE",		0x20},
	{"R_RX_STA0",		0x24},
	{"R_RX_STA1",		0x25},
	{"R_RX_STA2",		0x26},
	{"R_RX_STA3",		0x27},
	{"R_JATT_DIR",		0x2b},
	{"R_SLIP",		0x2c},
	{"A_ST_RD_STA",		0x30},
	{"R_FAS_ECL",		0x30},
	{"R_FAS_ECH",		0x31},
	{"R_VIO_ECL",		0x32},
	{"R_VIO_ECH",		0x33},
	{"R_CRC_ECL / A_ST_SQ_RD",0x34},
	{"R_CRC_ECH",		0x35},
	{"R_E_ECL",		0x36},
	{"R_E_ECH",		0x37},
	{"R_SA6_SA13_ECL",	0x38},
	{"R_SA6_SA13_ECH",	0x39},
	{"R_SA6_SA23_ECL",	0x3A},
	{"R_SA6_SA23_ECH",	0x3B},
	{"A_ST_B1_RX",		0x3C},
	{"A_ST_B2_RX",		0x3D},
	{"A_ST_D_RX",		0x3E},
	{"A_ST_E_RX",		0x3F},
	{"R_GPIO_IN0",		0x40},
	{"R_GPIO_IN1",		0x41},
	{"R_GPI_IN0",		0x44},
	{"R_GPI_IN1",		0x45},
	{"R_GPI_IN2",		0x46},
	{"R_GPI_IN3",		0x47},
	{"A_FIFO_DATA0-2",	0x80},
	{"A_FIFO_DATA0-2_NOINC",0x84},
	{"R_INT_DATA",		0x88},
	{"R_RAM_DATA",		0xC0},
	{"R_IRQ_FIFO_BL0",	0xC8},
	{"R_IRQ_FIFO_BL1",	0xC9},
	{"R_IRQ_FIFO_BL2",	0xCA},
	{"R_IRQ_FIFO_BL3",	0xCB},
	{"R_IRQ_FIFO_BL4",	0xCC},
	{"R_IRQ_FIFO_BL5",	0xCD},
	{"R_IRQ_FIFO_BL6",	0xCE},
	{"R_IRQ_FIFO_BL7",	0xCF},
};
#endif /* HFC_REGISTER_MAP */

/* ACCESS TO PCI MEMORY MAPPED REGISTERS */

#ifdef CONFIG_HFCMULTI_PCIMEM

#define HFC_outl(a,b,c) (*((volatile u_long *)((a->pci_membase)+b)) = c)
#define HFC_inl(a,b) (*((volatile u_long *)((a->pci_membase)+b)))

/* no debug */
#define HFC_outl_(a,b,c) (*((volatile u_long *)((a->pci_membase)+b)) = c)
#define HFC_inl_(a,b) (*((volatile u_long *)((a->pci_membase)+b)))
#define HFC_inw_(a,b) (*((volatile u_short *)((a->pci_membase)+b)))
#define HFC_outb_(a,b,c) (*((volatile u_char *)((a->pci_membase)+b)) = c)
#define HFC_inb_(a,b) (*((volatile u_char *)((a->pci_membase)+b)))
#define HFC_wait_(a) while((*((volatile u_char *)((a->pci_membase)+R_STATUS))) & V_BUSY)

/* macros */
#ifndef HFC_REGISTER_MAP

/* usage: HFC_outX(card,register,value); */
#define HFC_outb(a,b,c) (*((volatile u_char *)((a->pci_membase)+b)) = c)
/* usage: register=HFC_inX(card,register); */
#define HFC_inb(a,b) (*((volatile u_char *)((a->pci_membase)+b)))
#define HFC_inw(a,b) (*((volatile u_short *)((a->pci_membase)+b)))
/* usage: HFC_wait(card); */
#define HFC_wait(a) while((*((volatile u_char *)((a->pci_membase)+R_STATUS))) & V_BUSY)

#else /* HFC_REGISTER_MAP */

#define HFC_outb(a,b,c) _HFC_outb(a, b, c, __FUNCTION__, __LINE__)
static unsigned char _HFC_outb(hfc_multi_t *a, unsigned char b, unsigned char c, char *function, int line)
{
	char regname[256]="", bits[9]="xxxxxxxx";
	int i;

	i = -1;
	while(hfc_register_names[++i].name) {
		if (hfc_register_names[i].reg == b)
			strcat(regname, hfc_register_names[i].name);
	}
	if (regname[0] == '\0')
		strcpy(regname, "register");

	bits[7] = '0'+(!!(c&1));
	bits[6] = '0'+(!!(c&2));
	bits[5] = '0'+(!!(c&4));
	bits[4] = '0'+(!!(c&8));
	bits[3] = '0'+(!!(c&16));
	bits[2] = '0'+(!!(c&32));
	bits[1] = '0'+(!!(c&64));
	bits[0] = '0'+(!!(c&128));
	printk(KERN_DEBUG "HFC_outb(\"%s\", %02x=%s, 0x%02x=%s); in %s() line %d\n", a->name, b, regname, c, bits, function, line);
	return(*(((volatile u_char *)a->pci_membase)+b) = c);
}
#define HFC_inb(a,b) _HFC_inb(a, b, __FUNCTION__, __LINE__)
static unsigned char _HFC_inb(hfc_multi_t *a, unsigned char b, char *function, int line)
{
	char regname[256]="", bits[9]="xxxxxxxx";
	u_char c = (*(((volatile u_char *)a->pci_membase)+b));
	int i;

	i = 0;
	while(hfc_register_names[i++].name)
		;
	while(hfc_register_names[++i].name) {
		if (hfc_register_names[i].reg == b)
			strcat(regname, hfc_register_names[i].name);
	}
	if (regname[0] == '\0')
		strcpy(regname, "register");

	bits[7] = '0'+(!!(c&1));
	bits[6] = '0'+(!!(c&2));
	bits[5] = '0'+(!!(c&4));
	bits[4] = '0'+(!!(c&8));
	bits[3] = '0'+(!!(c&16));
	bits[2] = '0'+(!!(c&32));
	bits[1] = '0'+(!!(c&64));
	bits[0] = '0'+(!!(c&128));
	printk(KERN_DEBUG "HFC_inb(\"%s\", %02x=%s) = 0x%02x=%s; in %s() line %d\n", a->name, b, regname, c, bits, function, line);
	return(c);
}
#define HFC_inw(a,b) _HFC_inw(a, b, __FUNCTION__, __LINE__)
static unsigned short _HFC_inw(hfc_multi_t *a, unsigned char b, char *function, int line)
{
	char regname[256]="";
	u_short c = (*(((volatile u_short *)a->pci_membase)+b));
	int i;

	i = 0;
	while(hfc_register_names[i++].name)
		;
	while(hfc_register_names[++i].name) {
		if (hfc_register_names[i].reg == b)
			strcat(regname, hfc_register_names[i].name);
	}
	if (regname[0] == '\0')
		strcpy(regname, "register");

	printk(KERN_DEBUG "HFC_inw(\"%s\", %02x=%s) = 0x%04x; in %s() line %d\n", a->name, b, regname, c, function, line);
	return(c);
}
#define HFC_wait(a) _HFC_wait(a, __FUNCTION__, __LINE__)
static void _HFC_wait(hfc_multi_t *a, char *function, int line)
{
	printk(KERN_DEBUG "HFC_wait(\"%s\"); in %s() line %d\n", a->name, function, line);
	while((*(((volatile u_char *)a->pci_membase)+R_STATUS)) & V_BUSY);
}

#endif /* else HFC_REGISTER_MAP */

#else /* CONFIG_HFCMULTI_PCIMEM */

/* ACCESS TO PCI IO REGISTERS */

#ifdef HFC_REGISTER_MAP
#error Please use "HFC_REGISTER_MAP" debugging only in conjuction with PCIMEM access.
#endif

/* usage: HFC_outX(card,register,value); */
static inline void HFC_outb(hfc_multi_t *a, u_char b, u_char c)
{
	outb(b,(a->pci_iobase)+4);
	outb(c,a->pci_iobase);
}
static inline void HFC_outl(hfc_multi_t *a, u_char b, u_long c)
{
	outb(b,(a->pci_iobase)+4);
	outl(c,a->pci_iobase);
}

/* usage: value=HFC_inX(card,register); */
static inline u_char HFC_inb(hfc_multi_t *a, u_char b)
{
	outb(b,(a->pci_iobase)+4);
	return (inb((volatile u_int)a->pci_iobase));
}
static inline u_short HFC_inw(hfc_multi_t *a, u_char b)
{
	outb(b,(a->pci_iobase)+4);
	return (inw((volatile u_int)a->pci_iobase));
}
static inline u_long HFC_inl(hfc_multi_t *a, u_char b)
{
	outb(b,(a->pci_iobase)+4);
	return (inl((volatile u_int)a->pci_iobase));
}

/* usage: HFC_wait(card); */
static inline void HFC_wait(hfc_multi_t *a)
{
	outb(R_STATUS,(a->pci_iobase)+4);
	while(inb((volatile u_int)a->pci_iobase) & V_BUSY);
}

/* usage: HFC_set(card,register); */
#define HFC_set(a, b) outb(b,(a->pci_iobase)+4)

/* usage: HFC_putX(card,value); */
#define HFC_putb(a,b) outb(b,a->pci_iobase)
#define HFC_putl(a,b) outl(b,a->pci_iobase)

/* usage: value=HFC_getX(card); */
#define HFC_getb(a) inb((volatile u_int)a->pci_iobase)
#define HFC_getl(a) inl((volatile u_int)a->pci_iobase)

/* no debug */
#define HFC_outl_(a,b,c) HFC_outl(a,b,c)
#define HFC_inl_(a,b) HFC_inl(a,b)
#define HFC_inw_(a,b) HFC_inw(a,b)
#define HFC_outb_(a,b,c) HFC_outb(a,b,c)
#define HFC_inb_(a,b) HFC_inb(a,b)
#define HFC_wait_(a) HFC_wait(a)

#endif /* else CONFIG_HFCMULTI_PCIMEM */

