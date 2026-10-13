/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body - ASK Manchester Decoder Edition
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "log_message.h"
#include "wav_header.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// 44117 Hz / 100 bps = 441 samples per bit.
#define SAMPLES_PER_BIT 281
#define PREAMBLE_BITS   48         // 6 bytes of 00h = 48 bits
#define SYNC_BITS       16         // B2 4D = 16 bits
#define PAYLOAD_BITS    40         // 03 AB CD 90 35 = 5 bytes = 40 bits
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_rx;

SD_HandleTypeDef hsd;
DMA_HandleTypeDef hdma_sdio_rx;
DMA_HandleTypeDef hdma_sdio_tx;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
osThreadId_t ReadMicroHandle;
const osThreadAttr_t ReadMicro_attributes = {
  .name = "ReadMicro",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t SDAudioWriterHandle;
const osThreadAttr_t SDAudioWriter_attributes = {
  .name = "SDAudioWriter",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

osThreadId_t ManchesterDecoderHandle;
const osThreadAttr_t ManchesterDecoder_attributes = {
  .name = "ManchDecoder",
  .stack_size = 4096 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

#define AUDIO_BUFFER_SIZE 1024
#define SAFE_POOL_DEPTH 30

int16_t AudioSafePool[SAFE_POOL_DEPTH][AUDIO_BUFFER_SIZE / 2];
volatile uint8_t pool_write_idx = 0;

__attribute__((aligned(2))) int16_t AudioDMA_Buffer[AUDIO_BUFFER_SIZE];
__attribute__((aligned(2))) int16_t SDWriteAudioBuffer[AUDIO_BUFFER_SIZE];

osMessageQueueId_t AudioLogQueueHandle;
osMessageQueueId_t DecoderAudioQueueHandle;

osSemaphoreId_t SDMountedSem;
osSemaphoreId_t StartCaptureSem;

float agc_gain = 1.0f;
const float AGC_TARGET_PEAK = 10000.0f;
const float AGC_ATTACK = 0.05;
const float AGC_RELEASE = 0.01;

osMutexId_t SDMutexHandle;
const osMutexAttr_t SDMutex_attributes = {
  .name = "SDMutex"
};

// Globals required by log_message.h
osMessageQueueId_t LogQueueHandle = NULL;
osSemaphoreId_t LoggerReadySem = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SDIO_SD_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2S2_Init(void);
void StartReadMicro(void *argument);
void StartSDAudioWriter(void *argument);
void StartManchesterDecoder(void *argument);

/* USER CODE BEGIN PFP */
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, 100);
    return len;
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SDIO_SD_Init();
  MX_USART1_UART_Init();
  MX_FATFS_Init();
  MX_I2S2_Init();

  osKernelInitialize();

  SDMutexHandle = osMutexNew(&SDMutex_attributes);
  if (SDMutexHandle == NULL) Error_Handler();

  const osSemaphoreAttr_t SDMounted_attr = { .name = "SDMounted" };
  const osSemaphoreAttr_t StartCapture_attr = { .name = "StartCapture" };
  SDMountedSem = osSemaphoreNew(1, 0, &SDMounted_attr);
  StartCaptureSem = osSemaphoreNew(1, 0, &StartCapture_attr);
  if (SDMountedSem == NULL || StartCaptureSem == NULL) Error_Handler();

  AudioLogQueueHandle = osMessageQueueNew(30, sizeof(LogMessage_t), NULL);
  DecoderAudioQueueHandle = osMessageQueueNew(30, sizeof(LogMessage_t), NULL);
  if (AudioLogQueueHandle == NULL || DecoderAudioQueueHandle == NULL) Error_Handler();

  ReadMicroHandle = osThreadNew(StartReadMicro, NULL, &ReadMicro_attributes);
  SDAudioWriterHandle = osThreadNew(StartSDAudioWriter, NULL, &SDAudioWriter_attributes);
  ManchesterDecoderHandle = osThreadNew(StartManchesterDecoder, NULL, &ManchesterDecoder_attributes);

  osKernelStart();

  while (1) {}
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void MX_I2S2_Init(void)
{
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_RX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_44K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK) Error_Handler();
}

static void MX_SDIO_SD_Init(void)
{
  hsd.Instance = SDIO;
  hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
  hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
  hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
  hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
  hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd.Init.ClockDiv = 20;
  if (HAL_SD_Init(&hsd) != HAL_OK) Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
}

/* USER CODE BEGIN 4 */
volatile uint32_t irq_count = 0;

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &hi2s2)
    {
        irq_count++;
        LogMessage_t msg;
        msg.type = MSG_TYPE_AUDIO;

        uint8_t current_idx = pool_write_idx;
        pool_write_idx++;
        if (pool_write_idx >= SAFE_POOL_DEPTH) pool_write_idx = 0;

        memcpy(AudioSafePool[current_idx], AudioDMA_Buffer, (AUDIO_BUFFER_SIZE / 2) * sizeof(int16_t));

        msg.data.audio.bufferPtr = AudioSafePool[current_idx];
        msg.data.audio.sampleCount = AUDIO_BUFFER_SIZE / 2;

        osMessageQueuePut(AudioLogQueueHandle, &msg, 0, 0);
        osMessageQueuePut(DecoderAudioQueueHandle, &msg, 0, 0);
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &hi2s2)
    {
        irq_count++;
        LogMessage_t msg;
        msg.type = MSG_TYPE_AUDIO;

        uint8_t current_idx = pool_write_idx;
        pool_write_idx++;
        if (pool_write_idx >= SAFE_POOL_DEPTH) pool_write_idx = 0;

        memcpy(AudioSafePool[current_idx], &AudioDMA_Buffer[AUDIO_BUFFER_SIZE / 2], (AUDIO_BUFFER_SIZE / 2) * sizeof(int16_t));

        msg.data.audio.bufferPtr = AudioSafePool[current_idx];
        msg.data.audio.sampleCount = AUDIO_BUFFER_SIZE / 2;

        osMessageQueuePut(AudioLogQueueHandle, &msg, 0, 0);
        osMessageQueuePut(DecoderAudioQueueHandle, &msg, 0, 0);
    }
}
/* USER CODE END 4 */

