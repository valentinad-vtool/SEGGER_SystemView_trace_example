	
						SystemView with MCU configuration!
			
1. Add FreeRTOS library to your project and check the version of it (just open any FreeRTOS file and, in comments, you can find version displayed). 
Check this site and see how steps for FreeRTOS and SEGGER should look like: https://wiki.segger.com/FreeRTOS_with_SystemView#System_Configuration .
2. Run application(add two tasks and start scheduler) only with FreeRTOS library and check if it's working correctly! In this example you can find 
how to run tasks and to write them(leave the parts in tasks for SEGGER functions).
3. When FreeRTOS is checked and verifyied, download from SEGGER site SystemView library for MCU : https://www.segger.com/downloads/systemview/ . 
It is in part: SystemView, Target Sources. Download THE SAME version of SEGGER SystemView for MCU as you downloaded SEGGER SV(SystemView) 
application for host board(deppending on OS, you can download Windows,Linux or macOS  SV app).
4. Make directory called "SEGGER" in you project where you will put your root directory of FreeRTOS library(root directories of both libraries 
should be in the same level).
5. Add/extract directories called "Config", "Sample" and "SEGGER" in downloaded zipped file and everything what is in them in your "SEGGER" directory.
6. Go to extracted directory "Sample". Find which version of OS you used in your application on MCU and ONLY LEAVE that one subdirectory in "Sample" 
directory( in my case it was directory called FreeRTOSV10, because I had version of FreeRTOS installed v10.3.1(for ST) and v10.3.0(for NXP), so I left 
only that directory and deleted others).
7. Check again this site https://wiki.segger.com/FreeRTOS_with_SystemView#System_Configuration and part for "System Configuration". You have to add 
in your Project path includes to your freshly added SEGGER library. You have to right click on project name and click "Properties". Go to C/C++ General->Path 
and Symbols->Includes->GNU C. Add path to extracted directories in your SEGGER directory:
				   1. /root_of_FreeRTOS_and_SEGGER/SEGGER/Config
				   2. /root_of_FreeRTOS_and_SEGGER/SEGGER/Sample
				   3. /root_of_FreeRTOS_and_SEGGER/SEGGER/Sample/FreeRTOSV10 (this is in my case, in yours can be different!)
				   4. /root_of_FreeRTOS_and_SEGGER/SEGGER/SEGGER
8. At the end of FreeRTOSConfig.h add #include "SEGGER_SYSVIEW_FreeRTOS.h" (I put it  before macro __NVIC_PRIO_BITS).
 - Troubleshooting: If you are working with ST microcontrollers, STM32CubeIDE can generate you a FreeRTOS code with SysTick_Handler() function called in 
 stm32xxxx_it.c file and you can get compiler errors for predefinition of that function. Just comment that function in that file and add 
 #define xPortSysTickHandler SysTick_Handler in your FreeRTOSConfig.h file. 
 build system again. It should fix your problem. Also, remember to, when using ST microcontrollers, configure some timer for FreeRTOS base system clock, 
 to not get this confusion in project.
