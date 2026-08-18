#include "usbd_core.h"

#include "board.h"
#include "usbd_conf.h"

PCD_HandleTypeDef hpcd_USB_OTG_FS;

void HAL_PCD_MspInit(PCD_HandleTypeDef *handle) {
  GPIO_InitTypeDef gpio = {0};
  if (handle->Instance != USB_OTG_FS) {
    return;
  }
  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &gpio);
  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *handle) {
  if (handle->Instance == USB_OTG_FS) {
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
  }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *handle) {
  USBD_LL_SetupStage(handle->pData, (uint8_t *)handle->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *handle, uint8_t endpoint) {
  USBD_LL_DataOutStage(handle->pData, endpoint, handle->OUT_ep[endpoint].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *handle, uint8_t endpoint) {
  USBD_LL_DataInStage(handle->pData, endpoint, handle->IN_ep[endpoint].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *handle) {
  USBD_LL_SOF(handle->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *handle) {
  USBD_LL_Reset(handle->pData);
  USBD_LL_SetSpeed(handle->pData, USBD_SPEED_FULL);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *handle) {
  USBD_LL_Suspend(handle->pData);
  __HAL_PCD_GATE_PHYCLOCK(handle);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *handle) {
  __HAL_PCD_UNGATE_PHYCLOCK(handle);
  USBD_LL_Resume(handle->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *handle, uint8_t endpoint) {
  USBD_LL_IsoOUTIncomplete(handle->pData, endpoint);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *handle, uint8_t endpoint) {
  USBD_LL_IsoINIncomplete(handle->pData, endpoint);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *handle) {
  USBD_LL_DevConnected(handle->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *handle) {
  USBD_LL_DevDisconnected(handle->pData);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device) {
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4U;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_FS.pData = device;
  device->pData = &hpcd_USB_OTG_FS;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK) {
    return USBD_FAIL;
  }

  /* 320 32-bit FIFO words total on OTG FS: RX, EP0, UVC, CDC data, CDC notify. */
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 96U);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0U, 32U);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1U, 128U);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2U, 48U);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 3U, 16U);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device) {
  return HAL_PCD_DeInit(device->pData) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device) {
  /* A debugger reset can leave the host believing the old USB session is
   * still attached. Guarantee a visible detach interval before enabling the
   * internal D+ pull-up, which is also required when no VBUS sense is wired. */
  (void)HAL_PCD_DevDisconnect(device->pData);
  HAL_Delay(100U);
  return HAL_PCD_Start(device->pData) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device) {
  return HAL_PCD_Stop(device->pData) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t address,
                                  uint8_t type, uint16_t max_packet) {
  return HAL_PCD_EP_Open(device->pData, address, max_packet, type) == HAL_OK
             ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_EP_Close(device->pData, address) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_EP_Flush(device->pData, address) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_EP_SetStall(device->pData, address) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_EP_ClrStall(device->pData, address) == HAL_OK ? USBD_OK : USBD_FAIL;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t address) {
  PCD_HandleTypeDef *pcd = device->pData;
  return (address & 0x80U) != 0U ? pcd->IN_ep[address & 0x0FU].is_stall
                                : pcd->OUT_ep[address & 0x0FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_SetAddress(device->pData, address) == HAL_OK ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t address,
                                    uint8_t *buffer, uint32_t size) {
  return HAL_PCD_EP_Transmit(device->pData, address, buffer, size) == HAL_OK
             ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t address,
                                          uint8_t *buffer, uint32_t size) {
  return HAL_PCD_EP_Receive(device->pData, address, buffer, size) == HAL_OK
             ? USBD_OK : USBD_FAIL;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_EP_GetRxCount(device->pData, address);
}

void USBD_LL_Delay(uint32_t delay) {
  HAL_Delay(delay);
}
