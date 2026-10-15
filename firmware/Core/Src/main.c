/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
ADC_HandleTypeDef hadc1;

DAC_HandleTypeDef hdac1;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim6;

/* USER CODE BEGIN PV */
#define N          16000            // max pulse: 16 ms at 1 MSps
#define INA_ADDR   (0x40 << 1)      // INA219 I2C address
#define R_SHUNT    0.1f             // INA219 shunt (ohms)
#define BUDGET_mAh 2.0f             // demo battery budget
#define R_MAX_m    100.0f           // max sonar range -> listening window
#define USE_DIAL   1                // 1 = potentiometer scenario dial, 0 = real turbidity sensor
static uint32_t wave[N];
volatile uint8_t ping_done = 1;
volatile uint8_t mode = 0;          // 0-2 audible (LFM, GEO, BPSK), 3-5 ultrasonic (LFM, GEO, BPSK)
uint32_t turbidity = 0, tds = 0;
float temp_c = 25.0f;
float i_mA = 0.0f, v_bus = 0.0f, used_mAh = 0.0f;
uint8_t ina_ok = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void SystemPower_Config(void);
static void MX_GPIO_Init(void);
static void MX_GPDMA1_Init(void);
static void MX_TIM1_Init(void);
static void MX_DAC1_Init(void);
static void MX_ICACHE_Init(void);
static void MX_TIM6_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ================= waveform engine ================= */
static float sample_rate(void)
{
  return 160000000.0f / (float)(TIM6->ARR + 1);
}

static float window(int i, int n, int tukey)            // Hann, or Tukey(0.2) for phase-coded
{
  if (!tukey) return 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (n - 1)));
  float taper = 0.1f * (n - 1);
  if (i < taper)         return 0.5f * (1.0f - cosf(3.14159265f * i / taper));
  if (i > n - 1 - taper) return 0.5f * (1.0f - cosf(3.14159265f * (n - 1 - i) / taper));
  return 1.0f;
}

static void build_wave(int mod, float f0, float f1, float amp, uint32_t n)
{
  static const int8_t barker13[13] = { 1,1,1,1,1,-1,-1,1,1,-1,1,-1,1 };
  float fs = sample_rate(), T = n / fs;
  float lnK = logf(f1 / f0), fc = 0.5f * (f0 + f1);
  for (uint32_t i = 0; i < n; i++)
  {
    float t = i / fs, s, w;
    if (mod == 0)        // LFM chirp: frequency rises linearly
    { s = sinf(2.0f * 3.14159265f * (f0 * t + 0.5f * (f1 - f0) / T * t * t)); w = window(i, n, 0); }
    else if (mod == 1)   // geometric sweep: frequency rises by a constant ratio
    { s = sinf(2.0f * 3.14159265f * f0 * T / lnK * (expf(lnK * t / T) - 1.0f)); w = window(i, n, 0); }
    else                 // phase-coded: Barker-13 BPSK on the centre frequency
    { s = barker13[(i * 13) / n] * sinf(2.0f * 3.14159265f * fc * t); w = window(i, n, 1); }
    wave[i] = (uint32_t)(2048.0f + amp * w * s);
  }
}

static void fire_ping(uint32_t n)
{
  ping_done = 0;
  HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)wave, n, DAC_ALIGN_12B_R);
}

/* ================= turbidity + TDS ================= */
static void read_sensors(void)
{
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);  turbidity = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);  tds       = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
}

