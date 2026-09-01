#include "presentation_displays_plugin.h"

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace presentation_displays {

namespace {

constexpr char kChannelName[] = "presentation_displays_plugin";
constexpr char kEventChannelName[] = "presentation_displays_plugin_events";
constexpr char kEngineChannelName[] =
    "presentation_displays_plugin_engine";
constexpr char kMainDisplayChannelName[] = "main_display_channel";
constexpr wchar_t kSecondaryWindowClassName[] =
    L"PresentationDisplaysSecondaryWindow";
constexpr char kPresentationCategory[] =
    "android.hardware.display.category.PRESENTATION";

BOOL CALLBACK MonitorEnumProc(HMONITOR monitor,
                              HDC,
                              LPRECT,
                              LPARAM user_data) {
  auto* monitors = reinterpret_cast<std::vector<HMONITOR>*>(user_data);
  monitors->push_back(monitor);
  return TRUE;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }

  int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                 static_cast<int>(value.size()), nullptr, 0,
                                 nullptr, nullptr);
  if (size == 0) {
    size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                               static_cast<int>(value.size()), nullptr, 0,
                               nullptr, nullptr);
  }
  if (size == 0) {
    return {};
  }

  std::string converted(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), converted.data(), size,
                      nullptr, nullptr);
  return converted;
}

std::string JsonEscape(const std::string& value) {
  std::ostringstream escaped;
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (character < 0x20) {
          constexpr char kHexDigits[] = "0123456789abcdef";
          escaped << "\\u00" << kHexDigits[(character >> 4) & 0x0F]
                  << kHexDigits[character & 0x0F];
        } else {
          escaped << character;
        }
    }
  }
  return escaped.str();
}

std::optional<int> EncodableInteger(const flutter::EncodableValue& value) {
  if (const auto* integer = std::get_if<int32_t>(&value)) {
    return *integer;
  }
  if (const auto* integer = std::get_if<int64_t>(&value)) {
    // Parenthesize these calls so the Windows min/max macros cannot expand.
    if (*integer >= (std::numeric_limits<int>::min)() &&
        *integer <= (std::numeric_limits<int>::max)()) {
      return static_cast<int>(*integer);
    }
  }
  return std::nullopt;
}

