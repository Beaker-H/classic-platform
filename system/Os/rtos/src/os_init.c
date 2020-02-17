/*-------------------------------- Arctic Core ------------------------------
 * Copyright (C) 2013, ArcCore AB, Sweden, www.arccore.com.
 * Contact: <contact@arccore.com>
 * 
 * You may ONLY use this file:
 * 1)if you have a valid commercial ArcCore license and then in accordance with  
 * the terms contained in the written license agreement between you and ArcCore, 
 * or alternatively
 * 2)if you follow the terms found in GNU General Public License version 2 as 
 * published by the Free Software Foundation and appearing in the file 
 * LICENSE.GPL included in the packaging of this file or here 
 * <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>
 *-------------------------------- Arctic Core -----------------------------*/

/** @reqSettings DEFAULT_SPECIFICATION_REVISION=4.3.0 */

/* Generic Requirements */

/** @req SWS_Os_00328
 * If OsStatus is STANDARD and OsScalabilityClass is SC3 or SC4 the consistency check shall issue an error.
 * Note: This requirement is implemented in Os.chk (as requirement tagging in .chk file is not parsed, tagging is done here) */


/* ----------------------------[includes]------------------------------------*/

#include "os_i.h"
/* ----------------------------[private define]------------------------------*/
/* ----------------------------[private macro]-------------------------------*/

/* Count to avoid irq enable before os_start*/
#define INITIAL_DISABLE_COUNT 100

/* ----------------------------[private typedef]-----------------------------*/
/* ----------------------------[private function prototypes]-----------------*/
/* ----------------------------[private variables]---------------------------*/
Os_SysType Os_Sys[OS_NUM_CORES];

Os_IntCounterType Os_IntDisableAllCnt;
Os_IntCounterType Os_IntSuspendAllCnt;
Os_IntCounterType Os_IntSuspendOsCnt;

OsErrorType os_error;

#if (OS_NUM_CORES > 1)
volatile boolean beforeStartHooks[OS_NUM_CORES] = {FALSE};
volatile boolean afterStartHooks[OS_NUM_CORES] = {FALSE};
volatile boolean afterIsrSetup[OS_NUM_CORES] = {FALSE};
#endif

/* ----------------------------[extern]--------------------------------------*/
extern struct OsHooks os_conf_global_hooks;
extern void EcuM_Init(void);
#if !defined(CFG_OS_SYSTICK2)
extern uint32 Mcu_Arc_GetSystemClock( void );
#endif
/* ----------------------------[private functions]---------------------------*/
/**
 * Copy rom pcb data(r_pcb) to ram data
 *
 * @param 	pcb		ram data
 * @param 	r_pcb	rom data
 */
static void copyPcbParts( OsTaskVarType *pcb, const OsTaskConstType *r_pcb ) {
    pcb->activePriority = r_pcb->prio;
    pcb->stack= r_pcb->stack;
    pcb->constPtr = r_pcb;
}
#if (OS_NUM_CORES > 1)
/**
 * This will not work in a cached system.
 */
static void syncCores(volatile boolean syncedCores[]) {
    CoreIDType coreId = GetCoreID();
    ASSERT(coreId < OS_NUM_CORES && coreId >= 0);
    boolean syncOk = false;
    ASSERT(syncedCores[coreId] == false);
    syncedCores[coreId] = true;
    while (!syncOk) {
        syncOk = true;
        for (int i = 0; i < OS_NUM_CORES; i++) {
            if (syncedCores[i] == false) {
                syncOk = false;
            }
        }
    }
}
#endif
/* ----------------------------[public functions]----------------------------*/

/**
 * Initialization of kernel structures and start of the first
 * task.
 */

