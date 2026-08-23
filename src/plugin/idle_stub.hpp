#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct IdleStubCopy {
  std::string subtitle;
  std::string device_label;
  std::string how_label;
  std::string how_value;
  std::string how_hint;
  std::string network_label;
  std::string network_value;
  std::string footer;
};

struct PauseStubCopy {
  std::string header;
  std::string body;
};

std::vector<uint8_t> render_idle_stub(uint32_t w, uint32_t h, const std::string &receiver_name,
                                      const IdleStubCopy &copy);

std::vector<uint8_t> render_pause_stub(uint32_t w, uint32_t h, const PauseStubCopy &copy);

void letterbox_bgra(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh);