std::optional<int> LegacyDisplayId(const std::string& json) {
  const size_t key = json.find("\"displayId\"");
  if (key == std::string::npos) {
    return std::nullopt;
  }
  const size_t colon = json.find(':', key);
  if (colon == std::string::npos) {
    return std::nullopt;
  }

  size_t start = colon + 1;
  while (start < json.size() &&
         std::isspace(static_cast<unsigned char>(json[start]))) {
    ++start;
  }
  size_t end = start;
  if (end < json.size() && (json[end] == '-' || json[end] == '+')) {
    ++end;
  }
  while (end < json.size() &&
         std::isdigit(static_cast<unsigned char>(json[end]))) {
    ++end;
  }
  if (end == start) {
    return std::nullopt;
  }

  try {
    size_t parsed = 0;
    const int value = std::stoi(json.substr(start, end - start), &parsed);
    if (parsed != end - start) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> LegacyRouterName(const std::string& json) {
  const size_t key = json.find("\"routerName\"");
  if (key == std::string::npos) {
    return std::nullopt;
  }
  const size_t colon = json.find(':', key);
  const size_t quote =
      colon == std::string::npos ? std::string::npos : json.find('"', colon);
  if (quote == std::string::npos) {
    return std::nullopt;
  }

  std::string value;
  bool escaped = false;
  for (size_t index = quote + 1; index < json.size(); ++index) {
    const char character = json[index];
    if (escaped) {
      switch (character) {
        case '"':
        case '\\':
        case '/':
          value.push_back(character);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          return std::nullopt;
      }
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else if (character == '"') {
      return value;
    } else {
      value.push_back(character);
    }
  }
  return std::nullopt;
}

bool ParseShowArguments(const flutter::EncodableValue* arguments,
                        int* display_id,
                        std::string* router_name) {
  if (!arguments) {
    return false;
  }
  if (const auto* map = std::get_if<flutter::EncodableMap>(arguments)) {
    const auto display = map->find(flutter::EncodableValue("displayId"));
    const auto route = map->find(flutter::EncodableValue("routerName"));
    if (display == map->end() || route == map->end()) {
      return false;
    }
    const auto parsed_id = EncodableInteger(display->second);
    const auto* parsed_route = std::get_if<std::string>(&route->second);
    if (!parsed_id || !parsed_route || parsed_route->empty()) {
      return false;
    }
    *display_id = *parsed_id;
    *router_name = *parsed_route;
    return true;
  }
  if (const auto* json = std::get_if<std::string>(arguments)) {
    const auto parsed_id = LegacyDisplayId(*json);
    const auto parsed_route = LegacyRouterName(*json);
    if (!parsed_id || !parsed_route || parsed_route->empty()) {
      return false;
    }
    *display_id = *parsed_id;
    *router_name = *parsed_route;
    return true;
  }
  return false;
}

bool ParseHideArguments(const flutter::EncodableValue* arguments,
                        int* display_id) {
  if (!arguments) {
    return false;
  }
  if (const auto* map = std::get_if<flutter::EncodableMap>(arguments)) {
    const auto display = map->find(flutter::EncodableValue("displayId"));
    if (display == map->end()) {
      return false;
    }
    const auto parsed_id = EncodableInteger(display->second);
    if (!parsed_id) {
      return false;
    }
    *display_id = *parsed_id;
    return true;
  }
  if (const auto* json = std::get_if<std::string>(arguments)) {
    const auto parsed_id = LegacyDisplayId(*json);
    if (!parsed_id) {
      return false;
    }
    *display_id = *parsed_id;
    return true;
  }
  return false;
}

}  // namespace

struct PresentationDisplaysPlugin::DisplayInfo {
  int display_id = -1;
  HMONITOR monitor = nullptr;
  std::wstring device_name;
  std::string friendly_name;
  RECT bounds = {};
  int rotation = 0;
  bool is_primary = false;
};

struct PresentationDisplaysPlugin::SecondaryWindow {
  PresentationDisplaysPlugin* owner = nullptr;
  int display_id = -1;
  std::wstring device_name;
  HWND host_window = nullptr;
  FlutterDesktopViewControllerRef view_controller = nullptr;
  FlutterDesktopMessengerRef messenger = nullptr;
};

void PresentationDisplaysPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), kChannelName,
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<PresentationDisplaysPlugin>(registrar);
  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  auto event_channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          registrar->messenger(), kEventChannelName,
          &flutter::StandardMethodCodec::GetInstance());
  auto stream_handler = std::make_unique<
      flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
      [plugin_pointer = plugin.get()](
          const flutter::EncodableValue*,
          std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& sink) {
        plugin_pointer->event_sink_ = std::move(sink);
        return std::unique_ptr<
            flutter::StreamHandlerError<flutter::EncodableValue>>();
      },
      [plugin_pointer = plugin.get()](const flutter::EncodableValue*) {
        plugin_pointer->event_sink_.reset();
        return std::unique_ptr<
            flutter::StreamHandlerError<flutter::EncodableValue>>();
      });
  event_channel->SetStreamHandler(std::move(stream_handler));

  registrar->AddPlugin(std::move(plugin));
}

PresentationDisplaysPlugin::PresentationDisplaysPlugin(
    flutter::PluginRegistrarWindows* registrar)
    : registrar_(registrar),
      main_display_channel_(
          std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
              registrar->messenger(), kMainDisplayChannelName,
              &flutter::StandardMethodCodec::GetInstance())) {
  for (const auto& display : EnumerateDisplays()) {
    known_display_devices_.insert(display.device_name);
  }

  window_proc_delegate_id_ = registrar_->RegisterTopLevelWindowProcDelegate(
      [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        return HandleWindowMessage(hwnd, message, wparam, lparam);
      });
}

PresentationDisplaysPlugin::~PresentationDisplaysPlugin() {
  if (window_proc_delegate_id_ >= 0) {
    registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_delegate_id_);
  }

  while (!secondary_windows_.empty()) {
    CloseSecondaryWindow(secondary_windows_.begin()->first);
  }
}

void PresentationDisplaysPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name() == "listDisplay") {
    const std::string* category = nullptr;
    if (method_call.arguments()) {
      category = std::get_if<std::string>(method_call.arguments());
    }
    result->Success(flutter::EncodableValue(GetDisplayList(category)));
    return;
  }

  if (method_call.method_name() == "showPresentation") {
    int display_id = -1;
    std::string router_name;
    if (!ParseShowArguments(method_call.arguments(), &display_id,
                            &router_name)) {
      result->Error("INVALID_ARGUMENT",
                    "Expected displayId and a non-empty routerName");
      return;
    }
    result->Success(
        flutter::EncodableValue(ShowPresentation(display_id, router_name)));
    return;
  }

  if (method_call.method_name() == "hidePresentation") {
    int display_id = -1;
    if (!ParseHideArguments(method_call.arguments(), &display_id)) {
      result->Error("INVALID_ARGUMENT", "Expected displayId");
      return;
    }
    result->Success(flutter::EncodableValue(HidePresentation(display_id)));
    return;
  }

  if (method_call.method_name() == "transferDataToPresentation") {
    if (!method_call.arguments()) {
      result->Error("INVALID_ARGUMENT", "Arguments are required");
      return;
    }
    result->Success(flutter::EncodableValue(
        TransferDataToPresentation(*method_call.arguments())));
    return;
  }

  result->NotImplemented();
}

