// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The in-game quick menu (2026-11-10), adapted from Shaolin Monks NX's (port/common/quick_menu.cpp: from
// YabaSanshiro's NX overlay, itself from NaGa's Dolphin NX frontend). Kept: the page and selection model, the controller
// buttons (D-pad moves, A selects, Left / Right flip a setting, B goes back), Minus + Plus to open and close it, and the
// drawing and style. The pages are this port's: cheats on the game's own variables (scud_game_env.cpp).
//
// How it lives in the SDK: it takes the achievements overlay's place (this game has none; ScudApp::
// CreateAchievementsOverlay), so it exists only while open (nothing is drawn or read in play), ReXApp keeps the
// controller from the game while it is open, and the input driver opens and closes it on Minus + Plus (the press goes
// to the game neither then nor until every button is up). The buttons are read from ImGui's gamepad keys, by letter.
//
// Each cheat is a cvar the game hooks read live; a change is written to scud.toml at once, only its own line (replaced,
// or added at the end), so the rest of the file and its comments stay as they are.

#include "scud_cheats.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DECLARE(bool, scud_cheat_time);
REXCVAR_DECLARE(bool, scud_cheat_first);

extern std::atomic<bool> g_scud_request_quit;  // scud_game_env.cpp
extern std::atomic<bool> g_scud_request_menu;

namespace scud::cheats {

namespace {

struct Cheat {
  const char* cvar;
  const char* name;
  bool& (*value)();
};

bool& CheatTime() { return REXCVAR_GET(scud_cheat_time); }
bool& CheatFirst() { return REXCVAR_GET(scud_cheat_first); }

const Cheat kCheats[] = {
    {"scud_cheat_time", "Unlimited time", CheatTime},
    {"scud_cheat_first", "Always first place", CheatFirst},
};
constexpr int kCount = int(std::size(kCheats));

enum class Page { Main, Cheats };
constexpr int kMainItems = 4;  // Resume game, Cheats, Return to the main menu, Exit game
constexpr int kMainMenu = 2, kMainExit = 3;

// Replaces (or appends) "name = value" in the configuration file, leaving every other line alone.
void SaveLine(const std::filesystem::path& path, const std::string& name, const std::string& value) {
  if (path.empty()) return;
  std::vector<std::string> lines;
  {
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
  }
  bool replaced = false;
  for (std::string& line : lines) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line.compare(i, name.size(), name) != 0) continue;
    size_t j = line.find_first_not_of(" \t", i + name.size());
    if (j == std::string::npos || line[j] != '=') continue;
    line = name + " = " + value;
    replaced = true;
  }
  if (!replaced) {
    lines.push_back("# Quick menu (Minus + Plus): written by the game");
    lines.push_back(name + " = " + value);
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    REXLOG_WARN("[menu] could not write {}", path.string());
    return;
  }
  for (const std::string& line : lines) out << line << '\n';
  REXLOG_INFO("[menu] {} = {} saved to {}", name, value, path.string());
}

