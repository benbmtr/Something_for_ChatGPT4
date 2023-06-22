/*! \file
 *  \brief Scheduling module for the OS.
 *
 * Contains everything needed to realise the scheduling between multiple processes.
 * Also contains functions to start the execution of programs.
 *
 *  \author   Lehrstuhl Informatik 11 - RWTH Aachen
 *  \date     2013
 *  \version  2.0
 */

#include "os_scheduler.h"
#include "util.h"
#include "os_input.h"
#include "os_scheduling_strategies.h"
#include "os_taskman.h"
#include "os_core.h"
#include "lcd.h"
#include <stdlib.h>
#include <avr/interrupt.h>
#include <stdbool.h>

//----------------------------------------------------------------------------
// Private Types
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Globals
//----------------------------------------------------------------------------

//! Array of states for every possible process
Process os_processes[MAX_NUMBER_OF_PROCESSES];


//! Index of process that is currently executed (default: idle)
uint8_t currentProcess = 0;

//! Contains the process id of the currently active process.
ProcessID currentProc = 0;

//----------------------------------------------------------------------------
// Private variables
//----------------------------------------------------------------------------

//! Currently active scheduling strategy
SchedulingStrategy actual;

//! Count of currently nested critical sections
uint8_t criticalSectionCount = 0;

//----------------------------------------------------------------------------
// Private function declarations
//----------------------------------------------------------------------------

//! ISR for timer compare match (scheduler)
ISR(TIMER2_COMPA_vect) __attribute__((naked));

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------

/*!
 *  Timer interrupt that implements our scheduler. Execution of the running
 *  process is suspended and the context saved to the stack. Then the periphery
 *  is scanned for any input events. If everything is in order, the next process
 *  for execution is derived with an exchangeable strategy. Finally the
 *  scheduler restores the next process for execution and releases control over
 *  the processor to that process.
 */
ISR(TIMER2_COMPA_vect) {
	saveContext(); // 2
	
	os_processes[currentProcess].sp.as_int = SP; //3
	
	SP = BOTTOM_OF_ISR_STACK; // 4 Scheduler Stack
	
	os_processes[currentProcess].checksum = os_getStackChecksum(currentProcess);
	
	if(os_getInput() == 9){ // Like F12 to BIOS
		os_waitForNoInput();
		os_taskManMain();
	}
	
	os_processes[currentProcess].state = OS_PS_READY; // 5
		
	switch (os_getSchedulingStrategy()){ // 6&7
	case OS_SS_EVEN:{
		//currentProcess = os_Scheduler_Even(os_processes, currentProcess); Alternative
		os_processes[os_Scheduler_Even(os_processes, currentProcess)].state = OS_PS_RUNNING;
		break;
	}
	case OS_SS_RANDOM:{
		//currentProcess = os_Scheduler_Random(os_processes, currentProcess); Alternative
		os_processes[os_Scheduler_Random(os_processes, currentProcess)].state = OS_PS_RUNNING;
		break;
	}
	case OS_SS_RUN_TO_COMPLETION:{
		//currentProcess = os_Scheduler_RunToCompletion(os_processes, currentProcess); Alternative
		os_processes[os_Scheduler_RunToCompletion(os_processes, currentProcess)].state = OS_PS_RUNNING;
		break;
	}
	case OS_SS_ROUND_ROBIN:{
		//currentProcess = os_Scheduler_RoundRobin(os_processes, currentProcess); Alternative
		os_processes[os_Scheduler_RoundRobin(os_processes, currentProcess)].state = OS_PS_RUNNING;
		break;
	}
	case OS_SS_INACTIVE_AGING:{
		//currentProcess = os_Scheduler_InactiveAging(os_processes, currentProcess); Alternative
		os_processes[os_Scheduler_InactiveAging(os_processes, currentProcess)].state = OS_PS_RUNNING;
		break;
	}
	default: break;
	}
	// currentProcess.state = OS_PS_RUNNING; Alternative
	
	SP = os_processes[currentProcess].sp.as_int; // 8
	
	if(os_processes[currentProcess].checksum != os_getStackChecksum(currentProcess)){
		os_errorPStr(PSTR("Checksum incorrect"));
	}
	
	restoreContext(); // 9
}

/*!
 *  This is the idle program. The idle process owns all the memory
 *  and processor time no other process wants to have.
 */
void idle(void) {
    while(1){
		lcd_writeChar('.');
		delayMs(DEFAULT_OUTPUT_DELAY);
	}
}

/*!
 *  This function is used to execute a program that has been introduced with
 *  os_registerProgram.
 *  A stack will be provided if the process limit has not yet been reached.
 *  This function is multitasking safe. That means that programs can repost
 *  themselves, simulating TinyOS 2 scheduling (just kick off interrupts ;) ).
 *
 *  \param program  The function of the program to start.
 *  \param priority A priority ranging 0..255 for the new process:
 *                   - 0 means least favourable
 *                   - 255 means most favourable
 *                  Note that the priority may be ignored by certain scheduling
 *                  strategies.
 *  \return The index of the new process or INVALID_PROCESS as specified in
 *          defines.h on failure
 */
