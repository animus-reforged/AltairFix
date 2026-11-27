#include "UI/Input.h"

#include "imgui_impl_win32.h"
#include "Logging/Logging.h"
#include "spdlog/spdlog.h"
#include "UI/Overlay.h"

#include <atomic>

// imgui_impl_win32.h intentionally leaves this declaration commented out so
// the header does not drag in <windows.h>. Declare it here for the subclass.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    /// Chained window-procedure call matching the game's ANSI/Unicode window.
    using CallWindowProcFn = LRESULT(WINAPI*)(WNDPROC, HWND, UINT, WPARAM, LPARAM);

    /// Original game window procedure, called for messages the overlay does not
    /// consume. Published before the subclass is installed so WndProc can never
    /// observe a null original.
    std::atomic<WNDPROC> g_originalWndProc{nullptr};

    /// CallWindowProcA for the game's ANSI window, or CallWindowProcW if that
    /// ever changes.
    std::atomic<CallWindowProcFn> g_callWindowProc{nullptr};

    /// Ensures the window is subclassed only once.
    bool g_installed = false;

    /// @brief Returns true for messages that represent game input.
    ///
    /// Focus, activation, sizing, display-change, cursor, and device messages
    /// are deliberately excluded so they always reach the game. The game is
    /// sensitive to losing activation while the overlay is visible.
    bool IsGameInputMessage(UINT msg)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_UNICHAR:
        case WM_DEADCHAR:
        case WM_SYSDEADCHAR:
            return true;
        default:
            return false;
        }
    }

    /// @brief Emits debug-level lifecycle messages useful for focus debugging.
    void LogWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_ACTIVATE:
            spdlog::debug("[input] WM_ACTIVATE hwnd=0x{:X} active={} minimized={}", reinterpret_cast<std::uintptr_t>(hwnd), LOWORD(wParam), HIWORD(wParam));
            break;
        case WM_ACTIVATEAPP:
            spdlog::debug("[input] WM_ACTIVATEAPP hwnd=0x{:X} active={}", reinterpret_cast<std::uintptr_t>(hwnd), wParam != 0);
            break;
        case WM_SETFOCUS:
            spdlog::debug("[input] WM_SETFOCUS hwnd=0x{:X}", reinterpret_cast<std::uintptr_t>(hwnd));
            break;
        case WM_KILLFOCUS:
            spdlog::debug("[input] WM_KILLFOCUS hwnd=0x{:X}", reinterpret_cast<std::uintptr_t>(hwnd));
            break;
        case WM_SIZE:
            spdlog::debug("[input] WM_SIZE hwnd=0x{:X} width={} height={}", reinterpret_cast<std::uintptr_t>(hwnd), LOWORD(lParam), HIWORD(lParam));
            break;
        case WM_DISPLAYCHANGE:
            spdlog::debug("[input] WM_DISPLAYCHANGE hwnd=0x{:X} bpp={} width={} height={}",
                          reinterpret_cast<std::uintptr_t>(hwnd),
                          wParam,
                          LOWORD(lParam),
                          HIWORD(lParam));
            break;
        default:
            break;
        }
    }

    /// @brief Window procedure that handles the overlay toggle and ImGui input.
    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LogWindowMessage(hwnd, msg, wParam, lParam);

        if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && wParam == VK_INSERT)
        {
            // Bit 30 is set on auto-repeat key-down messages. Toggle once per
            // physical press.
            if (msg == WM_KEYDOWN && (static_cast<DWORD>(lParam) & 0x40000000U) == 0)
            {
                AltairFix::UI::Overlay::Toggle();
            }
            return 0;
        }

        if (AltairFix::UI::Overlay::IsVisible())
        {
            // Keep the standard quit shortcut available even while the overlay
            // owns input.
            const bool altF4 = msg == WM_SYSKEYDOWN && wParam == VK_F4 && (static_cast<DWORD>(lParam) & 0x20000000U) != 0;
            if (!altF4)
            {
                ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
                if (IsGameInputMessage(msg))
                {
                    return 0;
                }
            }
        }

        const auto originalWndProc = g_originalWndProc.load(std::memory_order_acquire);
        const auto callWindowProc = g_callWindowProc.load(std::memory_order_acquire);
        if (originalWndProc == nullptr || callWindowProc == nullptr)
        {
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
        return callWindowProc(originalWndProc, hwnd, msg, wParam, lParam);
    }
} // namespace

namespace AltairFix::UI::Input
{
    void Install(HWND hwnd)
    {
        if (g_installed)
        {
            return;
        }

        const bool unicode = IsWindowUnicode(hwnd) != FALSE;
        const LONG_PTR previousRaw = unicode ? GetWindowLongPtrW(hwnd, GWLP_WNDPROC) : GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
        const auto previous = reinterpret_cast<WNDPROC>(previousRaw); // NOLINT(performance-no-int-to-ptr)
        if (previous == nullptr)
        {
            spdlog::warn("[input] subclass failed hwnd=0x{:X} reason=no-previous-wndproc", reinterpret_cast<std::uintptr_t>(hwnd));
            return;
        }

        const auto callWindowProc = unicode ? &CallWindowProcW : &CallWindowProcA;

        // Publish the state WndProc needs before the window can dispatch a
        // message through the new procedure.
        g_originalWndProc.store(previous, std::memory_order_release);
        g_callWindowProc.store(callWindowProc, std::memory_order_release);

        SetLastError(0);
        if (unicode)
        {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc));
        }
        else
        {
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc));
        }

        // SetWindowLongPtr returns the previous procedure, which can legitimately
        // be zero. Verify the installation directly instead of interpreting the
        // return value.
        const LONG_PTR installedRaw = unicode ? GetWindowLongPtrW(hwnd, GWLP_WNDPROC) : GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
        const auto installed = reinterpret_cast<WNDPROC>(installedRaw); // NOLINT(performance-no-int-to-ptr)
        if (installed != &WndProc)
        {
            const DWORD error = GetLastError();
            g_originalWndProc.store(nullptr, std::memory_order_release);
            g_callWindowProc.store(nullptr, std::memory_order_release);
            spdlog::warn("[input] subclass failed hwnd=0x{:X} win32=0x{:08X} ({})",
                         reinterpret_cast<std::uintptr_t>(hwnd),
                         error,
                         AltairFix::Logging::Win32ErrorMessage(error));
            return;
        }

        g_installed = true;
        spdlog::info("[input] subclass installed hwnd=0x{:X} unicode={} original_wndproc=0x{:X} setter={} chain={} t=+{}ms",
                     reinterpret_cast<std::uintptr_t>(hwnd),
                     unicode,
                     reinterpret_cast<std::uintptr_t>(previous),
                     unicode ? "SetWindowLongPtrW" : "SetWindowLongPtrA",
                     unicode ? "CallWindowProcW" : "CallWindowProcA",
                     AltairFix::Logging::ElapsedMilliseconds());
    }
} // namespace AltairFix::UI::Input
