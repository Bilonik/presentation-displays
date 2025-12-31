#include "presentation_displays_plugin.h"

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>

#include <windows.h>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace presentation_displays {

namespace {
const char kChannelName[] = "presentation_displays_plugin";
const char kEventChannelName[] = "presentation_displays_plugin_events";

// Helper function to enumerate displays
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
  auto* monitors = reinterpret_cast<std::vector<HMONITOR>*>(dwData);
  monitors->push_back(hMonitor);
  return TRUE;
}

// Helper function to safely trim whitespace from a string
std::string TrimWhitespace(const std::string& str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    return "";  // String contains only whitespace
  }
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

}  // namespace

// static
void PresentationDisplaysPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), kChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<PresentationDisplaysPlugin>(registrar);

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  // Set up event channel for display connection/disconnection events
  auto event_channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          registrar->messenger(), kEventChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  auto handler = std::make_unique<
      flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
      [plugin_pointer = plugin.get()](
          const flutter::EncodableValue* arguments,
          std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events)
          -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
        plugin_pointer->event_sink_ = std::move(events);
        return nullptr;
      },
      [plugin_pointer = plugin.get()](const flutter::EncodableValue* arguments)
          -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
        plugin_pointer->event_sink_ = nullptr;
        return nullptr;
      });

  event_channel->SetStreamHandler(std::move(handler));

  registrar->AddPlugin(std::move(plugin));
}

PresentationDisplaysPlugin::PresentationDisplaysPlugin(
    flutter::PluginRegistrarWindows *registrar)
    : registrar_(registrar) {}

PresentationDisplaysPlugin::~PresentationDisplaysPlugin() {
  // Clean up any open secondary windows
  for (const auto& pair : secondary_windows_) {
    if (pair.second && IsWindow(pair.second)) {
      DestroyWindow(pair.second);
    }
  }
}

void PresentationDisplaysPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const std::string& method_name = method_call.method_name();

  if (method_name == "listDisplay") {
    const std::string* category = nullptr;
    if (method_call.arguments() && !method_call.arguments()->IsNull()) {
      category = std::get_if<std::string>(method_call.arguments());
    }
    std::string display_list = GetDisplayList(category);
    result->Success(flutter::EncodableValue(display_list));
  } else if (method_name == "showPresentation") {
    const auto* arguments = std::get_if<std::string>(method_call.arguments());
    if (!arguments) {
      result->Error("INVALID_ARGUMENT", "Arguments must be a JSON string");
      return;
    }

    try {
      // Parse JSON manually (simple parsing for displayId and routerName)
      // Format: {"displayId": <id>, "routerName": "<name>"}
      std::string json = *arguments;
      
      // Extract displayId
      size_t display_id_pos = json.find("\"displayId\"");
      if (display_id_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Missing displayId");
        return;
      }
      
      size_t colon_pos = json.find(":", display_id_pos);
      if (colon_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format for displayId");
        return;
      }
      
      size_t comma_pos = json.find(",", colon_pos);
      if (comma_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format");
        return;
      }
      
      std::string display_id_str = json.substr(colon_pos + 1, comma_pos - colon_pos - 1);
      display_id_str = TrimWhitespace(display_id_str);
      if (display_id_str.empty()) {
        result->Error("INVALID_ARGUMENT", "Empty display ID");
        return;
      }
      int display_id = std::stoi(display_id_str);

      // Extract routerName
      size_t router_name_pos = json.find("\"routerName\"");
      if (router_name_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Missing routerName");
        return;
      }
      
      size_t router_colon_pos = json.find(":", router_name_pos);
      if (router_colon_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format for routerName");
        return;
      }
      
      size_t first_quote = json.find("\"", router_colon_pos);
      if (first_quote == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format for routerName");
        return;
      }
      
      size_t second_quote = json.find("\"", first_quote + 1);
      if (second_quote == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format for routerName");
        return;
      }
      
      std::string router_name = json.substr(first_quote + 1, second_quote - first_quote - 1);

      bool success = ShowPresentation(display_id, router_name);
      result->Success(flutter::EncodableValue(success));
    } catch (const std::invalid_argument& e) {
      result->Error("INVALID_ARGUMENT", "Invalid display ID format");
      return;
    } catch (const std::out_of_range& e) {
      result->Error("INVALID_ARGUMENT", "Display ID out of range");
      return;
    } catch (const std::exception& e) {
      result->Error("PARSING_ERROR", std::string("Error parsing arguments: ") + e.what());
      return;
    }
  } else if (method_name == "hidePresentation") {
    const auto* arguments = std::get_if<std::string>(method_call.arguments());
    if (!arguments) {
      result->Error("INVALID_ARGUMENT", "Arguments must be a JSON string");
      return;
    }

    try {
      // Parse JSON for displayId
      std::string json = *arguments;
      size_t display_id_pos = json.find("\"displayId\"");
      if (display_id_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Missing displayId");
        return;
      }
      
      size_t colon_pos = json.find(":", display_id_pos);
      if (colon_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format for displayId");
        return;
      }
      
      size_t end_pos = json.find_first_of(",}", colon_pos);
      if (end_pos == std::string::npos) {
        result->Error("INVALID_ARGUMENT", "Invalid JSON format");
        return;
      }
      
      std::string display_id_str = json.substr(colon_pos + 1, end_pos - colon_pos - 1);
      display_id_str = TrimWhitespace(display_id_str);
      if (display_id_str.empty()) {
        result->Error("INVALID_ARGUMENT", "Empty display ID");
        return;
      }
      int display_id = std::stoi(display_id_str);

      bool success = HidePresentation(display_id);
      result->Success(flutter::EncodableValue(success));
    } catch (const std::invalid_argument& e) {
      result->Error("INVALID_ARGUMENT", "Invalid display ID format");
      return;
    } catch (const std::out_of_range& e) {
      result->Error("INVALID_ARGUMENT", "Display ID out of range");
      return;
    } catch (const std::exception& e) {
      result->Error("PARSING_ERROR", std::string("Error parsing arguments: ") + e.what());
      return;
    }
  } else if (method_name == "transferDataToPresentation") {
    bool success = TransferDataToPresentation(*method_call.arguments());
    result->Success(flutter::EncodableValue(success));
  } else {
    result->NotImplemented();
  }
}