static float tds_ppm(uint32_t raw)
{
  float v = raw * 3.3f / 4095.0f;
  return (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * 0.5f;
}

static float sound_speed(float T, float S_ppt)          // speed of sound in water (m/s)
{
  return 1449.2f + 4.6f * T - 0.055f * T * T + 0.00029f * T * T * T
         + (1.34f - 0.01f * T) * (S_ppt - 35.0f);
}

/* ================= microsecond delay ================= */
static void dwt_init(void)
{
#ifdef DCB
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
#else
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#endif
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT, ticks = us * (SystemCoreClock / 1000000U);
  while ((DWT->CYCCNT - start) < ticks) { }
}

/* ================= DS18B20 (1-Wire on PF12) ================= */
static inline void ow_low(void)     { DS18B20_GPIO_Port->BRR  = DS18B20_Pin; }
static inline void ow_release(void) { DS18B20_GPIO_Port->BSRR = DS18B20_Pin; }
static inline int  ow_level(void)   { return (DS18B20_GPIO_Port->IDR & DS18B20_Pin) ? 1 : 0; }

static int ow_reset(void)
{
  ow_low();     delay_us(480);
  ow_release(); delay_us(70);
  int present = !ow_level();
  delay_us(410);
  return present;
}

static void ow_write_bit(int b)
{
  __disable_irq();
  ow_low();
  if (b) { delay_us(6);  ow_release(); delay_us(64); }
  else   { delay_us(60); ow_release(); delay_us(10); }
  __enable_irq();
}

static int ow_read_bit(void)
{
  __disable_irq();
  ow_low(); delay_us(6);
  ow_release(); delay_us(9);
  int b = ow_level();
  delay_us(55);
  __enable_irq();
  return b;
}

static void ow_write_byte(uint8_t v) { for (int i = 0; i < 8; i++) { ow_write_bit(v & 1); v >>= 1; } }
static uint8_t ow_read_byte(void)    { uint8_t v = 0; for (int i = 0; i < 8; i++) v |= ow_read_bit() << i; return v; }

static void ds_start(void)
{
  if (ow_reset()) { ow_write_byte(0xCC); ow_write_byte(0x44); }
}

static int ds_read(float *t)
{
  if (!ow_reset()) return 0;
  ow_write_byte(0xCC); ow_write_byte(0xBE);
  uint8_t lo = ow_read_byte(), hi = ow_read_byte();
  *t = (int16_t)((hi << 8) | lo) / 16.0f;
  return 1;
}

/* ================= INA219 (I2C1) ================= */
static int ina_init(void)
{
  uint8_t cfg[2] = { 0x01, 0xFF };   // 16 V, +/-40 mV shunt, 12-bit, 128-sample average
  return HAL_I2C_Mem_Write(&hi2c1, INA_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, cfg, 2, 10) == HAL_OK;
}

static int ina_read(float *mA, float *V)
{
  uint8_t b[2];
  if (HAL_I2C_Mem_Read(&hi2c1, INA_ADDR, 0x01, I2C_MEMADD_SIZE_8BIT, b, 2, 10) != HAL_OK) return 0;
  *mA = fabsf((int16_t)((b[0] << 8) | b[1]) * 0.01f / R_SHUNT);
  if (HAL_I2C_Mem_Read(&hi2c1, INA_ADDR, 0x02, I2C_MEMADD_SIZE_8BIT, b, 2, 10) != HAL_OK) return 0;
  *V = (((b[0] << 8) | b[1]) >> 3) * 0.004f;
  return 1;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the System Power */
  SystemPower_Config();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_TIM1_Init();
  MX_DAC1_Init();
  MX_ICACHE_Init();
  MX_TIM6_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
  HAL_TIM_Base_Start(&htim6);                                        // 1 MSps trigger
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);    // idle at mid-scale
  dwt_init();
  ina_ok = ina_init();
  ds_start();
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_BLUE);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

	    /* USER CODE BEGIN 3 */
	    static const char *mod_name[3] = { "LFM ", "GEO ", "BPSK" };
	    static uint32_t t_avg = 0, d_avg = 0, ds_tick = 0, last_tick = 0;
	    GPIOE->BSRR = GPIO_PIN_0;                    // CPU-busy pin PE0 HIGH = CPU working
	    uint32_t now = HAL_GetTick();

	    /* ---- sensors ---- */
	    read_sensors();
	    t_avg = (t_avg * 3 + turbidity) / 4;
	    d_avg = (d_avg * 3 + tds) / 4;
	    if (now - ds_tick >= 800)
	    {
	      float t;
	      if (ds_read(&t) && t > -40.0f && t < 85.0f) temp_c = t;
	      ds_start();  ds_tick = now;
	    }

	    /* ---- turbidity calibration ---- */
	#if USE_DIAL
	    const float T_MUDDY = 200.0f;                // potentiometer: knob fully one way
	    const float T_CLEAR = 3900.0f;               // potentiometer: knob fully the other way
	#else
	    const float T_MUDDY = 750.0f;                // stirred muddy water (submerged)
	    static float T_CLEAR = 1150.0f;              // replaced by auto-calibration below
	    static float cal_sum = 0.0f;
	    static int   cal_n   = 0;
	    if (cal_n < 8)                               // first ~2 s after RESET: learn "clear water"
	    {
	      cal_sum += turbidity;
	      cal_n++;
	      if (cal_n == 8)
	      {
	        T_CLEAR = (cal_sum / 8.0f) * 0.95f;      // 5% margin below the clear reading
	        if (T_CLEAR < T_MUDDY + 150.0f) T_CLEAR = T_MUDDY + 150.0f;   // safety
	      }
	    }
	#endif

	    /* ---- power + battery budget ---- */
	    if (ina_ok) ina_read(&i_mA, &v_bus);
	    uint32_t dt = now - last_tick;  last_tick = now;
	    if (dt < 5000) used_mAh += i_mA * dt / 3600000.0f;
	    float remain = 1.0f - used_mAh / BUDGET_mAh;  if (remain < 0.0f) remain = 0.0f;
	    float e_mJ = i_mA * v_bus * dt / 1000.0f;
	    float factor = 1.0f, amp_scale = 1.0f, dur_scale = 1.0f;  const char *pw = "NORMAL ";
	    if      (remain < 0.2f) { factor = 4.0f; amp_scale = 0.6f; dur_scale = 0.5f;  pw = "SURVIVE"; }
	    else if (remain < 0.5f) { factor = 2.0f; amp_scale = 0.8f; dur_scale = 0.75f; pw = "ECONOMY"; }

	    /* ---- water physics ---- */
	    float ppm = tds_ppm(d_avg);
	    float c = sound_speed(temp_c, ppm / 1000.0f);
	    float listen_ms = 2.0f * R_MAX_m / c * 1000.0f;

	    /* ---- adaptation of the 3 wave parameters ---- */
	    int band = mode / 3, mod = mode % 3;
	    float t_norm = (t_avg - T_MUDDY) / (T_CLEAR - T_MUDDY);    // 0 = muddy, 1 = clear
	    if (t_norm < 0.0f) t_norm = 0.0f;
	    if (t_norm > 1.0f) t_norm = 1.0f;

	    float f0, f_hi;                                             // 1) BANDWIDTH / CENTRE FREQ
	    if (band == 0) { f0 = 2000.0f;   f_hi = 5000.0f   + t_norm * 12000.0f;  }   // 5-17 kHz
	    else           { f0 = 100000.0f; f_hi = 120000.0f + t_norm * 130000.0f; }   // 120-250 kHz
	    float f_max = 0.25f * sample_rate();  if (f_hi > f_max) f_hi = f_max;

	    float dur_ms = (4.0f + (1.0f - t_norm) * 12.0f) * dur_scale;               // 2) PULSE DURATION
	    uint32_t n = (uint32_t)(dur_ms * sample_rate() / 1000.0f);  if (n > N) n = N;

	    float amp = 1200.0f + d_avg * 0.2f;                                         // 3) AMPLITUDE
	    if (amp > 1800.0f) amp = 1800.0f;
	    amp *= amp_scale;

	    build_wave(mod, f0, f_hi, amp, n);

	    /* ---- report ---- */
	    char msg[240];
	    int len = snprintf(msg, sizeof msg,
	      "%s-%s T=%d.%dC c=%lu clarity=%lu (clear=%lu) tds=%luppm f=%lu-%luHz dur=%lums amp=%lu | I=%lu.%02lumA E=%lu.%lumJ batt=%lu%% %s\r\n",
	      band ? "US " : "AUD", mod_name[mod], (int)temp_c, abs((int)(temp_c * 10)) % 10, (uint32_t)c,
	      t_avg, (uint32_t)T_CLEAR, (uint32_t)ppm, (uint32_t)f0, (uint32_t)f_hi, (uint32_t)dur_ms, (uint32_t)amp,
	      (uint32_t)i_mA, (uint32_t)(i_mA * 100) % 100, (uint32_t)e_mJ, (uint32_t)(e_mJ * 10) % 10,
	      (uint32_t)(remain * 100), pw);
	    HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)msg, len, 50);

	    /* ---- transmit: DMA streams the ping, CPU sleeps ---- */
	    fire_ping(n);
	    BSP_LED_Toggle(LED_GREEN);
	    uint32_t t0 = HAL_GetTick();
	    GPIOE->BRR = GPIO_PIN_0;                     // CPU-busy pin LOW = CPU asleep
	    while (!ping_done && (HAL_GetTick() - t0) < 1000) { __WFI(); }
	    GPIOE->BSRR = GPIO_PIN_0;
	    if (!ping_done) BSP_LED_On(LED_RED);

	    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
	    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);   // park at mid-scale

	    /* ---- listen window (longer when the battery budget is low) ---- */
	    GPIOE->BRR = GPIO_PIN_0;
	    HAL_Delay((uint32_t)(listen_ms * factor));
	  }
	  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
  RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV4;
  RCC_OscInitStruct.PLL.PLLM = 3;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Power Configuration
  * @retval None
  */
