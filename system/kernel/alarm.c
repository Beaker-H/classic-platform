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

#include <assert.h>
#include <stdlib.h>
#include "Os.h"
#include "internal.h"


#define COUNTER_MAX(x) 			(x)->counter->alarm_base.maxallowedvalue
#define COUNTER_MIN_CYCLE(x) 	(x)->counter->alarm_base.mincycle
#define ALARM_CHECK_ID(x) 				\
	if( (x) > Oil_GetAlarmCnt()) { \
		rv = E_OS_ID;					\
		goto err; 						\
	}


/**
 * The system service  GetAlarmBase  reads the alarm base
 * characteristics. The return value <Info> is a structure in which
 * the information of data type AlarmBaseType is stored.
 *
 * @param alarm_id  Reference to alarm
 * @param info Reference to structure with constants of the alarm base.
 * @return
 */
StatusType GetAlarmBase( AlarmType AlarmId, AlarmBaseRefType Info ) {
    StatusType rv = Oil_GetAlarmBase(AlarmId,Info);
    if (rv != E_OK) {
        goto err;
    }
    OS_STD_END_2(OSServiceId_GetAlarmBase,AlarmId, Info);
}

StatusType GetAlarm(AlarmType AlarmId, TickRefType Tick) {
    StatusType rv = E_OK;
    OsAlarmType *aPtr;
    long flags;

    ALARM_CHECK_ID(AlarmId);
    aPtr = Oil_GetAlarmObj(AlarmId);

	Irq_Save(flags);
	if( aPtr->active == 0 ) {
		rv = E_OS_NOFUNC;
		Irq_Restore(flags);
		goto err;
	}

	*Tick =aPtr->expire_val -  aPtr->counter->val;

	Irq_Restore(flags);

	OS_STD_END_2(OSServiceId_GetAlarm,AlarmId, Tick);
}




/**
 * The system service occupies the alarm <AlarmID> element.
 * After <increment> ticks have elapsed, the task assigned to the
 * alarm <AlarmID> is activated or the assigned event (only for
 * extended tasks) is set or the alarm-callback routine is called.
 *
 * @param alarm_id Reference to the alarm element
 * @param increment Relative value in ticks
 * @param cycle Cycle value in case of cyclic alarm. In case of single alarms, cycle shall be zero.
 * @return
 */

StatusType SetRelAlarm(AlarmType AlarmId, TickType Increment, TickType Cycle){
	StatusType rv = E_OK;
	OsAlarmType *aPtr;

	ALARM_CHECK_ID(AlarmId);

	aPtr = Oil_GetAlarmObj(AlarmId);

	os_isr_printf(D_ALARM,"SetRelAlarm id:%d inc:%d cycle:%d\n",AlarmId,Increment,Cycle);


	if( (Increment == 0) ||
		(Increment > COUNTER_MAX(aPtr)) ||
		(Cycle < COUNTER_MIN_CYCLE(aPtr)) ||
		(Cycle > COUNTER_MAX(aPtr)) )
	{
		/** @req OS304 */
		rv =  E_OS_VALUE;
		goto err;
	}

	{

		Irq_Disable();
		if( aPtr->active == 0 ) {
			aPtr->active = 1;
		} else {
			rv = E_OS_STATE;
			goto err;
		}

		TickType curr_val = aPtr->counter->val;
		TickType left = COUNTER_MAX(aPtr) - curr_val;

		aPtr->expire_val = (left < Increment ) ?
									(curr_val + Increment) :
									(Increment - curr_val);
		aPtr->cycletime = Cycle;

		Irq_Enable();
		os_isr_printf(D_ALARM,"  expire:%d cycle:%d\n",aPtr->expire_val,aPtr->cycletime);
	}

	OS_STD_END_3(OSServiceId_SetRelAlarm,AlarmId, Increment, Cycle);
}

StatusType SetAbsAlarm(AlarmType AlarmId, TickType Start, TickType Cycle) {

	OsAlarmType *a_p;
	long flags;
	StatusType rv = E_OK;

	a_p = Oil_GetAlarmObj(AlarmId);

	if( a_p == NULL ) {
	    rv = E_OS_ID;
	    goto err;
	}

	if( (Start > COUNTER_MAX(a_p)) ||
		(Cycle < COUNTER_MIN_CYCLE(a_p)) ||
		(Cycle > COUNTER_MAX(a_p)) )
	{
		/* See SWS, OS304 */
	    rv = E_OS_VALUE;
	    goto err;
	}

	Irq_Save(flags);
	if( a_p->active == 1 ) {
		rv = E_OS_STATE;
		goto err;
	}

	a_p->active = 1;

	a_p->expire_val = Start;
	a_p->cycletime = Cycle;
	Irq_Restore(flags);

	os_isr_printf(D_ALARM,"  expire:%d cycle:%d\n",a_p->expire_val,a_p->cycletime);

	OS_STD_END_3(OSServiceId_SetAbsAlarm,AlarmId, Start, Cycle);
}

StatusType CancelAlarm(AlarmType AlarmId) {
	StatusType rv = E_OK;
	OsAlarmType *aPtr;
	long flags;

	ALARM_CHECK_ID(AlarmId);

	aPtr = Oil_GetAlarmObj(AlarmId);

	Irq_Save(flags);
	if( aPtr->active == 0 ) {
		rv = E_OS_NOFUNC;
		Irq_Restore(flags);
		goto err;
	}

	aPtr->active = 0;

	Irq_Restore(flags);

	OS_STD_END_1(OSServiceId_CancelAlarm,AlarmId);
}


