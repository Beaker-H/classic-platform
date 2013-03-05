/* -------------------------------- Arctic Core ------------------------------
 * Arctic Core - the open source AUTOSAR platform http://arccore.com
 *
 * Copyright (C) 2009  ArcCore AB <contact@arccore.com>
 *
 * This source code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; See <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * -------------------------------- Arctic Core ------------------------------*/

/* ----------------------------[includes]------------------------------------*/

#include "Std_Types.h"
#include "Mcu.h"
#include "io.h"
#include "mpc55xx.h"
#include "Mcu_Arc.h"
#if defined(USE_FEE)
#include "Fee_Memory_Cfg.h"
#endif
#if defined(USE_DMA)
#include "Dma.h"
#endif
#include "asm_ppc.h"
#include "Os.h"

/* ----------------------------[private define]------------------------------*/
/* ----------------------------[private macro]-------------------------------*/
/* ----------------------------[private typedef]-----------------------------*/
/* ----------------------------[private function prototypes]-----------------*/
/* ----------------------------[private variables]---------------------------*/

#if defined(CFG_MPC5XXX_TEST)
uint32_t Mpc5xxx_vectorMask;
uint8_t Mpc5xxx_Esr;
uint8_t Mpc5xxx_Intc_Esr;
#endif

#if defined(USE_FLS)
extern uint32 EccErrReg;
#endif


/* ----------------------------[private functions]---------------------------*/
/* ----------------------------[public functions]----------------------------*/



void Mcu_Arc_InitPre( void ) {

}

void Mcu_Arc_InitPost( void ) {

}


/**
 *
 * @param error
 * @param pData
 */
void Os_Panic( uint32_t error, void *pData ) {

	(void)error;
	(void)pData;

	ShutdownOS(E_OS_PANIC);

}


static uint32_t checkEcc(void) {
	uint32_t rv = EXC_NOT_HANDLED;

#if defined(USE_FEE) || defined(CFG_MPC5XXX_TEST)

	uint8 esr;
	do {
		esr = READ8( ECSM_BASE + ECSM_ESR );
	} while( esr != READ8( ECSM_BASE + ECSM_ESR ) );

#endif

#if defined(USE_FLS)
#if defined(USE_FEE)
	uint32_t excAddr = READ32( ECSM_BASE + ECSM_FEAR );
#endif

	/* Find FLS errors */

	if (esr & ESR_FNCE) {

		/* Record that something bad has happened */
		EccErrReg = READ8( ECSM_BASE + ECSM_ESR );
		/* Clear the exception */
		WRITE8(ECSM_BASE+ECSM_ESR,ESR_F1BC+ESR_FNCE);
#if defined(USE_FEE)
		/* Check if we are in FEE range */
		if ( ((FEE_BANK1_OFFSET >= excAddr) &&
						(FEE_BANK1_OFFSET + FEE_BANK1_LENGTH < excAddr)) ||
				((FEE_BANK2_OFFSET >= excAddr) &&
						(FEE_BANK2_OFFSET + FEE_BANK2_LENGTH < excAddr)) )
		{
			rv = EXC_HANDLED | EXC_ADJUST_ADDR;
		}
#endif
	}
#endif	 /* USE_FLS */

#if defined(CFG_MPC5XXX_TEST)
	if( esr & (ESR_R1BC+ESR_RNCE) ) {
		/* ECC RAM problems */
		Mpc5xxx_Esr = esr;
		WRITE8(ECSM_BASE+ECSM_ESR,ESR_R1BC+ESR_RNCE);
		rv = (EXC_HANDLED | EXC_ADJUST_ADDR);
	} else if (esr & ESR_FNCE) {
		Mpc5xxx_Esr = esr;
		WRITE8(ECSM_BASE+ECSM_ESR,ESR_F1BC+ESR_FNCE);
		rv = (EXC_HANDLED | EXC_ADJUST_ADDR);
	}
#endif
	return rv;
}


uint32_t Mcu_Arc_ExceptionHook(uint32_t exceptionVector) {
	uint32_t rv = EXC_NOT_HANDLED;

#if defined(CFG_MPC5XXX_TEST)
	Mpc5xxx_vectorMask |= (1<<exceptionVector);
#endif

	switch (exceptionVector) {
	case 1:
		/* CSRR0, CSRR1, MCSR */
		/* ECC: MSR[EE] = 0 */
#if defined(CFG_MPC5XXX_TEST)
		if( get_spr(SPR_MCSR) & ( MCSR_BUS_DRERR | MCSR_BUS_WRERR )) {
			/* We have a bus error */
			rv = EXC_HANDLED | EXC_ADJUST_ADDR;
			break;
		}
#endif
		rv = checkEcc();
		break;
	case 2:
		/* SRR0, SRR1, ESR, DEAR */
		/* ECC: MSR[EE] = 1 */
#if defined(CFG_MPC5XXX_TEST)
		if( get_spr(SPR_ESR) &  ESR_XTE) {
			/* We have a external termination bus error */
			rv = EXC_HANDLED | EXC_ADJUST_ADDR;
			break;
		}
#endif
		rv = checkEcc();
		break;
	case 3:
	{
		/* SRR0, SRR1, ESR */
		rv = checkEcc();
		break;
	}

	default:
		break;
	}

	return rv;
}


