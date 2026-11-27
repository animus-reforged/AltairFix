#include "UI/Overlay.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "spdlog/spdlog.h"
#include "UI/Input.h"
#include "UI/Ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace
{
    /// ImGui context and both backends are ready.
    bool g_initialized = false;

    /// Initialization failed once; do not retry every frame.
    bool g_failed = false;

    /// Whether the AltairFix window is shown. Starts hidden so the overlay does
    /// not cover the startup intro; Insert shows it.
    std::atomic_bool g_visible{false};

    /// Set after the game's first successful Present. The overlay renders no
    /// frames before this, because drawing during the game's first frames can
    /// prevent its window from appearing.
    std::atomic_bool g_gamePresented{false};

    /// Game window whose messages drive the Win32 backend.
    HWND g_hwnd = nullptr;

    /// Whether the last rendered frame had the debug window visible.
    bool g_lastRenderedVisible = false;

    /// Display time accumulated while the welcome notification was on screen.
    std::int64_t g_notificationElapsedMs = 0;

    /// Time of the previous welcome-notification tick.
    std::int64_t g_notificationLastTickMs = 0;

    /// Whether the welcome notification timer has started.
    bool g_notificationStarted = false;

    /// Total visible time for the welcome notification.
    constexpr float kNotificationDurationSeconds = 5.0F;

    /// Largest wall-clock step counted per display frame. The game's intro and
    /// loading paths do not call the display hook, so a multi-second gap must
    /// not consume the notification before it can be drawn.
    constexpr std::int64_t kNotificationMaxStepMs = 100;

    /// Fade-in time for the welcome notification.
    constexpr float kNotificationFadeInSeconds = 0.25F;

    /// Fade-out time for the welcome notification.
    constexpr float kNotificationFadeOutSeconds = 0.5F;

    /// Renderer detected at startup.
    AltairFix::Game::Renderer g_renderer = AltairFix::Game::Renderer::Unknown;

    /// @brief Returns the current steady-clock time in milliseconds.
    std::int64_t NowMilliseconds()
    {
        return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /// @brief Computes the welcome notification fade alpha.
    ///
    /// Time advances only while the overlay is drawing display frames. The game
    /// renders its intro and loading screens through a different path that does
    /// not call the overlay, and counting that wall-clock time would expire the
    /// notification before it is ever visible.
    ///
    /// @return Alpha in [0, 1], or 0 before it starts or after it expires.
    float WelcomeNotificationAlpha()
    {
        if (!g_initialized)
        {
            return 0.0F;
        }

        const bool windowVisible = g_hwnd != nullptr && IsWindowVisible(g_hwnd) == TRUE;
        const bool windowIconic = g_hwnd != nullptr && IsIconic(g_hwnd) == TRUE;
        const std::int64_t now = NowMilliseconds();

        if (!g_notificationStarted)
        {
            if (!g_gamePresented.load(std::memory_order_acquire))
            {
                return 0.0F;
            }

            if (!windowVisible || windowIconic)
            {
                return 0.0F;
            }

            g_notificationStarted = true;
            g_notificationElapsedMs = 0;
            g_notificationLastTickMs = now;
            spdlog::info("Startup notification shown.");
        }
        else
        {
            if (!windowVisible || windowIconic)
            {
                // Do not count time while the notification cannot be displayed.
                g_notificationLastTickMs = now;
                return 0.0F;
            }

            const std::int64_t deltaMs = now - g_notificationLastTickMs;
            g_notificationLastTickMs = now;

            if (deltaMs > 0)
            {
                g_notificationElapsedMs += std::min(deltaMs, kNotificationMaxStepMs);
            }
        }

        const float seconds = static_cast<float>(g_notificationElapsedMs) / 1000.0F;
        if (seconds >= kNotificationDurationSeconds)
        {
            return 0.0F;
        }

        float alpha = 1.0F;
        if (seconds < kNotificationFadeInSeconds)
        {
            alpha = seconds / kNotificationFadeInSeconds;
        }
        else if (seconds > kNotificationDurationSeconds - kNotificationFadeOutSeconds)
        {
            alpha = (kNotificationDurationSeconds - seconds) / kNotificationFadeOutSeconds;
        }

        alpha = std::clamp(alpha, 0.0F, 1.0F);
        return alpha * alpha * (3.0F - (2.0F * alpha));
    }

    /// @brief Clears input state accumulated before the debug window was shown.
    ///
    /// The Win32 handler and ImGui frames are skipped while the overlay is
    /// hidden, so a key or button released during that time is never delivered.
    /// Reset the state and seed the cursor position before the first visible
    /// frame.
    void ResetImGuiInputState()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
        ImGui::ClearActiveID();

        POINT cursor{};
        if (g_hwnd != nullptr && GetCursorPos(&cursor) != FALSE && ScreenToClient(g_hwnd, &cursor) != FALSE)
        {
            io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
        }
    }
} // namespace

