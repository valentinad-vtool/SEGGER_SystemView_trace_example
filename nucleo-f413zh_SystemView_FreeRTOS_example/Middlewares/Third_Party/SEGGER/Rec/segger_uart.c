/**********************************************************
*          SEGGER MICROCONTROLLER SYSTEME GmbH
*   Solutions for real time microcontroller applications
***********************************************************
File    : HIF_UART.c
Purpose : Terminal control for Flasher using USART1 on PA9/PA10
--------- END-OF-HEADER ---------------------------------*/


#include "SEGGER_SYSVIEW.h"
#include "SEGGER_RTT.h"

UART_HandleTypeDef huartSegger;

typedef void UART_ON_RX_FUNC(uint8_t Data);
typedef int  UART_ON_TX_FUNC(uint8_t* pChar);

typedef UART_ON_TX_FUNC* UART_ON_TX_FUNC_P;
typedef UART_ON_RX_FUNC* UART_ON_RX_FUNC_P;


static UART_ON_RX_FUNC_P _cbOnRx;
static UART_ON_TX_FUNC_P _cbOnTx;


void HIF_UART_Init(USART_TypeDef * instance, uint32_t baudrate, uint32_t intNum, UART_ON_TX_FUNC_P cbOnTx, UART_ON_RX_FUNC_P cbOnRx);


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
  _StartSysView();
  SEGGER_RTT_WriteDownBuffer(_SVInfo.ChannelID, &Data, 1);  // Write data into corresponding RTT buffer for application to read and handle accordingly
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

void SEGGER_UART_init(USART_TypeDef * instance, U32 baud, U32 intNum)
{
	HIF_UART_Init(instance,baud,intNum, _cbOnUARTTx, _cbOnUARTRx);
}


/*********************************************************************
*
*       HIF_UART_WaitForTxEnd
*/
void HIF_UART_WaitForTxEnd(void) {
  //
  // Wait until transmission has finished (e.g. before changing baudrate).
  //
  while ((READ_REG(huartSegger.Instance->SR) & USART_SR_TXE) == 0);  // Wait until transmit buffer empty (Last byte shift from data to shift register)
  while ((READ_REG(huartSegger.Instance->SR) & USART_SR_TC) == 0);   // Wait until transmission is complete
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
void SEGGER_UARTX_IRQHandler(void) {
  int UsartStatus;
  uint8_t v;
  int r;

  UsartStatus = READ_REG(huartSegger.Instance->SR);   // Examine status register
  if ((UsartStatus & USART_SR_RXNE) != 0) {            // Data received?
    v = huartSegger.Instance->DR;                     // Read data
    if ((UsartStatus & (uint32_t)(USART_SR_PE |
    		USART_SR_FE | USART_SR_ORE | USART_SR_NE)) == 0) {   // Only process data if no error occurred
      (void)v;                                         // Avoid warning in BTL
      if (_cbOnRx) {
        _cbOnRx(v);
      }
    }
  }
  if ((UsartStatus & USART_SR_TXE) != 0) {                // Tx (data register) empty? => Send next character Note: Shift register may still hold a character that has not been sent yet.
    //
    // Under special circumstances, (old) BTL of Flasher does not wait until a complete string has been sent via UART,
    // so there might be an TxE interrupt pending *before* the FW had a chance to set the callbacks accordingly which would result in a NULL-pointer call...
    // Therefore, we need to check if the function pointer is valid.
    //
    if (_cbOnTx == NULL) {  // No callback set? => Nothing to do...
      return;
    }
    r = _cbOnTx(&v);
    if (r == 0) {                          // No more characters to send ?
      __HAL_UART_DISABLE_IT(&huartSegger, UART_IT_TXE); // Disable further tx interrupts
    } else {
      READ_REG(huartSegger.Instance->SR);      // Makes sure that "transmission complete" flag in USART_SR is reset to 0 as soon as we write USART_DR. If USART_SR is not read before, writing USART_DR does not clear "transmission complete". See STM32F4 USART documentation for more detailed description.
      huartSegger.Instance->DR = v;  // Start transmission by writing to data register
    }
  }
}

/*********************************************************************
*
*       HIF_UART_EnableTXEInterrupt()
*/
void HIF_UART_EnableTXEInterrupt(void) {
  __HAL_UART_ENABLE_IT(&huartSegger, UART_IT_TXE); // enable Tx empty interrupt => Triggered as soon as data register content has been copied to shift register
}



/*********************************************************************
*
*       HIF_UART_Init()
*/
void HIF_UART_Init(USART_TypeDef * instance,uint32_t baudrate, uint32_t intNum, UART_ON_TX_FUNC_P cbOnTx, UART_ON_RX_FUNC_P cbOnRx) {

	huartSegger.Instance = instance;
	huartSegger.Init.BaudRate = baudrate;
	huartSegger.Init.WordLength = UART_WORDLENGTH_8B;
	huartSegger.Init.StopBits = UART_STOPBITS_1;
	huartSegger.Init.Parity = UART_PARITY_NONE;
	huartSegger.Init.Mode = UART_MODE_TX_RX;
	huartSegger.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huartSegger.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huartSegger) != HAL_OK)
  {
	  __disable_irq();
	    while (1)
	    {
	    }
  }
  //
  // Setup callbacks which are called by ISR handler and enable interrupt in NVIC
  //
  _cbOnRx = cbOnRx;
  _cbOnTx = cbOnTx;
  NVIC_SetPriority(intNum, 8);  // Highest prio, so it is not disabled by FreeRTOS
  __HAL_UART_ENABLE_IT(&huartSegger, UART_IT_RXNE);
  __HAL_UART_ENABLE_IT(&huartSegger, UART_IT_TXE);
  NVIC_EnableIRQ(intNum); //USART2_IRQn
}


