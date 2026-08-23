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

  // Drop VT/FFmpeg session; keep SPS/PPS/VPS so an IDR after pause can rebuild.
  void reset_session();

private:
  struct Impl;
  Impl *impl_;
};
