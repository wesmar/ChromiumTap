// gui/Handle.h - RAII wrappers for Win32 and COM resources (Zero CRT)
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <utility>

namespace kvc::gui
{
    // Generic RAII handle owner. Traits provides Type, Invalid() and Close().
    template <class Traits>
    class Unique
    {
    public:
        using Type = typename Traits::Type;

        Unique() noexcept = default;
        explicit Unique(Type value) noexcept : m_value(value) {}
        Unique(const Unique&) = delete;
        Unique& operator=(const Unique&) = delete;
        Unique(Unique&& other) noexcept : m_value(std::exchange(other.m_value, Traits::Invalid())) {}
        Unique& operator=(Unique&& other) noexcept
        {
            if (this != &other)
                reset(std::exchange(other.m_value, Traits::Invalid()));
            return *this;
        }
        ~Unique() { reset(); }

        [[nodiscard]] Type get() const noexcept { return m_value; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_value != Traits::Invalid(); }
        [[nodiscard]] Type* put() noexcept { reset(); return &m_value; }
        Type release() noexcept { return std::exchange(m_value, Traits::Invalid()); }
        void reset(Type value = Traits::Invalid()) noexcept
        {
            if (m_value != Traits::Invalid())
                Traits::Close(m_value);
            m_value = value;
        }

    private:
        Type m_value = Traits::Invalid();
    };

    namespace traits
    {
        template <class T, auto CloseFn>
        struct NullIsInvalid
        {
            using Type = T;
            static Type Invalid() noexcept { return nullptr; }
            static void Close(Type h) noexcept { if (h) CloseFn(h); }
        };

        struct File
        {
            using Type = HANDLE;
            static Type Invalid() noexcept { return INVALID_HANDLE_VALUE; }
            static void Close(Type h) noexcept { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
        };

        inline void CloseKernel(HANDLE h) noexcept { CloseHandle(h); }
        inline void CloseKey(HKEY h) noexcept { RegCloseKey(h); }
        inline void DeleteGdi(HGDIOBJ h) noexcept { DeleteObject(h); }
        inline void DestroyMenuHandle(HMENU h) noexcept { DestroyMenu(h); }
        inline void DestroyAccel(HACCEL h) noexcept { DestroyAcceleratorTable(h); }
        inline void DeleteMemoryDc(HDC h) noexcept { DeleteDC(h); }
        inline void DestroyIconHandle(HICON h) noexcept { DestroyIcon(h); }
        inline void Unhook(HHOOK h) noexcept { UnhookWindowsHookEx(h); }
    }

    using FileHandle = Unique<traits::File>;
    using KernelHandle = Unique<traits::NullIsInvalid<HANDLE, traits::CloseKernel>>;
    using RegKey = Unique<traits::NullIsInvalid<HKEY, traits::CloseKey>>;
    using MenuHandle = Unique<traits::NullIsInvalid<HMENU, traits::DestroyMenuHandle>>;
    using AccelHandle = Unique<traits::NullIsInvalid<HACCEL, traits::DestroyAccel>>;
    using MemoryDc = Unique<traits::NullIsInvalid<HDC, traits::DeleteMemoryDc>>;
    using IconHandle = Unique<traits::NullIsInvalid<HICON, traits::DestroyIconHandle>>;
    using HookHandle = Unique<traits::NullIsInvalid<HHOOK, traits::Unhook>>;

    template <class T>
    using Gdi = Unique<traits::NullIsInvalid<T, traits::DeleteGdi>>;

    // Window DC from GetDC, released on scope exit
    class WindowDc
    {
    public:
        explicit WindowDc(HWND window) noexcept : m_window(window), m_dc(GetDC(window)) {}
        WindowDc(const WindowDc&) = delete;
        WindowDc& operator=(const WindowDc&) = delete;
        ~WindowDc() { if (m_dc) ReleaseDC(m_window, m_dc); }
        [[nodiscard]] HDC get() const noexcept { return m_dc; }

    private:
        HWND m_window;
        HDC m_dc;
    };

    // BeginPaint / EndPaint RAII scope
    class PaintScope
    {
    public:
        explicit PaintScope(HWND hwnd) noexcept : m_hwnd(hwnd), m_dc(BeginPaint(hwnd, &m_ps)) {}
        PaintScope(const PaintScope&) = delete;
        PaintScope& operator=(const PaintScope&) = delete;
        ~PaintScope() { EndPaint(m_hwnd, &m_ps); }

        [[nodiscard]] HDC dc() const noexcept { return m_dc; }
        [[nodiscard]] const PAINTSTRUCT& ps() const noexcept { return m_ps; }
        [[nodiscard]] const RECT& rcPaint() const noexcept { return m_ps.rcPaint; }

    private:
        HWND m_hwnd;
        PAINTSTRUCT m_ps{};
        HDC m_dc;
    };

    // Selects GDI object and restores previous on scope exit
    class SelectGuard
    {
    public:
        SelectGuard(HDC dc, HGDIOBJ object) noexcept : m_dc(dc), m_old(SelectObject(dc, object)) {}
        SelectGuard(const SelectGuard&) = delete;
        SelectGuard& operator=(const SelectGuard&) = delete;
        ~SelectGuard() { if (m_old) SelectObject(m_dc, m_old); }

    private:
        HDC m_dc;
        HGDIOBJ m_old;
    };

    // Zero-CRT COM smart pointer template
    template <typename T>
    class ComPtr
    {
    public:
        ComPtr() noexcept : m_ptr(nullptr) {}
        ComPtr(decltype(nullptr)) noexcept : m_ptr(nullptr) {}
        explicit ComPtr(T* p) noexcept : m_ptr(p)
        {
            if (m_ptr) m_ptr->AddRef();
        }
        ComPtr(const ComPtr& other) noexcept : m_ptr(other.m_ptr)
        {
            if (m_ptr) m_ptr->AddRef();
        }
        ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr)
        {
            other.m_ptr = nullptr;
        }
        ~ComPtr()
        {
            Reset();
        }

        ComPtr& operator=(const ComPtr& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_ptr = other.m_ptr;
                if (m_ptr) m_ptr->AddRef();
            }
            return *this;
        }

        ComPtr& operator=(ComPtr&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        ComPtr& operator=(decltype(nullptr)) noexcept
        {
            Reset();
            return *this;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator*() const noexcept { return *m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        T** GetAddressOf() noexcept { return &m_ptr; }
        T** ReleaseAndGetAddressOf() noexcept { Reset(); return &m_ptr; }
        T** put() noexcept { Reset(); return &m_ptr; }

        void Reset() noexcept
        {
            if (m_ptr)
            {
                T* temp = m_ptr;
                m_ptr = nullptr;
                temp->Release();
            }
        }

        T* Detach() noexcept
        {
            T* temp = m_ptr;
            m_ptr = nullptr;
            return temp;
        }

        void Attach(T* p) noexcept
        {
            Reset();
            m_ptr = p;
        }

    private:
        T* m_ptr;
    };
}