void InitOS( void ) {
    uint32 i;
    OsTaskVarType *tmpPcbPtr;
    OsIsrStackType intStack;

    DEBUG(DEBUG_LOW,"os_init");

    /* Clear sys */
    memset(OS_SYS_PTR,0,sizeof(Os_SysType));

    OS_SYS_PTR->status.init_os_called = TRUE;
    OS_SYS_PTR->resScheduler = &resScheduler;

    // Initialize suspend and disable count with higher value to avoid irq enable before os_start
    Os_IntSuspendAllCnt = INITIAL_DISABLE_COUNT;
    Os_IntDisableAllCnt = INITIAL_DISABLE_COUNT;

    Os_ArchInit();

    /* Get the numbers defined in the editor */
    OS_SYS_PTR->isrCnt = OS_ISR_CNT;

    // Assign pcb list and init ready queue
    OS_SYS_PTR->pcb_list = Os_TaskVarList;
    /*lint -e{9036} MISRA:EXTERNAL_FILE::[MISRA 2012 Rule 14.4, required] */
    TAILQ_INIT(& (OS_SYS_PTR->ready_head));
#if defined(CFG_KERNEL_EXTRA)
    TAILQ_INIT(& OS_SYS_PTR->timerHead);
#endif

    // Calc interrupt stack
    Os_IsrGetStackInfo(&intStack);
    // Calculate interrupt stack, ARCH_BACKCHAIN_SIZE is common for all arch(16byes)
    /*lint -e{923, 9033} MISRA:FALSE_POSITIVE:Pointer conversion and arithmetic operation for Stack calculation:
     *[MISRA 2012 Rule 11.1, required], [MISRA 2012 Rule 11.4, advisory], [MISRA 2012 Rule 11.6, required] */
    OS_SYS_PTR->intStack = (void *)((size_t)intStack.top + (size_t)intStack.size - ARCH_BACKCHAIN_SIZE);

    // Init counter.. with alarms and schedule tables
    // Master core will initialize for all cores
    /*lint -e774 MISRA:FALSE_POSITIVE:Multicore check:[MISRA 2012 Rule 14.3, required] */
    if (OS_CORE_IS_ID_MASTER(GetCoreID())) {
#if OS_COUNTER_CNT!=0
        Os_CounterInit();
#endif
#if OS_SCHTBL_CNT!=0
        Os_SchTblInit();
#endif
#if (OS_NUM_CORES > 1)
        IocInit();
#endif
    }

    // Put all tasks in the pcb list
    // Put the one that belong in the ready queue there
    for( i=0; i < OS_TASK_CNT; i++) {
        tmpPcbPtr = Os_TaskGet((TaskType)i);
#if (OS_NUM_CORES > 1)
        if (Os_OnRunningCore(OBJECT_TASK,i)) {
            TAILQ_INIT(&tmpPcbPtr->spinlockHead);
#endif
            copyPcbParts(tmpPcbPtr,&Os_TaskConstList[i]);
            Os_TaskContextInit(tmpPcbPtr);
            /*lint -e{9036} MISRA:EXTERNAL_FILE::[MISRA 2012 Rule 14.4, required] */
            TAILQ_INIT(&tmpPcbPtr->resourceHead);
            DEBUG(DEBUG_LOW,"pid:%d name:%s prio:%d\n",tmpPcbPtr->constPtr->pid,tmpPcbPtr->constPtr->name,tmpPcbPtr->activePriority);
#if (OS_NUM_CORES > 1)
        }
#endif

        DEBUG(DEBUG_LOW,"pid:%d name:%s prio:%d\n",tmpPcbPtr->constPtr->pid,tmpPcbPtr->constPtr->name,tmpPcbPtr->activePriority);
    }

    Os_ResourceInit();

    // Now all tasks should be created.
}

