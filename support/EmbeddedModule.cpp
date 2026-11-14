#include "EmbeddedModule.h"

#include "RuntimeSupport.h"
#include "resource.h"

#include <bcrypt.h>
#include <fcntl.h>
#include <fdi.h>
#include <shlobj.h>
#include <cstdint>

namespace
{
    using U8 = std::uint8_t;
    using U32 = std::uint32_t;

    constexpr SIZE_T kMaxContainerBytes = 64ull * 1024ull * 1024ull;
    constexpr U32 kContainerVersion = 3;
    constexpr char kContainerMagic[8] = {'I', 'M', 'P', 'L', '0', '0', '0', '3'};
    constexpr U32 kFileFlagDosHeaderStripped = 0x00000001;
    constexpr U8 kMaskedCabMagic[4] = {
        static_cast<U8>('M' ^ 0xA5),
        static_cast<U8>('S' ^ 0x5A),
        static_cast<U8>('C' ^ 0xF0),
        static_cast<U8>('F' ^ 0x0F)
    };

    struct Buffer
    {
        U8* data = nullptr;
        SIZE_T size = 0;
        SIZE_T capacity = 0;

        Buffer() = default;
        ~Buffer() { Clear(); }
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&& other) noexcept
            : data(other.data), size(other.size), capacity(other.capacity)
        {
            other.data = nullptr;
            other.size = 0;
            other.capacity = 0;
        }

        Buffer& operator=(Buffer&& other) noexcept
        {
            if (this != &other)
            {
                Clear();
                data = other.data;
                size = other.size;
                capacity = other.capacity;
                other.data = nullptr;
                other.size = 0;
                other.capacity = 0;
            }
            return *this;
        }

        void Clear() noexcept
        {
            if (data)
                HeapFree(GetProcessHeap(), 0, data);
            data = nullptr;
            size = 0;
            capacity = 0;
        }

