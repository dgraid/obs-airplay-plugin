#include "../common/ipc.hpp"
#include "audio_decoder.hpp"
#include "video_decoder.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dns_sd.h>
#include <ifaddrs.h>
#include <mutex>
#include <net/if_dl.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctime>
#include <unistd.h>
#include <vector>

extern "C" {
#include "dnssd.h"
#include "logger.h"
#include "raop.h"
#include "stream.h"
}

using airplay_ipc::Header;
using airplay_ipc::MsgType;
using airplay_ipc::State;

static std::mutex g_mu;
static int g_fd = -1;
static uint32_t g_generation = 1;
static volatile sig_atomic_t g_run = 1;
static raop_t *g_raop = nullptr;
static dnssd_t *g_dnssd = nullptr;
static VideoDecoder g_vdec;
static AudioDecoder g_adec;
static std::string g_name = "OBS AirPlay";
static std::atomic<uint64_t> g_last_video_ns{0};
static std::atomic<uint32_t> g_ipc_state{(uint32_t)State::Starting};

static uint64_t now_ns() {
  using namespace std::chrono;
  return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool send_msg(MsgType type, uint64_t ts, uint32_t w, uint32_t h, uint32_t sr, uint32_t ch,
                     const void *data, uint32_t bytes) {
  Header hdr{};
  hdr.magic = airplay_ipc::kMagic;
  hdr.version = airplay_ipc::kVersion;
  hdr.type = (uint32_t)type;
  hdr.generation = g_generation;
  hdr.timestamp_ns = ts;
  hdr.width = w;
  hdr.height = h;
  hdr.pixel_format = (uint32_t)airplay_ipc::PixelFormat::BGRA;
  hdr.sample_rate = sr;
  hdr.channels = ch;
  hdr.bytes = bytes;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_fd < 0)
    return false;
  if (write(g_fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
    return false;
  if (bytes && write(g_fd, data, bytes) != (ssize_t)bytes)
    return false;
  return true;
}

static void send_state(State st) {
  uint32_t v = (uint32_t)st;
  if (g_ipc_state.exchange(v, std::memory_order_relaxed) == v)
    return;
  send_msg(MsgType::State, now_ns(), 0, 0, 0, 0, &v, sizeof(v));
}

static std::string random_mac() {
  char buf[32];
  unsigned octet = (unsigned)(rand() % 64);
  octet = (octet << 1) | 2; // locally administered unicast
  snprintf(buf, sizeof(buf), "%02x", octet);
  std::string mac = buf;
  for (int i = 1; i < 6; ++i) {
    snprintf(buf, sizeof(buf), ":%02x", rand() % 256);
    mac += buf;
  }
  return mac;
}

static std::string find_mac() {
  ifaddrs *ifap = nullptr;
  if (getifaddrs(&ifap) != 0)
    return {};
  std::string mac;
  for (auto *p = ifap; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_LINK)
      continue;
    auto *sdl = (sockaddr_dl *)p->ifa_addr;
    if (sdl->sdl_alen < 6)
      continue;
    auto *a = (unsigned char *)LLADDR(sdl);
    int nz = 0;
    for (int i = 0; i < 6; ++i)
      if (a[i])
        nz++;
    if (!nz)
      continue;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
    mac = buf;
    break;
  }
  freeifaddrs(ifap);
  return mac;
}

static void parse_mac(const std::string &s, std::vector<char> &out) {
  out.clear();
  for (size_t i = 0; i + 1 < s.size(); i += 3)
    out.push_back((char)strtol(s.c_str() + i, nullptr, 16));
}

static void on_log(void *, int, const char *msg) {
  if (msg)
    fprintf(stderr, "[helper] %s\n", msg);
}

static void conn_init(void *) { send_state(State::Connecting); }
static void conn_destroy(void *) { send_state(State::Discoverable); }
static void conn_reset(void *, int) { send_state(State::Disconnected); }
static void conn_teardown(void *, bool *, bool *) {}
static void audio_flush(void *) {}
static void video_flush(void *) { g_vdec.reset_session(); }
static void video_pause(void *) {
  fprintf(stderr, "[helper] video_pause (client sleep)\n");
  send_state(State::Paused);
}
static void video_resume(void *) {
  fprintf(stderr, "[helper] video_resume (client wake)\n");
}
static void conn_feedback(void *) {
  if (g_ipc_state.load(std::memory_order_relaxed) != (uint32_t)State::Streaming)
    return;
  uint64_t last = g_last_video_ns.load(std::memory_order_relaxed);
  if (last == 0)
    return;
  uint64_t now = now_ns();
  if (now >= last && now - last >= 2500000000ull) {
    fprintf(stderr, "[helper] video stall >=2.5s, pause\n");
    send_state(State::Paused);
  }
}
static void video_reset(void *, reset_type_t) { g_vdec.reset_session(); }
static double audio_set_client_volume(void *) { return 0.0; }
static void audio_set_volume(void *, float volume) {
  // Screen Mirroring: iPhone hardware volume is not capture level. OBS pad + mixer.
  fprintf(stderr, "[helper] SET_PARAMETER volume ignored: %f dB\n", volume);
}
static void audio_set_metadata(void *, const void *, int) {}
static void audio_set_coverart(void *, const void *, int) {}
static void audio_stop_coverart_rendering(void *) {}
static void audio_remote_control_id(void *, const char *, const char *) {}
static void audio_set_progress(void *, uint32_t *, uint32_t *, uint32_t *) {}
static void audio_get_format(void *, unsigned char *, unsigned short *, bool *, bool *, uint64_t *) {}
static void video_report_size(void *, float *, float *, float *, float *) {}
static void mirror_video_running(void *, bool running) {
  fprintf(stderr, "[helper] mirror_video_running=%d\n", (int)running);
}
static void report_client_request(void *, char *deviceid, char *model, char *name, bool *admit) {
  fprintf(stderr, "[helper] client request id=%s model=%s name=%s\n",
          deviceid ? deviceid : "?", model ? model : "?", name ? name : "?");
  if (admit)
    *admit = true;
}
static void display_pin(void *, char *pin) {
  fprintf(stderr, "[helper] pairing pin=%s\n", pin ? pin : "?");
}
static void register_client(void *, const char *, const char *, const char *) {}
static bool check_register(void *, const char *) { return true; }
static const char *passwd(void *, int *len) {
  if (len)
    *len = 0;
  return nullptr;
}
static void export_dacp(void *, const char *, const char *) {}
static int video_set_codec(void *, video_codec_t codec) {
  return (codec == VIDEO_CODEC_H264 || codec == VIDEO_CODEC_H265) ? 0 : -1;
}
static void on_video_play(void *, const char *, float) {}
static void on_video_scrub(void *, float) {}
static void on_video_rate(void *, float) {}
static void on_video_stop(void *) {}
static void on_video_acquire_playback_info(void *, playback_info_t *info) {
  if (info)
    memset(info, 0, sizeof(*info));
}
static float on_video_playlist_remove(void *) { return 0.f; }

static void DNSSD_API browse_airplay(DNSServiceRef, DNSServiceFlags flags, uint32_t, DNSServiceErrorType err,
                                     const char *name, const char *, const char *, void *) {
  if (err || !(flags & kDNSServiceFlagsAdd) || !name)
    return;
  if (g_name == name)
    return;
  fprintf(stderr, "[helper] other _airplay._tcp: %s (picker will show it too; not a protocol lock)\n",
          name);
}

static void warn_other_receivers() {
  DNSServiceRef ref = nullptr;
  if (DNSServiceBrowse(&ref, 0, 0, "_airplay._tcp", "local.", browse_airplay, nullptr) !=
      kDNSServiceErr_NoError)
    return;
  int fd = DNSServiceRefSockFD(ref);
  for (int i = 0; i < 8; ++i) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{0, 250000};
    int n = select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (n > 0)
      DNSServiceProcessResult(ref);
  }
  DNSServiceRefDeallocate(ref);
}

