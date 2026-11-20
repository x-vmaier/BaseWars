/**
 * @file Serialization.hpp
 * @brief Little-endian Writer / Reader for protocol messages and savegames.
 */
#pragma once
#include "common/Shared.hpp"

// serialization
struct Writer
{
    std::vector<uint8_t> b;

    template <class T> Writer &put(T v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        auto p = reinterpret_cast<const uint8_t *>(&v);
        b.insert(b.end(), p, p + sizeof(T));
        return *this;
    }

    void bytes(const void *d, size_t n)
    {
        auto p = static_cast<const uint8_t *>(d);
        b.insert(b.end(), p, p + n);
    }

    void str(const std::string &s)
    {
        put<uint16_t>(uint16_t(std::min<size_t>(s.size(), 65535)));
        bytes(s.data(), std::min<size_t>(s.size(), 65535));
    }
};

struct Reader
{
    const uint8_t *p;
    const uint8_t *e;
    bool ok = true;

    template <class T> T get()
    {
        T v{};
        if (size_t(e - p) < sizeof(T))
        {
            ok = false;
            return v;
        }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }

    const uint8_t *bytes(size_t n)
    {
        if (size_t(e - p) < n)
        {
            ok = false;
            return nullptr;
        }
        auto r = p;
        p += n;
        return r;
    }

    std::string str()
    {
        size_t n = get<uint16_t>();
        auto d = bytes(n);
        return d ? std::string(reinterpret_cast<const char *>(d), n) : std::string();
    }
};
