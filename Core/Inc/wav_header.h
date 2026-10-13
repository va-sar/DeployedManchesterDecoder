#ifndef WAV_HEADER_H
#define WAV_HEADER_H

#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    char ChunkID[4];      // "RIFF"
    uint32_t ChunkSize;   // Размер всего файла минус 8 байт
    char Format[4];       // "WAVE"

    char Subchunk1ID[4];  // "fmt "
    uint32_t Subchunk1Size; // 16 для PCM
    uint16_t AudioFormat; // 1 для PCM
    uint16_t NumChannels; // 1 = моно, 2 = стерео
    uint32_t SampleRate;  // Например, 44100
    uint32_t ByteRate;    // SampleRate * NumChannels * BitsPerSample/8
    uint16_t BlockAlign;  // NumChannels * BitsPerSample/8
    uint16_t BitsPerSample; // 16 для 16-битного аудио

    char Subchunk2ID[4];  // "data"
    uint32_t Subchunk2Size; // NumSamples * NumChannels * BitsPerSample/8
} WavHeader;
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализирует WAV-заголовок с заданными параметрами
 * @param header Указатель на структуру заголовка
 * @param sampleRate Частота дискретизации
 * @param bitsPerSample Битовая глубина
 * @param channels Количество каналов (1=моно, 2=стерео)
 */
void WAV_InitHeader(WavHeader* header, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels);

/**
 * @brief Обновляет размеры в WAV-заголовке после записи данных
 * @param header Указатель на структуру заголовка
 * @param totalSamples Общее количество записанных сэмплов
 */
void WAV_UpdateHeaderSizes(WavHeader* header, uint32_t totalSamples);

#ifdef __cplusplus
}
#endif

#endif /* WAV_HEADER_H */
