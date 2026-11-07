// nier - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include "lifecycle.h"
#include <rex/audio/downmix.h>
#include <cstdio>
#include <cstdlib>
#include <rex/input/input_system.h>
#include "virtual_pad_probe.h"

class NierApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<NierApp>(new NierApp(ctx, "nier",
        PPCImageConfig));
  }

  void OnPostSetup() override {
    rex::audio::StereoFold fold;
    fold.scale = 1.0f;
    rex::audio::SetStereoFold(fold);
    rex::audio::SetOutputGain(1.0f);
    std::fprintf(stderr, "NIER_AUDIO stereo_fold=1.000 output_gain=1.000\n");
    lifecycle_.Start();
    if (std::getenv("NIER_VIRTUAL_PAD_PROBE"))
      NierProbeVirtualGamepad(dynamic_cast<rex::input::InputSystem*>(runtime()->input_system()));
  }
  void OnShutdown() override { lifecycle_.Stop(); }

 private:
  NierLifecycle lifecycle_;

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
