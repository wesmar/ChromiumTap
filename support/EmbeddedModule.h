#pragma once

#include <Windows.h>
#include <string>

namespace kvc
{
    // Reconstructs the extraction module embedded in the executable resource.
    // The temporary DLL remains open with FILE_FLAG_DELETE_ON_CLOSE for the
    // lifetime of this object, so no file is left behind on normal shutdown.
    class EmbeddedModule
    {
    public:
        EmbeddedModule() = default;
        ~EmbeddedModule();
        EmbeddedModule(const EmbeddedModule&) = delete;
        EmbeddedModule& operator=(const EmbeddedModule&) = delete;

        bool Extract(HMODULE owner, std::string& error);

        [[nodiscard]] bool Valid() const noexcept
        {
            return m_file != INVALID_HANDLE_VALUE && !m_path.empty();
        }

        [[nodiscard]] const std::wstring& Path() const noexcept { return m_path; }

    private:
        void Close() noexcept;

        HANDLE m_file = INVALID_HANDLE_VALUE;
        std::wstring m_path;
    };
}