namespace AltairFix::UI::Overlay
{
    void Initialize(IDirect3DDevice9* device, HWND hwnd, Game::Renderer renderer)
    {
        if (g_initialized || g_failed)
        {
            return;
        }

        if (device == nullptr || hwnd == nullptr)
        {
            return;
        }

        g_renderer = renderer;

        IMGUI_CHECKVERSION();
        if (ImGui::CreateContext() == nullptr)
        {
            g_failed = true;
            spdlog::error("Failed to create the ImGui context; the overlay is disabled.");
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags =
            static_cast<ImGuiConfigFlags>(static_cast<std::uint32_t>(io.ConfigFlags) | static_cast<std::uint32_t>(ImGuiConfigFlags_NavEnableKeyboard));
        io.ConfigFlags =
            static_cast<ImGuiConfigFlags>(static_cast<std::uint32_t>(io.ConfigFlags) | static_cast<std::uint32_t>(ImGuiConfigFlags_NoMouseCursorChange));
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        if (!ImGui_ImplWin32_Init(hwnd))
        {
            g_failed = true;
            spdlog::error("Failed to initialize the Win32 ImGui backend; the overlay is disabled.");
            if (io.BackendPlatformUserData != nullptr)
            {
                ImGui_ImplWin32_Shutdown();
            }
            ImGui::DestroyContext();
            return;
        }

        if (!ImGui_ImplDX9_Init(device))
        {
            g_failed = true;
            spdlog::error("Failed to initialize the DX9 ImGui backend; the overlay is disabled.");
            if (io.BackendRendererUserData != nullptr)
            {
                ImGui_ImplDX9_Shutdown();
            }
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return;
        }

        Input::Install(hwnd);
        g_hwnd = hwnd;
        g_initialized = true;
        spdlog::info("ImGui overlay initialized (DirectX 9, HWND 0x{:X}); press Insert to show it.", reinterpret_cast<std::uintptr_t>(hwnd));
    }

    void Render()
    {
        if (!g_initialized)
        {
            return;
        }

        if (!g_gamePresented.load(std::memory_order_acquire))
        {
            g_lastRenderedVisible = false;
            return;
        }

        bool visible = g_visible.load(std::memory_order_acquire);
        const float notificationAlpha = WelcomeNotificationAlpha();
        if (!visible && notificationAlpha <= 0.0F)
        {
            g_lastRenderedVisible = false;
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = visible;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        if (visible && !g_lastRenderedVisible)
        {
            ResetImGuiInputState();
            io.DeltaTime = std::min(io.DeltaTime, 1.0F / 15.0F);
        }

        ImGui::NewFrame();
        if (visible)
        {
            AltairFix::UI::Draw(g_renderer, &visible);
            if (!visible)
            {
                g_visible.store(false, std::memory_order_release);
            }
        }

        if (notificationAlpha > 0.0F)
        {
            AltairFix::UI::DrawWelcomeNotification(g_renderer, notificationAlpha);
        }

        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_lastRenderedVisible = visible;
    }

    void NotifyGamePresented()
    {
        if (!g_gamePresented.exchange(true, std::memory_order_acq_rel))
        {
            spdlog::info("Game presented its first frame; overlay frames enabled.");
        }
    }

    void Toggle()
    {
        // std::atomic<bool> has no fetch_xor on this standard library, so use
        // a compare-exchange loop to keep the read-modify-write atomic.
        bool expected = g_visible.load(std::memory_order_relaxed);
        while (!g_visible.compare_exchange_weak(expected, !expected, std::memory_order_acq_rel, std::memory_order_relaxed))
        {
        }
    }

    bool IsInitialized()
    {
        return g_initialized || g_failed;
    }

    bool IsVisible()
    {
        return g_visible.load(std::memory_order_acquire);
    }
} // namespace AltairFix::UI::Overlay