        bool Reserve(SIZE_T requested) noexcept
        {
            if (requested <= capacity)
                return true;
            if (requested > kMaxContainerBytes)
                return false;

            SIZE_T next = capacity == 0 ? 4096 : capacity;
            while (next < requested)
            {
                if (next > kMaxContainerBytes / 2)
                {
                    next = requested;
                    break;
                }
                next *= 2;
            }

            void* memory = data
                ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, data, next)
                : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, next);
            if (!memory)
                return false;

            data = static_cast<U8*>(memory);
            capacity = next;
            return true;
        }

        bool Resize(SIZE_T requested) noexcept
        {
            if (!Reserve(requested))
                return false;
            if (requested > size)
                memset(data + size, 0, requested - size);
            size = requested;
            return true;
        }
    };

    struct FileRecord
    {
        wchar_t name[128];
        std::uint64_t offset;
        std::uint64_t storedSize;
        std::uint64_t originalSize;
        U8 storedSha256[32];
        U8 originalSha256[32];
        U32 flags;
        U32 eLfanew;
    };

    struct ContainerHeader
    {
        char magic[8];
        U32 version;
        U32 count;
        U32 headerSize;
        U32 dataOffset;
        std::uint64_t dataSize;
        U8 templateSha256[32];
        U8 xorKeySha256[32];
        U32 xorMode;
        U32 xorKeyLength;
        U32 flags;
        U32 reserved;
    };

    struct ContainerView
    {
        const ContainerHeader* header = nullptr;
        const FileRecord* records = nullptr;
        const U8* data = nullptr;
    };

    U32 ReadU32(const U8* source) noexcept
    {
        return static_cast<U32>(source[0])
            | (static_cast<U32>(source[1]) << 8)
            | (static_cast<U32>(source[2]) << 16)
            | (static_cast<U32>(source[3]) << 24);
    }

    void WriteU32(U8* destination, U32 value) noexcept
    {
        destination[0] = static_cast<U8>(value);
        destination[1] = static_cast<U8>(value >> 8);
        destination[2] = static_cast<U8>(value >> 16);
        destination[3] = static_cast<U8>(value >> 24);
    }

    bool AddSize(SIZE_T left, SIZE_T right, SIZE_T& result) noexcept
    {
        if (right > static_cast<SIZE_T>(-1) - left)
            return false;
        result = left + right;
        return true;
    }

    bool UnwrapBitmap(const U8* bitmap, SIZE_T bitmapSize, Buffer& cab) noexcept
    {
        if (!bitmap || bitmapSize < 40)
            return false;

        U32 cabSize = 0;
        U32 declaredSize = 0;
        SIZE_T pixelOffset = 0;
        if (bitmapSize >= 54 && bitmap[0] == 'B' && bitmap[1] == 'M')
        {
            cabSize = ReadU32(bitmap + 6);
            pixelOffset = ReadU32(bitmap + 10);
            declaredSize = ReadU32(bitmap + 2);
            if (declaredSize > bitmapSize)
                return false;
        }
        else
        {
            const U32 dibSize = ReadU32(bitmap);
            const std::uint16_t bitsPerPixel = static_cast<std::uint16_t>(
                bitmap[14] | (static_cast<std::uint16_t>(bitmap[15]) << 8));
            const U32 compression = ReadU32(bitmap + 16);
            const U32 colorsUsed = ReadU32(bitmap + 32);
            if (dibSize < 40 || dibSize > bitmapSize ||
                bitsPerPixel != 24 || compression != 0 ||
                colorsUsed > (bitmapSize - dibSize) / 4)
                return false;

            cabSize = ReadU32(bitmap + 36);
            pixelOffset = static_cast<SIZE_T>(dibSize) +
                          static_cast<SIZE_T>(colorsUsed) * 4;
        }

        if (cabSize < 4 || pixelOffset > bitmapSize ||
            cabSize > bitmapSize - pixelOffset || declaredSize > bitmapSize)
            return false;

        if (!cab.Resize(cabSize))
            return false;
        memcpy(cab.data, bitmap + pixelOffset, cabSize);
        return true;
    }

    bool UndoCabMask(Buffer& cab) noexcept
    {
        if (!cab.data || cab.size < 4)
            return false;
        for (SIZE_T i = 0; i < 4; ++i)
            cab.data[i] ^= kMaskedCabMagic[i];
        return cab.data[0] == 'M' && cab.data[1] == 'S' &&
               cab.data[2] == 'C' && cab.data[3] == 'F';
    }

    struct FdiInput
    {
        SIZE_T offset = 0;
        bool active = false;
    };

    struct FdiState
    {
        const U8* data = nullptr;
        SIZE_T size = 0;
        FdiInput inputs[8];
        Buffer output;
        SIZE_T position = 0;
        bool active = false;
        bool complete = false;
    };

    FdiState* g_fdi = nullptr;

    LPVOID DIAMONDAPI FdiAlloc(ULONG size)
    {
        return HeapAlloc(GetProcessHeap(), 0, size);
    }

    void DIAMONDAPI FdiFree(LPVOID memory)
    {
        if (memory)
            HeapFree(GetProcessHeap(), 0, memory);
    }

    INT_PTR DIAMONDAPI FdiOpen(char*, int, int)
    {
        if (!g_fdi)
            return -1;
        for (int i = 0; i < 8; ++i)
        {
            if (!g_fdi->inputs[i].active)
            {
                g_fdi->inputs[i].active = true;
                g_fdi->inputs[i].offset = 0;
                return i + 1;
            }
        }
        return -1;
    }

    UINT DIAMONDAPI FdiRead(INT_PTR handle, void* destination, UINT requested)
    {
        if (!g_fdi || handle < 1 || handle > 8)
            return 0;
        FdiInput& input = g_fdi->inputs[handle - 1];
        if (!input.active || input.offset >= g_fdi->size)
            return 0;
        const SIZE_T available = g_fdi->size - input.offset;
        const SIZE_T amount = requested < available ? requested : available;
        memcpy(destination, g_fdi->data + input.offset, amount);
        input.offset += amount;
        return static_cast<UINT>(amount);
    }

    UINT DIAMONDAPI FdiWrite(INT_PTR handle, void* source, UINT count)
    {
        if (!g_fdi || handle != 0x1001 || !g_fdi->active || count == 0)
            return 0;
        SIZE_T end = 0;
        if (!AddSize(g_fdi->position, count, end) || !g_fdi->output.Resize(end))
            return 0;
        memcpy(g_fdi->output.data + g_fdi->position, source, count);
        g_fdi->position = end;
        return count;
    }

    int DIAMONDAPI FdiClose(INT_PTR handle)
    {
        if (!g_fdi)
            return -1;
        if (handle >= 1 && handle <= 8)
        {
            g_fdi->inputs[handle - 1].active = false;
            return 0;
        }
        if (handle == 0x1001)
        {
            g_fdi->complete = true;
            return 0;
        }
        return -1;
    }

    long DIAMONDAPI FdiSeek(INT_PTR handle, long distance, int origin)
    {
        if (!g_fdi)
            return -1;
        if (handle >= 1 && handle <= 8)
        {
            FdiInput& input = g_fdi->inputs[handle - 1];
            std::int64_t next = 0;
            if (origin == SEEK_SET)
                next = distance;
            else if (origin == SEEK_CUR)
                next = static_cast<std::int64_t>(input.offset) + distance;
            else if (origin == SEEK_END)
                next = static_cast<std::int64_t>(g_fdi->size) + distance;
            else
                return -1;
            if (next < 0 || static_cast<SIZE_T>(next) > g_fdi->size)
                return -1;
            input.offset = static_cast<SIZE_T>(next);
            return static_cast<long>(input.offset);
        }
        if (handle == 0x1001)
        {
            std::int64_t next = 0;
            if (origin == SEEK_SET)
                next = distance;
            else if (origin == SEEK_CUR)
                next = static_cast<std::int64_t>(g_fdi->position) + distance;
            else if (origin == SEEK_END)
                next = static_cast<std::int64_t>(g_fdi->output.size) + distance;
            else
                return -1;
            if (next < 0)
                return -1;
            if (!g_fdi->output.Resize(static_cast<SIZE_T>(next)))
                return -1;
            g_fdi->position = static_cast<SIZE_T>(next);
            return static_cast<long>(g_fdi->position);
        }
        return -1;
    }

    INT_PTR DIAMONDAPI FdiNotify(FDINOTIFICATIONTYPE type, PFDINOTIFICATION notification)
    {
        if (!g_fdi || !notification)
            return -1;
        if (type == fdintCOPY_FILE)
        {
            if (g_fdi->active)
                return 0;
            g_fdi->active = true;
            g_fdi->position = 0;
            g_fdi->output.Clear();
            return 0x1001;
        }
        if (type == fdintCLOSE_FILE_INFO)
        {
            if (notification->hf == 0x1001)
                g_fdi->complete = true;
            return TRUE;
        }
        return 0;
    }

    bool ExtractCab(const U8* cabData, SIZE_T cabSize, Buffer& moduleImage)
    {
        if (!cabData || cabSize < 4)
            return false;

        FdiState state;
        state.data = cabData;
        state.size = cabSize;
        g_fdi = &state;

        ERF error{};
        HFDI fdi = FDICreate(FdiAlloc, FdiFree, FdiOpen, FdiRead, FdiWrite,
                             FdiClose, FdiSeek, cpuUNKNOWN, &error);
        if (!fdi)
        {
            g_fdi = nullptr;
            return false;
        }

        char name[] = "module.cab";
        char path[] = "";
        const BOOL copied = FDICopy(fdi, name, path, 0, FdiNotify, nullptr, nullptr);
        FDIDestroy(fdi);
        g_fdi = nullptr;

        if (!copied || !state.complete)
            return false;
        moduleImage = static_cast<Buffer&&>(state.output);
        return true;
    }

    bool OpenContainer(const U8* data, SIZE_T size, ContainerView& output) noexcept
    {
        if (!data || size < sizeof(ContainerHeader))
            return false;
        const auto* header = reinterpret_cast<const ContainerHeader*>(data);
        if (memcmp(header->magic, kContainerMagic, sizeof(kContainerMagic)) != 0 ||
            header->version != kContainerVersion || header->count != 1 ||
            header->headerSize != sizeof(ContainerHeader) ||
            header->xorMode != 1)
            return false;

        const SIZE_T recordsSize = sizeof(FileRecord);
        if (header->dataOffset < sizeof(ContainerHeader) + recordsSize ||
            header->dataOffset > size ||
            header->dataSize > size - header->dataOffset)
            return false;

        const auto* record = reinterpret_cast<const FileRecord*>(data + header->headerSize);
        if (record->offset > header->dataSize ||
            record->storedSize > header->dataSize - record->offset)
            return false;

        output.header = header;
        output.records = record;
        output.data = data + header->dataOffset;
        return true;
    }

    bool ComputeSha256(const U8* data, SIZE_T size, U8 output[32])
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        PUCHAR object = nullptr;
        bool ok = false;

        do
        {
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                            nullptr, 0) < 0)
                break;

            DWORD objectSize = 0;
            DWORD resultSize = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&objectSize),
                                  sizeof(objectSize), &resultSize, 0) < 0)
                break;

            object = static_cast<PUCHAR>(HeapAlloc(GetProcessHeap(), 0, objectSize));
            if (!object)
                break;

            if (BCryptCreateHash(algorithm, &hash, object, objectSize,
                                 nullptr, 0, 0) < 0)
                break;

            SIZE_T offset = 0;
            while (offset < size)
            {
                const ULONG amount = static_cast<ULONG>(
                    size - offset < 1024 * 1024 ? size - offset : 1024 * 1024);
                if (BCryptHashData(hash, const_cast<PUCHAR>(data + offset), amount, 0) < 0)
                    break;
                offset += amount;
            }
            if (offset != size || BCryptFinishHash(hash, output, 32, 0) < 0)
                break;
            ok = true;
        } while (false);

        if (hash)
            BCryptDestroyHash(hash);
        if (object)
            HeapFree(GetProcessHeap(), 0, object);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        return ok;
    }

    bool Sha256Equals(const U8 left[32], const U8 right[32]) noexcept
    {
        U8 difference = 0;
        for (int i = 0; i < 32; ++i)
            difference |= static_cast<U8>(left[i] ^ right[i]);
        return difference == 0;
    }

    bool ResolveUserTempDirectory(std::wstring& directory, std::string& error)
    {
        PWSTR localAppData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
        {
            error = "Failed to resolve the current user's local application data directory.";
            return false;
        }

        directory = kvc::paths::Join(localAppData, L"Temp");
        CoTaskMemFree(localAppData);

        if (!kvc::paths::EnsureDirectory(directory))
        {
            error = "Failed to access the current user's temporary directory.";
            return false;
        }
        return true;
    }

    bool ReadKernelDosTemplate(U8 templateData[0x80], std::string& error)
    {
        wchar_t systemDirectory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(
            systemDirectory,
            static_cast<UINT>(std::size(systemDirectory)));
        if (length == 0 || length >= std::size(systemDirectory))
        {
            error = "Failed to resolve the Windows system directory.";
            return false;
        }

        const std::wstring path = kvc::paths::Join(
            systemDirectory,
            L"drivers\\ks.sys");
        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = "Failed to open the local ks.sys DOS-header template.";
            return false;
        }

        DWORD read = 0;
        const BOOL readOk = ReadFile(file, templateData, 0x80, &read, nullptr);
        CloseHandle(file);
        if (!readOk ||
            read != 0x80 ||
            templateData[0] != 'M' ||
            templateData[1] != 'Z')
        {
            error = "The local ks.sys DOS-header template is invalid.";
            return false;
        }
        return true;
    }
}