static void SystemPower_Config(void)
{

  /*
   * Disable the internal Pull-Up in Dead Battery pins of UCPD peripheral
   */
  HAL_PWREx_DisableUCPDDeadBattery();

  /*
   * Switch to SMPS regulator instead of LDO
   */
  if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
/* USER CODE BEGIN PWR */
/* USER CODE END PWR */
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.GainCompensation = 0;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_68CYCLES;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};
  DAC_AutonomousModeConfTypeDef sAutonomousMode = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_ABOVE_80MHZ;
  sConfig.DAC_DMADoubleDataMode = DISABLE;
  sConfig.DAC_SignedFormat = DISABLE;
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Autonomous Mode
  */
  sAutonomousMode.AutonomousModeState = DAC_AUTONOMOUS_MODE_DISABLE;
  if (HAL_DACEx_SetConfigAutonomousMode(&hdac1, &sAutonomousMode) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */

  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief GPDMA1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPDMA1_Init(void)
{

  /* USER CODE BEGIN GPDMA1_Init 0 */

  /* USER CODE END GPDMA1_Init 0 */

  /* Peripheral clock enable */
  __HAL_RCC_GPDMA1_CLK_ENABLE();

  /* GPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);

  /* USER CODE BEGIN GPDMA1_Init 1 */

  /* USER CODE END GPDMA1_Init 1 */
  /* USER CODE BEGIN GPDMA1_Init 2 */

  /* USER CODE END GPDMA1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x30909DEC;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief ICACHE Initialization Function
  * @param None
  * @retval None
  */
static void MX_ICACHE_Init(void)
{

  /* USER CODE BEGIN ICACHE_Init 0 */

  /* USER CODE END ICACHE_Init 0 */

  /* USER CODE BEGIN ICACHE_Init 1 */

  /* USER CODE END ICACHE_Init 1 */

  /** Enable instruction cache in 1-way (direct mapped cache)
  */
  if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ICACHE_Enable() != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ICACHE_Init 2 */

  /* USER CODE END ICACHE_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 159;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 80;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 0;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 159;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, DAC_B2_Pin|DAC_B3_Pin|DAC_B4_Pin|DAC_B5_Pin
                          |DAC_B6_Pin|DAC_B7_Pin|DAC_B8_Pin|DAC_B9_Pin
                          |DAC_B10_Pin|DAC_B11_Pin|DAC_B0_Pin|DAC_B1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(DS18B20_GPIO_Port, DS18B20_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : DAC_B2_Pin DAC_B3_Pin DAC_B4_Pin DAC_B5_Pin
                           DAC_B6_Pin DAC_B7_Pin DAC_B8_Pin DAC_B9_Pin
                           DAC_B10_Pin DAC_B11_Pin DAC_B0_Pin DAC_B1_Pin */
  GPIO_InitStruct.Pin = DAC_B2_Pin|DAC_B3_Pin|DAC_B4_Pin|DAC_B5_Pin
                          |DAC_B6_Pin|DAC_B7_Pin|DAC_B8_Pin|DAC_B9_Pin
                          |DAC_B10_Pin|DAC_B11_Pin|DAC_B0_Pin|DAC_B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : DS18B20_Pin */
  GPIO_InitStruct.Pin = DS18B20_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DS18B20_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
  ping_done = 1;
}

void BSP_PB_Callback(Button_TypeDef Button)          // blue USER button cycles 6 modes
{
  mode = (mode + 1) % 6;
  if (mode < 3) BSP_LED_Off(LED_BLUE); else BSP_LED_On(LED_BLUE);   // BLUE = ultrasonic
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM17 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM17)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  BSP_LED_Init(LED_BLUE);
  BSP_LED_On(LED_BLUE);                      // BLUE steady from start = setup failed
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