std::vector<PresentationDisplaysPlugin::DisplayInfo>
PresentationDisplaysPlugin::EnumerateDisplays() const {
  std::vector<HMONITOR> monitor_handles;
  EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                      reinterpret_cast<LPARAM>(&monitor_handles));

  std::vector<DisplayInfo> displays;
  displays.reserve(monitor_handles.size());
  for (const HMONITOR monitor : monitor_handles) {
    MONITORINFOEXW monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor,
                         reinterpret_cast<LPMONITORINFO>(&monitor_info))) {
      continue;
    }

    DisplayInfo display;
    display.monitor = monitor;
    display.device_name = monitor_info.szDevice;
    display.bounds = monitor_info.rcMonitor;
    display.is_primary =
        (monitor_info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    DISPLAY_DEVICEW display_device = {};
    display_device.cb = sizeof(display_device);
    if (EnumDisplayDevicesW(monitor_info.szDevice, 0, &display_device, 0)) {
      display.friendly_name = WideToUtf8(display_device.DeviceString);
    }

    DEVMODEW display_mode = {};
    display_mode.dmSize = sizeof(display_mode);
    if (EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS,
                             &display_mode)) {
      switch (display_mode.dmDisplayOrientation) {
        case DMDO_90:
          display.rotation = 1;
          break;
        case DMDO_180:
          display.rotation = 2;
          break;
        case DMDO_270:
          display.rotation = 3;
          break;
        default:
          display.rotation = 0;
      }
    }
    displays.push_back(std::move(display));
  }

  std::sort(displays.begin(), displays.end(),
            [](const DisplayInfo& left, const DisplayInfo& right) {
              if (left.is_primary != right.is_primary) {
                return left.is_primary;
              }
              return left.device_name < right.device_name;
            });
  for (size_t index = 0; index < displays.size(); ++index) {
    displays[index].display_id = static_cast<int>(index);
    if (displays[index].friendly_name.empty()) {
      displays[index].friendly_name = displays[index].is_primary
                                           ? "Primary display"
                                           : "Display " +
                                                 std::to_string(index);
    }
  }
  return displays;
}

std::string PresentationDisplaysPlugin::GetDisplayList(
    const std::string* category) const {
  const bool presentation_only =
      category && *category == kPresentationCategory;
  const auto displays = EnumerateDisplays();

  std::ostringstream json;
  json << '[';
  bool first = true;
  for (const auto& display : displays) {
    if (presentation_only && display.is_primary) {
      continue;
    }
    if (!first) {
      json << ',';
    }
    first = false;
    json << "{\"displayId\":" << display.display_id
         << ",\"flags\":0,\"rotation\":" << display.rotation
         << ",\"name\":\"" << JsonEscape(display.friendly_name)
         << "\"}";
  }
  json << ']';
  return json.str();
}