void StartReadMicro(void *argument)
{
    printf(">>> Audio: Waiting...\r\n");
    if (osSemaphoreAcquire(StartCaptureSem, osWaitForever) == osOK) {
        printf(">>> Audio: Starting DMA...\r\n");
        if (HAL_I2S_Receive_DMA(&hi2s2, (uint16_t*)AudioDMA_Buffer, AUDIO_BUFFER_SIZE) != HAL_OK) {
            Error_Handler();
        }
    }
    while(1) { osDelay(1000); }
}

void StartSDAudioWriter(void *argument)
{
    printf(">>> SD Writer: Task started.\r\n");
	LogMessage_t msg;
	FRESULT fres;
	UINT bytesWritten;
	char filename[32] = "Recording.WAV";

	const uint32_t SAMPLE_RATE = 44117;
	const uint16_t BITS_PER_SAMPLE = 16;
	const uint16_t CHANNELS = 1;
	const uint32_t RECORD_DURATION_SEC = 15;
	const uint32_t targetSamples = RECORD_DURATION_SEC * SAMPLE_RATE;

	WavHeader header;
	WAV_InitHeader(&header, SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS);

    printf(">>> SD Writer: Attempting to mount SD card...\r\n");
	if (f_mount(&SDFatFS, (TCHAR const*)SDPath, 1) != FR_OK) {
        printf(">>> SD Writer: MOUNT FAILED! Entering Error_Handler.\r\n");
        Error_Handler();
    }
    printf(">>> SD Writer: SD Card mounted successfully.\r\n");
    osSemaphoreRelease(SDMountedSem);

    printf(">>> SD Writer: Opening WAV file...\r\n");
	osMutexAcquire(SDMutexHandle, osWaitForever);
	fres = f_open(&SDFile, filename, FA_CREATE_ALWAYS | FA_WRITE);
	if (fres != FR_OK) {
        printf(">>> SD Writer: FILE OPEN FAILED! Error code: %d\r\n", fres);
        osMutexRelease(SDMutexHandle);
        Error_Handler();
    }

    printf(">>> SD Writer: Writing WAV header...\r\n");
	fres = f_write(&SDFile, &header, sizeof(header), &bytesWritten);
	osMutexRelease(SDMutexHandle);

    printf(">>> SD Writer: Releasing StartCapture semaphore.\r\n");
    osSemaphoreRelease(StartCaptureSem);

	uint32_t totalSamplesRecorded = 0;

	for(;;)
	{
		if (osMessageQueueGet(AudioLogQueueHandle, &msg, NULL, osWaitForever) == osOK)
		{
			if (msg.type == MSG_TYPE_AUDIO)
			{
				int16_t* src16 = (int16_t*)msg.data.audio.bufferPtr;
				uint16_t monoCount = msg.data.audio.sampleCount / 2;

				float peak = 0.0f;
				for(uint32_t i = 0; i < monoCount; i++) {
					float val = fabsf((float)src16[i * 2]);
					if (val > peak) peak = val;
				}
				if (peak < 1.0f) peak = 1.0f;
				float desired_gain = AGC_TARGET_PEAK / peak;
				if (desired_gain < agc_gain) agc_gain = (agc_gain * (1.0f - AGC_ATTACK)) + (desired_gain * AGC_ATTACK);
				else agc_gain = (agc_gain * (1.0f - AGC_RELEASE)) + (desired_gain * AGC_RELEASE);

				for(uint32_t i = 0; i < monoCount; i++) {
					float val = (float)src16[i * 2] * agc_gain;
					if (val > 32767.0f) val = 32767.0f;
					if (val < -32768.0f) val = -32768.0f;
					SDWriteAudioBuffer[i] = (int16_t)val;
				}

				uint32_t byteSize = monoCount * 2;
				osMutexAcquire(SDMutexHandle, osWaitForever);
				fres = f_write(&SDFile, SDWriteAudioBuffer, byteSize, &bytesWritten);
				osMutexRelease(SDMutexHandle);

				if (fres == FR_OK && bytesWritten == byteSize) {
					totalSamplesRecorded += monoCount;
					if (totalSamplesRecorded >= targetSamples) break;
				}
			}
		}
	}

	WAV_UpdateHeaderSizes(&header, totalSamplesRecorded);
	f_lseek(&SDFile, 0);
	f_write(&SDFile, &header, sizeof(header), &bytesWritten);
	f_close(&SDFile);
    printf(">>> SD Writer: Recording finished.\r\n");
	HAL_I2S_DMAStop(&hi2s2);
	osThreadTerminate(osThreadGetId());
}

