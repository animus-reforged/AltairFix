#include "UI/Ui.h"

#include "Config/Config.h"
#include "Game/CpuCoreLimit.h"
#include "Logging/Logging.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cstdint>

namespace AltairFix::UI
{
    namespace
    {
        ImGuiWindowFlags WelcomeWindowFlags()
        {
            std::uint32_t flags = 0;
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_NoDecoration);
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_AlwaysAutoResize);
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_NoSavedSettings);
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_NoFocusOnAppearing);
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_NoNav);
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_NoInputs);
            flags |= static_cast<std::uint32_t>(ImGuiWindowFlags_NoMove);
            return static_cast<ImGuiWindowFlags>(flags);
        }
    } // namespace

    void Draw(Game::Renderer renderer, bool* visible)
    {
        if (!ImGui::Begin("AltairFix", visible))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("DirectX version: %s", Game::GetDirectXVersionLabel(renderer));
        ImGui::Text("Detected from: executable import table");
        ImGui::Text("Runtime: Direct3D 9 device active");
        ImGui::Separator();
        ImGui::Text("AltairFix %s", ALTAIRFIX_VERSION);
        ImGui::Text("CPU cores: %u (game sees %u)",
                    static_cast<unsigned int>(Game::GetActualProcessorCount()),
                    static_cast<unsigned int>(Game::GetEffectiveProcessorCount()));
        bool limitCores = Game::IsCpuCoreLimitEnabled();
        if (ImGui::Checkbox("Limit CPU cores to 31", &limitCores))
        {
            Game::SetCpuCoreLimitEnabled(limitCores);
            spdlog::info("[cpu] core limit toggled enabled={} t=+{}ms", limitCores, AltairFix::Logging::ElapsedMilliseconds());
        }
        ImGui::TextDisabled("Set fixes.cpu_core_limit for the next launch.");
        ImGui::TextDisabled("Insert toggles this window.");
        ImGui::End();
    }

    void DrawWelcomeNotification(Game::Renderer renderer, float alpha)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0F || io.DisplaySize.y <= 0.0F)
        {
            return;
        }

        constexpr float kMargin = 18.0F;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kMargin, kMargin), ImGuiCond_Always, ImVec2(1.0F, 0.0F));
        ImGui::SetNextWindowBgAlpha(0.88F);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

        const ImGuiWindowFlags flags = WelcomeWindowFlags();
        if (ImGui::Begin("##AltairFixLoaded", nullptr, flags))
        {
            ImGui::TextUnformatted("AltairFix");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ALTAIRFIX_VERSION);
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", Game::GetDirectXVersionLabel(renderer));
            ImGui::TextColored(ImVec4(0.45F, 0.85F, 0.55F, 1.0F), "Mod loaded successfully");
            ImGui::TextDisabled("Insert toggles this menu");
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
} // namespace AltairFix::UI
