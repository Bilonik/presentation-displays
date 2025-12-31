#include "include/presentation_displays/presentation_displays_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "presentation_displays_plugin.h"

void PresentationDisplaysPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  presentation_displays::PresentationDisplaysPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
