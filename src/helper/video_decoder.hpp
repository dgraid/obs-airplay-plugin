#pragma once

#include <cstdint>
#include <span>
#include <vector>

struct DecodeDiag {
  int vcl_nal = -1;
  int vt_status = 0;
  bool params_recreated = false;
};

class VideoDecoder {
public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;

  // Annex-B H.264 or HEVC. Returns packed BGRA on success.
  bool decode(std::span<const uint8_t> annexb, bool hevc, std::vector<uint8_t> &bgra, int &width,
              int &height, DecodeDiag *diag = nullptr);

  // Drop VT/FFmpeg session; keep SPS/PPS/VPS so an IDR after pause can rebuild.
  void reset_session();

private:
  struct Impl;
  Impl *impl_;
};
