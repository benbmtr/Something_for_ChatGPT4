/*! \file

Scheduling strategies used by the Interrupt Service RoutineA from Timer 2 (in scheduler.c)
to determine which process may continue its execution next.

The file contains five strategies:
-even
-random
-round-robin
-inactive-aging
-run-to-completion
*/

#include "os_scheduling_strategies.h"
#include "defines.h"

#include <stdlib.h>

SchedulingInformation schedulingInfo;

/*!
 *  Reset the scheduling information for a specific strategy
 *  This is only relevant for RoundRobin and InactiveAging
 *  and is done when the strategy is changed through os_setSchedulingStrategy
 *
 *  \param strategy  The strategy to reset information for
 */
void os_resetSchedulingInformation(SchedulingStrategy strategy) {
    if(strategy == OS_SS_ROUND_ROBIN){
		schedulingInfo.timeSlice = os_getCurrentProc()->priority; // Don't know how to access priority of currentProc
	}
	if(strategy == OS_SS_INACTIVE_AGING){
		uint8_t iterator = 0;
		while(schedulingInfo.age[iterator] != 0){
			schedulingInfo.age[iterator] = 0;
		}
	}
}

/*!
 *  Reset the scheduling information for a specific process slot
 *  This is necessary when a new process is started to clear out any
 *  leftover data from a process that previously occupied that slot
 *
 *  \param id  The process slot to erase state for
 */
void os_resetProcessSchedulingInformation(ProcessID id) {
    schedulingInfo.age[id] = 0;
}

/*!
 *  This function implements the even strategy. Every process gets the same
 *  amount of processing time and is rescheduled after each scheduler call
 *  if there are other processes running other than the idle process.
 *  The idle process is executed if no other process is ready for execution
 *
 *  \param processes An array holding the processes to choose the next process from.
 *  \param current The id of the current process.
 *  \return The next process to be executed determined on the basis of the even strategy.
 */
ProcessID os_Scheduler_Even(Process const processes[], ProcessID current) {
	uint8_t numberOfReadyProcs = 0;
	for(uint8_t i = 0;i < MAX_NUMBER_OF_PROCESSES;i++){
		if(processes[i].state == OS_PS_READY){
			numberOfReadyProcs++;
		}
	}
	if(numberOfReadyProcs == 1){ //When only the idle process is available return pid 0
		return 0;
	}
	while(1){ //Find next ready process
		if(current == 7){ //Cycle through pids
			current = 1;
			}else{
			current++;
		}
		if(processes[current].state == OS_PS_READY){
			return current;
		}
	}
}

/*!
 *  This function implements the random strategy. The next process is chosen based on
 *  the result of a pseudo random number generator.
 *
 *  \param processes An array holding the processes to choose the next process from.
 *  \param current The id of the current process.
 *  \return The next process to be executed determined on the basis of the random strategy.
 */
ProcessID os_Scheduler_Random(Process const processes[], ProcessID current) {
    uint8_t numberOfReadyProcs = 0;
	for(uint8_t i = 0;i < MAX_NUMBER_OF_PROCESSES;i++){
		if(processes[i].state == OS_PS_READY){
			numberOfReadyProcs++;
		}
	}
	if(numberOfReadyProcs == 1){
		return 0;
	}
	uint8_t array[numberOfReadyProcs];
	numberOfReadyProcs = 0;
	for(uint8_t i = 0;i < MAX_NUMBER_OF_PROCESSES;i++){
		if(processes[i].state == OS_PS_READY){
			array[numberOfReadyProcs] = i;
			numberOfReadyProcs++;
		}
	}
	return array[(rand()%(numberOfReadyProcs-1))+1]; // Random Idea to exclude 0 (the first idle entry) by reducing numberOfReadyProcs and incrementing the solution 
}

/*!
 *  This function implements the round-robin strategy. In this strategy, process priorities
 *  are considered when choosing the next process. A process stays active as long its time slice
 *  does not reach zero. This time slice is initialized with the priority of each specific process
 *  and decremented each time this function is called. If the time slice reaches zero, the even
 *  strategy is used to determine the next process to run.
 *
 *  \param processes An array holding the processes to choose the next process from.
 *  \param current The id of the current process.
 *  \return The next process to be executed determined on the basis of the round robin strategy.
 */
