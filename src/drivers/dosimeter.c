#include "dosimeter.h"

#include "app_config.h"
#include "board.h"
#include "health.h"
#include "settings.h"

#include <string.h>

#define ADC_PAIR_COUNT 128U
#define ADC_DMA_WORDS (ADC_PAIR_COUNT * 2U)
#define VREFINT_CAL_ADDRESS ((const uint16_t *)0x1FFF7A2AU)

static uint16_t adc_dma[ADC_DMA_WORDS];
static volatile uint8_t pending_halves;
static dosimeter_snapshot_t current;
static int32_t zero_uv;
static bool zero_valid;
static uint16_t zero_remaining;
static uint64_t zero_accumulator;

static uint32_t integer_sqrt(uint64_t value) {
  uint64_t bit = (uint64_t)1U << 62;
  uint64_t result = 0U;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit != 0U) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)result;
}

bool dosimeter_init(void) {
  memset(&current, 0, sizeof(current));
  zero_uv = settings_get()->dosimeter_zero_uv;
  zero_valid = zero_uv != 0;
  current.flags = DOSIMETER_FLAG_NOMINAL_CALIBRATION | DOSIMETER_FLAG_STALE;
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma, ADC_DMA_WORDS) != HAL_OK) {
    return false;
  }
  return HAL_TIM_Base_Start(&htim2) == HAL_OK;
}

static void process_half(uint8_t half) {
  const size_t pairs_per_half = ADC_PAIR_COUNT / 2U;
  const size_t start = (size_t)half * pairs_per_half * 2U;
  uint64_t sum = 0U;
  uint64_t square_sum = 0U;
  uint64_t vref_sum = 0U;
  uint16_t minimum = 4095U;
  uint16_t maximum = 0U;

  for (size_t pair = 0U; pair < pairs_per_half; ++pair) {
    uint16_t sample = adc_dma[start + pair * 2U];
    uint16_t vref = adc_dma[start + pair * 2U + 1U];
    sum += sample;
    square_sum += (uint32_t)sample * sample;
    vref_sum += vref;
    if (sample < minimum) {
      minimum = sample;
    }
    if (sample > maximum) {
      maximum = sample;
    }
  }

  uint32_t mean = (uint32_t)(sum / pairs_per_half);
  uint32_t vref_mean = (uint32_t)(vref_sum / pairs_per_half);
  uint32_t vdda_mv = 3300U;
  if (vref_mean != 0U) {
    vdda_mv = ((uint32_t)(*VREFINT_CAL_ADDRESS) * 3300U) / vref_mean;
  }
  uint64_t mean_square = square_sum / pairs_per_half;
  uint64_t variance = mean_square > (uint64_t)mean * mean
                          ? mean_square - (uint64_t)mean * mean
                          : 0U;
  uint32_t voltage_uv = (uint32_t)(((uint64_t)mean * vdda_mv * 1000U) / 4095U);
  /* Seeded from the first block so the reading is usable immediately rather
   * than ramping up from zero. */
  uint32_t filtered = voltage_uv;
  if (current.filtered_voltage_uv != 0U) {
    const uint32_t weight = 1U << APP_DOSIMETER_FILTER_SHIFT;
    filtered = (uint32_t)(((uint64_t)current.filtered_voltage_uv *
                               (weight - 1U) + voltage_uv) / weight);
  }

  current.timestamp_ms = HAL_GetTick();
  current.raw_mean = (uint16_t)mean;
  current.raw_min = minimum;
  current.raw_max = maximum;
  current.raw_stddev = (uint16_t)integer_sqrt(variance);
  current.vdda_mv = vdda_mv;
  current.voltage_uv = voltage_uv;
  current.filtered_voltage_uv = filtered;

  if (zero_remaining > 0U) {
    /* Average the filtered signal rather than the instantaneous reading so a
     * single noisy block cannot bias the reference. */
    zero_accumulator += filtered;
    --zero_remaining;
    if (zero_remaining == 0U) {
      zero_uv = (int32_t)(zero_accumulator / APP_DOSIMETER_ZERO_SAMPLES);
      zero_valid = true;
      (void)settings_set_dosimeter_zero(zero_uv);
    }
  }

  current.zero_voltage_uv = zero_uv;
  /* Signed difference in microvolts, then microrad at 2.5 mV per rad. The
   * multiply is done in 64-bit because a full-scale swing overflows int32. */
  int64_t delta_uv = (int64_t)filtered - zero_uv;
  current.dose_microrad =
      (int32_t)((delta_uv * 1000000) / (int64_t)APP_DOSIMETER_UV_PER_RAD);

  current.flags = DOSIMETER_FLAG_NOMINAL_CALIBRATION;
  if (maximum >= 4090U) {
    current.flags |= DOSIMETER_FLAG_SATURATED;
  }
  if (zero_remaining > 0U) {
    current.flags |= DOSIMETER_FLAG_ZEROING;
  }
  if (!zero_valid) {
    current.flags |= DOSIMETER_FLAG_UNZEROED;
  }
}

void dosimeter_task(void) {
  __disable_irq();
  uint8_t halves = pending_halves;
  pending_halves = 0U;
  __enable_irq();

  if ((halves & 0x01U) != 0U) {
    process_half(0U);
  }
  if ((halves & 0x02U) != 0U) {
    process_half(1U);
  }
  if ((HAL_GetTick() - current.timestamp_ms) > 500U) {
    current.flags |= DOSIMETER_FLAG_STALE;
  }
}

bool dosimeter_begin_zero(void) {
  if (zero_remaining > 0U) {
    return false;
  }
  zero_accumulator = 0U;
  zero_remaining = APP_DOSIMETER_ZERO_SAMPLES;
  return true;
}

bool dosimeter_zero_in_progress(void) { return zero_remaining > 0U; }

void dosimeter_set_zero(int32_t microvolts) {
  zero_remaining = 0U;
  zero_uv = microvolts;
  zero_valid = true;
  (void)settings_set_dosimeter_zero(microvolts);
}

void dosimeter_get_snapshot(dosimeter_snapshot_t *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  __disable_irq();
  *snapshot = current;
  __enable_irq();
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *handle) {
  if (handle->Instance == ADC1) {
    if ((pending_halves & 0x01U) != 0U) {
      health_increment(&g_health.adc_overruns);
    }
    pending_halves |= 0x01U;
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *handle) {
  if (handle->Instance == ADC1) {
    if ((pending_halves & 0x02U) != 0U) {
      health_increment(&g_health.adc_overruns);
    }
    pending_halves |= 0x02U;
  }
}

