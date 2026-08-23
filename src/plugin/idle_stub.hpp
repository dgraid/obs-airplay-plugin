#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct IdleStubCopy {
  std::string header;
  std::string step1;
  std::string step2_prefix;
  std::string step2;
  std::string step2_hint_a;
  std::string step2_hint_b;
  std::string step3_prefix;
  std::string step3_suffix;
  std::string step3_hint;
};

struct PauseStubCopy {
  std::string header;
  std::string body;
};

std::vector<uint8_t> render_idle_stub(uint32_t w, uint32_t h, const std::string &receiver_name,
                                      const IdleStubCopy &copy);

std::vector<uint8_t> render_pause_stub(uint32_t w, uint32_t h, const PauseStubCopy &copy);

void letterbox_bgra(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh);