/* ========================================================================
   MANCHESTER DECODER THREAD
   ======================================================================== */

// Optimized to only search 16 bits of the preamble to save CPU/RAM
int find_initial_offset(float* signal, int len) {
    int best_offset = 0;
    float max_score = -1e9f;
    int half = SAMPLES_PER_BIT / 2;
    int search_bits = 16;

    for (int offset = 0; offset < SAMPLES_PER_BIT; offset++) {
        float score = 0.0f;
        int valid_bits = 0;

        for (int b = 0; b < search_bits; b++) {
            int start = offset + b * SAMPLES_PER_BIT;
            int mid = start + half;
            int end = start + SAMPLES_PER_BIT;

            if (end >= len) break;

            float sum1 = 0, sum2 = 0;
            for (int i = 0; i < half; i++) {
                sum1 += signal[start + i];
                sum2 += signal[mid + i];
            }

            float metric = fabsf((sum1 / half) - (sum2 / half));
            score += metric;
            valid_bits++;
        }

        if (valid_bits > 0) {
            score /= valid_bits;
            if (score > max_score) {
                max_score = score;
                best_offset = offset;
            }
        }
    }
    return best_offset;
}

void StartManchesterDecoder(void *argument)
{
    LogMessage_t msg;
    FRESULT fres;
    UINT bw;
    FIL DecodedFile, RefFile;

    osSemaphoreAcquire(SDMountedSem, osWaitForever);

    osMutexAcquire(SDMutexHandle, osWaitForever);
    fres = f_open(&RefFile, "reference.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (fres == FR_OK) {
        char ref_str[] = "Expected Hex: B2 4D 03 AB CD 90 35\n";
        f_write(&RefFile, ref_str, strlen(ref_str), &bw);
        f_close(&RefFile);
    }

    fres = f_open(&DecodedFile, "decoded_bits.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        osMutexRelease(SDMutexHandle);
        Error_Handler();
    }

    char header_str[] = "SampleIdx | Status | Decoded Hex\n";
    f_write(&DecodedFile, header_str, strlen(header_str), &bw);
    osMutexRelease(SDMutexHandle);

    printf(">>> Decoder: Ready. Waiting for audio...\r\n");

    float temp_float_buffer[AUDIO_BUFFER_SIZE / 2];
    int samples_in_current_bit = 0;
    float sum1 = 0.0f, sum2 = 0.0f;
    int half_bit = SAMPLES_PER_BIT / 2;

    // ASK Envelope Detector State
    float env = 0.0f;
    const float ENV_ALPHA = 0.98f;   // ~1.5 ms smoothing at 44.1 kHz

    // Phase Acquisition Buffer (20 bits is plenty to lock phase, saves RAM)
    #define ACQ_BITS 20
    #define ACQ_SAMPLES (ACQ_BITS * SAMPLES_PER_BIT)
    static float acq_buf[ACQ_SAMPLES];
    static uint32_t acq_fill = 0;

    uint32_t bit_shift_reg = 0;
    int decoded_bits_count = 0;
    uint8_t payload_bytes[5] = {0};
    int payload_byte_idx = 0;
    int payload_bit_idx = 0;

    enum { STATE_SEARCH_SYNC, STATE_READ_PAYLOAD } state = STATE_SEARCH_SYNC;
    uint32_t global_sample_idx = 0;
    bool phase_locked = false;
    int locked_offset = 0;

    for(;;)
    {
        if (osMessageQueueGet(DecoderAudioQueueHandle, &msg, NULL, osWaitForever) == osOK)
        {
            if (msg.type == MSG_TYPE_AUDIO)
            {
                int16_t* src16 = (int16_t*)msg.data.audio.bufferPtr;
                uint16_t stereoCount = msg.data.audio.sampleCount;
                uint16_t monoCount = stereoCount / 2;

                for(uint32_t i = 0; i < monoCount; i++) {
                    float x = (float)src16[i * 2];

                    // Envelope detector: rectify + exponential moving average
                    env = ENV_ALPHA * env + (1.0f - ENV_ALPHA) * fabsf(x);
                    float processed = env;

                    if (!phase_locked) {
                        if (acq_fill < ACQ_SAMPLES) {
                            acq_buf[acq_fill++] = processed;
                        }
                        if (acq_fill == ACQ_SAMPLES) {
                            locked_offset = find_initial_offset(acq_buf, ACQ_SAMPLES);
                            samples_in_current_bit = (ACQ_SAMPLES - locked_offset) % SAMPLES_PER_BIT;
                            phase_locked = true;
                            printf(">>> Decoder: Phase locked! Offset = %d\r\n", locked_offset);
                        }
                    } else {
                        if (samples_in_current_bit < half_bit) {
                            sum1 += processed;
                        } else {
                            sum2 += processed;
                        }
                        samples_in_current_bit++;
                        global_sample_idx++;

                        if (samples_in_current_bit == SAMPLES_PER_BIT) {
                            float metric = (sum2 / half_bit) - (sum1 / half_bit);
                            uint8_t bit = (metric >= 0.0f) ? 1 : 0;

                            bit_shift_reg = (bit_shift_reg << 1) | bit;
                            decoded_bits_count++;

                            if (state == STATE_SEARCH_SYNC) {
                                if ((bit_shift_reg & 0xFFFF) == 0xB24D) {
                                    printf(">>> Decoder: SYNC FOUND at sample %lu!\r\n", global_sample_idx);
                                    state = STATE_READ_PAYLOAD;
                                    payload_byte_idx = 0;
                                    payload_bit_idx = 0;
                                    memset(payload_bytes, 0, sizeof(payload_bytes));
                                }
                            }
                            else if (state == STATE_READ_PAYLOAD) {
                                payload_bytes[payload_byte_idx] = (payload_bytes[payload_byte_idx] << 1) | bit;
                                payload_bit_idx++;

                                if (payload_bit_idx == 8) {
                                    payload_bit_idx = 0;
                                    payload_byte_idx++;

                                    if (payload_byte_idx >= 5) {
                                        char log_str[128];
                                        int len = snprintf(log_str, sizeof(log_str), "%lu | SUCCESS | %02X %02X %02X %02X %02X\n",
                                                           global_sample_idx,
                                                           payload_bytes[0], payload_bytes[1], payload_bytes[2],
                                                           payload_bytes[3], payload_bytes[4]);

                                        osMutexAcquire(SDMutexHandle, osWaitForever);
                                        f_write(&DecodedFile, log_str, len, &bw);
                                        osMutexRelease(SDMutexHandle);

                                        printf(">>> Decoder: Payload: %02X %02X %02X %02X %02X\r\n",
                                               payload_bytes[0], payload_bytes[1], payload_bytes[2],
                                               payload_bytes[3], payload_bytes[4]);

                                        state = STATE_SEARCH_SYNC;
                                    }
                                }
                            }

                            samples_in_current_bit = 0;
                            sum1 = 0.0f;
                            sum2 = 0.0f;
                        }
                    }
                }
            }
        }
    }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif /* USE_FULL_ASSERT */
