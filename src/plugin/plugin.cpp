#include "../common/ipc.hpp"
#include "idle_stub.hpp"
#include "module_settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <callback/calldata.h>
#include <callback/proc.h>
#include <callback/signal.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-airplay", "en-US")

MODULE_EXPORT const char *obs_module_description(void) {
  return "AirPlay Receiver (helper process)";
}

using airplay_ipc::Header;
using airplay_ipc::MsgType;
using airplay_ipc::State;

namespace {

constexpr double kAudioGainDbMin = -24.0;
constexpr double kAudioGainDbMax = 0.0;
constexpr double kAudioGainDbDefault = -6.0;
constexpr size_t kAirPlayNameMaxBytes = 63;

std::string airplay_name_from_utf8(const char *raw) {
  std::string s = raw ? raw : "";
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  if (s.empty())
    s = "AirPlay Receiver";
  if (s.size() <= kAirPlayNameMaxBytes)
    return s;
  size_t i = kAirPlayNameMaxBytes;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
    --i;
  s.resize(i);
  return s;
}

std::string airplay_name_from_source(obs_source_t *src) {
  return airplay_name_from_utf8(src ? obs_source_get_name(src) : nullptr);
}

float audio_gain_lin_from_db(double db) {
  db = std::clamp(db, kAudioGainDbMin, kAudioGainDbMax);
  return std::pow(10.0f, static_cast<float>(db) / 20.0f);
}

const char *state_name(uint32_t s) {
  switch ((State)s) {
  case State::Starting:
    return "starting";
  case State::Discoverable:
    return "discoverable";
  case State::Connecting:
    return "connecting";
  case State::Streaming:
    return "streaming";
  case State::Disconnected:
    return "disconnected";
  case State::Failed:
    return "failed";
  case State::Paused:
    return "paused";
  }
  return "unknown";
}

bool state_is_live(uint32_t st) { return st == (uint32_t)State::Streaming; }
bool state_is_connected(uint32_t st) {
  return st == (uint32_t)State::Streaming || st == (uint32_t)State::Paused;
}

std::string helper_path() {
  const char *plug = obs_get_module_binary_path(obs_current_module());
  if (!plug)
    return {};
  std::string p = plug;
  auto pos = p.rfind("/Contents/MacOS/");
  if (pos == std::string::npos)
    return {};
  return p.substr(0, pos) + "/Contents/Resources/AirPlayReceiverHelper.app/Contents/MacOS/AirPlayReceiverHelper";
}

bool read_full(int fd, void *buf, size_t n) {
  auto *p = (uint8_t *)buf;
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fd, p + got, n - got);
    if (r == 0)
      return false;
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    got += (size_t)r;
  }
  return true;
}

std::string make_mac() {
  char buf[32];
  unsigned octet = arc4random_uniform(64);
  octet = (octet << 1) | 2;
  snprintf(buf, sizeof(buf), "%02x", octet);
  std::string mac = buf;
  for (int i = 1; i < 6; ++i) {
    snprintf(buf, sizeof(buf), ":%02x", arc4random_uniform(256));
    mac += buf;
  }
  return mac;
}

void log_helper_stderr(int fd) {
  char buf[512];
  std::string line;
  while (true) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0)
      break;
    line.append(buf, (size_t)n);
    size_t pos;
    while ((pos = line.find('\n')) != std::string::npos) {
      std::string one = line.substr(0, pos);
      if (!one.empty() && one.back() == '\r')
        one.pop_back();
      if (!one.empty())
        blog(LOG_INFO, "[obs-airplay] %s", one.c_str());
      line.erase(0, pos + 1);
    }
  }
  if (!line.empty())
    blog(LOG_INFO, "[obs-airplay] %s", line.c_str());
  close(fd);
}

IdleStubCopy stub_copy() {
  IdleStubCopy c;
  if (language_is_en(module_settings().language)) {
    c.header = "Follow the instructions below on your iPhone or iPad:";
    c.step1 = "1. Connect to the same network as this Mac";
    c.step2_prefix = "2. Tap ";
    c.step2 = "Screen Mirroring";
    c.step2_hint_a = "Location: swipe down from the top-right corner of the screen";
    c.step2_hint_b = "For iOS 11 and earlier: swipe up from the bottom";
    c.step3_prefix = "3. Select ";
    c.step3_hint = "Item not showing? Restart the device";
  } else {
    c.header = "Соблюдайте приведенные ниже инструкции на вашем iPhone или iPad:";
    c.step1 = "1. Подключитесь к той же сети, что и это устройство Mac";
    c.step2_prefix = "2. Нажмите ";
    c.step2 = "Повтор экрана";
    c.step2_hint_a = "Расположение: смахните от правого верхнего угла экрана вниз";
    c.step2_hint_b = "Для ОС iOS 11 и более ранних версий: смахните снизу вверх";
    c.step3_prefix = "3. Выберите ";
    c.step3_hint = "Не отображается элемент? Перезагрузите устройство";
  }
  return c;
}

