#include "../common/ipc.hpp"

#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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
  }
  return "unknown";
}

std::string helper_path() {
  const char *plug = obs_get_module_binary_path(obs_current_module());
  if (!plug)
    return {};
  // .../obs-airplay.plugin/Contents/MacOS/obs-airplay
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

struct Source {
  obs_source_t *source = nullptr;
  std::string name = "OBS AirPlay";
  int max_w = 1920, max_h = 1080, max_fps = 30;
  bool audio = true;
  bool auto_restart = true;
  bool low_latency = true;

  std::mutex mu;
  int listen_fd = -1;
  int conn_fd = -1;
  pid_t helper_pid = -1;
  std::string sock_path;
  uint32_t generation = 1;
  std::atomic<bool> run{false};
  std::atomic<uint32_t> last_state{(uint32_t)State::Disconnected};
  std::thread reader;
  std::thread supervisor;
  uint32_t width = 16, height = 16;
  std::chrono::steady_clock::time_point last_start;
  int restarts = 0;
  std::vector<uint8_t> frame;
  std::vector<uint8_t> pcm;

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
    sock_path = "/tmp/obs-airplay-" + std::to_string(getpid()) + "-" +
                std::to_string((uintptr_t)this) + ".sock";
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
                          nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, hp.c_str(), nullptr, nullptr, (char *const *)argv, environ) != 0) {
      blog(LOG_ERROR, "[obs-airplay] posix_spawn failed: %s", strerror(errno));
      return false;
    }
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
    blog(LOG_INFO, "[obs-airplay] helper pid=%d socket=%s gen=%u", (int)pid, sock_path.c_str(),
         generation);
    return true;
  }

  void output_video(const Header &h, const std::vector<uint8_t> &bgra) {
    if (h.width == 0 || h.height == 0)
      return;
    if (bgra.size() < (size_t)h.width * h.height * 4)
      return;
    width = h.width;
    height = h.height;
    obs_source_frame f{};
    f.data[0] = (uint8_t *)bgra.data();
    f.linesize[0] = h.width * 4;
    f.width = h.width;
    f.height = h.height;
    f.format = VIDEO_FORMAT_BGRA;
    f.timestamp = h.timestamp_ns;
    f.full_range = true;
    obs_source_output_video(source, &f);
  }

  void output_audio(const Header &h, const std::vector<uint8_t> &bytes) {
    if (!audio || h.sample_rate == 0 || h.channels == 0)
      return;
    obs_source_audio a{};
    a.data[0] = (uint8_t *)bytes.data();
    a.frames = (uint32_t)(bytes.size() / (sizeof(int16_t) * h.channels));
    a.speakers = h.channels >= 2 ? SPEAKERS_STEREO : SPEAKERS_MONO;
    a.samples_per_sec = h.sample_rate;
    a.format = AUDIO_FORMAT_16BIT;
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
        last_state.store(st);
        blog(LOG_INFO, "[obs-airplay] state %s", state_name(st));
      } else if (t == MsgType::Video) {
        output_video(h, payload);
      } else if (t == MsgType::Audio) {
        output_audio(h, payload);
      }
    }
    last_state.store((uint32_t)State::Disconnected);
    if (conn_fd >= 0) {
      close(conn_fd);
      conn_fd = -1;
    }
    obs_source_output_video(source, nullptr);
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
          last_state.store((uint32_t)State::Failed);
          break;
        }
        auto now = std::chrono::steady_clock::now();
        if (now - last_start < std::chrono::seconds(30))
          restarts++;
        else
          restarts = 0;
        if (restarts > 10) {
          blog(LOG_ERROR, "[obs-airplay] restart budget exceeded");
          last_state.store((uint32_t)State::Failed);
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
    run = true;
    last_state.store((uint32_t)State::Starting);
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
    last_state.store((uint32_t)State::Disconnected);
  }
};

const char *get_name(void *) { return "AirPlay Receiver"; }

