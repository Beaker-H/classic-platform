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

//lint -emacro(904,VALIDATE,VALIDATE_RV,VALIDATE_NO_RV) //904 PC-Lint exception to MISRA 14.7 (validate macros).

#include "Std_Types.h"
#include "EcuM.h"
#include "EcuM_Generated_Types.h"
#include "EcuM_Internals.h"

#if defined(USE_DEM)
#include "Dem.h"
#endif
#if defined(USE_NVM)
#include "NvM.h"
#endif


static uint32 internal_data_run_state_timeout = 0;
#if defined(USE_NVM)
static uint32 internal_data_go_off_one_state_timeout = 0;
static NvM_RequestResultType writeAllResult;
#endif

static uint32 internal_data_go_sleep_state_timeout = 0;

#ifdef CFG_ECUM_USE_SERVICE_COMPONENT
/** @req EcuM2749 */
static Rte_ModeType_EcuM_Mode currentMode;

void set_current_state(EcuM_StateType state) {
	/* Update the state */
	internal_data.current_state = state;

	Rte_ModeType_EcuM_Mode newMode = currentMode;
	switch( state ) {
	case ECUM_STATE_WAKEUP:
	case ECUM_STATE_WAKEUP_ONE:
	case ECUM_STATE_WAKEUP_VALIDATION:
	case ECUM_STATE_WAKEUP_REACTION:
	case ECUM_STATE_WAKEUP_TWO:
	case ECUM_STATE_SLEEP:
	case ECUM_STATE_SHUTDOWN:
		newMode = RTE_MODE_EcuM_Mode_SLEEP;
		break;
	case ECUM_STATE_GO_SLEEP:
		if( internal_data.shutdown_target == ECUM_STATE_SLEEP ) {
			newMode = RTE_MODE_EcuM_Mode_SLEEP; /** @req EcuM2752 */
		}
		break;
	case ECUM_STATE_GO_OFF_ONE:
	case ECUM_STATE_GO_OFF_TWO:
		newMode = RTE_MODE_EcuM_Mode_SHUTDOWN;
		break;
	case ECUM_STATE_WAKEUP_TTII:
		if( internal_data.shutdown_target == ECUM_STATE_SLEEP ) {
			newMode = RTE_MODE_EcuM_Mode_WAKE_SLEEP; /** @req EcuM2752 */
		}
		break;
	case ECUM_STATE_PREP_SHUTDOWN:
	case ECUM_STATE_APP_POST_RUN: /* Assuming this is same as RUN_III */
		newMode = RTE_MODE_EcuM_Mode_POST_RUN;
		break;
	case ECUM_STATE_APP_RUN: /* Assuming this is same as RUN_II */
		newMode = RTE_MODE_EcuM_Mode_RUN;
		break;
	case ECUM_STATE_STARTUP_TWO:
		newMode = RTE_MODE_EcuM_Mode_STARTUP;
		break;
	default:
		/* Do nothing */
		break;
	}

	if( newMode != currentMode ) {
		currentMode = newMode;
		Rte_Switch_EcuM_CurrentMode_currentMode(currentMode); /** @req EcuM2750 */
	}
}
#endif


/**
 * RUN II entry
 * - Called from EcuM_StartupTwo()
 * - Called from
 *
 *
 */
void EcuM_enter_run_mode(void){
	set_current_state(ECUM_STATE_APP_RUN);
	EcuM_OnEnterRun(); /** @req EcuM2308 */

#if defined(USE_WDGM)
	/* This seems strange, should be in FW instead */
	WdgM_SetMode(internal_data.config->EcuMWdgMConfig->EcuMWdgMRunMode);
#endif

#if defined(USE_COMM)
	/*
	 * Loop over all channels that have requested run,
	 * ie EcuM_ComM_RequestRUN()
	 */
	{
		uint32 cMask = internal_data.run_comm_requests;
		uint8  channel;

		for (; cMask; cMask &= ~(1ul << channel)) {
			channel = ilog2(cMask);
			ComM_EcuM_RunModeIndication(channel);
		}
	}
#endif

	/* We have a configurable minimum time (EcuMRunMinimumDuration)
	 * we have to stay in RUN state  */
	internal_data_run_state_timeout = internal_data.config->EcuMRunMinimumDuration / ECUM_MAIN_FUNCTION_PERIOD; /** @req EcuM2310 */
}


//--------- Local functions ------------------------------------------------------------------------------------------------



/**
 * Enter GO SLEEP state ( soon in state ECUM_STATE_GO_SLEEP)
 */
static inline void enter_go_sleep_mode(void){
	EcuM_WakeupSourceType wakeupSource;
	set_current_state(ECUM_STATE_GO_SLEEP);

	EcuM_OnGoSleep();

#if defined(USE_NVM)
	NvM_WriteAll();

	/* Start timer */
	internal_data_go_sleep_state_timeout = internal_data.config->EcuMNvramWriteAllTimeout / ECUM_MAIN_FUNCTION_PERIOD;

	wakeupSource = EcuM_GetPendingWakeupEvents();
#else
	wakeupSource = EcuM_GetPendingWakeupEvents();
#endif
}

