/**
 * @file Assets.cpp
 * @brief Asset loading: asset directory lookup, texture/animation cache, a tiny JSON parser and Tiled (.tmj) UI maps.
 */
#include "rendering/Assets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

// assets
namespace
{
std::string assetRoot;
std::unordered_map<std::string, Texture2D> texCache;
} // namespace

void findAssets()
{
    std::vector<fs::path> bases{fs::path(GetApplicationDirectory()), fs::current_path()};
    for (auto b : bases)
    {
        for (int up = 0; up < 6; up++)
        {
            fs::path c = b / "assets";
            if (fs::exists(c / "Units") && fs::exists(c / "Terrain"))
            {
                assetRoot = c.lexically_normal().string();
                return;
            }
            if (!b.has_parent_path() || b.parent_path() == b)
                break;
            b = b.parent_path();
        }
    }

    assetRoot = "assets";
    TraceLog(LOG_ERROR, "assets directory not found next to the executable or in a parent folder");
}

std::string asset(const std::string &rel)
{
    return assetRoot + "/" + rel;
}

Texture2D tex(const std::string &rel)
{
    auto it = texCache.find(rel);
    if (it != texCache.end())
        return it->second;
    Texture2D t = LoadTexture(asset(rel).c_str());
    if (t.id == 0)
        TraceLog(LOG_WARNING, "missing texture %s", rel.c_str());
    else
        SetTextureFilter(t, TEXTURE_FILTER_POINT);
    texCache[rel] = t;
    return t;
}

Anim mkAnim(const std::string &rel, int fw, int fh, float fps, float fx, float fy)
{
    Texture2D t = tex(rel);
    return {t, fw, fh, std::max(1, t.width / fw), fps, fx, fy};
}

Rectangle frameRect(const Anim &a, int f)
{
    return {float(f % a.n * a.fw), 0, float(a.fw), float(a.fh)};
}

namespace
{
// tiny JSON (for Tiled .tmj)
struct J
{
    enum T
    {
        Null,
        Num,
        Str,
        Arr,
        Obj
    } t = Null;

    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    const J *get(const std::string &k) const
    {
        for (auto &p : o)
            if (p.first == k)
                return &p.second;
        return nullptr;
    }
};

struct JParser
{
    const std::string &s;
    size_t i = 0;
    bool ok = true;

    void ws()
    {
        while (i < s.size() && std::isspace(uint8_t(s[i])))
            i++;
    }

    std::string str()
    {
        std::string r;
        i++;
        while (i < s.size() && s[i] != '"')
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                i++;
                r += s[i] == 'n' ? '\n' : (s[i] == 't' ? '\t' : s[i]);
            }
            else
                r += s[i];
            i++;
        }
        i++;
        return r;
    }

    J parse()
    {
        J v;
        ws();
        if (i >= s.size())
        {
            ok = false;
            return v;
        }
        char c = s[i];
        if (c == '{')
        {
            v.t = J::Obj;
            i++;
            ws();
            while (i < s.size() && s[i] != '}')
            {
                ws();
                std::string k = str();
                ws();
                if (s[i] == ':')
                    i++;
                v.o.push_back({k, parse()});
                ws();
                if (s[i] == ',')
                    i++;
                ws();
            }
            i++;
        }
        else if (c == '[')
        {
            v.t = J::Arr;
            i++;
            ws();
            while (i < s.size() && s[i] != ']')
            {
                v.a.push_back(parse());
                ws();
                if (s[i] == ',')
                    i++;
                ws();
            }
            i++;
        }
        else if (c == '"')
        {
            v.t = J::Str;
            v.s = str();
        }
        else if (s.compare(i, 4, "true") == 0)
        {
            v.t = J::Num;
            v.n = 1;
            i += 4;
        }
        else if (s.compare(i, 5, "false") == 0)
        {
            v.t = J::Num;
            v.n = 0;
            i += 5;
        }
        else if (s.compare(i, 4, "null") == 0)
        {
            i += 4;
        }
        else
        {
            size_t e = i;
            while (e < s.size() && (std::isdigit(uint8_t(s[e])) || s[e] == '-' || s[e] == '+' || s[e] == '.' ||
                                    s[e] == 'e' || s[e] == 'E'))
                e++;
            if (e == i)
            {
                ok = false;
                i++;
                return v;
            }
            v.t = J::Num;
            v.n = std::atof(s.substr(i, e - i).c_str());
            i = e;
        }
        return v;
    }
};

} // namespace