static void os_start( void ) {
    uint16 i;
    OsTaskVarType *tmpPcbPtr = NULL_PTR;

    /* We will be setting up interrupts, but shall not be triggered until the Os starts */
    Irq_Disable();
    ASSERT(OS_SYS_PTR->status.init_os_called);

    /* Call the startup hook */
#if (OS_NUM_CORES > 1)
    syncCores(beforeStartHooks);
#endif
    /** @req SWS_Os_00236 */ /** @req SWS_Os_00539 */ /* System specific startup hook called before application specific startup hook (see Os_ApplStart()) */

    OS_SYS_PTR->hooks = &os_conf_global_hooks;
    if( OS_SYS_PTR->hooks->StartupHook != NULL_PTR ) {
        OS_SYS_PTR->hooks->StartupHook();
    }
#if (OS_NUM_CORES > 1)
    syncCores(afterStartHooks);
#endif

#if	(OS_USE_APPLICATIONS == STD_ON)
    /* Start applications */
    Os_ApplStart();
#endif


    /* Alarm autostart */
#if OS_ALARM_CNT!=0
    Os_AlarmAutostart();
#endif

#if OS_SCHTBL_CNT!=0
    /** @req SWS_Os_00510 */ /* Autostart of schedule tables after autostart of Tasks and Alarms */
    Os_SchTblAutostart();
#endif

    // Set up the systick interrupt
    {
        Os_SysTickInit();
#if (OS_NUM_CORES > 1)
        Os_CoreNotificationInit();
#endif
        if (OS_CORE_IS_ID_MASTER(GetCoreID())) {
#if defined(CFG_OS_SYSTICK2)
            Os_SysTickStart2(OsTickFreq);
#else
            uint32 sys_freq = Mcu_Arc_GetSystemClock();
            Os_SysTickStart((TickType)(sys_freq/(uint32)OsTickFreq));
#endif
        }
    }
#if (OS_NUM_CORES > 1)
    /* This is not an autosar req, but cores need to be synchronnized here because
     * the isr handler uses shared data*/
    syncCores(afterIsrSetup);
#endif
    /* Find highest Autostart task */
    {
        OsTaskVarType *iterPcbPtr;
        OsPriorityType topPrio = 0;
        // NOTE: only the master core has one idle task, we need one for each core
        for(i=0;i<OS_TASK_CNT;i++) {
            iterPcbPtr = Os_TaskGet(i);
#if (OS_NUM_CORES > 1)
            if (Os_OnRunningCore(OBJECT_TASK,iterPcbPtr->constPtr->pid))
#endif
            {
                if(	iterPcbPtr->constPtr->autostart != FALSE ) {
                    if( (tmpPcbPtr == NULL_PTR) || (iterPcbPtr->activePriority > topPrio) ) {
                        tmpPcbPtr = iterPcbPtr;
                        topPrio = iterPcbPtr->activePriority;
                    }
                }
            }
        }
    }

    // Swap in prio proc.
    ASSERT(tmpPcbPtr != NULL_PTR);
    // IMPROVEMENT: Do this in a more structured way.. setting OS_SYS_PTR->currTaskPtr manually is not the way to go..
    OS_SYS_PTR->currTaskPtr = tmpPcbPtr;
#if	(OS_USE_APPLICATIONS == STD_ON)
    /* Set current application */
    OS_SYS_PTR->currApplId = tmpPcbPtr->constPtr->applOwnerId;
#endif

    // reset the suspend and disable count (which was set in os_init) to enable resume interrupt.
    Os_SysIntClearAll();

    // register this auto-start activation
    ASSERT(tmpPcbPtr->activations <= tmpPcbPtr->constPtr->activationLimit);

    Os_TaskSwapContextTo(NULL_PTR,tmpPcbPtr);
    // We should not return here
    ASSERT(0);
    
}



/**
 * Main function of the system
 */
/*lint -e970 MISRA:FALSE_POSITIVE:Return of main shall be int type:[MISRA 2012 Directive 4.6, advisory] */
int main( void )
{
    EcuM_Init();

    /* control will not come here */
    return TRUE;
}


/**
 * Starts the OS
 *
 * @param Mode - Application mode to start in
 *
 */
/* @req OSEK_SWS_SS_00002 */
void StartOS(AppModeType Mode) {
    OS_SYS_PTR->status.os_started = TRUE;

    OS_SYS_PTR->appMode = Mode;

    os_start();

    /* Control shall not return here after os_start() */
    /** @req SWS_Os_00424 */
    ASSERT(0);
}

/**
 * OS shutdown
 *
 * @param Error - Reason for shutdown
 */

/** @req SWS_Os_00071 */
/* @req OSEK_SWS_SS_00003 */
void ShutdownOS( StatusType Error ) {

	Irq_Disable();
    /*lint -e506 MISRA:FALSE_POSITIVE:Configuration check:[MISRA 2012 Rule 2.1, required] */
	if ((OS_SYS_PTR->hooks != NULL_PTR) && (OS_SYS_PTR->hooks->ShutdownHook != NULL_PTR)) {
		OS_SYS_PTR->hooks->ShutdownHook(Error);
	}

	/** @req SWS_Os_00425 */
	/*lint -e716 LINT:FALSE_POSITIVE:Infinite loop */
    /*lint -e9036 MISRA:FALSE_POSITIVE:Conditional expression not required for this while loop:[MISRA 2012 Rule 14.4, required] */
	while(TRUE) {	}
}