static void audio_process(void *, raop_ntp_t *, audio_decode_struct *data) {
  if (!data || !data->data || data->data_len <= 0)
    return;
  std::vector<int16_t> pcm;
  int sr = 44100, ch = 2;
  if (!g_adec.decode({data->data, (size_t)data->data_len}, pcm, sr, ch))
    return;
  send_msg(MsgType::Audio, now_ns(), 0, 0, (uint32_t)sr, (uint32_t)ch, pcm.data(),
           (uint32_t)(pcm.size() * sizeof(int16_t)));
}

static std::atomic<uint32_t> g_decode_fails{0};

static void video_process(void *, raop_ntp_t *, video_decode_struct *data) {
  if (!data || !data->data || data->data_len <= 0)
    return;
  std::vector<uint8_t> bgra;
  int w = 0, h = 0;
  DecodeDiag diag{};
  if (!g_vdec.decode({data->data, (size_t)data->data_len}, data->is_h265, bgra, w, h, &diag)) {
    uint32_t n = g_decode_fails.fetch_add(1, std::memory_order_relaxed);
    if (n < 8)
      fprintf(stderr, "[helper] video decode failed (len=%d h265=%d nal=%d vt=%d recre=%d)\n",
              data->data_len, (int)data->is_h265, diag.vcl_nal, diag.vt_status,
              (int)diag.params_recreated);
    return;
  }
  g_decode_fails.store(0, std::memory_order_relaxed);
  g_last_video_ns.store(now_ns(), std::memory_order_relaxed);
  send_state(State::Streaming);
  send_msg(MsgType::Video, now_ns(), (uint32_t)w, (uint32_t)h, 0, 0, bgra.data(),
           (uint32_t)bgra.size());
}

