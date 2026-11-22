// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "ap/seed.hpp"
#include "ap/session.hpp"
#include "client.hpp"
#include "game/saves.hpp"
#include "ui/overlay.hpp"
#include "version.hpp"

namespace ui
{
namespace
{

// The login fields. Server and slot fill from the saved login on the login window's first draw. The password starts empty, since nothing saves it.
char g_server[256] = {};
char g_slot[128] = {};
char g_password[128] = {};
bool g_filled = false;

const char *link_name(ap::LinkState link)
{
    switch (link)
    {
    case ap::LinkState::Disconnected:
        return "disconnected";
    case ap::LinkState::Connecting:
        return "connecting";
    case ap::LinkState::Connected:
        return "connected";
    case ap::LinkState::Refused:
        return "refused";
    }
    return "";
}

// The link state, then the bound seed, then the reason while refused. Status keeps the last reason after the link moves on, so it shows only while refused.
std::string status_text(const ap::Session &session)
{
    const ap::Status &status = session.status();
    std::string text = link_name(status.link);
    if (const ap::Seed *seed = ap::seed())
        text += ", seed " + seed->name + " slot " + std::to_string(seed->slot);
    if (status.link == ap::LinkState::Refused)
        text += ": " + status.reason;
    return text;
}

} // namespace

void draw_status()
{
    std::string text = "okami-apclient " + std::string(version::string) + "  ";
    if (const ap::Session *session = client::session())
    {
        text += status_text(*session);
        const std::vector<std::string> &keys = session->status().unknown_keys;
        for (size_t i = 0; i < keys.size(); ++i)
            text += (i == 0 ? "\nunknown slot_data keys: " : ", ") + keys[i];
    }
    else
    {
        text += "off: no data folder";
    }
    if (const std::string problem = game::save_problem(); !problem.empty())
        text += "\nAP save not written: " + problem;

    // A dark backing keeps the text readable over a bright frame.
    const float pad = ImGui::GetFontSize() * 0.5f;
    const ImVec2 at(pad * 2, pad * 2);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad), IM_COL32(0, 0, 0, 160));
    draw->AddText(at, IM_COL32_WHITE, text.c_str());
}

void draw_login(bool focused)
{
    ap::Session &session = *client::session();
    if (!g_filled)
    {
        g_filled = true;
        const ap::Login saved = session.saved_login();
        std::snprintf(g_server, sizeof(g_server), "%s", saved.server.empty() ? "archipelago.gg:38281" : saved.server.c_str());
        std::snprintf(g_slot, sizeof(g_slot), "%s", saved.slot.c_str());
    }

    // Placed below the status line every frame, since the status line draws over windows and grows with the font scale. The window sizes itself to its
    // fields.
    const float font = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(font, font * 4), ImGuiCond_Always);
    if (!ImGui::Begin("Archipelago", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }
    if (!focused)
        ImGui::TextUnformatted("Press the key left of 1 to use this window.");
    ImGui::InputText("Server", g_server, sizeof(g_server)); // host:port, or a URL
    ImGui::InputText("Slot", g_slot, sizeof(g_slot));
    ImGui::InputText("Password", g_password, sizeof(g_password), ImGuiInputTextFlags_Password);

    // A link that is connecting or connected, or a bound seed, has something to disconnect.
    const ap::LinkState link = session.status().link;
    if ((link == ap::LinkState::Disconnected || link == ap::LinkState::Refused) && ap::seed() == nullptr)
    {
        if (ImGui::Button("Connect"))
            session.connect(g_server, g_slot, g_password);
    }
    else if (ImGui::Button("Disconnect"))
    {
        session.disconnect();
    }
    ImGui::TextWrapped("%s", status_text(session).c_str());
    switch (game::title_block())
    {
    case game::TitleBlock::NoSeed:
        ImGui::TextWrapped("New Game and Continue are blocked: connect first.");
        break;
    case game::TitleBlock::HasSave:
        ImGui::TextWrapped("New Game is blocked: this seed has a save, use Continue.");
        break;
    case game::TitleBlock::NoSave:
        ImGui::TextWrapped("Continue is blocked: this seed has no save yet, use New Game.");
        break;
    }
    ImGui::End();
}

} // namespace ui
