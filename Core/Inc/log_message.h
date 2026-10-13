/*
 * log_message.h
 *
 *  Created on: Apr 26, 2026
 *      Author: VaSAR
 */
/* Core/Inc/log_message.h */
#ifndef __LOG_MESSAGE_H_
#define __LOG_MESSAGE_H_

#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    MSG_TYPE_AUDIO,
    MSG_TYPE_IMU
} MessageType_t;

// Структура для IMU-данных
typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    uint32_t timestamp;  // Временная метка в миллисекундах
} ImuData_t;

// Расширенная структура сообщения для очереди
typedef struct {
    MessageType_t type;  // Тип сообщения (аудио или IMU)
    union {
        struct {
            int16_t* bufferPtr;  // Указатель на буфер аудио
            uint16_t sampleCount; // Количество сэмплов
        } audio;
        ImuData_t imu;  // IMU-данные
    } data;
} LogMessage_t;

// Shared Globals
extern osMessageQueueId_t LogQueueHandle;
extern FIL SDFile;
extern FATFS SDFatFS;
extern char SDPath[4];
extern osSemaphoreId_t LoggerReadySem;

#endif