namespace kvc
{
    EmbeddedModule::~EmbeddedModule()
    {
        Close();
    }

    void EmbeddedModule::Close() noexcept
    {
        if (m_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
        m_path.clear();
    }

    bool EmbeddedModule::Extract(HMODULE owner, std::string& error)
    {
        Close();

        HRSRC resource = FindResourceW(owner, MAKEINTRESOURCEW(IDR_WALLPAPER_BMP), RT_BITMAP);
        if (!resource)
        {
            error = "Embedded wallpaper.bmp module resource was not found.";
            return false;
        }
        HGLOBAL loaded = LoadResource(owner, resource);
        const DWORD resourceSize = SizeofResource(owner, resource);
        const auto* resourceData = loaded
                                       ? static_cast<const U8*>(LockResource(loaded))
                                       : nullptr;
        if (!loaded || !resourceData || resourceSize < 54)
        {
            error = "Embedded wallpaper.bmp module resource is invalid.";
            return false;
        }

        Buffer cab;
        Buffer container;
        if (!UnwrapBitmap(resourceData, resourceSize, cab) ||
            !UndoCabMask(cab) ||
            !ExtractCab(cab.data, cab.size, container))
        {
            error = "Embedded extraction module could not be unpacked.";
            return false;
        }

        ContainerView view;
        if (!OpenContainer(container.data, container.size, view))
        {
            error = "Embedded extraction container is invalid.";
            return false;
        }

        const FileRecord& record = view.records[0];
        if (record.name[std::size(record.name) - 1] != L'\0' ||
            !kvc::text::IEqualsWide(record.name, L"ChromiumTap.dll"))
        {
            error = "Embedded container does not contain ChromiumTap.dll.";
            return false;
        }

        const U8* stored = view.data + static_cast<SIZE_T>(record.offset);
        U8 digest[32]{};
        if (!ComputeSha256(stored, static_cast<SIZE_T>(record.storedSize), digest) ||
            !Sha256Equals(digest, record.storedSha256))
        {
            error = "Embedded ChromiumTap.dll hash is invalid.";
            return false;
        }

        U8 templateData[0x80]{};
        if (!ReadKernelDosTemplate(templateData, error))
            return false;

        U8 templateDigest[32]{};
        if ((record.flags & kFileFlagDosHeaderStripped) != 0 &&
            (!ComputeSha256(templateData, 0x80, templateDigest) ||
             !Sha256Equals(templateDigest, view.header->templateSha256)))
        {
            error = "The local ks.sys DOS header does not match the container.";
            return false;
        }

        Buffer image;
        if ((record.flags & kFileFlagDosHeaderStripped) != 0)
        {
            SIZE_T originalSize = 0;
            if (record.originalSize != 0x80 + record.storedSize ||
                !AddSize(0x80, static_cast<SIZE_T>(record.storedSize), originalSize) ||
                !image.Resize(originalSize))
            {
                error = "Embedded PE size metadata is invalid.";
                return false;
            }
            memcpy(image.data, templateData, 0x80);
            WriteU32(image.data + 0x3C, record.eLfanew);
            memcpy(image.data + 0x80, stored, static_cast<SIZE_T>(record.storedSize));
        }
        else
        {
            if (record.originalSize != record.storedSize ||
                !image.Resize(static_cast<SIZE_T>(record.storedSize)))
            {
                error = "Embedded PE record is invalid.";
                return false;
            }
            memcpy(image.data, stored, image.size);
        }

        if (record.eLfanew > image.size - 4 ||
            image.data[record.eLfanew] != 'P' ||
            image.data[record.eLfanew + 1] != 'E' ||
            image.data[record.eLfanew + 2] != 0 ||
            image.data[record.eLfanew + 3] != 0 ||
            !ComputeSha256(image.data, image.size, digest) ||
            !Sha256Equals(digest, record.originalSha256))
        {
            error = "Reconstructed ChromiumTap.dll failed integrity verification.";
            return false;
        }

        std::wstring directory;
        if (!ResolveUserTempDirectory(directory, error))
            return false;

        wchar_t fileName[96]{};
        // Temporary diagnostic build: isolate temporary-file extension behavior.
        wsprintfW(fileName, L"wallpaper.bmp");
        m_path = kvc::paths::Join(directory, fileName);

        m_file = CreateFileW(m_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                             nullptr);
        if (m_file == INVALID_HANDLE_VALUE)
        {
            m_path.clear();
            error = "Failed to create the temporary extraction module.";
            return false;
        }

        SIZE_T offset = 0;
        while (offset < image.size)
        {
            DWORD written = 0;
            const DWORD amount = static_cast<DWORD>(
                image.size - offset < 1024 * 1024 ? image.size - offset : 1024 * 1024);
            if (!WriteFile(m_file, image.data + offset, amount, &written, nullptr) ||
                written == 0)
            {
                error = "Failed to write the temporary extraction module.";
                Close();
                return false;
            }
            offset += written;
        }
        FlushFileBuffers(m_file);
        return true;
    }
}
