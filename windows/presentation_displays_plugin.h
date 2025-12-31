#ifndef FLUTTER_PLUGIN_PRESENTATION_DISPLAYS_PLUGIN_H_
#define FLUTTER_PLUGIN_PRESENTATION_DISPLAYS_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>

#include <memory>
#include <map>
#include <windows.h>

namespace presentation_displays {

class PresentationDisplaysPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  PresentationDisplaysPlugin(flutter::PluginRegistrarWindows *registrar);

  virtual ~PresentationDisplaysPlugin();

  // Disallow copy and assign.
  PresentationDisplaysPlugin(const PresentationDisplaysPlugin&) = delete;
  PresentationDisplaysPlugin& operator=(const PresentationDisplaysPlugin&) = delete;

 private:
  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  // Get list of displays
  std::string GetDisplayList(const std::string* category);
  
  // Show presentation on secondary display
  bool ShowPresentation(int display_id, const std::string& router_name);
  
  // Hide presentation on secondary display
  bool HidePresentation(int display_id);
  
  // Transfer data to presentation
  bool TransferDataToPresentation(const flutter::EncodableValue& arguments);

  // Event sink for display connection events
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;
  
  // Reference to the registrar
  flutter::PluginRegistrarWindows *registrar_;
  
  // Method channel for engine communication
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> engine_channel_;
  
  // Map to store secondary windows
  std::map<int, HWND> secondary_windows_;
};

}  // namespace presentation_displays

#endif  // FLUTTER_PLUGIN_PRESENTATION_DISPLAYS_PLUGIN_H_