10. Apply patch for FreeRTOS got in SEGGER SystemView library. Patch is on path root_of_SEGGER/Sample/Patch/ and inside is a FreeRTOSV10_Core.patch file, 
in my case. Copy this patch file in directory where are "include" and  "portable" directories of FreeRTOS library. 
		1. On linux use command patch -p1 -r . < FreeRTOSV10_Core.patch 
		2. On windows, use git tool or patch manually 
 - Troubleshooting: If you cannot patch FreeRTOS, you have to manually add functions for tracing FreeRTOS library. Just keep watch on tasks.c file 
 and for which platform you are using FreeRTOS(patch only for used platform!). You can, also, compare to this example files that are patched and 
 carefully copy parts that are missing in you FreeRTOS library.		
 11. Run the system with newly added trace capabilities and verify if everything is working well as before. If you have some errors, you need to 
 do corrections in your code and find out what is wrong.
 
 12. IMPORTANT! Recuirements for other steps: Uart clock and pins that you want to use for SEGGER recording data needs to be enabled and confgured!
 Test your uart if it is working correctly before you apply next steps. Also, if you have some errors, you need to adjust uart_segger.c file
 to fit your platform specifications!
  		   
 13. This is the time where we can add uart_segger.c file into our project! Copy from mine example project uart_segger.c file into newly created 
 "Rec" directory. Chose uart_segger.c file for proper platform(currently it is available for ST microcontrollers and NXP MCU).
 14. In directory /root_of_FreeRTOS_and_SEGGER/SEGGER/Config open file SEGGER_SYSVIEW_Conf.h. In this file you have to:
  		1. Add include for currently used device of your project(in my case it is #include "stm32f4xx.h" or #include "MIMXRT1176_cm7.h")
  		2. Add this part of the code for defining and exporting some functions:
				extern void SEGGER_UARTX_IRQHandler(void);
				extern void HIF_UART_EnableTXEInterrupt  (void);
				#define SEGGER_SYSVIEW_ON_EVENT_RECORDED(x)  HIF_UART_EnableTXEInterrupt()
				void SEGGER_UART_init(USART_TypeDef * instance, U32 baud, U32 intNum);
15.Call this two functions(arguments passed are for example perpose, you need to adjust it to your needs):
  		SEGGER_UART_init(USART2,500000,USART2_IRQn);
  		SEGGER_SYSVIEW_Conf();
First function pass USART2 instance for initialization, baud rate of 500000 and interrupt number(adjust arguments to match your needs). 
Second function initialize and configure SEGGER SV for use. 
16. Used interrupt for Uart handle needs to be defined to call this SEGGER_UARTX_IRQHandler() function. This is example of how can be done with USART2 instance
in main.c or in some other place like stm32xxxx_it.c file:
	void USART2_IRQHandler(void)
	{
		SEGGER_UARTX_IRQHandler();
	}
17. After those functions, add specific functions for ARM Cortex-M cores that are enabling DWT(Data Watchpoint and Trace) capabilities and execution cycle
counting. SystemView events timestamps are calculated using this counter mechanism by default. If you want to use your own counter you have to implement 
SEGGER_SYSVIEW_X_GetTimestamp(). This is what you have to do in main.c:
		1. Add defines:
			/* DWT adresses */
			#define  ARM_CM_DEMCR      (*(uint32_t *)0xE000EDFC) // Debug Exception and Monitor Control Register
			#define  ARM_CM_DWT_CTRL   (*(uint32_t *)0xE0001000) // DWT Control Register
			#define  ARM_CM_DWT_CYCCNT (*(uint32_t *)0xE0001004) // DWT Current PC Sampler Cycle Count Register
		2. Add this lines after you called SEGGER_SYSVIEW_Conf() function:
		  	ARM_CM_DEMCR      |= 1 << 24;  // Set bit 24(TRCENA)
  			ARM_CM_DWT_CYCCNT  = 0;
  			ARM_CM_DWT_CTRL   |= 1 << 0;   // Set bit 0
18. Build and run the system in debug mode. Connect used SystemView Uart to your PC by USB to TTL hardware. Run SystemView application on host(
on linux, run the application from terminal calling: $sudo systemview). When you open SV, click continue button. In the above tab click Target->Recorder Configuration.
Set you recorder to UART and baud rate to match with your running project(500000bps in current case). Click Target->Start Recording button once and stop immediately.
Start again recording and you should see your events collected via UART interface to your SV application. 
Troubleshooting: You have to start  recording 2 times after you will get data collected. This is maybe the bug in their application. SV application first time
after sending "Hello" message(this message represents sending 4 characters: "S","V","SEGGER_SYSVIEW_VERSION / 10000", "(SEGGER_SYSVIEW_VERSION / 1000) % 10") 
should send command for SystemView to start recording, but it doesn't send.

		
