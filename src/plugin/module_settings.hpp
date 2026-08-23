#pragma once

#include <string>

struct ModuleSettings {
  std::string language = "ru";
  std::string on_connect_scene;
  std::string on_disconnect_scene;
};

inline bool language_is_en(const std::string &lang) { return lang == "en"; }

ModuleSettings &module_settings();
void module_settings_load();
bool module_settings_save();

bool airplay_any_connected();
bool airplay_any_paused();
void airplay_refresh_idle_stubs();

void tools_dialog_register();
