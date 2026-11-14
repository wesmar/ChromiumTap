// support/Text.cpp - Localization & string resource implementation (Zero CRT)
#include "support/Text.h"
#include "support/RuntimeSupport.h"

namespace kvc
{
    namespace
    {
        constexpr wchar_t kRegKey[] = L"Software\\ChromiumTap";
        constexpr wchar_t kRegLanguage[] = L"Language";

        struct CatalogItem
        {
            Language info;
            WORD langId = 0;       // non-zero for built-in RT_STRING resource languages
            std::wstring filePath; // non-empty for external .lng file override/addition
        };

        struct IdEntry
        {
            UINT id;
            std::wstring value;
        };

        struct TextState
        {
            std::vector<CatalogItem> catalog;
            std::vector<Language> publicList;
            std::vector<IdEntry> fileEntries;
            std::wstring currentCode = L"en";
            WORD currentLangId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
            bool automatic = true;
            bool catalogScanned = false;
        };

        TextState& State()
        {
            static TextState s;
            return s;
        }

        std::wstring Trim(std::wstring_view text)
        {
            std::size_t start = 0;
            while (start < text.size() &&
                   (text[start] == L' ' || text[start] == L'\t' || text[start] == L'\r' || text[start] == L'\n'))
            {
                ++start;
            }
            std::size_t end = text.size();
            while (end > start &&
                   (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r' || text[end - 1] == L'\n'))
            {
                --end;
            }
            return std::wstring(text.substr(start, end - start));
        }

        std::wstring LowerAscii(std::wstring_view text)
        {
            std::wstring out(text);
            for (wchar_t& ch : out)
            {
                if (ch >= L'A' && ch <= L'Z')
                    ch = static_cast<wchar_t>(ch - L'A' + L'a');
            }
            return out;
        }

        std::wstring Unescape(std::wstring_view text)
        {
            std::wstring out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'\\' && i + 1 < text.size())
                {
                    const wchar_t next = text[++i];
                    if (next == L'n')      out.push_back(L'\n');
                    else if (next == L'r') out.push_back(L'\r');
                    else if (next == L't') out.push_back(L'\t');
                    else                   out.push_back(next);
                }
                else
                {
                    out.push_back(text[i]);
                }
            }
            return out;
        }

        bool ParseUint(std::wstring_view text, UINT& outVal)
        {
            if (text.empty())
                return false;
            UINT val = 0;
            for (wchar_t ch : text)
            {
                if (ch < L'0' || ch > L'9')
                    return false;
                val = val * 10u + static_cast<UINT>(ch - L'0');
            }
            outVal = val;
            return true;
        }

        std::wstring LoadResourceStringForLang(UINT id, WORD langId)
        {
            HMODULE hMod = GetModuleHandleW(nullptr);
            const UINT blockId = (id >> 4) + 1;
            const UINT indexInBlock = id & 0x0F;

            HRSRC hRes = FindResourceExW(hMod, RT_STRING, MAKEINTRESOURCEW(blockId), langId);
            if (!hRes)
                return {};

            HGLOBAL hGlob = LoadResource(hMod, hRes);
            if (!hGlob)
                return {};

            const auto* pData = static_cast<const wchar_t*>(LockResource(hGlob));
            if (!pData)
                return {};

            const DWORD totalWords = SizeofResource(hMod, hRes) / sizeof(wchar_t);
            DWORD offset = 0;
            for (UINT i = 0; i < 16 && offset < totalWords; ++i)
            {
                const WORD len = static_cast<WORD>(pData[offset++]);
                if (i == indexInBlock)
                {
                    if (len == 0 || offset + len > totalWords)
                        return {};
                    return std::wstring(pData + offset, len);
                }
                offset += len;
            }
            return {};
        }

        std::string ReadWholeFileBytes(const std::wstring& path)
        {
            HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE)
                return {};

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > (4 * 1024 * 1024))
            {
                CloseHandle(hFile);
                return {};
            }

            std::string buf(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            const BOOL ok = ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
            CloseHandle(hFile);
            if (!ok || read == 0)
                return {};
            buf.resize(read);
            return buf;
        }

        std::wstring StemOfFilename(std::wstring_view filename)
        {
            const std::size_t dot = filename.find_last_of(L'.');
            if (dot == std::wstring_view::npos)
                return std::wstring(filename);
            return std::wstring(filename.substr(0, dot));
        }

