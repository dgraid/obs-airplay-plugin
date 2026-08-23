#include "module_settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <mutex>
#include <string>

namespace {

std::mutex g_mu;
ModuleSettings g_settings;

std::string config_dir() {
  char *p = obs_module_get_config_path(obs_current_module(), "");
  std::string s = p ? p : "";
  bfree(p);
  return s;
}

std::string config_path() {
  char *p = obs_module_get_config_path(obs_current_module(), "obs-airplay.json");
  std::string s = p ? p : "";
  bfree(p);
  return s;
}

} // namespace

ModuleSettings &module_settings() { return g_settings; }

void module_settings_load() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::string dir = config_dir();
  if (!dir.empty())
    os_mkdirs(dir.c_str());
  std::string path = config_path();
  if (path.empty())
    return;
  obs_data_t *d = obs_data_create_from_json_file(path.c_str());
  if (!d)
    return;
  const char *n = obs_data_get_string(d, "receiver_name");
  if (n && *n)
    g_settings.receiver_name = n;
  const char *a = obs_data_get_string(d, "on_connect_scene");
  if (a)
    g_settings.on_connect_scene = a;
  const char *b = obs_data_get_string(d, "on_disconnect_scene");
  if (b)
    g_settings.on_disconnect_scene = b;
  g_settings.migrated_server_name = obs_data_get_bool(d, "migrated_server_name");
  obs_data_release(d);
}

bool module_settings_save() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::string dir = config_dir();
  if (!dir.empty())
    os_mkdirs(dir.c_str());
  std::string path = config_path();
  if (path.empty())
    return false;
  obs_data_t *d = obs_data_create();
  obs_data_set_string(d, "receiver_name", g_settings.receiver_name.c_str());
  obs_data_set_string(d, "on_connect_scene", g_settings.on_connect_scene.c_str());
  obs_data_set_string(d, "on_disconnect_scene", g_settings.on_disconnect_scene.c_str());
  obs_data_set_bool(d, "migrated_server_name", g_settings.migrated_server_name);
  bool ok = obs_data_save_json_safe(d, path.c_str(), "tmp", "bak");
  obs_data_release(d);
  return ok;
}

bool module_settings_migrate_server_name(const char *from_source) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_settings.migrated_server_name)
    return false;
  g_settings.migrated_server_name = true;
  if (from_source && *from_source && std::string(from_source) != "OBS AirPlay")
    g_settings.receiver_name = from_source;
  return true;
}
