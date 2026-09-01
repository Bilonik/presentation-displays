#include "include/presentation_displays/presentation_displays_plugin.h"

#include <flutter/plugin_registrar_windows.h>

#include "presentation_displays_plugin.h"

void PresentationDisplaysPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  presentation_displays::PresentationDisplaysPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