void canvas_size(int max_w, int max_h, uint32_t &w, uint32_t &h) {
  obs_video_info ovi{};
  if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
    w = ovi.base_width;
    h = ovi.base_height;
    return;
  }
  w = (uint32_t)std::max(640, max_w);
  h = (uint32_t)std::max(360, max_h);
}

struct StatusJob {
  obs_weak_source_t *weak = nullptr;
  bool connected = false;
  bool notify = false;
};

void status_job_run(void *p) {
  auto *j = static_cast<StatusJob *>(p);
  obs_source_t *src = obs_weak_source_get_source(j->weak);
  obs_weak_source_release(j->weak);
  if (src) {
    if (j->notify) {
      calldata cd;
      uint8_t stack[256];
      calldata_init_fixed(&cd, stack, sizeof(stack));
      calldata_set_ptr(&cd, "source", src);
      calldata_set_bool(&cd, "connected", j->connected);
      signal_handler_signal(obs_get_signal_handler(), "airplay_status", &cd);
      signal_handler_signal(obs_source_get_signal_handler(src), "airplay_status", &cd);
    }
    obs_source_update_properties(src);
    obs_source_release(src);
  }
  if (j->notify) {
    const std::string &want =
        j->connected ? module_settings().on_connect_scene : module_settings().on_disconnect_scene;
    if (!want.empty()) {
      obs_source_t *scene = obs_get_source_by_name(want.c_str());
      if (scene) {
        if (obs_source_get_type(scene) == OBS_SOURCE_TYPE_SCENE) {
          obs_source_t *cur = obs_frontend_get_current_scene();
          const char *cur_name = cur ? obs_source_get_name(cur) : nullptr;
          bool same = cur_name && want == cur_name;
          if (cur)
            obs_source_release(cur);
          if (!same)
            obs_frontend_set_current_scene(scene);
        }
        obs_source_release(scene);
      }
    }
  }
  delete j;
}

struct Source {
  obs_source_t *source = nullptr;
  std::string name = "AirPlay Receiver";
  std::string device_mac;
  int max_w = 1920, max_h = 1080, max_fps = 30;
  bool audio = true;
  bool auto_restart = true;
  bool low_latency = true;
  std::atomic<float> audio_gain_lin{0.501187f}; // -6 dB

  std::mutex mu;
  int listen_fd = -1;
  int conn_fd = -1;
  pid_t helper_pid = -1;
  std::string sock_path;
  uint32_t generation = 1;
  std::atomic<bool> run{false};
  std::atomic<uint32_t> last_state{(uint32_t)State::Disconnected};
  std::atomic<bool> last_connected{false};
  std::thread reader;
  std::thread supervisor;
  std::thread log_thread;
  uint32_t width = 16, height = 16;
  std::chrono::steady_clock::time_point last_start;
  int restarts = 0;
  std::vector<uint8_t> frame;
  std::vector<float> pcm;

  void close_fds() {
    if (conn_fd >= 0)
      close(conn_fd);
    conn_fd = -1;
    if (listen_fd >= 0)
      close(listen_fd);
    listen_fd = -1;
    if (!sock_path.empty())
      unlink(sock_path.c_str());
  }

  void stop_helper() {
    pid_t pid = helper_pid;
    helper_pid = -1;
    if (pid > 0) {
      kill(pid, SIGTERM);
      for (int i = 0; i < 20; ++i) {
        int st = 0;
        if (waitpid(pid, &st, WNOHANG) == pid)
          return;
        usleep(50000);
      }
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
    }
  }