void CenteredText(std::string_view text) {
  const float width = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
  ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

class QuickMenu : public rex::ui::ImGuiDialog {
 public:
  QuickMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config, std::function<void()> close)
      : ImGuiDialog(drawer), config_(std::move(config)), close_(std::move(close)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    Input();
    if (closing_) {
      if (!close_sent_ && close_) {
        close_sent_ = true;
        close_();
      }
      return;
    }

    const float scale = std::clamp(io.DisplaySize.y / 1080.0f, 0.66f, 1.0f);
    ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, io.DisplaySize, IM_COL32(0, 0, 0, 132));
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({880.0f * scale, 500.0f * scale}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {30.0f * scale, 22.0f * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10.0f * scale, 8.0f * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(7, 28, 44, 248));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(71, 190, 235, 255));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(15, 89, 126, 245));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(15, 89, 126, 245));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(15, 89, 126, 245));
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(71, 190, 235, 160));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Scud Race quick menu", nullptr, flags)) {
      // Shaolin Monks' font is 26 px at 1080p; this one is the SDK's, scaled to that size.
      ImGui::SetWindowFontScale(1.0f);  // the window keeps its scale between frames: measure the font unscaled
      ImGui::SetWindowFontScale(26.0f * scale / ImGui::GetFontSize());
      const float row = 44.0f * scale;
      CenteredText(page_ == Page::Main ? "Quick Menu" : "Cheats");
      ImGui::Separator();
      if (page_ == Page::Main) {
        Row("Resume game", 0, row);
        Row("Cheats                                           >", 1, row);
        Row(confirm_ == kMainMenu ? "Return to the main menu?   A  Yes     B  No" : "Return to the main menu", kMainMenu, row);
        Row(confirm_ == kMainExit ? "Exit the game (it saves first)?   A  Yes     B  No" : "Exit game", kMainExit, row);
        ImGui::Separator();
        CenteredText("D-Pad  Move    A  Select    B  Close    Minus + Plus  Menu");
      } else {
        for (int c = 0; c < kCount; ++c) {
          const bool on = kCheats[c].value();
          if (on) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 235, 150, 255));
          Row(std::string(on ? "[ ON  ]  " : "[ off ]  ") + kCheats[c].name, c, row);
          if (on) ImGui::PopStyleColor();
        }
        Row("Back", kCount, row);
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20.0f);
        ImGui::TextDisabled("Unlimited time: the race timer stays where it is (a checkpoint still adds its time). "
                            "Always first place: you lead the race order. Saved to %s.",
                            config_.filename().string().c_str());
        ImGui::PopTextWrapPos();
        CenteredText("A  Toggle     Left / Right  Toggle     B  Back");
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
  }

 private:
  int ItemCount() const { return page_ == Page::Main ? kMainItems : kCount + 1; }

  void SetPage(Page page) {
    page_ = page;
    selection_ = 0;
  }

  void Toggle(int c) {
    const bool on = !kCheats[c].value();
    rex::cvar::SetFlagByName(kCheats[c].cvar, on ? "true" : "false");
    SaveLine(config_, kCheats[c].cvar, on ? "true" : "false");
  }

  void Activate() {
    if (page_ == Page::Main) {
      if (selection_ == kMainMenu || selection_ == kMainExit) {
        if (confirm_ != selection_) {  // the first A asks, the second does it
          confirm_ = selection_;
          return;
        }
        (selection_ == kMainMenu ? g_scud_request_menu : g_scud_request_quit).store(true);
        confirm_ = -1;
        closing_ = true;
      } else if (selection_ == 0) {
        closing_ = true;
      } else {
        SetPage(Page::Cheats);
      }
    } else if (selection_ == kCount) {
      SetPage(Page::Main);
    } else {
      Toggle(selection_);
    }
  }

  void Back() {
    if (confirm_ >= 0) {
      confirm_ = -1;
      return;
    }
    if (page_ == Page::Main) closing_ = true;
    else SetPage(Page::Main);
  }

  // ImGui's gamepad keys by letter (switch_imgui_input.h: Switch A is FaceRight, B FaceDown), as Shaolin Monks' menu.
  void Input() {
    // The press that opened the menu (Minus + Plus) does not count until it is up.
    const bool any = ImGui::IsKeyDown(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) ||
                     ImGui::IsKeyDown(ImGuiKey_GamepadStart) || ImGui::IsKeyDown(ImGuiKey_GamepadBack);
    if (wait_release_) {
      if (any) return;
      wait_release_ = false;
    }
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
    const int count = std::max(1, ItemCount());
    if (pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp) || pressed(ImGuiKey_UpArrow)) {
      selection_ = (selection_ + count - 1) % count;
      confirm_ = -1;
    } else if (pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown) || pressed(ImGuiKey_DownArrow)) {
      selection_ = (selection_ + 1) % count;
      confirm_ = -1;
    }
    // Left and Right flip a setting in place, as on NaGa's menu.
    const bool side = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
    if (side && page_ == Page::Cheats && selection_ < kCount) {
      Toggle(selection_);
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Back();
    else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
      Activate();
  }

  void Row(const std::string& label, int index, float height) {
    ImGui::Selectable((label + "##row" + std::to_string(index)).c_str(), selection_ == index,
                      ImGuiSelectableFlags_None, {0.0f, height});
  }

  std::filesystem::path config_;
  std::function<void()> close_;
  Page page_ = Page::Main;
  int selection_ = 0;
  int confirm_ = -1;  // the main page's item waiting for a second A
  bool wait_release_ = true;
  bool closing_ = false;
  bool close_sent_ = false;
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> Create(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config,
                                             std::function<void()> close) {
  return std::make_unique<QuickMenu>(drawer, std::move(config), std::move(close));
}

}  // namespace scud::cheats
