#include "board.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

void NMI_Handler(void) {
}

void HardFault_Handler(void) {
  board_fatal(0xF001U);
}

void MemManage_Handler(void) {
  board_fatal(0xF002U);
}

void BusFault_Handler(void) {
  board_fatal(0xF003U);
}

void UsageFault_Handler(void) {
  board_fatal(0xF004U);
}

void SVC_Handler(void) {
}

void DebugMon_Handler(void) {
}

void PendSV_Handler(void) {
}

void SysTick_Handler(void) {
  HAL_IncTick();
}

void DMA1_Stream3_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_spi2_rx);
}

void DMA1_Stream4_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_spi2_tx);
}

void DMA1_Stream5_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

void DMA1_Stream6_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_usart2_tx);
}

void DMA2_Stream0_IRQHandler(void) {
  HAL_DMA_IRQHandler(&hdma_adc1);
}

void USART2_IRQHandler(void) {
  HAL_UART_IRQHandler(&huart2);
}

void EXTI15_10_IRQHandler(void) {
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}

void OTG_FS_IRQHandler(void) {
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}