ProcessID os_Scheduler_RoundRobin(Process const processes[], ProcessID current) {
	if(processes[current].state == OS_PS_READY && schedulingInfo.timeSlice > 0){
		schedulingInfo.timeSlice--;
		return current;
	}
	uint8_t numberOfReadyProcs = 0;
	for(uint8_t i = 0;i < MAX_NUMBER_OF_PROCESSES;i++){
		if(processes[i].state == OS_PS_READY){
			numberOfReadyProcs++;
		}
	}
	if(numberOfReadyProcs == 1){ //When only the idle process is available return pid 0
		return 0;
	}
	uint8_t processWithHighestPriority = 0;
    for(uint8_t i = 0;i < MAX_NUMBER_OF_PROCESSES;i++){
		if(processes[i].priority > processWithHighestPriority){
			processWithHighestPriority = i;
		}
	}
	//os_resetSchedulingInformation(); Lösung?
	return processWithHighestPriority;
}

/*!
 *  This function realizes the inactive-aging strategy. In this strategy a process specific integer ("the age") is used to determine
 *  which process will be chosen. At first, the age of every waiting process is increased by its priority. After that the oldest
 *  process is chosen. If the oldest process is not distinct, the one with the highest priority is chosen. If this is not distinct
 *  as well, the one with the lower ProcessID is chosen. Before actually returning the ProcessID, the age of the process who
 *  is to be returned is reset to its priority.
 *
 *  \param processes An array holding the processes to choose the next process from.
 *  \param current The id of the current process.
 *  \return The next process to be executed, determined based on the inactive-aging strategy.
 */
ProcessID os_Scheduler_InactiveAging(Process const processes[], ProcessID current){
	    for(uint8_t iterator = 0;iterator < MAX_NUMBER_OF_PROCESSES;iterator++){ // Increasing all ages except idle
		    if(processes[iterator].state == OS_PS_READY){
				schedulingInfo.age[iterator] += processes[iterator].priority;
			}
	    }
		uint8_t nextProcess = 0;
		for(uint8_t iterator = 0;iterator < MAX_NUMBER_OF_PROCESSES;iterator++){ //Increasing all ages except current and idle
			if(processes[iterator].state == OS_PS_READY && schedulingInfo.age[iterator] >= schedulingInfo.age[nextProcess]){
				if(schedulingInfo.age[iterator] == schedulingInfo.age[nextProcess] && processes[iterator].priority >= processes[nextProcess].priority){
					if(processes[iterator].priority > processes[nextProcess].priority){
						nextProcess = iterator;	
					} //If priority is the same -> do nothing
				}else{
					if(schedulingInfo.age[iterator] > schedulingInfo.age[nextProcess]){
						nextProcess = iterator;	
					} //If age is the same -> do nothing
				}
			}
		}
		schedulingInfo.age[nextProcess] = 0;
		return nextProcess; //If no one is ready -> idle
}

/*!
 *  This function realizes the run-to-completion strategy.
 *  As long as the process that has run before is still ready, it is returned again.
 *  If  it is not ready, the even strategy is used to determine the process to be returned
 *
 *  \param processes An array holding the processes to choose the next process from.
 *  \param current The id of the current process.
 *  \return The next process to be executed, determined based on the run-to-completion strategy.
 */
ProcessID os_Scheduler_RunToCompletion(Process const processes[], ProcessID current) {
	if(processes[current].state == OS_PS_READY){
		return current;
	}
	
	uint8_t numberOfReadyProcs = 0;
	for(uint8_t i = 0;i < MAX_NUMBER_OF_PROCESSES;i++){
		if(processes[i].state == OS_PS_READY){
			numberOfReadyProcs++;
		}
	}
	if(numberOfReadyProcs == 1){ //When only the idle process is available return pid 0
		return 0;
	}
	while(1){ //Find next ready process
		if(current == 7){ //Cycle through pids
			current = 1;
			}else{
			current++;
		}
		if(processes[current].state == OS_PS_READY){
			return current;
		}
	}
}
