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

#define DEMO_LPUART 			LPUART2
#define DEMO_LPUART_CLK_FREQ 	CLOCK_GetRootClockFreq(kCLOCK_Root_Lpuart2)
#define DEMO_LPUART_IRQn		LPUART2_IRQn
#define DEMO_LPUART_IRQHandler 	LPUART2_IRQHandler

typedef void UART_ON_RX_FUNC(uint8_t Data);
typedef int  UART_ON_TX_FUNC(uint8_t* pChar);

typedef UART_ON_TX_FUNC* UART_ON_TX_FUNC_P;
typedef UART_ON_RX_FUNC* UART_ON_RX_FUNC_P;


static UART_ON_RX_FUNC_P _cbOnRx;
static UART_ON_TX_FUNC_P _cbOnTx;


void HIF_UART_Init(uint32_t Baudrate, UART_ON_TX_FUNC_P cbOnTx, UART_ON_RX_FUNC_P cbOnRx);


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

void SEGGER_UART_init(U32 baud)
{
	HIF_UART_Init(baud, _cbOnUARTTx, _cbOnUARTRx);
}


/*********************************************************************
*
*       HIF_UART_WaitForTxEnd
*/
void HIF_UART_WaitForTxEnd(void) {
  //
  // Wait until transmission has finished (e.g. before changing baudrate).
  //
  while ((kLPUART_TxDataRegEmptyFlag & LPUART_GetStatusFlags(DEMO_LPUART)) == 0);		// Wait until transmit buffer empty (Last byte shift from data to shift register)
  while ((kLPUART_TransmissionCompleteFlag & LPUART_GetStatusFlags(DEMO_LPUART)) == 0);	// Wait until transmission is complete
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
void DEMO_LPUART_IRQHandler(void)
{
	uint32_t UsartStatus = LPUART_GetStatusFlags(DEMO_LPUART);
	uint8_t v;
	int r;

    /* If new data arrived. */
    if ((kLPUART_RxDataRegFullFlag) & UsartStatus)				// Data received?
    {
        v = LPUART_ReadByte(DEMO_LPUART);										// Read data


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
        	LPUART_DisableInterrupts(DEMO_LPUART, kLPUART_TxDataRegEmptyInterruptEnable); // Disable further tx interrupts
        } else
        {
        	while((kLPUART_TransmissionCompleteFlag & LPUART_GetStatusFlags(DEMO_LPUART)) == 0); // Makes sure that "transmission complete" flag in USART_SR is reset to 0 as soon as we write USART_DR.
        	LPUART_WriteByte(DEMO_LPUART,v);	// Start transmission by writing to data register
        }

    }
    SDK_ISR_EXIT_BARRIER;
}
/*********************************************************************
*
*       HIF_UART_EnableTXEInterrupt()
*/
void HIF_UART_EnableTXEInterrupt(void) {
	LPUART_EnableInterrupts(DEMO_LPUART, kLPUART_TxDataRegEmptyInterruptEnable);  // enable Tx empty interrupt => Triggered as soon as data register content has been copied to shift register
}

/*********************************************************************
*
*       HIF_UART_Init()
*/
void HIF_UART_Init(uint32_t Baudrate, UART_ON_TX_FUNC_P cbOnTx, UART_ON_RX_FUNC_P cbOnRx) {

  uint8_t g_tipString[] =
      "Lpuart 2 functional API SystemView SEGGER trace example for NXP board\r\nBoard can be debbuged with UART1 via USB debug port & traced via UART2!\r\n";
  lpuart_config_t config;
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
  config.baudRate_Bps = Baudrate;
  config.enableTx     = true;
  config.enableRx     = true;

  LPUART_Init(DEMO_LPUART, &config, DEMO_LPUART_CLK_FREQ);

  /* Send g_tipString out. */
//  if(LPUART_WriteBlocking(DEMO_LPUART, g_tipString, sizeof(g_tipString) / sizeof(g_tipString[0])) != kStatus_Success)
//	  PRINTF("ERROR with UART2!.\r\n");
//  else
//	  PRINTF("UART2 is good!\r\n");

  //
  // Setup callbacks which are called by ISR handler and enable interrupt in NVIC
  //
  _cbOnRx = cbOnRx;
  _cbOnTx = cbOnTx;
  NVIC_SetPriority(DEMO_LPUART_IRQn, 6);  // Highest priority, so it is not interrupted by FreeRTOS
  /* Enable TX&RX interrupt. */
  LPUART_EnableInterrupts(DEMO_LPUART, kLPUART_TxDataRegEmptyInterruptEnable);
  LPUART_EnableInterrupts(DEMO_LPUART, kLPUART_RxDataRegFullInterruptEnable);
  NVIC_EnableIRQ(DEMO_LPUART_IRQn);
}



