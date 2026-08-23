#include "audio_decoder.hpp"

#include <fdk-aac/aacdecoder_lib.h>

#include <cstdio>

AudioDecoder::AudioDecoder() : decoder_(aacDecoder_Open(TT_MP4_RAW, 1)), frame_(8192) {
  UCHAR conf[] = {0xF8, 0xE8, 0x50, 0x00}; // AAC-ELD 44.1 kHz stereo (AirPlay mirror default)
  UCHAR *ptr[1] = {conf};
  UINT length = 4;
  if (decoder_)
    aacDecoder_ConfigRaw(decoder_, ptr, &length);
}

AudioDecoder::~AudioDecoder() {
  if (decoder_)
    aacDecoder_Close(decoder_);
}

bool AudioDecoder::decode(std::span<const uint8_t> data, std::vector<int16_t> &pcm, int &sample_rate,
                          int &channels) {
  if (!decoder_ || data.empty())
    return false;
  UINT valid = (UINT)data.size();
  uint8_t *d[2] = {const_cast<uint8_t *>(data.data()), nullptr};
  UINT size[2] = {(UINT)data.size(), 0};
  if (aacDecoder_Fill(decoder_, d, size, &valid) != AAC_DEC_OK)
    return false;
  if (aacDecoder_DecodeFrame(decoder_, frame_.data(), (INT)frame_.size(), 0) != AAC_DEC_OK)
    return false;
  CStreamInfo *info = aacDecoder_GetStreamInfo(decoder_);
  if (!info || info->numChannels <= 0 || info->frameSize <= 0)
    return false;
  sample_rate = info->sampleRate;
  channels = info->numChannels;
  const int samples = info->numChannels * info->frameSize;
  pcm.assign(frame_.data(), frame_.data() + samples);
  return true;
}