bool PresentationDisplaysPlugin::EnsureSecondaryWindowClassRegistered()
    const {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW existing = {};
  existing.cbSize = sizeof(existing);
  if (GetClassInfoExW(instance, kSecondaryWindowClassName, &existing)) {
    return true;
  }

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = SecondaryWindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  window_class.lpszClassName = kSecondaryWindowClassName;
  return RegisterClassExW(&window_class) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool PresentationDisplaysPlugin::ShowPresentation(
    int display_id,
    const std::string& router_name) {
  const auto displays = EnumerateDisplays();
  const auto display =
      std::find_if(displays.begin(), displays.end(),
                   [display_id](const DisplayInfo& candidate) {
                     return candidate.display_id == display_id;
                   });
  if (display == displays.end() || display->is_primary ||
      !EnsureSecondaryWindowClassRegistered()) {
    return false;
  }

  CloseSecondaryWindow(display->device_name);

  auto window = std::make_unique<SecondaryWindow>();
  window->owner = this;
  window->display_id = display_id;
  window->device_name = display->device_name;

  const int width = display->bounds.right - display->bounds.left;
  const int height = display->bounds.bottom - display->bounds.top;
  window->host_window = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kSecondaryWindowClassName,
      L"Presentation Display", WS_POPUP, display->bounds.left,
      display->bounds.top, width, height, nullptr, nullptr,
      GetModuleHandleW(nullptr), window.get());
  if (!window->host_window) {
    return false;
  }

  const std::wstring assets_path = L"data\\flutter_assets";
  const std::wstring icu_path = L"data\\icudtl.dat";
  const std::wstring aot_path = L"data\\app.so";
  constexpr char kDartEntrypoint[] = "secondaryDisplayMain";
  FlutterDesktopEngineProperties engine_properties = {};
  engine_properties.assets_path = assets_path.c_str();
  engine_properties.icu_data_path = icu_path.c_str();
  engine_properties.aot_library_path = aot_path.c_str();
  engine_properties.dart_entrypoint = kDartEntrypoint;

  FlutterDesktopEngineRef engine =
      FlutterDesktopEngineCreate(&engine_properties);
  if (!engine) {
    DestroyWindow(window->host_window);
    return false;
  }

  window->messenger = FlutterDesktopEngineGetMessenger(engine);
  const std::string navigation_message =
      "{\"method\":\"setInitialRoute\",\"args\":\"" +
      JsonEscape(router_name) + "\"}";
  if (!FlutterDesktopMessengerSend(
          window->messenger, "flutter/navigation",
          reinterpret_cast<const uint8_t*>(navigation_message.data()),
          navigation_message.size())) {
    FlutterDesktopEngineDestroy(engine);
    DestroyWindow(window->host_window);
    return false;
  }

  FlutterDesktopMessengerSetCallback(window->messenger,
                                     kMainDisplayChannelName,
                                     SecondaryMainChannelCallback,
                                     window.get());
  window->view_controller =
      FlutterDesktopViewControllerCreate(width, height, engine);
  if (!window->view_controller) {
    DestroyWindow(window->host_window);
    return false;
  }

  const FlutterDesktopViewRef view =
      FlutterDesktopViewControllerGetView(window->view_controller);
  const HWND flutter_window = view ? FlutterDesktopViewGetHWND(view) : nullptr;
  if (!flutter_window) {
    FlutterDesktopViewControllerDestroy(window->view_controller);
    window->view_controller = nullptr;
    DestroyWindow(window->host_window);
    return false;
  }

  SetParent(flutter_window, window->host_window);
  MoveWindow(flutter_window, 0, 0, width, height, TRUE);
  ShowWindow(flutter_window, SW_SHOW);
  ShowWindow(window->host_window, SW_SHOWNOACTIVATE);
  UpdateWindow(window->host_window);
  FlutterDesktopViewControllerForceRedraw(window->view_controller);

  secondary_windows_[display->device_name] = std::move(window);
  return true;
}

bool PresentationDisplaysPlugin::HidePresentation(int display_id) {
  const auto displays = EnumerateDisplays();
  const auto display =
      std::find_if(displays.begin(), displays.end(),
                   [display_id](const DisplayInfo& candidate) {
                     return candidate.display_id == display_id;
                   });
  if (display != displays.end() &&
      secondary_windows_.find(display->device_name) !=
          secondary_windows_.end()) {
    CloseSecondaryWindow(display->device_name);
    return true;
  }

  const auto open_window = std::find_if(
      secondary_windows_.begin(), secondary_windows_.end(),
      [display_id](const auto& entry) {
        return entry.second->display_id == display_id;
      });
  if (open_window == secondary_windows_.end()) {
    return false;
  }
  CloseSecondaryWindow(open_window->first);
  return true;
}

void PresentationDisplaysPlugin::CloseSecondaryWindow(
    const std::wstring& device_name) {
  const auto found = secondary_windows_.find(device_name);
  if (found == secondary_windows_.end()) {
    return;
  }

  auto window = std::move(found->second);
  secondary_windows_.erase(found);
  if (window->view_controller) {
    const FlutterDesktopViewRef view =
        FlutterDesktopViewControllerGetView(window->view_controller);
    const HWND flutter_window =
        view ? FlutterDesktopViewGetHWND(view) : nullptr;
    if (flutter_window) {
      ShowWindow(flutter_window, SW_HIDE);
      SetParent(flutter_window, nullptr);
    }
    if (window->messenger) {
      FlutterDesktopMessengerSetCallback(window->messenger,
                                         kMainDisplayChannelName, nullptr,
                                         nullptr);
    }
    FlutterDesktopViewControllerDestroy(window->view_controller);
    window->view_controller = nullptr;
    window->messenger = nullptr;
  }
  if (window->host_window && IsWindow(window->host_window)) {
    DestroyWindow(window->host_window);
  }
}