  bool listen_socket() {
    close_fds();
    sock_path = "/tmp/obs-airplay-" + std::to_string(getpid()) + "-" + std::to_string((uintptr_t)this) + ".sock";
    unlink(sock_path.c_str());
    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0)
      return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path.c_str());
    if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) != 0)
      return false;
    if (listen(listen_fd, 1) != 0)
      return false;
    return true;
  }

  bool spawn() {
    std::string hp = helper_path();
    if (hp.empty() || access(hp.c_str(), X_OK) != 0) {
      blog(LOG_ERROR, "[obs-airplay] helper missing: %s", hp.c_str());
      return false;
    }
    if (!listen_socket()) {
      blog(LOG_ERROR, "[obs-airplay] socket listen failed");
      return false;
    }
    generation++;
    std::string gen = std::to_string(generation);
    std::string mw = std::to_string(max_w);
    std::string mh = std::to_string(max_h);
    std::string mf = std::to_string(max_fps);
    if (device_mac.empty())
      device_mac = make_mac();
    const char *argv[] = {hp.c_str(),
                          "--socket",
                          sock_path.c_str(),
                          "--name",
                          name.c_str(),
                          "--generation",
                          gen.c_str(),
                          "--max-width",
                          mw.c_str(),
                          "--max-height",
                          mh.c_str(),
                          "--max-fps",
                          mf.c_str(),
                          "--mac",
                          device_mac.c_str(),
                          nullptr};
    int errpipe[2];
    if (pipe(errpipe) != 0) {
      blog(LOG_ERROR, "[obs-airplay] stderr pipe failed");
      return false;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, errpipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, errpipe[0]);
    posix_spawn_file_actions_addclose(&fa, errpipe[1]);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, hp.c_str(), &fa, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(errpipe[1]);
    if (rc != 0) {
      close(errpipe[0]);
      blog(LOG_ERROR, "[obs-airplay] posix_spawn failed: %s", strerror(rc));
      return false;
    }
    if (log_thread.joinable())
      log_thread.join();
    log_thread = std::thread(log_helper_stderr, errpipe[0]);
    helper_pid = pid;
    last_start = std::chrono::steady_clock::now();
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    timeval tv{3, 0};
    int sel = select(listen_fd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) {
      blog(LOG_ERROR, "[obs-airplay] helper did not connect");
      return false;
    }
    conn_fd = accept(listen_fd, nullptr, nullptr);
    if (conn_fd < 0)
      return false;
    blog(LOG_INFO, "[obs-airplay] helper pid=%d name=\"%s\" socket=%s gen=%u", (int)pid, name.c_str(),
         sock_path.c_str(), generation);
    return true;
  }

  void publish_status(bool connected) {
    bool prev = last_connected.exchange(connected);
    auto *job = new StatusJob();
    job->weak = obs_source_get_weak_source(source);
    job->connected = connected;
    job->notify = prev != connected;
    obs_queue_task(OBS_TASK_UI, status_job_run, job, false);
  }

  void push_stub() {
    uint32_t cw = 0, ch = 0;
    canvas_size(max_w, max_h, cw, ch);
    auto pixels = render_idle_stub(cw, ch, name, stub_copy());
    if (pixels.empty())
      return;
    std::lock_guard<std::mutex> lock(mu);
    frame = std::move(pixels);
    width = cw;
    height = ch;
    obs_source_frame f{};
    f.data[0] = frame.data();
    f.linesize[0] = cw * 4;
    f.width = cw;
    f.height = ch;
    f.format = VIDEO_FORMAT_BGRA;
    f.timestamp = os_gettime_ns();
    f.full_range = true;
    obs_source_output_video(source, &f);
  }

  void apply_state(uint32_t st) {
    uint32_t prev = last_state.exchange(st);
    if (prev == st)
      return;
    blog(LOG_INFO, "[obs-airplay] state %s", state_name(st));
    bool live = state_is_live(st);
    bool connected = state_is_connected(st);
    publish_status(connected);
    if (!live)
      push_stub();
  }

  void output_video(const Header &h, const std::vector<uint8_t> &bgra) {
    if (h.width == 0 || h.height == 0)
      return;
    if (bgra.size() < (size_t)h.width * h.height * 4)
      return;
    uint32_t cw = 0, ch = 0;
    canvas_size(max_w, max_h, cw, ch);
    std::lock_guard<std::mutex> lock(mu);
    frame.resize((size_t)cw * ch * 4);
    letterbox_bgra(bgra.data(), h.width, h.height, frame.data(), cw, ch);
    width = cw;
    height = ch;
    obs_source_frame f{};
    f.data[0] = frame.data();
    f.linesize[0] = cw * 4;
    f.width = cw;
    f.height = ch;
    f.format = VIDEO_FORMAT_BGRA;
    f.timestamp = h.timestamp_ns;
    f.full_range = true;
    obs_source_output_video(source, &f);
  }

  void output_audio(const Header &h, const std::vector<uint8_t> &bytes) {
    if (!audio || h.sample_rate == 0 || h.channels == 0)
      return;
    const uint32_t ch = h.channels;
    const size_t n = bytes.size() / sizeof(int16_t);
    if (n == 0 || n % ch != 0)
      return;
    const float scale = audio_gain_lin.load(std::memory_order_relaxed) * (1.0f / 32768.0f);
    const auto *in = reinterpret_cast<const int16_t *>(bytes.data());
    pcm.resize(n);
    for (size_t i = 0; i < n; ++i)
      pcm[i] = (float)in[i] * scale;
    obs_source_audio a{};
    a.data[0] = reinterpret_cast<uint8_t *>(pcm.data());
    a.frames = (uint32_t)(n / ch);
    a.speakers = ch >= 2 ? SPEAKERS_STEREO : SPEAKERS_MONO;
    a.samples_per_sec = h.sample_rate;
    a.format = AUDIO_FORMAT_FLOAT;
    a.timestamp = h.timestamp_ns;
    obs_source_output_audio(source, &a);
  }

  void reader_loop() {
    while (run.load()) {
      int fd = conn_fd;
      if (fd < 0)
        break;
      Header h{};
      if (!read_full(fd, &h, sizeof(h)) || !airplay_ipc::valid(h))
        break;
      std::vector<uint8_t> payload(h.bytes);
      if (h.bytes && !read_full(fd, payload.data(), h.bytes))
        break;
      if (h.generation != 0 && h.generation != generation)
        continue;
      auto t = (MsgType)h.type;
      if (t == MsgType::State && payload.size() >= 4) {
        uint32_t st = 0;
        memcpy(&st, payload.data(), 4);
        apply_state(st);
      } else if (t == MsgType::Video) {
        output_video(h, payload);
      } else if (t == MsgType::Audio) {
        output_audio(h, payload);
      }
    }
    last_state.store((uint32_t)State::Disconnected);
    publish_status(false);
    if (conn_fd >= 0) {
      close(conn_fd);
      conn_fd = -1;
    }
    if (run.load())
      push_stub();
  }

  bool helper_alive() {
    if (helper_pid <= 0)
      return false;
    int st = 0;
    pid_t r = waitpid(helper_pid, &st, WNOHANG);
    if (r == helper_pid) {
      helper_pid = -1;
      return false;
    }
    if (kill(helper_pid, 0) != 0) {
      helper_pid = -1;
      return false;
    }
    return true;
  }

  void supervisor_loop() {
    while (run.load()) {
      if (conn_fd < 0 || !helper_alive()) {
        stop_helper();
        close_fds();
        if (!auto_restart) {
          apply_state((uint32_t)State::Failed);
          break;
        }
        auto now = std::chrono::steady_clock::now();
        if (now - last_start < std::chrono::seconds(30))
          restarts++;
        else
          restarts = 0;
        if (restarts > 10) {
          blog(LOG_ERROR, "[obs-airplay] restart budget exceeded");
          apply_state((uint32_t)State::Failed);
          break;
        }
        int delay_ms = 500 << std::min(restarts, 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        if (!run.load())
          break;
        if (spawn()) {
          if (reader.joinable())
            reader.join();
          reader = std::thread([this] { reader_loop(); });
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  void start() {
    if (run.load())
      return;
    name = airplay_name_from_source(source);
    run = true;
    apply_state((uint32_t)State::Starting);
    if (spawn())
      reader = std::thread([this] { reader_loop(); });
    supervisor = std::thread([this] { supervisor_loop(); });
  }

  void stop() {
    run = false;
    if (conn_fd >= 0)
      shutdown(conn_fd, SHUT_RDWR);
    stop_helper();
    close_fds();
    if (reader.joinable())
      reader.join();
    if (supervisor.joinable())
      supervisor.join();
    if (log_thread.joinable())
      log_thread.join();
    last_state.store((uint32_t)State::Disconnected);
    publish_status(false);
    push_stub();
  }

  void on_receiver_name(const std::string &n) {
    if (n.empty() || n == name)
      return;
    bool restart = run.load();
    if (restart)
      stop();
    name = n;
    if (restart)
      start();
    else if (last_state.load() != (uint32_t)State::Streaming)
      push_stub();
  }

  void tick_canvas() {
    uint32_t cw = 0, ch = 0;
    canvas_size(max_w, max_h, cw, ch);
    if (cw == width && ch == height)
      return;
    if (last_state.load() != (uint32_t)State::Streaming)
      push_stub();
    else {
      width = cw;
      height = ch;
    }
  }
};

void on_source_rename(void *data, calldata_t *cd) {
  auto *s = static_cast<Source *>(data);
  s->on_receiver_name(airplay_name_from_utf8(calldata_string(cd, "new_name")));
}

std::mutex g_sources_mu;
std::vector<Source *> g_sources;

void proc_get_airplay_status(void *data, calldata_t *cd) {
  auto *s = static_cast<Source *>(data);
  bool on = state_is_connected(s->last_state.load());
  calldata_set_bool(cd, "connected", on);
}

const char *get_name(void *) { return obs_module_text("AirPlay"); }

void *create(obs_data_t *settings, obs_source_t *source) {
  auto *s = new Source();
  s->source = source;
  s->name = airplay_name_from_source(source);
  s->max_w = (int)obs_data_get_int(settings, "max_width");
  s->max_h = (int)obs_data_get_int(settings, "max_height");
  s->max_fps = (int)obs_data_get_int(settings, "max_fps");
  s->audio = obs_data_get_bool(settings, "audio");
  s->auto_restart = obs_data_get_bool(settings, "auto_restart");
  s->low_latency = obs_data_get_bool(settings, "low_latency");
  s->audio_gain_lin.store(audio_gain_lin_from_db(obs_data_get_double(settings, "audio_gain_db")),
                          std::memory_order_relaxed);
  if (s->max_w <= 0)
    s->max_w = 1920;
  if (s->max_h <= 0)
    s->max_h = 1080;
  if (s->max_fps <= 0)
    s->max_fps = 30;
  const char *mac = obs_data_get_string(settings, "device_mac");
  if (!mac || !*mac) {
    s->device_mac = make_mac();
    obs_data_set_string(settings, "device_mac", s->device_mac.c_str());
  } else {
    s->device_mac = mac;
  }

  signal_handler_add(obs_source_get_signal_handler(source), "void airplay_status(bool connected)");
  proc_handler_add(obs_source_get_proc_handler(source), "void get_airplay_status(out bool connected)",
                   proc_get_airplay_status, s);
  signal_handler_connect(obs_source_get_signal_handler(source), "rename", on_source_rename, s);

  {
    std::lock_guard<std::mutex> lock(g_sources_mu);
    g_sources.push_back(s);
  }
  s->push_stub();
  return s;
}

void activate(void *data) {
  auto *s = (Source *)data;
  if (s->last_state.load() != (uint32_t)State::Streaming)
    s->push_stub();
  if (!s->run.load())
    s->start();
}

void deactivate(void *data) { ((Source *)data)->stop(); }

void destroy(void *data) {
  auto *s = (Source *)data;
  signal_handler_disconnect(obs_source_get_signal_handler(s->source), "rename", on_source_rename, s);
  {
    std::lock_guard<std::mutex> lock(g_sources_mu);
    g_sources.erase(std::remove(g_sources.begin(), g_sources.end(), s), g_sources.end());
  }
  s->stop();
  delete s;
}

uint32_t get_width(void *data) { return ((Source *)data)->width; }
uint32_t get_height(void *data) { return ((Source *)data)->height; }

void video_tick(void *data, float) { ((Source *)data)->tick_canvas(); }

void get_defaults(obs_data_t *d) {
  obs_data_set_default_int(d, "max_width", 1920);
  obs_data_set_default_int(d, "max_height", 1080);
  obs_data_set_default_int(d, "max_fps", 30);
  obs_data_set_default_bool(d, "audio", true);
  obs_data_set_default_double(d, "audio_gain_db", kAudioGainDbDefault);
  obs_data_set_default_bool(d, "auto_restart", true);
  obs_data_set_default_bool(d, "low_latency", true);
}

obs_properties_t *get_properties(void *data) {
  auto *s = (Source *)data;
  obs_properties_t *p = obs_properties_create();
  uint32_t st = s ? s->last_state.load() : (uint32_t)State::Disconnected;
  const char *status_key = "Status.Waiting";
  if (st == (uint32_t)State::Paused)
    status_key = "Status.Paused";
  else if (state_is_live(st))
    status_key = "Status.Connected";
  obs_properties_add_text(p, "connection_status", obs_module_text(status_key), OBS_TEXT_INFO);
  obs_properties_add_text(p, "name_hint", obs_module_text("Prop.NameHint"), OBS_TEXT_INFO);
  obs_properties_add_int(p, "max_width", obs_module_text("Prop.MaxWidth"), 640, 3840, 2);
  obs_properties_add_int(p, "max_height", obs_module_text("Prop.MaxHeight"), 360, 2160, 2);
  obs_properties_add_int(p, "max_fps", obs_module_text("Prop.MaxFps"), 15, 60, 1);
  obs_properties_add_bool(p, "audio", obs_module_text("Prop.Audio"));
  obs_properties_add_float_slider(p, "audio_gain_db", obs_module_text("Prop.AudioGain"), kAudioGainDbMin,
                                  kAudioGainDbMax, 0.5);
  obs_properties_add_text(p, "audio_gain_help", obs_module_text("Prop.AudioGainHelp"), OBS_TEXT_INFO);
  obs_properties_add_bool(p, "auto_restart", obs_module_text("Prop.AutoRestart"));
  obs_properties_add_bool(p, "low_latency", obs_module_text("Prop.LowLatency"));
  return p;
}

void save_settings(void *, obs_data_t *settings) {
  obs_data_unset_user_value(settings, "connection_status");
  obs_data_unset_user_value(settings, "audio_gain_help");
  obs_data_unset_user_value(settings, "name_hint");
}

void update(void *data, obs_data_t *settings) {
  auto *s = (Source *)data;
  int mw = (int)obs_data_get_int(settings, "max_width");
  int mh = (int)obs_data_get_int(settings, "max_height");
  int mf = (int)obs_data_get_int(settings, "max_fps");
  bool audio = obs_data_get_bool(settings, "audio");
  bool ar = obs_data_get_bool(settings, "auto_restart");
  bool ll = obs_data_get_bool(settings, "low_latency");
  s->audio_gain_lin.store(audio_gain_lin_from_db(obs_data_get_double(settings, "audio_gain_db")),
                          std::memory_order_relaxed);
  bool restart = mw != s->max_w || mh != s->max_h || mf != s->max_fps || ll != s->low_latency;
  s->max_w = mw;
  s->max_h = mh;
  s->max_fps = mf;
  s->audio = audio;
  s->auto_restart = ar;
  s->low_latency = ll;
  if (restart && s->run.load()) {
    s->stop();
    s->start();
  } else if (s->last_state.load() != (uint32_t)State::Streaming) {
    s->push_stub();
  }
}

obs_source_info make_info() {
  obs_source_info info{};
  info.id = "airplay_receiver";
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
  info.get_name = get_name;
  info.create = create;
  info.destroy = destroy;
  info.activate = activate;
  info.deactivate = deactivate;
  info.get_width = get_width;
  info.get_height = get_height;
  info.video_tick = video_tick;
  info.get_defaults = get_defaults;
  info.get_properties = get_properties;
  info.update = update;
  info.save = save_settings;
  info.icon_type = OBS_ICON_TYPE_DESKTOP_CAPTURE;
  return info;
}

} // namespace

bool airplay_any_connected() {
  std::lock_guard<std::mutex> lock(g_sources_mu);
  for (auto *s : g_sources) {
    uint32_t st = s->last_state.load();
    if (st == (uint32_t)State::Streaming || st == (uint32_t)State::Paused)
      return true;
  }
  return false;
}

bool airplay_any_paused() {
  std::lock_guard<std::mutex> lock(g_sources_mu);
  for (auto *s : g_sources) {
    if (s->last_state.load() == (uint32_t)State::Paused)
      return true;
  }
  return false;
}

void airplay_refresh_idle_stubs() {
  std::vector<Source *> copy;
  {
    std::lock_guard<std::mutex> lock(g_sources_mu);
    copy = g_sources;
  }
  for (auto *s : copy) {
    if (s->last_state.load() != (uint32_t)State::Streaming)
      s->push_stub();
  }
}

bool obs_module_load(void) {
  module_settings_load();
  signal_handler_add(obs_get_signal_handler(), "void airplay_status(ptr source, bool connected)");
  static obs_source_info info = make_info();
  obs_register_source(&info);
  tools_dialog_register();
  blog(LOG_INFO, "[obs-airplay] loaded (helper+plugin, UxPlay 1.73.6)");
  return true;
}

void obs_module_unload(void) {}
