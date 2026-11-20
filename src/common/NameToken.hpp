/**
 * @file NameToken.hpp
 * @brief Player names are never stored or trusted by the server: it writes `\x01<SteamID>\x02` tokens into text and
 * clients replace them with the Steam persona name. Chat text is stripped of control characters, so tokens cannot be
 * forged.
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>

inline std::string nameToken(uint64_t steamId)
{
    return "\x01" + std::to_string(steamId) + "\x02";
}

// Replaces every well-formed token in `text` by `resolve(steamId)`.
inline std::string expandNameTokens(const std::string &text, const std::function<std::string(uint64_t)> &resolve)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        size_t end = text[i] == '\x01' ? text.find('\x02', i + 1) : std::string::npos;
        if (end == std::string::npos || end - i - 1 == 0 || end - i - 1 > 20)
        {
            out += text[i];
            continue;
        }
        out += resolve(std::strtoull(text.substr(i + 1, end - i - 1).c_str(), nullptr, 10));
        i = end;
    }
    return out;
}
