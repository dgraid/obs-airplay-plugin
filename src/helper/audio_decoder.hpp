#pragma once

#include <cstdint>
#include <span>
#include <vector>

class AudioDecoder {
public:
  AudioDecoder();
  ~AudioDecoder();
  AudioDecoder(const AudioDecoder &) = delete;
  AudioDecoder &operator=(const AudioDecoder &) = delete;

  bool decode(std::span<const uint8_t> data, std::vector<int16_t> &pcm, int &sample_rate,
              int &channels);

private:
  struct AAC_DECODER_INSTANCE *decoder_ = nullptr;
  std::vector<int16_t> frame_;
};
