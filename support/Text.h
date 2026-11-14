// support/Text.h - Localization & string resource system (Zero CRT)
#pragma once

#include <Windows.h>
#include <string>
#include <string_view>
#include <vector>

namespace kvc
{
    struct Language
    {
        std::wstring code;
        std::wstring name;
    };

    class Text
    {
    public:
        static void UseSaved();
        static void Use(std::wstring_view code, bool save = true);
        static std::wstring Detect();
        static std::wstring Current();
        static bool Automatic();
        static const std::vector<Language>& Available();

        static std::wstring Get(UINT id);
        static std::wstring English(UINT id);

        static std::wstring Format(UINT id, std::wstring_view arg0);
        static std::wstring Format(UINT id, int arg0);
        static std::wstring Format(UINT id, std::wstring_view arg0, std::wstring_view arg1);
        static std::wstring Format(UINT id, const std::string_view* arguments,
                                   std::size_t count);
    };

    inline std::wstring S(UINT id)
    {
        return Text::Get(id);
    }
}