/**
  In GO SLEEP state (in state ECUM_STATE_GO_SLEEP)
 */
static void in_state_goSleep( void ) {

	/* We only wait for NvM_WriteAll() for so long */
	if (internal_data_go_sleep_state_timeout){
		internal_data_go_sleep_state_timeout--;
	}

	if( (internal_data_go_sleep_state_timeout == 0) ) {
		/*
		 * We should go to sleep , enable source that should wake us
		 * */
		uint32 cMask;
		uint8  source;
		const EcuM_SleepModeType *sleepModePtr;

		/* Get the current sleep mode */

		sleepModePtr = &internal_data.config->EcuMSleepModeConfig[internal_data.sleep_mode];

		cMask = sleepModePtr->EcuMWakeupSourceMask;

		/* Loop over the WKSOURCE for this sleep mode */
		for (; cMask; cMask &= ~(1ul << source)) {
			source = ilog2(cMask);
			/* @req 3.1.5/ECUM2389 */
			EcuM_EnableWakeupSources( 1<< source );

#if defined(WDGM)
			WdgM_SetMode(sleepModePtr->EcuMSleepModeWdgMMode);
#endif

			/* Let no one else run */
			GetResource(RES_SCHEDULER);
		}

	} else if( EcuM_GetPendingWakeupEvents() != 0 ) {
		/* We have pending wakeup events, need to startup again */
#if defined(USE_NVM)
		NvM_CancelWriteAll();
#endif
	}
}


/**
 * In "Sleep Sequence I"  (in state ECUM_STATE_SLEEP)
 */
static void in_state_sleep ( void ) {
	const EcuM_SleepModeType *sleepModePtr;
	sleepModePtr = &internal_data.config->EcuMSleepModeConfig[internal_data.sleep_mode];

	EcuM_GenerateRamHash();

	Mcu_SetMode(sleepModePtr->EcuMSleepModeMcuMode);

	/* @req 3.1.5/ECUM2863 */
	if( EcuM_CheckRamHash() == 0) {
#if defined(USE_DEM)
		//
		EcuM_ErrorHook(ECUM_E_RAM_CHECK_FAILED);
#endif
	}

	set_current_state(ECUM_STATE_WAKEUP_ONE);
}

static inline void enter_go_off_one_mode(void){
	set_current_state(ECUM_STATE_GO_OFF_ONE);
	EcuM_OnGoOffOne();

#if defined(USE_COMM)
	ComM_DeInit();
#endif

#if defined(USE_NVM)

	// Start NvM_WriteAll and timeout timer
	NvM_WriteAll();

	internal_data_go_off_one_state_timeout = internal_data.config->EcuMNvramWriteAllTimeout / ECUM_MAIN_FUNCTION_PERIOD;
#endif
}


static inline boolean hasRunRequests(void){
	uint32 result = internal_data.run_requests;

#if defined(USE_COMM)
	result |= internal_data.run_comm_requests;
#endif

	return (result != 0);
}

static inline boolean hasPostRunRequests(void){
	return (internal_data.postrun_requests != 0);
}


/**
 * RUN II Loop (in state ECUM_STATE_APP_RUN)
 * - The entry to RUN II is done in
 */
static inline void in_state_appRun(void){
	if (internal_data_run_state_timeout){
		internal_data_run_state_timeout--;
	}

	if ((!hasRunRequests()) && (internal_data_run_state_timeout == 0)){
		EcuM_OnExitRun();	/** @req EcuM2865 */

#if defined(USE_WDGM)
		WdgM_SetMode(internal_data.config->EcuMWdgMConfig->EcuMWdgMPostRunMode);
#endif

#if defined(USE_RTE) && defined(CFG_ECUM_USE_SERVICE_COMPONENT)
		Rte_Switch_currentMode_currentMode(RTE_MODE_EcuM_Mode_POSTRUN);
#endif

		set_current_state(ECUM_STATE_APP_POST_RUN);/** @req EcuM2865 */
	}
}


/**
 * RUN III states (in state ECUM_STATE_APP_POST_RUN)
 */
static inline void in_state_appPostRun(void){

	/* @req 3.1.5/ECUM2866 */
	if (hasRunRequests()){
		/* We have run requests, return to RUN II */
		EcuM_enter_run_mode();

	} else if (!hasPostRunRequests()){
		EcuM_OnExitPostRun(); /** @req EcuM2761 */
		set_current_state(ECUM_STATE_PREP_SHUTDOWN);/** @req EcuM2761 */
	} else {
		/* TODO: We have postrun requests */
	}
}


/**
 * PREP SHUTDOWN state (in state ECUM_STATE_PREP_SHUTDOWN)
 */
