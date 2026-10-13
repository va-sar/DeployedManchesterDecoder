#include "wav_header.h"

void WAV_InitHeader(WavHeader* header, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels)
{
    memset(header, 0, sizeof(WavHeader));

    // RIFF-заголовок
    memcpy(header->ChunkID, "RIFF", 4);
    header->ChunkSize = 0; // Будет заполнено позже
    memcpy(header->Format, "WAVE", 4);

    // Формат-подзаголовок
    memcpy(header->Subchunk1ID, "fmt ", 4);
    header->Subchunk1Size = 16;
    header->AudioFormat = 1; // PCM
    header->NumChannels = channels;
    header->SampleRate = sampleRate;
    header->BitsPerSample = bitsPerSample;
    header->BlockAlign = header->NumChannels * header->BitsPerSample / 8;
    header->ByteRate = header->SampleRate * header->BlockAlign;

    // Подзаголовок данных (пока размер неизвестен)
    memcpy(header->Subchunk2ID, "data", 4);
    header->Subchunk2Size = 0;
}

void WAV_UpdateHeaderSizes(WavHeader* header, uint32_t totalSamples)
{
    header->Subchunk2Size = totalSamples * header->BlockAlign;
    header->ChunkSize = 36 + header->Subchunk2Size; // 36 = размер заголовка без "RIFF" и ChunkSize
}