// Tiled maps used as UI backgrounds
TMap loadTMap(const std::string &name)
{
    TMap m;
    std::string path = asset("ui/" + name + ".tmj");
    std::ifstream f(path);
    if (!f)
    {
        TraceLog(LOG_WARNING, "missing UI map %s", path.c_str());
        return m;
    }

    std::stringstream ss;
    ss << f.rdbuf();
    std::string txt = ss.str();
    JParser p{txt};
    J root = p.parse();
    if (!p.ok || root.t != J::Obj)
    {
        TraceLog(LOG_WARNING, "bad UI map %s", path.c_str());
        return m;
    }

    auto num = [&](const J &j, const char *k, double d)
    {
        auto v = j.get(k);
        return v ? v->n : d;
    };

    m.w = int(num(root, "width", 0));
    m.h = int(num(root, "height", 0));
    m.tw = int(num(root, "tilewidth", 64));
    m.th = int(num(root, "tileheight", 64));

    if (auto props = root.get("properties"))
        for (auto &pr : props->a)
        {
            auto nm = pr.get("name");
            auto v = pr.get("value");
            if (!nm || !v)
                continue;
            if (nm->s == "scale")
                m.scale = float(v->n);
            if (nm->s == "padding")
                m.padding = int(v->n);
        }

    if (auto ts = root.get("tilesets"))
        for (auto &t : ts->a)
        {
            auto img = t.get("image");
            if (!img)
            {
                TraceLog(LOG_WARNING, "%s: external tilesets are not supported; use 'Embed tileset' in Tiled",
                         name.c_str());
                continue;
            }

            std::string full = (fs::path(asset("ui")) / img->s).lexically_normal().string();
            Texture2D tx = LoadTexture(full.c_str());
            if (tx.id == 0)
            {
                TraceLog(LOG_WARNING, "missing tileset image %s", full.c_str());
                continue;
            }

            SetTextureFilter(tx, TEXTURE_FILTER_POINT);
            m.sets.push_back({int(num(t, "firstgid", 1)), int(num(t, "columns", 1)), int(num(t, "tilewidth", 64)),
                              int(num(t, "tileheight", 64)), tx});
        }

    if (auto ls = root.get("layers"))
        for (auto &l : ls->a)
        {
            auto ty = l.get("type"), d = l.get("data");
            if (!ty || ty->s != "tilelayer" || !d)
                continue;

            if (m.data.empty())
                m.data.assign(size_t(m.w) * size_t(m.h), 0);

            for (size_t i = 0; i < d->a.size() && i < m.data.size(); i++)
            {
                uint32_t g = uint32_t(d->a[i].n) & 0x1FFFFFFFu;
                if (g)
                    m.data[i] = g;
            }
        }

    std::sort(m.sets.begin(), m.sets.end(), [](const TMap::TS &a, const TMap::TS &b) { return a.first > b.first; });

    m.ok = m.w > 0 && m.h > 0 && !m.sets.empty() && !m.data.empty();
    TraceLog(LOG_INFO, "UI map %s: %dx%d tiles, scale %.2f, ok=%d", name.c_str(), m.w, m.h, m.scale, int(m.ok));
    return m;
}
// Draws the whole map stretched into `dst` (natural size when dst matches size()).
void drawTMap(const TMap &m, Rectangle dst, Color tint)
{
    if (!m.ok)
    {
        DrawRectangleRec(dst, rgb(60, 50, 40, 230));
        return;
    }

    float cw = dst.width / float(m.w), ch = dst.height / float(m.h);

    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++)
        {
            uint32_t g = m.data[size_t(y * m.w + x)];
            if (!g)
                continue;

            const TMap::TS *ts = nullptr;
            for (auto &s : m.sets)
                if (int(g) >= s.first)
                {
                    ts = &s;
                    break;
                }

            if (!ts)
                continue;

            int id = int(g) - ts->first;
            Rectangle src{float(id % ts->cols * ts->tw), float(id / ts->cols * ts->th), float(ts->tw), float(ts->th)};
            float x0 = std::floor(dst.x + float(x) * cw), x1 = std::floor(dst.x + float(x + 1) * cw);
            float y0 = std::floor(dst.y + float(y) * ch), y1 = std::floor(dst.y + float(y + 1) * ch);
            DrawTexturePro(ts->t, src, {x0, y0, x1 - x0, y1 - y0}, {0, 0}, 0, tint);
        }
}
