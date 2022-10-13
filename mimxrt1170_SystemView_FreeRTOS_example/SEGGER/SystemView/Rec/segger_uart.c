/**********************************************************
*          SEGGER MICROCONTROLLER SYSTEME GmbH
*   Solutions for real time microcontroller applications
***********************************************************
File    : HIF_UART.c
Purpose : Terminal control for Flasher using USART1 on PA9/PA10
--------- END-OF-HEADER ---------------------------------*/


#include "SEGGER_SYSVIEW.h"
#include "SEGGER_RTT.h"

/* For easy use and HAL layer,this library is included.
 * You can remove it by changing variables and structures that
 * it calls. */
#include "fsl_lpuart.h"

/* Adjust priority of SEGEGR UART interrupt routine to match your FreeRTOSConfig.h file */
#define SEGGER_UART_PRIORITY_LEVEL 6

/* Static array of Uart pointers and theirs interrupt routines(specific for NXP boards driver, they have this macros).
 * Handle of segger Uart */
static LPUART_Type *const seggerLpuartBases[] = LPUART_BASE_PTRS;
static const IRQn_Type seggerUartIRQ[] = LPUART_RX_TX_IRQS;
static LPUART_Type *  seggerUart;

typedef void UART_ON_RX_FUNC(uint8_t Data);
typedef int  UART_ON_TX_FUNC(uint8_t* pChar);

typedef UART_ON_TX_FUNC* UART_ON_TX_FUNC_P;
typedef UART_ON_RX_FUNC* UART_ON_RX_FUNC_P;


static UART_ON_RX_FUNC_P _cbOnRx;
static UART_ON_TX_FUNC_P _cbOnTx;

void HIF_UART_Init(U32 instanceNum, U32 baudrate, U32 rootClkLpuart, UART_ON_TX_FUNC_P cbOnTx, UART_ON_RX_FUNC_P cbOnRx);


#define _SERVER_HELLO_SIZE        (4)
#define _TARGET_HELLO_SIZE        (4)

static const U8 _abHelloMsg[_TARGET_HELLO_SIZE] = { 'S', 'V', (SEGGER_SYSVIEW_VERSION / 10000), (SEGGER_SYSVIEW_VERSION / 1000) % 10 };  // "Hello" message expected by SysView: [ 'S', 'V', <PROTOCOL_MAJOR>, <PROTOCOL_MINOR> ]

static struct {
  U8         NumBytesHelloRcvd;
  U8         NumBytesHelloSent;
  int        ChannelID;
} _SVInfo = {0,0,1};

static void _StartSysView(void) {
  int r;

  r = SEGGER_SYSVIEW_IsStarted();
  if (r == 0) {
    SEGGER_SYSVIEW_Start();
  }
}

static void _cbOnUARTRx(U8 Data) {
  if (_SVInfo.NumBytesHelloRcvd < _SERVER_HELLO_SIZE) {  // Not all bytes of <Hello> message received by SysView yet?
    _SVInfo.NumBytesHelloRcvd++;
    goto Done;
  }
  SEGGER_RTT_WriteDownBuffer(_SVInfo.ChannelID, &Data, 1);  // Write data into corresponding RTT buffer for application to read and handle accordingly
  _StartSysView();
  Done:
    return;
}

static int _cbOnUARTTx(U8* pChar) {
  int r;

  if (_SVInfo.NumBytesHelloSent < _TARGET_HELLO_SIZE) {  // Not all bytes of <Hello> message sent to SysView yet?
    *pChar = _abHelloMsg[_SVInfo.NumBytesHelloSent];
    _SVInfo.NumBytesHelloSent++;
    r = 1;
    goto Done;
  }
  r = SEGGER_RTT_ReadUpBufferNoLock(_SVInfo.ChannelID, pChar, 1);
  if (r < 0) {  // Failed to read from up buffer?
    r = 0;
  }
Done:
  return r;
}

void SEGGER_UART_init(U32 instanceNum, U32 baudrate, U32 rootClkLpuart)
{
	HIF_UART_Init(instanceNum, baudrate, rootClkLpuart, _cbOnUARTTx, _cbOnUARTRx);
}


/*********************************************************************
*
*       HIF_UART_WaitForTxEnd
*/
void HIF_UART_WaitForTxEnd(void) {
  //
  // Wait until transmission has finished (e.g. before changing baudrate).
  //
  while ((kLPUART_TxDataRegEmptyFlag & LPUART_GetStatusFlags(seggerUart)) == 0);		// Wait until transmit buffer empty (Last byte shift from data to shift register)
  while ((kLPUART_TransmissionCompleteFlag & LPUART_GetStatusFlags(seggerUart)) == 0);	// Wait until transmission is complete
}

