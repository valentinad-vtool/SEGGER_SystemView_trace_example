/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes. */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

#include <stdio.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* Task priorities. */
#define hello_task_PRIORITY (configMAX_PRIORITIES - 1)

/* DWT adresses */
#define  ARM_CM_DEMCR      (*(uint32_t *)0xE000EDFC) // Debug Exception and Monitor Control Register
#define  ARM_CM_DWT_CTRL   (*(uint32_t *)0xE0001000) // DWT Control Register
#define  ARM_CM_DWT_CYCCNT (*(uint32_t *)0xE0001004) // DWT Current PC Sampler Cycle Count Register
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void task1_handler(void *pvParameters);
static void task2_handler(void *pvParameters);

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Application entry point.
 */
int main(void)
{
	TaskHandle_t task1_handle;
	TaskHandle_t task2_handle;
	TimerHandle_t timerHndl;
    /* Init board hardware. */
    BOARD_ConfigMPU();
    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();
    SEGGER_UART_init(500000);
    SEGGER_SYSVIEW_Conf();

    /*
     * 	The CoreSight debug port found on most Cortex-M based processors contains a 32-bit free running
     *  counter that counts CPU clock cycles. The counter is part of the Debug Watch and Trace (DWT)
     *  module and can easily be used to measure the execution time of code.
     *  The following code is all that is needed to enable and initialize this highly useful feature.
     */
    // enable DWT(Debug Watch and Trace) module
    //if (ARM_CM_DWT_CTRL != 0) {        // See if DWT is available

    	/*
    	 * Global enable for all DWT and ITM features:
    	 * 		0 = DWT and ITM blocks disabled.
    	 * 		1 = DWT and ITM blocks enabled.
    	 */
        ARM_CM_DEMCR      |= 1 << 24;  // Set bit 24(TRCENA)

        ARM_CM_DWT_CYCCNT  = 0;

        ARM_CM_DWT_CTRL   |= 1 << 0;   // Set bit 0

    //}

    if (xTaskCreate(task1_handler, "task 1", configMINIMAL_STACK_SIZE + 100, "Hello world from Task-1", hello_task_PRIORITY, &task1_handle) !=
        pdPASS)
    {
        PRINTF("Task creation failed!\r\n");
        while (1)
            ;
    }
    if (xTaskCreate(task2_handler, "task 2", configMINIMAL_STACK_SIZE + 100, "Hello world from Task-2", hello_task_PRIORITY, &task2_handle) !=
        pdPASS)
    {
        PRINTF("Task creation failed!\r\n");
        while (1)
            ;
    }
//    timerHndl = xTimerCreate(
//          "timerSw", /* name */
//          pdMS_TO_TICKS(1000), /* period/time */
//          pdTRUE, /* auto reload */
//          (void*)0, /* timer ID */
//          vTimerCallback1SecExpired); /* callback */
//    if (timerHndl==NULL) {
//      for(;;); /* failure! */
//    }
    vTaskStartScheduler();
    for (;;)
        ;
}

/*!
 * @brief Task responsible for printing of "Hello world." message.
 */
static void task2_handler(void *pvParameters)
{
	char msg[100];
    for (;;)
    {
		snprintf(msg,100,"%s\n", (char*)pvParameters);
		SEGGER_SYSVIEW_PrintfTarget(msg);
        PRINTF("Hello world.\r\n");
        vTaskDelay(200);
        taskYIELD();
    }
}

static void task1_handler(void* parameters)
{

	char msg[100];

	while(1)
	{
		snprintf(msg,100,"%s\n", (char*)parameters);
		SEGGER_SYSVIEW_PrintfTarget(msg);
		PRINTF("-task2: Hello!\r\n");
		vTaskDelay(300);
		taskYIELD();
	}

}
