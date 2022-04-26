//test_r2.c

//This test tests not only that we schedule earlier deadlines first (in
//accordance with EDF), but that if there are no ready processes, we don't
//just exit the code, but instead wait for them to become ready

#include "utils.h"
#include "3140_concur.h"
#include "realtime.h"

/*--------------------------*/
/* Parameters for test case */
/*--------------------------*/



/* Stack space for processes */
#define RT_STACK  20



/*--------------------------------------*/
/* Time structs for real-time processes */
/*--------------------------------------*/


/* Process start time */
realtime_t t_pRT1 = {1, 0};
realtime_t t_pRT2 = {1, 5};

/* Process deadlines */
realtime_t t_d = {5,0}; //A sufficiently long deadline that we won't worry about misses


/*------------------*/
/* Helper functions */
/*------------------*/
void shortDelay(){delay();}
void mediumDelay() {delay(); delay();}

/*-------------------
 * Real-time process
 *-------------------*/

void pRT1(void) {
	int i;
	for (i=0; i<3;i++){
	LEDGreen_On();
	shortDelay();
	LEDGreen_Toggle();
	shortDelay();
	}
}

void pRT2(void) {
	int i;
	for (i=0; i<3;i++){
	LEDRed_On();
	shortDelay();
	LEDRed_Toggle();
	shortDelay();
	}
}


/*--------------------------------------------*/
/* Main function - start concurrent execution */
/*--------------------------------------------*/
int main(void) {

	LED_Initialize();

    /* Create processes */
    if (process_rt_create(pRT1, RT_STACK, &t_pRT1, &t_d) < 0) { return -1; }
    if (process_rt_create(pRT2, RT_STACK, &t_pRT2, &t_d) < 0) { return -1; }

    /* Launch concurrent execution */
	process_start();
	//No processes currently ready - will be a delay before blinking

  LED_Off();
  while(process_deadline_miss>0) {
		LEDRed_On();
		LEDGreen_On();
		shortDelay();
		LED_Off();
		shortDelay();
		process_deadline_miss--;
	}

  while(process_deadline_met>0) {
  		LEDRed_On();
  		shortDelay();
  		LEDGreen_On();
  		mediumDelay();
  		LED_Off();
  		shortDelay();
  		process_deadline_met--;
  	}

	/* Hang out in infinite loop (so we can inspect variables if we want) */
	while (1);
	return 0;
}
