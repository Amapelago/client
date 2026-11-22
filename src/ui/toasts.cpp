// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <deque>
#include <string>

#include <imgui.h>

#include "ap/session.hpp"
#include "client.hpp"
#include "ui/overlay.hpp"

namespace ui
{
namespace
{

struct Toast
{
    std::string text;
    double shown_at = 0; // ImGui::GetTime() when the toast arrived
};

constexpr double kLife = 6.0; // held for 5 s, then faded out over the last second
constexpr std::size_t kShown = 5;

std::deque<Toast> g_toasts; // oldest first

} // namespace

void draw_toasts()
{
    const double now = ImGui::GetTime();
    for (std::string &text : client::session()->take_prints())
        g_toasts.push_back({std::move(text), now});
    while (!g_toasts.empty() && now - g_toasts.front().shown_at >= kLife)
        g_toasts.pop_front();

    const ImGuiIO &io = ImGui::GetIO();
    const float font = ImGui::GetFontSize();
    const float pad = font * 0.5f;
    const float wrap = io.DisplaySize.x / 3;
    const float right = io.DisplaySize.x - pad * 2;
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    float top = pad * 2;
    const std::size_t shown = std::min(g_toasts.size(), kShown);
    for (auto toast = g_toasts.rbegin(); toast != g_toasts.rbegin() + shown; ++toast)
    {
        const float alpha = static_cast<float>(std::min(1.0, kLife - (now - toast->shown_at)));
        const ImVec2 size = ImGui::CalcTextSize(toast->text.c_str(), nullptr, false, wrap);
        const ImVec2 at(right - size.x, top);
        draw->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(right + pad, at.y + size.y + pad), IM_COL32(0, 0, 0, static_cast<int>(160 * alpha)));
        draw->AddText(ImGui::GetFont(), font, at, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), toast->text.c_str(), nullptr, wrap);
        top += size.y + pad * 3;
    }
}

} // namespace ui