        void ReadLngMetadata(const std::wstring& filePath, std::wstring_view stem, Language& outLang)
        {
            outLang.code = LowerAscii(stem);
            outLang.name = std::wstring(stem);

            const std::string raw = ReadWholeFileBytes(filePath);
            if (raw.empty())
                return;

            std::string_view sv(raw);
            if (sv.size() >= 3 &&
                static_cast<unsigned char>(sv[0]) == 0xEF &&
                static_cast<unsigned char>(sv[1]) == 0xBB &&
                static_cast<unsigned char>(sv[2]) == 0xBF)
            {
                sv.remove_prefix(3);
            }

            int linesChecked = 0;
            while (!sv.empty() && linesChecked < 16)
            {
                const std::size_t nl = sv.find('\n');
                std::string_view line = (nl == std::string_view::npos) ? sv : sv.substr(0, nl);
                if (nl == std::string_view::npos)
                    sv = {};
                else
                    sv.remove_prefix(nl + 1);

                const std::wstring wline = Trim(text::Utf8ToWide(line));
                if (wline.rfind(L"#name=", 0) == 0)
                {
                    const std::wstring n = Trim(std::wstring_view(wline).substr(6));
                    if (!n.empty())
                        outLang.name = n;
                }
                else if (wline.rfind(L"#locale=", 0) == 0)
                {
                    const std::wstring c = LowerAscii(Trim(std::wstring_view(wline).substr(8)));
                    if (!c.empty())
                        outLang.code = c;
                }
                ++linesChecked;
            }
        }