ProcessID os_exec(Program *program, Priority priority) {
	os_enterCriticalSection();
	if (program == NULL) {
		return INVALID_PROCESS;
	}
	ProcessID pid;
	for (pid = 0; pid < MAX_NUMBER_OF_PROCESSES; pid++) {
		if (os_processes[pid].state == OS_PS_UNUSED) {
			break;
		}
	}
	// If maximum reached
	if (pid == MAX_NUMBER_OF_PROCESSES) {
		return INVALID_PROCESS;
	}
	
	// Set values for new process
	os_processes[pid].program = program;
	os_processes[pid].state = OS_PS_READY;
	os_processes[pid].priority = priority;
	os_processes[pid].sp.as_int = PROCESS_STACK_BOTTOM(pid);
	os_processes[pid].checksum = 0; // Initialize checksum 
	os_resetProcessSchedulingInformation(pid); // Set Age to 0 (Not bound to a scheduling strategy)
		
	// Write low Byte on stack
	*(os_processes[pid].sp.as_ptr--) = (uint8_t)((uint16_t)program & 0xFF);

	// Write high byte on stack
	*(os_processes[pid].sp.as_ptr--) = (uint8_t)((uint16_t)program >> 8);

	// Set SREG and 32 Regs on 0
	for (int i = 0; i < 33; i++) {
		*(os_processes[pid].sp.as_ptr--) = 0;
	}
	
	os_leaveCriticalSection();
	return pid;
}

/*!
 *  If all processes have been registered for execution, the OS calls this
 *  function to start the idle program and the concurrent execution of the
 *  applications.
 */
void os_startScheduler(void) {
    currentProc = 0;
	os_processes[0].state = OS_PS_RUNNING;
	SP = os_processes[0].sp.as_int;
	restoreContext();
}

/*!
 *  In order for the Scheduler to work properly, it must have the chance to
 *  initialize its internal data-structures and register.
 */
void os_initScheduler(void) {
	for (uint8_t i = 0; i < MAX_NUMBER_OF_PROCESSES; i++) {
		os_processes[i].state = OS_PS_UNUSED;
	}
	if(autostart_head->program == idle){
		autostart_head = autostart_head->next;
	}
    while(autostart_head != NULL){
		os_exec(autostart_head->program, DEFAULT_PRIORITY);
		autostart_head = autostart_head->next;
	}
	os_exec(idle, DEFAULT_PRIORITY);
}

/*!
 *  A simple getter for the slot of a specific process.
 *
 *  \param pid The processID of the process to be handled
 *  \return A pointer to the memory of the process at position pid in the os_processes array.
 */
Process* os_getProcessSlot(ProcessID pid) {
    return os_processes + pid;
}

/*!
 *  A simple getter to retrieve the currently active process.
 *
 *  \return The process id of the currently active process.
 */
ProcessID os_getCurrentProc(void) {
	return currentProc;
}

/*!
 *  Sets the current scheduling strategy.
 *
 *  \param strategy The strategy that will be used after the function finishes.
 */
void os_setSchedulingStrategy(SchedulingStrategy strategy) {
	os_resetSchedulingInformation(strategy); 
}

/*!
 *  This is a getter for retrieving the current scheduling strategy.
 *
 *  \return The current scheduling strategy.
 */
SchedulingStrategy os_getSchedulingStrategy(void) {
    return actual;
}

/*!
 *  Enters a critical code section by disabling the scheduler if needed.
 *  This function stores the nesting depth of critical sections of the current
 *  process (e.g. if a function with a critical section is called from another
 *  critical section) to ensure correct behavior when leaving the section.
 *  This function supports up to 255 nested critical sections.
 */
void os_enterCriticalSection(void) {
    uint8_t GIEB = SREG>>7; // 1
	
	SREG &= 0b01111111; // 2
	if(criticalSectionCount == 255){
		os_errorPStr(PSTR("Critical section overflow"));
	}
	criticalSectionCount++; // 3
	TIMSK2 &= ~(1 << OCIE2A); // 4
	
	SREG |= GIEB<<7; // 5
}

/*!
 *  Leaves a critical code section by enabling the scheduler if needed.
 *  This function utilizes the nesting depth of critical sections
 *  stored by os_enterCriticalSection to check if the scheduler
 *  has to be reactivated.
 */
void os_leaveCriticalSection(void) {
    uint8_t GIEB = SREG>>7; // 1
    SREG &= 0b01111111; // 2

    //If criticalSectionCount < 0 -> Error:	
	if(criticalSectionCount == 0){
		os_errorPStr(PSTR("Critical Sections don't match"));
	}
    criticalSectionCount--; // 3 
	
    if(criticalSectionCount == 0){
		TIMSK2 |= 1 << OCIE2A; // 4
	}
	
    SREG |= GIEB<<7; // 5
}

/*!
 *  Calculates the checksum of the stack for a certain process.
 *
 *  \param pid The ID of the process for which the stack's checksum has to be calculated.
 *  \return The checksum of the pid'th stack.
 */
StackChecksum os_getStackChecksum(ProcessID pid) {
    uint8_t checksum = 0;
	for (uint8_t p = 0; p <= STACK_SIZE_PROC; p++) {
	    checksum ^= *(PROCESS_STACK_BOTTOM(pid)+p);
    }
	return checksum;
}