static inline void in_state_prepShutdown(void){

	// TODO: The specification does not state what events to clear
	EcuM_ClearWakeupEvent(ECUM_WKSTATUS_NONE);

	EcuM_OnPrepShutdown();

#if defined(USE_DEM)
	// DEM shutdown
	Dem_Shutdown();
#endif

	// Switch shutdown mode
	switch(internal_data.shutdown_target){
		//If in state Off or Reset go into Go_Off_One:
		case ECUM_STATE_OFF:
		case ECUM_STATE_RESET:
			enter_go_off_one_mode();
			break;
		case ECUM_STATE_SLEEP:
			enter_go_sleep_mode();
			break;
		default:
			//TODO: Report error.
			break;
	}
}

static inline void in_state_goOffOne(void){
#if defined(USE_NVM)
		if (internal_data_go_off_one_state_timeout){
			internal_data_go_off_one_state_timeout--;
		}
		// Wait for the NVM job (NvmWriteAll) to terminate
		NvM_GetErrorStatus(0, &writeAllResult);
		if ((writeAllResult != NVM_REQ_PENDING) || (internal_data_go_off_one_state_timeout == 0)){

#if defined(USE_WDGM)
			WdgM_SetMode(internal_data.config->EcuMWdgMConfig->EcuMWdgMShutdownMode);
#endif
			ShutdownOS(E_OK);
		}
#else

#if defined(USE_WDGM)
		WdgM_SetMode(internal_data.config->EcuMWdgMConfig->EcuMWdgMShutdownMode);
#endif
		ShutdownOS(E_OK);
#endif
}


//----- MAIN -----------------------------------------------------------------------------------------------------------------
void EcuM_MainFunction(void){
	EcuM_WakeupSourceType wMask;

	VALIDATE_NO_RV(internal_data.initiated, ECUM_MAINFUNCTION_ID, ECUM_E_NOT_INITIATED);

	switch(internal_data.current_state){

		case ECUM_STATE_APP_RUN:
			/* RUN II state */
			in_state_appRun();
			break;
		case ECUM_STATE_APP_POST_RUN:
			/* RUN III state */
			in_state_appPostRun();
			break;
		case ECUM_STATE_PREP_SHUTDOWN:
			in_state_prepShutdown();
			break;
		case ECUM_STATE_GO_OFF_ONE:
			in_state_goOffOne();
			break;
		case ECUM_STATE_GO_SLEEP:
			in_state_goSleep();
			break;
		case ECUM_STATE_SLEEP:
			in_state_sleep();
			break;
		case ECUM_STATE_WAKEUP_ONE:
		{
			/* TODO: we must have a normal RUN mode.. can't find any
			 * in the A3.1.5 spec. */
			Mcu_SetMode(MCU_MODE_NORMAL);

#if defined(USE_WDGM)
			WdgM_SetMode(internal_data.config->EcuMWdgMConfig->EcuMWdgMWakeupMode);
#endif

			wMask = EcuM_GetPendingWakeupEvents();

			EcuM_DisableWakeupSources(wMask);

			EcuM_AL_DriverRestart();

			ReleaseResource(RES_SCHEDULER);

			set_current_state(ECUM_STATE_WAKEUP_VALIDATION);

			break;
		}

		case ECUM_STATE_WAKEUP_VALIDATION:
		{
			wMask = EcuM_GetPendingWakeupEvents();

			EcuM_StartWakeupSources(wMask);

			EcuM_CheckValidation( wMask );

			// TODO:
			// ComM_EcuM_WakeupIndication( network handle )

			set_current_state(ECUM_STATE_WAKEUP_REACTION);
			break;
		}

		case ECUM_STATE_WAKEUP_REACTION:
		{
			/*
			 * At this stage we want to know how to react to the wakeup, e.g. go
			 * back to RUN or SHUTDOWN, etc.
			 */
			EcuM_WakeupReactionType wReaction;

			wMask = EcuM_GetValidatedWakeupEvents();

			/* TODO: We have skipped the TTII timer here */

			/* If the wakeup mask here is != 0 we have a validated wakeup event ->
			 * go back to RUN */
			wReaction = ( 0 == wMask ) ? ECUM_WKACT_SHUTDOWN : ECUM_WKACT_RUN;
			wReaction = EcuM_OnWakeupReaction(wReaction);

			if( wReaction == ECUM_WKACT_RUN) {
				set_current_state(ECUM_STATE_WAKEUP_TWO);
			} else {
				/* From figure 28 it seems that we should go to SHUTDOWN/GO SLEEP) again from wakeup
				 * not going up to RUN/RUN II state again. */
				set_current_state(ECUM_STATE_GO_SLEEP);
			}
			break;
		}

		case ECUM_STATE_WAKEUP_TWO:
#if defined(USE_DEM)
			Dem_Init();
#endif
			set_current_state(ECUM_STATE_RUN);
			break;

		default:
			//TODO: Report error.
			break;
	}
}