std::string PresentationDisplaysPlugin::GetDisplayList(const std::string* category) {
  std::vector<HMONITOR> monitors;
  EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors));

  std::ostringstream json;
  json << "[";

  for (size_t i = 0; i < monitors.size(); ++i) {
    MONITORINFOEX monitor_info;
    monitor_info.cbSize = sizeof(MONITORINFOEX);
    
    if (GetMonitorInfo(monitors[i], &monitor_info)) {
      if (i > 0) json << ",";
      
      json << "{";
      json << "\"displayId\":" << i << ",";
      json << "\"flags\":0,";  // Windows doesn't have direct equivalents to Android flags
      json << "\"rotation\":0,";  // Windows rotation detection is complex, default to 0
      
      // Get device name
      std::string device_name(monitor_info.szDevice);
      
      // Check if this is the primary display
      bool is_primary = (monitor_info.dwFlags & MONITORINFOF_PRIMARY) != 0;
      
      if (is_primary) {
        json << "\"name\":\"Built-in Screen\"";
      } else {
        json << "\"name\":\"Screen " << i << "\"";
      }
      
      json << "}";
    }
  }

  json << "]";
  return json.str();
}

bool PresentationDisplaysPlugin::ShowPresentation(int display_id, const std::string& router_name) {
  std::vector<HMONITOR> monitors;
  EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors));

  if (display_id < 0 || static_cast<size_t>(display_id) >= monitors.size()) {
    return false;
  }

  HMONITOR target_monitor = monitors[display_id];
  MONITORINFO monitor_info;
  monitor_info.cbSize = sizeof(MONITORINFO);
  
  if (!GetMonitorInfo(target_monitor, &monitor_info)) {
    return false;
  }

  // Initialize engine channel for data transfer
  // Note: Full Flutter engine integration on secondary windows requires
  // significant Flutter framework support. This implementation provides
  // the infrastructure but full functionality would require Flutter engine
  // multi-window support which is still evolving on desktop platforms.
  
  if (!engine_channel_) {
    engine_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar_->messenger(), 
        "presentation_displays_plugin_engine",
        &flutter::StandardMethodCodec::GetInstance());
  }
  
  // For now, we create a placeholder window on the secondary display
  // that can be used when Flutter fully supports multiple windows on Windows
  RECT monitor_rect = monitor_info.rcMonitor;
  
  // Create a simple window on the secondary display
  HWND hwnd = CreateWindowEx(
      WS_EX_TOPMOST,
      L"STATIC",  // Simple static window class
      L"Presentation Display",
      WS_POPUP | WS_VISIBLE,
      monitor_rect.left,
      monitor_rect.top,
      monitor_rect.right - monitor_rect.left,
      monitor_rect.bottom - monitor_rect.top,
      nullptr,
      nullptr,
      GetModuleHandle(nullptr),
      nullptr);

  if (hwnd) {
    secondary_windows_[display_id] = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
  }

  return false;
}

bool PresentationDisplaysPlugin::HidePresentation(int display_id) {
  auto it = secondary_windows_.find(display_id);
  if (it != secondary_windows_.end() && it->second && IsWindow(it->second)) {
    DestroyWindow(it->second);
    secondary_windows_.erase(it);
    return true;
  }
  return false;
}

bool PresentationDisplaysPlugin::TransferDataToPresentation(
    const flutter::EncodableValue& arguments) {
  // Transfer data to the engine channel if it exists
  if (engine_channel_) {
    engine_channel_->InvokeMethod("DataTransfer", 
        std::make_unique<flutter::EncodableValue>(arguments));
    return true;
  }
  return false;
}

}  // namespace presentation_displays