void Mcu_Arc_InitClockPre( const Mcu_ClockSettingConfigType *clockSettingsPtr )
{
#if defined(CFG_MPC5604B) || defined(CFG_MPC5606B)
    // Write pll parameters.
    CGM.FMPLL_CR.B.IDF = clockSettingsPtr->Pll1;
    CGM.FMPLL_CR.B.NDIV = clockSettingsPtr->Pll2;
    CGM.FMPLL_CR.B.ODF = clockSettingsPtr->Pll3;

    /* RUN0 cfg: 16MHzIRCON,OSC0ON,PLL0ON,syclk=PLL0 */
    ME.RUN[0].R = 0x001F0074;
    /* Peri. Cfg. 1 settings: only run in RUN0 mode */
    ME.RUNPC[1].R = 0x00000010;
    /* MPC56xxB/S: select ME.RUNPC[1] */
    ME.PCTL[68].R = 0x01; //SIUL control
    ME.PCTL[91].R = 0x01; //RTC/API control
    ME.PCTL[92].R = 0x01; //PIT_RTI control
    ME.PCTL[72].R = 0x01; //eMIOS0 control
    ME.PCTL[73].R = 0x01; //eMIOS1 control
    ME.PCTL[16].R = 0x01; //FlexCAN0 control
    ME.PCTL[17].R = 0x01; //FlexCAN1 control
    ME.PCTL[4].R = 0x01;  /* MPC56xxB/P/S DSPI0  */
    ME.PCTL[5].R = 0x01;  /* MPC56xxB/P/S DSPI1:  */
    ME.PCTL[32].R = 0x01; //ADC0 control
#if defined(CFG_MPC5606B)
    ME.PCTL[33].R = 0x01; //ADC1 control
#endif
    ME.PCTL[23].R = 0x01; //DMAMUX control
    ME.PCTL[48].R = 0x01; /* MPC56xxB/P/S LINFlex  */
    ME.PCTL[49].R = 0x01; /* MPC56xxB/P/S LINFlex  */
    /* Mode Transition to enter RUN0 mode: */
    /* Enter RUN0 Mode & Key */
    ME.MCTL.R = 0x40005AF0;
    /* Enter RUN0 Mode & Inverted Key */
    ME.MCTL.R = 0x4000A50F;

    /* Wait for mode transition to complete */
    while (ME.GS.B.S_MTRANS) {}
    /* Verify RUN0 is the current mode */
    while(ME.GS.B.S_CURRENTMODE != 4) {}

    CGM.SC_DC[0].R = 0x80; /* MPC56xxB/S: Enable peri set 1 sysclk divided by 1 */
    CGM.SC_DC[1].R = 0x80; /* MPC56xxB/S: Enable peri set 2 sysclk divided by 1 */
    CGM.SC_DC[2].R = 0x80; /* MPC56xxB/S: Enable peri set 3 sysclk divided by 1 */

    SIU.PSMI[0].R = 0x01; /* CAN1RX on PCR43 */
    SIU.PSMI[6].R = 0x01; /* CS0/DSPI_0 on PCR15 */

#elif defined(CFG_MPC5606S)
    // Write pll parameters.
    CGM.FMPLL[0].CR.B.IDF = clockSettingsPtr->Pll1;
    CGM.FMPLL[0].CR.B.NDIV = clockSettingsPtr->Pll2;
    CGM.FMPLL[0].CR.B.ODF = clockSettingsPtr->Pll3;

    /* RUN0 cfg: 16MHzIRCON,OSC0ON,PLL0ON,syclk=PLL0 */
    ME.RUN[0].R = 0x001F0074;
    /* Peri. Cfg. 1 settings: only run in RUN0 mode */
    ME.RUNPC[1].R = 0x00000010;
    /* MPC56xxB/S: select ME.RUNPC[1] */
    ME.PCTL[68].R = 0x01; //SIUL control
    ME.PCTL[91].R = 0x01; //RTC/API control
    ME.PCTL[92].R = 0x01; //PIT_RTI control
    ME.PCTL[72].R = 0x01; //eMIOS0 control
    ME.PCTL[73].R = 0x01; //eMIOS1 control
    ME.PCTL[16].R = 0x01; //FlexCAN0 control
    ME.PCTL[17].R = 0x01; //FlexCAN1 control
    ME.PCTL[4].R = 0x01;  /* MPC56xxB/P/S DSPI0  */
    ME.PCTL[5].R = 0x01;  /* MPC56xxB/P/S DSPI1:  */
    ME.PCTL[32].R = 0x01; //ADC0 control
    ME.PCTL[23].R = 0x01; //DMAMUX control
    ME.PCTL[48].R = 0x01; /* MPC56xxB/P/S LINFlex  */
    ME.PCTL[49].R = 0x01; /* MPC56xxB/P/S LINFlex  */
    /* Mode Transition to enter RUN0 mode: */
    /* Enter RUN0 Mode & Key */
    ME.MCTL.R = 0x40005AF0;
    /* Enter RUN0 Mode & Inverted Key */
    ME.MCTL.R = 0x4000A50F;

    /* Wait for mode transition to complete */
    while (ME.GS.B.S_MTRANS) {}
    /* Verify RUN0 is the current mode */
    while(ME.GS.B.S_CURRENTMODE != 4) {}

    CGM.SC_DC[0].R = 0x80; /* MPC56xxB/S: Enable peri set 1 sysclk divided by 1 */
    CGM.SC_DC[1].R = 0x80; /* MPC56xxB/S: Enable peri set 2 sysclk divided by 1 */
    CGM.SC_DC[2].R = 0x80; /* MPC56xxB/S: Enable peri set 3 sysclk divided by 1 */

#elif defined(CFG_MPC5604P)
    // Write pll parameters.
    CGM.FMPLL[0].CR.B.IDF = clockSettingsPtr->Pll1;
    CGM.FMPLL[0].CR.B.NDIV = clockSettingsPtr->Pll2;
    CGM.FMPLL[0].CR.B.ODF = clockSettingsPtr->Pll3;
    // PLL1 must be higher than 120MHz for PWM to work */
    CGM.FMPLL[1].CR.B.IDF = clockSettingsPtr->Pll1_1;
    CGM.FMPLL[1].CR.B.NDIV = clockSettingsPtr->Pll2_1;
    CGM.FMPLL[1].CR.B.ODF = clockSettingsPtr->Pll3_1;

    /* RUN0 cfg: 16MHzIRCON,OSC0ON,PLL0ON, PLL1ON,syclk=PLL0 */
  	ME.RUN[0].R = 0x001F00F4;
    /* Peri. Cfg. 1 settings: only run in RUN0 mode */
    ME.RUNPC[1].R = 0x00000010;

    /* MPC56xxB/S: select ME.RUNPC[1] */
    ME.PCTL[68].R = 0x01; //SIUL control
    ME.PCTL[92].R = 0x01; //PIT_RTI control
    ME.PCTL[41].R = 0x01; //flexpwm0 control
    ME.PCTL[16].R = 0x01; //FlexCAN0 control
    ME.PCTL[26].R = 0x01; //FlexCAN1(SafetyPort) control
    ME.PCTL[4].R = 0x01;  /* MPC56xxB/P/S DSPI0  */
    ME.PCTL[5].R = 0x01;  /* MPC56xxB/P/S DSPI1:  */
    ME.PCTL[6].R = 0x01;  /* MPC56xxB/P/S DSPI2  */
    ME.PCTL[7].R = 0x01;  /* MPC56xxB/P/S DSPI3:  */
    ME.PCTL[32].R = 0x01; //ADC0 control
    ME.PCTL[33].R = 0x01; //ADC1 control
    ME.PCTL[48].R = 0x01; /* MPC56xxB/P/S LINFlex  */
    ME.PCTL[49].R = 0x01; /* MPC56xxB/P/S LINFlex  */
    /* Mode Transition to enter RUN0 mode: */
    /* Enter RUN0 Mode & Key */
    ME.MCTL.R = 0x40005AF0;
    /* Enter RUN0 Mode & Inverted Key */
    ME.MCTL.R = 0x4000A50F;

    /* Wait for mode transition to complete */
    while (ME.GS.B.S_MTRANS) {}
    /* Verify RUN0 is the current mode */
    while(ME.GS.B.S_CURRENTMODE != 4) {}

    /* Pwm, adc, etimer clock */
    CGM.AC0SC.R = 0x05000000;  /* MPC56xxP: Select FMPLL1 for aux clk 0  */
    CGM.AC0DC.R = 0x80000000;  /* MPC56xxP: Enable aux clk 0 div by 1 */

    /* Safety port clock */
    CGM.AC2SC.R = 0x04000000;  /* MPC56xxP: Select FMPLL0 for aux clk 2  */
    CGM.AC2DC.R = 0x80000000;  /* MPC56xxP: Enable aux clk 2 div by 1 */
#endif

}

void Mcu_Arc_InitClockPost( const Mcu_ClockSettingConfigType *clockSettingsPtr )
{

}


void Mcu_Arc_SetModePre( Mcu_ModeType mcuMode)
{
}

void Mcu_Arc_SetModePost( Mcu_ModeType mcuMode)
{

}



