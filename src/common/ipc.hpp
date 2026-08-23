#pragma once

#include <cstdint>
#include <cstring>

namespace airplay_ipc {

inline constexpr uint32_t kMagic = 0x41505243; // 'APRC'
inline constexpr uint32_t kVersion = 1;

enum class MsgType : uint32_t {
  Hello = 1,
  State = 2,
  Video = 3,
  Audio = 4,
  Log = 5,
};

enum class State : uint32_t {
  Starting = 1,
  Discoverable = 2,
  Connecting = 3,
  Streaming = 4,
  Disconnected = 5,
  Failed = 6,
  Paused = 7,
};

enum class PixelFormat : uint32_t {
  BGRA = 1,
};

#pragma pack(push, 1)
struct Header {
  uint32_t magic;
  uint32_t version;
  uint32_t type;
  uint32_t generation;
  uint64_t timestamp_ns;
  uint32_t width;
  uint32_t height;
  uint32_t pixel_format;
  uint32_t sample_rate;
  uint32_t channels;
  uint32_t bytes;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 48, "ipc header size");

inline bool valid(const Header &h) {
  return h.magic == kMagic && h.version == kVersion && h.bytes < (64u * 1024u * 1024u);
}

} // namespace airplay_ipc