bool PresentationDisplaysPlugin::TransferDataToPresentation(
    const flutter::EncodableValue& arguments) const {
  if (secondary_windows_.empty()) {
    return false;
  }

  const auto& codec = flutter::StandardMethodCodec::GetInstance();
  const flutter::MethodCall<flutter::EncodableValue> call(
      "DataTransfer",
      std::make_unique<flutter::EncodableValue>(arguments));
  const auto encoded = codec.EncodeMethodCall(call);

  bool sent = false;
  for (const auto& entry : secondary_windows_) {
    const auto* window = entry.second.get();
    if (window->messenger &&
        FlutterDesktopMessengerSend(window->messenger, kEngineChannelName,
                                    encoded->data(), encoded->size())) {
      sent = true;
    }
  }
  return sent;
}

void PresentationDisplaysPlugin::TransferDataToMain(
    const flutter::EncodableValue& arguments) const {
  main_display_channel_->InvokeMethod(
      "dataToMain", std::make_unique<flutter::EncodableValue>(arguments));
}

void PresentationDisplaysPlugin::SecondaryMainChannelCallback(
    FlutterDesktopMessengerRef messenger,
    const FlutterDesktopMessage* message,
    void* user_data) {
  auto* window = static_cast<SecondaryWindow*>(user_data);
  const auto& codec = flutter::StandardMethodCodec::GetInstance();
  std::unique_ptr<std::vector<uint8_t>> response;

  const auto call = codec.DecodeMethodCall(message->message,
                                           message->message_size);
  if (!window || !window->owner || !call ||
      call->method_name() != "transferDataToMain" || !call->arguments()) {
    response = codec.EncodeErrorEnvelope(
        "INVALID_METHOD", "Expected transferDataToMain with arguments");
  } else {
    window->owner->TransferDataToMain(*call->arguments());
    const flutter::EncodableValue success(true);
    response = codec.EncodeSuccessEnvelope(&success);
  }

  if (message->response_handle) {
    FlutterDesktopMessengerSendResponse(messenger, message->response_handle,
                                        response->data(), response->size());
  }
}

void PresentationDisplaysPlugin::HandleDisplayChange() {
  const auto displays = EnumerateDisplays();
  std::set<std::wstring> current_devices;
  for (const auto& display : displays) {
    current_devices.insert(display.device_name);
  }

  std::vector<std::wstring> disconnected;
  std::set_difference(known_display_devices_.begin(),
                      known_display_devices_.end(), current_devices.begin(),
                      current_devices.end(),
                      std::back_inserter(disconnected));
  std::vector<std::wstring> connected;
  std::set_difference(current_devices.begin(), current_devices.end(),
                      known_display_devices_.begin(),
                      known_display_devices_.end(),
                      std::back_inserter(connected));

  for (const auto& device : disconnected) {
    CloseSecondaryWindow(device);
    if (event_sink_) {
      event_sink_->Success(flutter::EncodableValue(0));
    }
  }
  for (size_t index = 0; index < connected.size(); ++index) {
    if (event_sink_) {
      event_sink_->Success(flutter::EncodableValue(1));
    }
  }
  known_display_devices_ = std::move(current_devices);
}

std::optional<LRESULT> PresentationDisplaysPlugin::HandleWindowMessage(
    HWND,
    UINT message,
    WPARAM,
    LPARAM) {
  if (message == WM_DISPLAYCHANGE) {
    HandleDisplayChange();
  }
  return std::nullopt;
}

LRESULT CALLBACK PresentationDisplaysPlugin::SecondaryWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
  auto* window = reinterpret_cast<SecondaryWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    window = static_cast<SecondaryWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(window));
  }

  if (window && window->view_controller) {
    LRESULT result = 0;
    if (FlutterDesktopViewControllerHandleTopLevelWindowProc(
            window->view_controller, hwnd, message, wparam, lparam,
            &result)) {
      return result;
    }
  }

  switch (message) {
    case WM_SIZE:
      if (window && window->view_controller) {
        const FlutterDesktopViewRef view =
            FlutterDesktopViewControllerGetView(window->view_controller);
        const HWND flutter_window =
            view ? FlutterDesktopViewGetHWND(view) : nullptr;
        if (flutter_window) {
          MoveWindow(flutter_window, 0, 0, LOWORD(lparam), HIWORD(lparam),
                     TRUE);
        }
      }
      return 0;
    case WM_FONTCHANGE:
      if (window && window->view_controller) {
        FlutterDesktopEngineReloadSystemFonts(
            FlutterDesktopViewControllerGetEngine(window->view_controller));
      }
      return 0;
    case WM_CLOSE:
      if (window && window->owner) {
        window->owner->CloseSecondaryWindow(window->device_name);
      }
      return 0;
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      break;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace presentation_displays