void *create(obs_data_t *settings, obs_source_t *source) {
  auto *s = new Source();
  s->source = source;
  const char *n = obs_data_get_string(settings, "server_name");
  s->name = (n && *n) ? n : "OBS AirPlay";
  s->max_w = (int)obs_data_get_int(settings, "max_width");
  s->max_h = (int)obs_data_get_int(settings, "max_height");
  s->max_fps = (int)obs_data_get_int(settings, "max_fps");
  s->audio = obs_data_get_bool(settings, "audio");
  s->auto_restart = obs_data_get_bool(settings, "auto_restart");
  s->low_latency = obs_data_get_bool(settings, "low_latency");
  if (s->max_w <= 0)
    s->max_w = 1920;
  if (s->max_h <= 0)
    s->max_h = 1080;
  if (s->max_fps <= 0)
    s->max_fps = 30;
  return s;
}

void activate(void *data) {
  auto *s = (Source *)data;
  if (!s->run.load())
    s->start();
}

void deactivate(void *data) { ((Source *)data)->stop(); }

void destroy(void *data) {
  auto *s = (Source *)data;
  s->stop();
  delete s;
}

uint32_t get_width(void *data) { return ((Source *)data)->width; }
uint32_t get_height(void *data) { return ((Source *)data)->height; }

void get_defaults(obs_data_t *d) {
  obs_data_set_default_string(d, "server_name", "OBS AirPlay");
  obs_data_set_default_int(d, "max_width", 1920);
  obs_data_set_default_int(d, "max_height", 1080);
  obs_data_set_default_int(d, "max_fps", 30);
  obs_data_set_default_bool(d, "audio", true);
  obs_data_set_default_bool(d, "auto_restart", true);
  obs_data_set_default_bool(d, "low_latency", true);
}

obs_properties_t *get_properties(void *) {
  obs_properties_t *p = obs_properties_create();
  obs_properties_add_text(p, "server_name", "Receiver name", OBS_TEXT_DEFAULT);
  obs_properties_add_int(p, "max_width", "Max width", 640, 3840, 2);
  obs_properties_add_int(p, "max_height", "Max height", 360, 2160, 2);
  obs_properties_add_int(p, "max_fps", "Max FPS", 15, 60, 1);
  obs_properties_add_bool(p, "audio", "Audio");
  obs_properties_add_bool(p, "auto_restart", "Restart helper on crash");
  obs_properties_add_bool(p, "low_latency", "Low-latency preset");
  return p;
}

void update(void *data, obs_data_t *settings) {
  auto *s = (Source *)data;
  const char *n = obs_data_get_string(settings, "server_name");
  std::string name = (n && *n) ? n : "OBS AirPlay";
  int mw = (int)obs_data_get_int(settings, "max_width");
  int mh = (int)obs_data_get_int(settings, "max_height");
  int mf = (int)obs_data_get_int(settings, "max_fps");
  bool audio = obs_data_get_bool(settings, "audio");
  bool ar = obs_data_get_bool(settings, "auto_restart");
  bool ll = obs_data_get_bool(settings, "low_latency");
  bool restart = name != s->name || mw != s->max_w || mh != s->max_h || mf != s->max_fps || ll != s->low_latency;
  s->name = name;
  s->max_w = mw;
  s->max_h = mh;
  s->max_fps = mf;
  s->audio = audio;
  s->auto_restart = ar;
  s->low_latency = ll;
  if (restart && s->run.load()) {
    s->stop();
    s->start();
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
  info.get_defaults = get_defaults;
  info.get_properties = get_properties;
  info.update = update;
  info.icon_type = OBS_ICON_TYPE_DESKTOP_CAPTURE;
  return info;
}

} // namespace

bool obs_module_load(void) {
  static obs_source_info info = make_info();
  obs_register_source(&info);
  blog(LOG_INFO, "[obs-airplay] loaded (helper+plugin, UxPlay 1.73.6)");
  return true;
}

void obs_module_unload(void) {}
