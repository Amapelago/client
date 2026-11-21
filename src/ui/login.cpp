#include <string>
#include <vector>

#include <imgui.h>

#include "ap/seed.hpp"
#include "ap/session.hpp"
#include "client.hpp"
#include "ui/overlay.hpp"
#include "version.hpp"

namespace ui
{
namespace
{

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

    // A dark backing keeps the text readable over a bright frame.
    const float pad = ImGui::GetFontSize() * 0.5f;
    const ImVec2 at(pad * 2, pad * 2);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad), IM_COL32(0, 0, 0, 160));
    draw->AddText(at, IM_COL32_WHITE, text.c_str());
}

} // namespace ui
