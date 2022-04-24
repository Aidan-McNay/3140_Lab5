/*
process.c for ECE3140 Lab 5
Aidan McNay and Claudia Valero de la Flor
 */

#include "3140_concur.h"
#include <stdlib.h>
#include <MKL46Z4.h>
#include "realtime.h"

const int countdown_time = 12000;

process_t * current_process;
process_t * process_queue;

int first_process = 1;

process_t * unready_rt_queue = NULL;
process_t * ready_rt_queue = NULL;

int process_deadline_met;
int process_deadline_miss;

volatile realtime_t current_time;

//For unready queue, we maintain the invariant that processes with earlier start times are at the front
//For ready queue, we maintain the invariant that processes with earlier deadlines are at the front

struct process_state {
	unsigned int* sp; //Pointer to process stack
	unsigned int* original_sp; //Pointer to start of stack
	int size_of_process;
	struct process_state* next_process; //Pointer to next process in queue
	int realtime_signal;
	realtime_t start;
	realtime_t deadline;
};

//Helper functions for linked list operations

void list_append (process_t** list_start, process_t* process_to_add)
{
	//Appends a given process to the list of processes
	process_t* last_process = *list_start;
	if (last_process==NULL) //No process yet in stack - just set list_start to the process
	{
		*list_start = process_to_add;
		process_to_add->next_process = NULL;
		return;
	}

	//We now need to find the last process in the queue
	while(last_process->next_process != NULL)
	{
		last_process = last_process->next_process;
	}
	//Now, we have last_process as the last_process in the queue
	last_process->next_process = process_to_add;
	process_to_add->next_process = NULL;
}

process_t* list_pop (process_t** list_start)
{
	//Pops the front-most process off of the list, removing it from the list
	if(*list_start==NULL) //Empty list
	{
		return NULL;
	}
	process_t* process_to_pop = (*list_start);
	*list_start = process_to_pop->next_process;
	process_to_pop->next_process = NULL;
	return process_to_pop;
}

int compareTime(realtime_t first_time, realtime_t second_time)
{
	//Used to compare two times. Returns 1 if second time is after first, returns 0 otherwise
	if (first_time.sec > second_time.sec){
		return 0;
	}
	else if (second_time.sec > first_time.sec){
		return 1;
	}
	else{
		if (second_time.sec > first_time.sec){
			return 1;
		}
		else{return 0;}
	}
}

int compareTimeEqual(realtime_t first_time, realtime_t second_time)
{
	//Used to compare two times. Returns 1 if second time is after or equal to first, returns 0 otherwise
	if (first_time.sec > second_time.sec){
		return 0;
	}
	else if (second_time.sec > first_time.sec){
		return 1;
	}
	else{
		if (first_time.sec > second_time.sec){
			return 0;
		}
		else{return 1;}
	}
}

static void startTimeSorting(process_t ** first_process, process_t * added_process) {
	process_t * current_process;
	if (*first_process == NULL) {
		added_process->next_process = *first_process;
		*first_process = added_process;
	} else {
		current_process = *first_process;
		while (current_process->next_process != NULL && compareTime(current_process->start, added_process->start)) {
			current_process = current_process->next_process;
		}
		if(current_process->next_process ==NULL){
			added_process->next_process = NULL;
		}
		else{
			added_process->next_process = current_process->next_process;
		}
		current_process->next_process = added_process;
	}
}

static void DeadlineSorting(process_t ** first_process, process_t * added_process) {
	process_t * current_process;
	if (*first_process == NULL) {
		added_process->next_process = *first_process;
		*first_process = added_process;
	} else {
		current_process = *first_process;
		while (current_process->next_process != NULL && compareTime(current_process->deadline, added_process->deadline)) {
			current_process = current_process->next_process;
		}
		if(current_process->next_process ==NULL){
			added_process->next_process = NULL;
		}
		else{
			added_process->next_process = current_process->next_process;
		}
		current_process->next_process = added_process;
	}
}


void process_start (void) {
	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
	PIT->MCR = 0;
	PIT->CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK / 10;
	NVIC_EnableIRQ(PIT_IRQn);
//	NVIC_SetPriority(PIT_IRQn, 2);
	// Don't enable the timer yet. The scheduler will do so itself

	//Generates interrupts every millisecond and updates the current time
	PIT->CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000;     //0.001 secs
	PIT->CHANNEL[1].TCTRL = 3;
//	NVIC_EnableIRQ(SVCall_IRQn);
//	NVIC_SetPriority(SVCall_IRQn, 1);
	current_time.sec = 0;
	current_time.msec = 0;

	// Bail out fast if no processes were ever created
	if ((process_queue == NULL) && (ready_rt_queue == NULL) && (unready_rt_queue == NULL)) return;
	process_begin();
}

void PIT1_Service(void) {
//	__disable_irq();
	PIT->CHANNEL[1].TFLG = PIT_TFLG_TIF_MASK;  //clear flags
	//PTE->PCOR = (1<<26);
	PIT->CHANNEL[1].TCTRL &= ~PIT_TCTRL_TEN_MASK;         //timer disable
	PIT->CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000;  //load 0.001 seconds into timer
	PIT->CHANNEL[1].TCTRL |= PIT_TCTRL_TEN_MASK;
	current_time.msec = current_time.msec + 1;
	if (current_time.msec > 999) {
		current_time.sec = current_time.sec + 1;
		current_time.msec = 0;
	}
//	__enable_irq();
}

