#include "Paths/Paths.h"

#include <vector>

namespace AltairFix::Paths
{
    std::filesystem::path GetModulePath(HMODULE module)
    {
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }

            if (length < buffer.size())
            {
                return {std::wstring(buffer.data(), length)};
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    std::filesystem::path GetFileNextToModule(HMODULE module, const wchar_t* fileName)
    {
        const std::filesystem::path modulePath = GetModulePath(module);
        if (modulePath.empty())
        {
            return {};
        }

        return modulePath.parent_path() / fileName;
    }

    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
} // namespace AltairFix::Paths