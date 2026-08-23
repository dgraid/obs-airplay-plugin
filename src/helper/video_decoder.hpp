#pragma once

#include <cstdint>
#include <span>
#include <vector>

class VideoDecoder {
public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;

  // Annex-B H.264 or HEVC. Returns packed BGRA on success.
  bool decode(std::span<const uint8_t> annexb, bool hevc, std::vector<uint8_t> &bgra,
              int &width, int &height);

private:
  struct Impl;
  Impl *impl_;
};
