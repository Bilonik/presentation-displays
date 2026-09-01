#ifndef FLUTTER_PLUGIN_PRESENTATION_DISPLAYS_PLUGIN_H_
#define FLUTTER_PLUGIN_PRESENTATION_DISPLAYS_PLUGIN_H_

#include <flutter/encodable_value.h>
#include <flutter/event_sink.h>
#include <flutter/method_call.h>
#include <flutter/method_channel.h>
#include <flutter/method_result.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter_windows.h>

#include <windows.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace presentation_displays {

class PresentationDisplaysPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(
      flutter::PluginRegistrarWindows* registrar);

  explicit PresentationDisplaysPlugin(
      flutter::PluginRegistrarWindows* registrar);
  ~PresentationDisplaysPlugin() override;

  PresentationDisplaysPlugin(const PresentationDisplaysPlugin&) = delete;
  PresentationDisplaysPlugin& operator=(const PresentationDisplaysPlugin&) =
      delete;

 private:
  struct DisplayInfo;
  struct SecondaryWindow;

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  std::string GetDisplayList(const std::string* category) const;
  bool ShowPresentation(int display_id, const std::string& router_name);
  bool HidePresentation(int display_id);
  bool TransferDataToPresentation(
      const flutter::EncodableValue& arguments) const;
  void TransferDataToMain(const flutter::EncodableValue& arguments) const;

  std::vector<DisplayInfo> EnumerateDisplays() const;
  void HandleDisplayChange();
  void CloseSecondaryWindow(const std::wstring& device_name);
  bool EnsureSecondaryWindowClassRegistered() const;

  std::optional<LRESULT> HandleWindowMessage(HWND hwnd,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam);
  static LRESULT CALLBACK SecondaryWindowProc(HWND hwnd,
                                               UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam);
  static void SecondaryMainChannelCallback(
      FlutterDesktopMessengerRef messenger,
      const FlutterDesktopMessage* message,
      void* user_data);

  flutter::PluginRegistrarWindows* registrar_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      main_display_channel_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;
  std::map<std::wstring, std::unique_ptr<SecondaryWindow>> secondary_windows_;
  std::set<std::wstring> known_display_devices_;
  int window_proc_delegate_id_ = -1;
};

}  // namespace presentation_displays

#endif  // FLUTTER_PLUGIN_PRESENTATION_DISPLAYS_PLUGIN_H_