        void LoadLngEntries(const std::wstring& filePath, std::vector<IdEntry>& outEntries)
        {
            outEntries.clear();
            if (filePath.empty())
                return;

            const std::string raw = ReadWholeFileBytes(filePath);
            if (raw.empty())
                return;

            std::string_view sv(raw);
            if (sv.size() >= 3 &&
                static_cast<unsigned char>(sv[0]) == 0xEF &&
                static_cast<unsigned char>(sv[1]) == 0xBB &&
                static_cast<unsigned char>(sv[2]) == 0xBF)
            {
                sv.remove_prefix(3);
            }

            while (!sv.empty())
            {
                const std::size_t nl = sv.find('\n');
                std::string_view line = (nl == std::string_view::npos) ? sv : sv.substr(0, nl);
                if (nl == std::string_view::npos)
                    sv = {};
                else
                    sv.remove_prefix(nl + 1);

                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);

                const std::wstring wline = text::Utf8ToWide(line);
                const std::wstring trimmed = Trim(wline);
                if (trimmed.empty() || trimmed[0] == L'#' || trimmed[0] == L';')
                    continue;

                const std::size_t eq = wline.find(L'=');
                if (eq == std::wstring::npos)
                    continue;

                UINT id = 0;
                if (!ParseUint(Trim(std::wstring_view(wline).substr(0, eq)), id) || id == 0)
                    continue;

                std::wstring val = Unescape(std::wstring_view(wline).substr(eq + 1));
                bool updated = false;
                for (auto& existing : outEntries)
                {
                    if (existing.id == id)
                    {
                        existing.value = std::move(val);
                        updated = true;
                        break;
                    }
                }
                if (!updated)
                    outEntries.push_back({id, std::move(val)});
            }
        }

        std::wstring ExecutableDirectory()
        {
            wchar_t buf[MAX_PATH]{};
            const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (len == 0 || len >= MAX_PATH)
                return paths::CurrentDirectory();
            std::wstring full(buf, len);
            const std::size_t slash = full.find_last_of(L"\\/");
            return (slash == std::wstring::npos) ? full : full.substr(0, slash);
        }

        void ScanCatalog()
        {
            auto& st = State();
            st.catalog.clear();
            st.publicList.clear();

            // Built-in English & Polish resources
            st.catalog.push_back({{L"en", L"English"}, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), {}});
            st.catalog.push_back({{L"pl", L"Polski"},  MAKELANGID(LANG_POLISH, SUBLANG_DEFAULT), {}});

            // Scan optional language\*.lng next to the executable (or in current directory)
            const std::wstring dirs[2] = {
                paths::Join(ExecutableDirectory(), L"language"),
                paths::Join(paths::CurrentDirectory(), L"language")
            };

            for (const auto& dir : dirs)
            {
                const std::wstring pattern = paths::Join(dir, L"*.lng");
                WIN32_FIND_DATAW fd{};
                HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE)
                    continue;

                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        continue;

                    const std::wstring fullPath = paths::Join(dir, fd.cFileName);
                    const std::wstring stem = StemOfFilename(fd.cFileName);
                    Language meta;
                    ReadLngMetadata(fullPath, stem, meta);
                    if (meta.code.empty() || meta.code == L"en")
                        continue;

                    bool merged = false;
                    for (auto& item : st.catalog)
                    {
                        if (item.info.code == meta.code)
                        {
                            item.info.name = meta.name;
                            // Built-in languages are authoritative. External .lng files extend
                            // the catalog for languages that are not compiled into the binary.
                            if (item.langId == 0 && item.filePath.empty())
                                item.filePath = fullPath;
                            merged = true;
                            break;
                        }
                    }
                    if (!merged)
                    {
                        st.catalog.push_back({meta, 0, fullPath});
                    }
                } while (FindNextFileW(hFind, &fd));

                FindClose(hFind);
            }

            for (const auto& item : st.catalog)
                st.publicList.push_back(item.info);

            st.catalogScanned = true;
        }

        const CatalogItem* MatchLanguage(std::wstring_view requested)
        {
            auto& st = State();
            if (!st.catalogScanned)
                ScanCatalog();

            const std::wstring code = LowerAscii(Trim(requested));
            for (const auto& item : st.catalog)
            {
                if (item.info.code == code)
                    return &item;
            }
            const std::size_t dash = code.find_first_of(L"-_");
            if (dash != std::wstring::npos)
            {
                const std::wstring base = code.substr(0, dash);
                for (const auto& item : st.catalog)
                {
                    if (item.info.code == base)
                        return &item;
                }
            }
            return st.catalog.empty() ? nullptr : &st.catalog.front();
        }

        std::wstring ReadSavedLanguage()
        {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                return L"auto";

            wchar_t buf[64]{};
            DWORD bytes = sizeof(buf);
            DWORD type = 0;
            const LSTATUS status = RegQueryValueExW(hKey, kRegLanguage, nullptr, &type,
                                                    reinterpret_cast<LPBYTE>(buf), &bytes);
            RegCloseKey(hKey);

            if (status != ERROR_SUCCESS || type != REG_SZ || buf[0] == L'\0')
                return L"auto";
            return buf;
        }

        void WriteSavedLanguage(std::wstring_view code)
        {
            HKEY hKey = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
                return;

            std::wstring val(code);
            RegSetValueExW(hKey, kRegLanguage, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(val.c_str()),
                           static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }

    const std::vector<Language>& Text::Available()
    {
        ScanCatalog();
        return State().publicList;
    }

    std::wstring Text::Detect()
    {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) <= 0)
            return L"en";

        if (const auto* match = MatchLanguage(locale))
            return match->info.code;
        return L"en";
    }

    void Text::UseSaved()
    {
        ScanCatalog();
        const std::wstring saved = LowerAscii(Trim(ReadSavedLanguage()));
        Use(saved.empty() ? L"auto" : saved, false);
    }

    void Text::Use(std::wstring_view code, bool save)
    {
        auto& st = State();
        if (!st.catalogScanned)
            ScanCatalog();

        const std::wstring clean = LowerAscii(Trim(code));
        st.automatic = clean.empty() || clean == L"auto";
        const std::wstring target = st.automatic ? Detect() : clean;
        const CatalogItem* item = MatchLanguage(target);

        st.fileEntries.clear();
        if (item)
        {
            st.currentCode = item->info.code;
            st.currentLangId = item->langId;
            if (!item->filePath.empty())
                LoadLngEntries(item->filePath, st.fileEntries);
        }
        else
        {
            st.currentCode = L"en";
            st.currentLangId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        }

        if (save)
            WriteSavedLanguage(st.automatic ? L"auto" : st.currentCode);
    }

    std::wstring Text::Current()
    {
        return State().currentCode;
    }

    bool Text::Automatic()
    {
        return State().automatic;
    }

    std::wstring Text::English(UINT id)
    {
        return LoadResourceStringForLang(id, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    }

    std::wstring Text::Get(UINT id)
    {
        auto& st = State();
        // 1. Check loaded .lng file overrides first
        for (const auto& entry : st.fileEntries)
        {
            if (entry.id == id)
                return entry.value;
        }

        // 2. Check embedded resource for current language
        if (st.currentLangId != 0)
        {
            std::wstring res = LoadResourceStringForLang(id, st.currentLangId);
            if (!res.empty())
                return res;
        }

        // 3. Fallback to embedded English resource
        return English(id);
    }

    std::wstring Text::Format(UINT id, std::wstring_view arg0)
    {
        std::wstring tpl = Get(id);
        const std::size_t pos = tpl.find(L"{}");
        if (pos != std::wstring::npos)
            tpl.replace(pos, 2, arg0);
        return tpl;
    }

    std::wstring Text::Format(UINT id, int arg0)
    {
        wchar_t numBuf[32]{};
        wsprintfW(numBuf, L"%d", arg0);
        return Format(id, std::wstring_view(numBuf));
    }

    std::wstring Text::Format(UINT id, std::wstring_view arg0, std::wstring_view arg1)
    {
        std::wstring tpl = Get(id);
        std::size_t pos = tpl.find(L"{}");
        if (pos != std::wstring::npos)
        {
            tpl.replace(pos, 2, arg0);
            pos = tpl.find(L"{}", pos + arg0.size());
            if (pos != std::wstring::npos)
                tpl.replace(pos, 2, arg1);
        }
        return tpl;
    }

    std::wstring Text::Format(UINT id, const std::string_view* arguments,
                              std::size_t count)
    {
        std::wstring text = Get(id);
        std::size_t searchFrom = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t pos = text.find(L"{}", searchFrom);
            if (pos == std::wstring::npos)
                break;

            const std::wstring replacement = text::Utf8ToWide(arguments[i]);
            text.replace(pos, 2, replacement);
            searchFrom = pos + replacement.size();
        }
        return text;
    }
}