/*********************************************************************
*
*       USART1_IRQHandler
*
*  Function descriptio
*    Interrupt handler.
*    Handles both, Rx and Tx interrupts
*
*  Notes
*    (1) This is a high-prio interrupt so it may NOT use embOS functions
*        However, this also means that embOS will never disable this interrupt
*/

/*******************************************************************************
 * Code
 ******************************************************************************/
void SEGGER_UARTX_IRQHandler(void)
{
	uint32_t UsartStatus = LPUART_GetStatusFlags(seggerUart);
	uint8_t v;
	int r;

    /* If new data arrived. */
    if ((kLPUART_RxDataRegFullFlag) & UsartStatus)	// Data received?
    {
        v = LPUART_ReadByte(seggerUart);	// Read data


        if(((kLPUART_RxOverrunFlag | kLPUART_NoiseErrorFlag | kLPUART_FramingErrorFlag |
        		kLPUART_ParityErrorFlag) & UsartStatus) == 0)
        {   																		// Only process data if no error occurred
        	(void)v;                                         						// Avoid warning in BTL
            if(_cbOnRx)
            {
            	_cbOnRx(v);
            }
        }
    }

    if((kLPUART_TxDataRegEmptyFlag) & UsartStatus)	// Tx (data register) empty? => Send next character Note: Shift register may still hold a character that has not been sent yet.
    {	//
        // Under special circumstances, (old) BTL of Flasher does not wait until a complete string has been sent via UART,
        // so there might be an TxE interru_SVInfopt pending *before* the FW had a chance to set the callbacks accordingly which would result in a NULL-pointer call...
        // Therefore, we need to check if the function pointer is valid.
        //
        if (_cbOnTx == NULL) 	// No callback set? => Nothing to do...
        {
          return;
        }
        r = _cbOnTx(&v);
        if (r == 0) 			// No more characters to send ?
        {
        	LPUART_DisableInterrupts(seggerUart, kLPUART_TxDataRegEmptyInterruptEnable); // Disable further tx interrupts
        } else
        {
        	while((kLPUART_TransmissionCompleteFlag & LPUART_GetStatusFlags(seggerUart)) == 0); // Makes sure that "transmission complete" flag in USART_SR is reset to 0 as soon as we write USART_DR.
        	LPUART_WriteByte(seggerUart,v);	// Start transmission by writing to data register
        }

    }
    SDK_ISR_EXIT_BARRIER;
}
/*********************************************************************
*
*       HIF_UART_EnableTXEInterrupt()
*/
void HIF_UART_EnableTXEInterrupt(void) {
	LPUART_EnableInterrupts(seggerUart, kLPUART_TxDataRegEmptyInterruptEnable);  // enable Tx empty interrupt => Triggered as soon as data register content has been copied to shift register
}

/*********************************************************************
*
*       HIF_UART_Init()
*/
void HIF_UART_Init(U32 instanceNum, U32 baudrate, U32 rootClkLpuart, UART_ON_TX_FUNC_P cbOnTx, UART_ON_RX_FUNC_P cbOnRx) {

	lpuart_config_t config;
	seggerUart = seggerLpuartBases[instanceNum];
	/*
	* config.baudRate_Bps = 115200U;
	* config.parityMode = kLPUART_ParityDisabled;
	* config.stopBitCount = kLPUART_OneStopBit;
	* config.txFifoWatermark = 0;
	* config.rxFifoWatermark = 0;
	* config.enableTx = false;
	* config.enableRx = false;
	*/
	LPUART_GetDefaultConfig(&config);
	config.baudRate_Bps = baudrate;
	config.enableTx     = true;
	config.enableRx     = true;

	LPUART_Init(seggerUart, &config, CLOCK_GetRootClockFreq(rootClkLpuart));

	//
	// Setup callbacks which are called by ISR handler and enable interrupt in NVIC
	//
	_cbOnRx = cbOnRx;
	_cbOnTx = cbOnTx;
	NVIC_SetPriority(seggerUartIRQ[instanceNum], SEGGER_UART_PRIORITY_LEVEL);  // Highest priority, so it is not interrupted by FreeRTOS
	/* Enable TX&RX interrupt. */
	LPUART_EnableInterrupts(seggerUart, kLPUART_TxDataRegEmptyInterruptEnable);
	LPUART_EnableInterrupts(seggerUart, kLPUART_RxDataRegFullInterruptEnable);
	NVIC_EnableIRQ(seggerUartIRQ[instanceNum]);

}