//Functions to implement from 3140_concur.h

int process_create (void (*f)(void), int n)
{
	//PIT->CHANNEL[0].TCTRL = 1; //Disable interrupt flags so we can call process_stack_init
	unsigned int* sp = process_stack_init(f,n); //Create a new stack for the process
	if (sp==NULL) //Creating the process failed - no more space
	{
		return -1;
	}
	process_t* new_process = (process_t*)malloc(sizeof(process_t)); //Allocates space for the new process
	new_process->sp = sp;
	new_process->original_sp = sp;
	new_process->realtime_signal = 0;
	new_process->size_of_process = n;
	list_append(&process_queue, new_process); //Append the new process to our process queue
	//PIT->CHANNEL[0].TCTRL = 3; //Re-enable interrupts
	return 0;

}

int process_rt_create(void(*f)(void), int n, realtime_t *start, realtime_t *deadline) {
	unsigned int *sp = process_stack_init(f, n);
	if (!sp) return -1;

	process_t *aux_p = (process_t*) malloc(sizeof(process_t));
	if (aux_p==NULL) {
		return -1;
	}

	aux_p->sp = sp;
	aux_p->original_sp = sp;
	aux_p->size_of_process = n;
	aux_p->realtime_signal = 1; //it's a realtime process
	aux_p->start = *start;
	int total_msec = (1000*start->sec+start->msec)+(1000*deadline->sec+deadline->msec);
	int total_sec = total_msec / 1000;
	total_msec = total_msec % 1000;
	aux_p->deadline.sec = total_sec;
	aux_p->deadline.msec = total_msec;

	startTimeSorting(&unready_rt_queue, aux_p);
	return 0;
}

unsigned int * process_select(unsigned int * cursp)
{
	//Move ready rt processes from unready to ready
	int finished_moving = 0;
	while (!finished_moving){
		//Check if we're at or past the start time
		if (unready_rt_queue != NULL && compareTimeEqual(unready_rt_queue->start, current_time)){
			//The process is ready
			process_t* ready_process = list_pop(&unready_rt_queue); //Get process
			DeadlineSorting(&ready_rt_queue, ready_process); //Put process in ready queue
		}
		else{
			//No more ready processes - all others have later start times
			finished_moving = 1;
		}
	}

	//Selects the new process to begin running

	if (first_process == 1){ //Don't change the first time with current_process=NULL, as it's when we start up
		first_process = 0;
	}
	else{
		/*
		We only pop from the queue when a process has stopped running. This process is at the beginning
		of the queue, so we don't have to worry about list_pop returning NULL
		 */
		process_t* ended_process;
		int process_was_realtime;

		if(current_process->realtime_signal){ //There was a realtime process running
			ended_process = list_pop(&ready_rt_queue);
			process_was_realtime = 1;
		}
		else{ //Running process wasn't realtime
			ended_process = list_pop(&process_queue);
			process_was_realtime = 0;
		}

		if (cursp==NULL) //The process has terminated - free the used space
		{
			//If realtime, need to check if it met its deadline or not
			if(process_was_realtime){
				if(compareTimeEqual(ended_process->deadline, current_time)){
					//The current_time is after or equal to the deadline, so it was met
					process_deadline_met++;
				}
				else{
					//Deadline wasn't met
					process_deadline_miss++;
				}
			}

			//This is called from our interrupt handler, which disables interrupts, so we can call process_stack_free
			process_stack_free(ended_process->original_sp, ended_process->size_of_process); //Free process stack
			free(ended_process); //Free process_t struct for process
			current_process = NULL;

		}
		else //Process isn't done yet - put at end of appropriate queue
		{
			ended_process->sp = cursp; //Update stack pointer to where we left off
			if(process_was_realtime){
				DeadlineSorting(&ready_rt_queue, ended_process); //Put process in ready queue
			}
			else{
				list_append(&process_queue, ended_process); //Put process at the end of the queue
			}
		}
	}

	//Choose next process

	if(ready_rt_queue!=NULL){
		//There are ready realtime processes
		current_process = ready_rt_queue;
	}
	else if (process_queue!=NULL){
		//No ready realtime processes - schedule non-realtime one
		current_process = process_queue;
	}
	else if(unready_rt_queue!=NULL){
		//Processes exist, but aren't ready
		while(compareTimeEqual(unready_rt_queue->start, current_time))
		{
			//Manually change time
			if(PIT->CHANNEL[1].TFLG > 0x0){
				current_time.msec = current_time.msec + 1;
				if (current_time.msec > 999) {
					current_time.sec = current_time.sec + 1;
					current_time.msec = 0;
				}
				PIT->CHANNEL[1].TFLG = 0x1; // Clear flag by writing 1 to it
			}
		}
		//Move to ready queue and schedule
		process_t* ready_process = list_pop(&unready_rt_queue); //Get process
		DeadlineSorting(&ready_rt_queue, ready_process); //Put process in ready queue
		current_process = ready_rt_queue;
	}
	else{
		//No processes remaining
		return NULL;
	}
	return current_process->sp;
}
