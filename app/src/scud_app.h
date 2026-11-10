// Scud Race (wanszai's Xbox 360 build): the ReXGlue application object.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>

#include "scud_cheats.h"
#include "thorhax_intro.h"
#include "scud_pgo.h"
#include "scud_script_input.h"
#include "scud_snapshot.h"

// Renut's Direct3D-level renderer (src/renut_engine, from mk9-nx: reNut by masterspike52 through banjo-nx and
// sa2-nx), chosen at boot with sa2_renderer = "renut": deko3d on the Switch.
#if SA2_RENUT_RENDERER
#include "cpu/sa2_enhancements.h"
#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/overlay/app_integration.h"
#include "renut_engine/gpu/banjo_renut_capture.h"
#endif

#if defined(SCUD_BUILTIN_CONFIG)
#include "scud_builtin_config.h"  // generated from app/scud_switch_defaults.toml (CMake)
#endif

#if defined(__SWITCH__)
extern "C" void RexSwitchInputQuietUntilReleased(void);  // sdk switch_input_driver.cpp
#endif

namespace scud::fast_boot {
extern std::filesystem::path g_game_root;  // scud_fast_boot.cpp
}

class ScudApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ScudApp>(new ScudApp(ctx, "scud", PPCImageConfig));
  }

  // The Switch build's fixed settings (app/scud_switch_defaults.toml), applied before scud.toml.
  std::string_view BuiltInConfig() const override {
#if defined(SCUD_BUILTIN_CONFIG)
    return kScudBuiltInConfig;
#else
    return {};
#endif
  }
  std::string_view DefaultConfigFile() const override {
#if defined(SCUD_BUILTIN_CONFIG)
    return kScudPlayerConfig;
#else
    return {};
#endif
  }

  void OnPostInitLogging() override {
    // ReXGlue's emulated Xenos GPU unless Renut's renderer is chosen (sa2_renderer = "renut").
    if (!rex::cvar::HasNonDefaultValue("gpu_plugin")) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }
    // The game writes its settings, NVRAM, logs and abort reports under game:\ (scudrace.ini, abort.txt, ...).
    if (!rex::cvar::HasNonDefaultValue("allow_game_relative_writes")) {
      rex::cvar::SetFlagByName("allow_game_relative_writes", "true");
    }
    // The game's own sockets (system link, online): the SDK (from mk9-nx) fails socket() unless this is on.
    if (!rex::cvar::HasNonDefaultValue("mk9_guest_sockets")) {
      rex::cvar::SetFlagByName("mk9_guest_sockets", "true");
    }
#if SA2_RENUT_RENDERER
    sa2::enh::Resolve();
#endif
  }

  // Without --game_data_root: a game_root/ folder next to the executable or above it (on a PC build tree, the
  // repository's assets/game_root). It holds default.xex, scud.zip, dayto2pe.zip, ui/ and Music/.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    std::error_code ec;
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (folder.empty()) return;
    for (auto candidate = folder; !candidate.empty(); candidate = candidate.parent_path()) {
      for (const auto& sub : {std::filesystem::path("game_root"), std::filesystem::path("assets") / "game_root"}) {
        if (std::filesystem::is_regular_file(candidate / sub / "default.xex", ec)) {
          paths.game_data_root = candidate / sub;
          return;
        }
      }
      if (candidate == candidate.parent_path()) break;
    }
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    thorhax::intro::Start();  // the Thorhax intro (app/thorhax_intro): before the window and the audio device open
    config.input_factory = scud::script_input::CreateInputSystem;
#if SA2_RENUT_RENDERER
    sa2::enh::ApplyGpuBoost();
    if (banjo::renut_gpu::Active()) {
      // No SDK graphics system and no GPU plugin: the renderer is Renut's (its own swapchain and render thread).
      renut::gpu::ConfigureRuntime(config);
      REXLOG_INFO("graphics: Renut's Direct3D-level renderer (sa2_renderer = renut)");
    }
#endif
  }

#if SA2_RENUT_RENDERER
  // Renut draws the SDK's menus through its own drawer, resizes its swapchain itself, and starts once the window
  // exists (as mk9-nx's Mk9App).
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    if (banjo::renut_gpu::Active()) return renut::gpu::CreateOverlayDrawer();
    return rex::ReXApp::OnCreateImmediateDrawer();
  }
  void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override {
    rex::ReXApp::OnWindowPixelSizeChanged(pixel_width, pixel_height);
    if (banjo::renut_gpu::Active()) renut::gpu::OnWindowResized();
  }
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (banjo::renut_gpu::Active()) renut::gpu::StartRenderer(app_context(), [this] { return imgui_drawer(); });
    return rex::ReXApp::OnFinalizePaths(defaults, std::move(resume));
  }
#endif

  // Test snapshots of the game image (scud_snapshot_s): from Renut's frames, or the emulated GPU's presenter.
  void OnPostSetup() override {
    scud::fast_boot::g_game_root = game_data_root();  // the native unzip opens the game's zips there
    scud::pgo::Start();  // the instrumented build only (SCUD_PGO=generate)
#if SA2_RENUT_RENDERER
    sa2::enh::StartReport();
    if (banjo::renut_gpu::Active()) {
      scud::snapshot::StartWithSource([](rex::ui::RawImage& image) { return renut::gpu::CaptureFrame(image); });
      return;
    }
#endif
    scud::snapshot::Start([this]() -> rex::ui::Presenter* {
      const auto* rt = runtime();
      const auto* graphics = rt ? rt->graphics_system() : nullptr;
      return graphics ? graphics->presenter() : nullptr;
    });
  }
  void OnShutdown() override {
    scud::pgo::Stop();
#if SA2_RENUT_RENDERER
    if (banjo::renut_gpu::Active()) renut::gpu::ShutdownRenderer();
#endif
    scud::snapshot::Stop();
  }

  // The quick menu with the cheats (scud_cheats.cpp, from Shaolin Monks NX) in the achievements overlay's place (this
  // game has none): Minus + Plus (or L + R + Left) on the Switch, F7 on the PC. Closing from the menu presses that key again, on the UI thread after the
  // frame that drew it.
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    return scud::cheats::Create(imgui_drawer(), config_path(), [this] {
#if defined(__SWITCH__)
      RexSwitchInputQuietUntilReleased();  // the button that closed it is still held: not for the game
#endif
      app_context().CallInUIThreadDeferred([] {
        rex::ui::KeyEvent key(nullptr, rex::ui::VirtualKey::kF7, 1, false, false, false, false, false);
        rex::ui::ProcessKeyEvent(key);
      });
    });
  }

  // Analysis aid: SCUD_DUMP_IMAGE=<file> writes the loaded guest image.
  void OnPostLoadXexImage() override {
    if (!runtime() || !runtime()->memory()) return;
    const char* path = std::getenv("SCUD_DUMP_IMAGE");
    if (!path) return;
    const uint8_t* image = runtime()->memory()->TranslateVirtual<const uint8_t*>(PPCImageConfig.image_base);
    if (std::FILE* f = std::fopen(path, "wb")) {
      std::fwrite(image, 1, PPCImageConfig.image_size, f);
      std::fclose(f);
      REXLOG_INFO("guest image written to {}", path);
    }
  }
};
