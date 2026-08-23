#pragma once

#include <string>

struct ModuleSettings {
  std::string receiver_name = "OBS AirPlay";
  std::string on_connect_scene;
  std::string on_disconnect_scene;
  bool migrated_server_name = false;
};

ModuleSettings &module_settings();
void module_settings_load();
bool module_settings_save();
bool module_settings_migrate_server_name(const char *from_source);

void airplay_apply_receiver_name();

void tools_dialog_register();
