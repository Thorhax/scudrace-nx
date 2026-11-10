// The port's cheat menu (scud_cheats.cpp).

#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include <rex/ui/imgui_dialog.h>

namespace scud::cheats {

// `close` asks the app to close the menu (it is deleted on the UI thread, after the frame that drew it).
std::unique_ptr<rex::ui::ImGuiDialog> Create(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config,
                                             std::function<void()> close);

}  // namespace scud::cheats
