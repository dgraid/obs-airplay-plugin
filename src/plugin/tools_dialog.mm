#include "module_settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#import <Cocoa/Cocoa.h>

#include <string>

void tools_dialog_show(void *) {
  ModuleSettings &st = module_settings();
  @autoreleasepool {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @(obs_module_text("Tools.Title"));
    alert.informativeText = @(obs_module_text("Tools.ReceiverNameHelp"));
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 320, 24)];
    field.stringValue = @(st.receiver_name.c_str());
    field.placeholderString = @"OBS AirPlay";
    alert.accessoryView = field;
    [alert addButtonWithTitle:@(obs_module_text("Tools.OK"))];
    [alert addButtonWithTitle:@(obs_module_text("Tools.Cancel"))];
    if ([alert runModal] != NSAlertFirstButtonReturn)
      return;
    std::string name = field.stringValue.UTF8String ? field.stringValue.UTF8String : "";
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
      name.pop_back();
    if (name.empty())
      name = "OBS AirPlay";
    if (name == st.receiver_name)
      return;
    st.receiver_name = std::move(name);
    module_settings_save();
    airplay_apply_receiver_name();
  }
}

void tools_dialog_register() {
  obs_frontend_add_tools_menu_item(obs_module_text("Tools.Menu"), tools_dialog_show, nullptr);
}
