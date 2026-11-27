#include "Game/Executable.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace
{
    /// @brief Compares two ASCII strings without regard to case.
    ///
    /// Import table names are ASCII. A locale-aware comparison is unnecessary
    /// and would make the result depend on the process locale.
    ///
    /// @param lhs First string.
    /// @param rhs Second string.
    /// @return @c true when both strings contain the same ASCII letters.
    [[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        for (std::size_t index = 0; index < lhs.size(); ++index)
        {
            const auto left = static_cast<unsigned char>(lhs[index]);
            const auto right = static_cast<unsigned char>(rhs[index]);
            if (std::tolower(left) != std::tolower(right))
            {
                return false;
            }
        }

        return true;
    }

    /// @brief Reads a null-terminated import name from a loaded image.
    ///
    /// The length is bounded by the image size so a damaged import directory
    /// cannot walk beyond the module.
    ///
    /// @param base Image base address.
    /// @param imageSize Size of the mapped image in bytes.
    /// @param rva Address of the name relative to @p base.
    /// @return Name text, or an empty view when the RVA is invalid or
    ///         unterminated.
    [[nodiscard]] std::string_view ReadImportName(const std::uint8_t* base, std::size_t imageSize, std::uint32_t rva)
    {
        if (rva >= imageSize)
        {
            return {};
        }

        const char* text = reinterpret_cast<const char*>(base + rva);
        const std::size_t maxLength = imageSize - rva;
        std::size_t length = 0;
        while (length < maxLength && text[length] != '\0')
        {
            ++length;
        }

        if (length == maxLength)
        {
            return {};
        }

        return {text, length};
    }
} // namespace

namespace AltairFix::Game
{
    Renderer DetectRenderer(HMODULE module)
    {
        if (module == nullptr)
        {
            return Renderer::Unknown;
        }

        const auto* base = reinterpret_cast<const std::uint8_t*>(module);
        const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
        {
            return Renderer::Unknown;
        }

        const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE || ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            return Renderer::Unknown;
        }

        const std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;
        const IMAGE_DATA_DIRECTORY& importDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDirectory.VirtualAddress >= imageSize || importDirectory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
            importDirectory.Size > imageSize - importDirectory.VirtualAddress)
        {
            return Renderer::Unknown;
        }

        const auto* imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);
        const std::size_t descriptorCount = importDirectory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);

        bool importsDirect3D9 = false;
        bool importsDirect3D10 = false;
        bool importsDxgi = false;

        for (std::size_t index = 0; index < descriptorCount; ++index)
        {
            if (imports[index].Name == 0)
            {
                break;
            }

            const std::string_view name = ReadImportName(base, imageSize, imports[index].Name);
            if (EqualsIgnoreCase(name, "d3d9.dll"))
            {
                importsDirect3D9 = true;
            }
            else if (EqualsIgnoreCase(name, "d3d10.dll"))
            {
                importsDirect3D10 = true;
            }
            else if (EqualsIgnoreCase(name, "dxgi.dll"))
            {
                importsDxgi = true;
            }
        }

        // A DX10 executable also carries the launcher name strings for both
        // builds, so imports are the reliable discriminator. Direct3D 10 wins
        // over Direct3D 9, while DXGI is only a fallback signal.
        if (importsDirect3D10)
        {
            return Renderer::Direct3D10;
        }

        if (importsDirect3D9)
        {
            return Renderer::Direct3D9;
        }

        if (importsDxgi)
        {
            return Renderer::Direct3D10;
        }

        return Renderer::Unknown;
    }

    const char* GetRendererName(Renderer renderer)
    {
        switch (renderer)
        {
        case Renderer::Direct3D9:
            return "Direct3D 9";
        case Renderer::Direct3D10:
            return "Direct3D 10";
        case Renderer::Unknown:
            break;
        }

        return "Unknown";
    }
} // namespace AltairFix::Game