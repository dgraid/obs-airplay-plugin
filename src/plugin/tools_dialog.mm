#include "module_settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#import <Cocoa/Cocoa.h>

#include <string>

namespace {

NSTextField *make_label(const char *key, bool bold) {
  NSTextField *t = [NSTextField labelWithString:@(obs_module_text(key))];
  t.alignment = NSTextAlignmentRight;
  if (bold)
    t.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
  return t;
}

NSTextField *make_value_label() {
  NSTextField *t = [NSTextField labelWithString:@"—"];
  t.alignment = NSTextAlignmentLeft;
  return t;
}

std::string popup_string(NSPopUpButton *popup) {
  id ro = popup.selectedItem.representedObject;
  if ([ro isKindOfClass:[NSString class]])
    return std::string([ro UTF8String] ? [ro UTF8String] : "");
  return {};
}

void fill_scenes(NSPopUpButton *popup, const std::string &selected) {
  [popup removeAllItems];
  [popup addItemWithTitle:@(obs_module_text("Tools.SceneNone"))];
  popup.lastItem.representedObject = @"";
  NSInteger idx = 0;
  obs_frontend_source_list list{};
  obs_frontend_get_scenes(&list);
  for (size_t i = 0; i < list.sources.num; i++) {
    const char *n = obs_source_get_name(list.sources.array[i]);
    if (!n || !*n)
      continue;
    [popup addItemWithTitle:@(n)];
    popup.lastItem.representedObject = @(n);
    if (selected == n)
      idx = (NSInteger)popup.numberOfItems - 1;
  }
  obs_frontend_source_list_free(&list);
  [popup selectItemAtIndex:idx];
}

} // namespace

@interface AirPlaySettingsWindow : NSObject <NSWindowDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSPopUpButton *langPopup;
@property(nonatomic, strong) NSTextField *statusValue;
@property(nonatomic, strong) NSPopUpButton *connectScene;
@property(nonatomic, strong) NSPopUpButton *disconnectScene;
@property(nonatomic, strong) NSTimer *timer;
- (void)show;
@end

@implementation AirPlaySettingsWindow

- (void)refreshStatus {
  bool on = airplay_any_connected();
  self.statusValue.stringValue = @(obs_module_text(on ? "Tools.StatusConnected" : "Tools.StatusWaiting"));
}

- (void)loadFromSettings {
  ModuleSettings &st = module_settings();
  [self.langPopup selectItemAtIndex:language_is_en(st.language) ? 1 : 0];
  fill_scenes(self.connectScene, st.on_connect_scene);
  fill_scenes(self.disconnectScene, st.on_disconnect_scene);
  [self refreshStatus];
}

