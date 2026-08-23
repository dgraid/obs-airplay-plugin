#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct IdleStubCopy {
  std::string sub1;
  std::string sub2;
  std::string card_title;
  std::string step1;
  std::string step1_hint;
  std::string step2;
  std::string step2_hint;
  std::string step3;
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

void contain_blit_bgra(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh,
                       int32_t rx, int32_t ry, uint32_t rw, uint32_t rh);

void contain_blend_bgra(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh,
                        int32_t rx, int32_t ry, uint32_t rw, uint32_t rh, uint32_t a_256);
