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
    alert.informativeText = @(obs_module_text("Tools.Help"));

    NSView *box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 360, 70)];

    NSTextField *nameLbl = [NSTextField labelWithString:@(obs_module_text("Tools.ReceiverName"))];
    nameLbl.frame = NSMakeRect(0, 42, 100, 22);
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(108, 40, 252, 24)];
    field.stringValue = @(st.receiver_name.c_str());
    field.placeholderString = @"OBS AirPlay";

    NSTextField *langLbl = [NSTextField labelWithString:@(obs_module_text("Tools.Language"))];
    langLbl.frame = NSMakeRect(0, 8, 100, 22);
    NSPopUpButton *lang = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(108, 6, 180, 26) pullsDown:NO];
    [lang addItemWithTitle:@(obs_module_text("Tools.LangRu"))];
    [lang addItemWithTitle:@(obs_module_text("Tools.LangEn"))];
    [lang selectItemAtIndex:language_is_en(st.language) ? 1 : 0];

    [box addSubview:nameLbl];
    [box addSubview:field];
    [box addSubview:langLbl];
    [box addSubview:lang];
    alert.accessoryView = box;
    [alert addButtonWithTitle:@(obs_module_text("Tools.OK"))];
    [alert addButtonWithTitle:@(obs_module_text("Tools.Cancel"))];
    if ([alert runModal] != NSAlertFirstButtonReturn)
      return;

    std::string name = field.stringValue.UTF8String ? field.stringValue.UTF8String : "";
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
      name.pop_back();
    if (name.empty())
      name = "OBS AirPlay";
    std::string language = lang.indexOfSelectedItem == 1 ? "en" : "ru";
    if (name == st.receiver_name && language == st.language)
      return;
    st.receiver_name = std::move(name);
    st.language = std::move(language);
    module_settings_save();
    airplay_apply_receiver_name();
  }
}

void tools_dialog_register() {
  obs_frontend_add_tools_menu_item(obs_module_text("Tools.Menu"), tools_dialog_show, nullptr);
}