- (void)buildIfNeeded {
  if (self.window)
    return;

  NSTextField *helpName = [NSTextField wrappingLabelWithString:@(obs_module_text("Tools.Help"))];
  helpName.alignment = NSTextAlignmentLeft;
  helpName.textColor = NSColor.secondaryLabelColor;
  helpName.preferredMaxLayoutWidth = 420;
  NSView *helpNamePad = [[NSView alloc] initWithFrame:NSZeroRect];

  NSTextField *langLbl = make_label("Tools.Language", false);
  NSPopUpButton *lang = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [lang addItemWithTitle:@(obs_module_text("Tools.LangRu"))];
  lang.lastItem.representedObject = @"ru";
  [lang addItemWithTitle:@(obs_module_text("Tools.LangEn"))];
  lang.lastItem.representedObject = @"en";
  self.langPopup = lang;

  NSTextField *statusLbl = make_label("Tools.Status", false);
  NSTextField *statusVal = make_value_label();
  self.statusValue = statusVal;

  NSTextField *autoHdr = make_label("Tools.Automation", true);
  autoHdr.alignment = NSTextAlignmentLeft;
  NSView *autoPad = [[NSView alloc] initWithFrame:NSZeroRect];

  NSTextField *connectLbl = make_label("Tools.OnConnect", false);
  NSPopUpButton *connect = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  self.connectScene = connect;

  NSTextField *disconnectLbl = make_label("Tools.OnDisconnect", false);
  NSPopUpButton *disconnect = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  self.disconnectScene = disconnect;

  NSTextField *help = [NSTextField wrappingLabelWithString:@(obs_module_text("Tools.AutomationHelp"))];
  help.textColor = NSColor.secondaryLabelColor;
  help.preferredMaxLayoutWidth = 420;
  NSView *helpPad = [[NSView alloc] initWithFrame:NSZeroRect];

  NSGridView *grid = [NSGridView gridViewWithViews:@[
    @[ helpName, helpNamePad ],
    @[ langLbl, lang ],
    @[ statusLbl, statusVal ],
    @[ autoHdr, autoPad ],
    @[ connectLbl, connect ],
    @[ disconnectLbl, disconnect ],
    @[ help, helpPad ],
  ]];
  grid.translatesAutoresizingMaskIntoConstraints = NO;
  grid.rowSpacing = 10;
  grid.columnSpacing = 12;
  [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  [grid columnAtIndex:1].xPlacement = NSGridCellPlacementFill;
  [grid columnAtIndex:1].width = 260;
  [grid mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(0, 1)];
  [grid mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(3, 1)];
  [grid mergeCellsInHorizontalRange:NSMakeRange(0, 2) verticalRange:NSMakeRange(6, 1)];

  NSButton *cancel = [[NSButton alloc] initWithFrame:NSZeroRect];
  cancel.title = @(obs_module_text("Tools.Cancel"));
  cancel.bezelStyle = NSBezelStyleRounded;
  cancel.keyEquivalent = @"\e";
  cancel.target = self;
  cancel.action = @selector(cancel:);

  NSButton *ok = [[NSButton alloc] initWithFrame:NSZeroRect];
  ok.title = @(obs_module_text("Tools.OK"));
  ok.bezelStyle = NSBezelStyleRounded;
  ok.keyEquivalent = @"\r";
  ok.target = self;
  ok.action = @selector(ok:);

  NSStackView *buttons = [NSStackView stackViewWithViews:@[ cancel, ok ]];
  buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  buttons.spacing = 12;
  buttons.translatesAutoresizingMaskIntoConstraints = NO;

  NSView *cv = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 320)];
  [cv addSubview:grid];
  [cv addSubview:buttons];
  [NSLayoutConstraint activateConstraints:@[
    [grid.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:20],
    [grid.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-20],
    [grid.topAnchor constraintEqualToAnchor:cv.topAnchor constant:20],
    [buttons.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:20],
    [buttons.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-20],
    [buttons.bottomAnchor constraintEqualToAnchor:cv.bottomAnchor constant:-20],
  ]];

  NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 500, 360)
                                            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  w.title = @(obs_module_text("Tools.Title"));
  w.contentView = cv;
  w.releasedWhenClosed = NO;
  w.delegate = self;
  self.window = w;
}

- (void)show {
  [self buildIfNeeded];
  [self loadFromSettings];
  if (!self.timer) {
    self.timer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                  target:self
                                                selector:@selector(refreshStatus)
                                                userInfo:nil
                                                 repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
  }
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (void)stopTimer {
  [self.timer invalidate];
  self.timer = nil;
}

- (void)cancel:(id)sender {
  (void)sender;
  [self stopTimer];
  [self.window orderOut:nil];
}

- (void)ok:(id)sender {
  (void)sender;
  ModuleSettings &st = module_settings();
  st.language = popup_string(self.langPopup) == "en" ? "en" : "ru";
  st.on_connect_scene = popup_string(self.connectScene);
  st.on_disconnect_scene = popup_string(self.disconnectScene);
  module_settings_save();
  airplay_refresh_idle_stubs();
  [self stopTimer];
  [self.window orderOut:nil];
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  [self stopTimer];
}

@end

static AirPlaySettingsWindow *g_settings_window;

void tools_dialog_show(void *) {
  @autoreleasepool {
    if (!g_settings_window)
      g_settings_window = [[AirPlaySettingsWindow alloc] init];
    [g_settings_window show];
  }
}

void tools_dialog_register() {
  obs_frontend_add_tools_menu_item(obs_module_text("Tools.Menu"), tools_dialog_show, nullptr);
}