static void on_sig(int) { g_run = 0; }

int main(int argc, char **argv) {
  std::string sock_path;
  int max_w = 1920, max_h = 1080, max_fps = 30;
  bool use_random_mac = true;
  std::string mac_arg;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](int &dst) {
      if (i + 1 < argc)
        dst = atoi(argv[++i]);
    };
    if (a == "--socket" && i + 1 < argc)
      sock_path = argv[++i];
    else if (a == "--name" && i + 1 < argc)
      g_name = argv[++i];
    else if (a == "--generation" && i + 1 < argc)
      g_generation = (uint32_t)atoi(argv[++i]);
    else if (a == "--max-width")
      next(max_w);
    else if (a == "--max-height")
      next(max_h);
    else if (a == "--max-fps")
      next(max_fps);
    else if (a == "--mac" && i + 1 < argc)
      mac_arg = argv[++i];
    else if (a == "--system-mac")
      use_random_mac = false;
  }
  if (sock_path.empty()) {
    fprintf(stderr, "usage: AirPlayReceiverHelper --socket PATH [--name NAME]\n");
    return 2;
  }

  signal(SIGTERM, on_sig);
  signal(SIGINT, on_sig);
  signal(SIGPIPE, SIG_IGN);
  srand((unsigned)(time(nullptr) ^ getpid()));

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return 1;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path.c_str());
  if (connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("connect");
    return 1;
  }
  g_fd = fd;
  send_state(State::Starting);

  std::string mac = mac_arg;
  if (mac.empty())
    mac = use_random_mac ? std::string() : find_mac();
  if (mac.empty())
    mac = random_mac();
  std::vector<char> hw;
  parse_mac(mac, hw);

  int err = 0;
  g_dnssd = dnssd_init(g_name.c_str(), (int)g_name.size(), hw.data(), (int)hw.size(), &err, 0);
  if (!g_dnssd || err) {
    fprintf(stderr, "dnssd_init failed %d\n", err);
    send_state(State::Failed);
    return 1;
  }
  dnssd_set_airplay_features(g_dnssd, 7, 1);
  dnssd_set_airplay_features(g_dnssd, 9, 1);
  dnssd_set_airplay_features(g_dnssd, 27, 1);
  dnssd_set_airplay_features(g_dnssd, 30, 1);
  dnssd_set_airplay_features(g_dnssd, 0, 0);
  dnssd_set_airplay_features(g_dnssd, 4, 0);
  dnssd_set_airplay_features(g_dnssd, 42, 1);

  raop_callbacks_t cbs{};
  cbs.cls = nullptr;
  cbs.audio_process = audio_process;
  cbs.video_process = video_process;
  cbs.video_pause = video_pause;
  cbs.video_resume = video_resume;
  cbs.conn_feedback = conn_feedback;
  cbs.conn_reset = conn_reset;
  cbs.video_reset = video_reset;
  cbs.conn_init = conn_init;
  cbs.conn_destroy = conn_destroy;
  cbs.conn_teardown = conn_teardown;
  cbs.audio_flush = audio_flush;
  cbs.video_flush = video_flush;
  cbs.audio_set_client_volume = audio_set_client_volume;
  cbs.audio_set_volume = audio_set_volume;
  cbs.audio_set_metadata = audio_set_metadata;
  cbs.audio_set_coverart = audio_set_coverart;
  cbs.audio_stop_coverart_rendering = audio_stop_coverart_rendering;
  cbs.audio_remote_control_id = audio_remote_control_id;
  cbs.audio_set_progress = audio_set_progress;
  cbs.audio_get_format = audio_get_format;
  cbs.video_report_size = video_report_size;
  cbs.mirror_video_running = mirror_video_running;
  cbs.report_client_request = report_client_request;
  cbs.display_pin = display_pin;
  cbs.register_client = register_client;
  cbs.check_register = check_register;
  cbs.passwd = passwd;
  cbs.export_dacp = export_dacp;
  cbs.video_set_codec = video_set_codec;
  cbs.on_video_play = on_video_play;
  cbs.on_video_scrub = on_video_scrub;
  cbs.on_video_rate = on_video_rate;
  cbs.on_video_stop = on_video_stop;
  cbs.on_video_acquire_playback_info = on_video_acquire_playback_info;
  cbs.on_video_playlist_remove = on_video_playlist_remove;

  g_raop = raop_init(&cbs);
  if (!g_raop) {
    send_state(State::Failed);
    return 1;
  }
  raop_set_log_callback(g_raop, on_log, nullptr);
  raop_set_log_level(g_raop, LOGGER_INFO);

  char keyfile[256];
  snprintf(keyfile, sizeof(keyfile), "/tmp/obs-airplay-%d.key", (int)getpid());
  if (raop_init2(g_raop, 1, mac.c_str(), keyfile) != 0) {
    fprintf(stderr, "raop_init2 failed\n");
    send_state(State::Failed);
    return 1;
  }
  raop_set_plist(g_raop, "width", max_w);
  raop_set_plist(g_raop, "height", max_h);
  raop_set_plist(g_raop, "refreshRate", 60);
  raop_set_plist(g_raop, "maxFPS", max_fps);
  unsigned short tcp[3] = {0, 0, 0};
  unsigned short udp[3] = {0, 0, 0};
  raop_set_tcp_ports(g_raop, tcp);
  raop_set_udp_ports(g_raop, udp);
  unsigned short port = raop_get_port(g_raop);
  raop_start_httpd(g_raop, &port);
  raop_set_port(g_raop, port);
  raop_set_dnssd(g_raop, g_dnssd);

  if (dnssd_register_raop(g_dnssd, port) != 0) {
    fprintf(stderr, "dnssd_register_raop failed\n");
    send_state(State::Failed);
    return 1;
  }
  if (dnssd_register_airplay(g_dnssd, port) != 0) {
    fprintf(stderr, "dnssd_register_airplay failed\n");
    send_state(State::Failed);
    return 1;
  }
  fprintf(stderr, "[helper] advertising '%s' raop=%u airplay=%u mac=%s\n", g_name.c_str(), port, port,
          mac.c_str());
  send_state(State::Discoverable);
  warn_other_receivers();

  while (g_run)
    sleep(1);

  send_state(State::Disconnected);
  dnssd_unregister_raop(g_dnssd);
  dnssd_unregister_airplay(g_dnssd);
  raop_destroy(g_raop);
  dnssd_destroy(g_dnssd);
  close(fd);
  unlink(keyfile);
  return 0;
}
