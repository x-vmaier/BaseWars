#include "rendering/Shaders.hpp"

#include "rendering/Assets.hpp"

#include <string>

Shader loadShader(const char *name, bool &ok)
{
    Shader s = LoadShader(nullptr, asset(std::string("shaders/") + name).c_str());
    ok = s.id > 0 && IsShaderValid(s);
    if (!ok)
        TraceLog(LOG_WARNING, "shader %s unavailable; falling back to plain rendering", name);
    return s;
}
void setU(Shader &s, const char *n, const void *v, int type, int count)
{
    int l = GetShaderLocation(s, n);
    if (l >= 0)
        SetShaderValueV(s, l, v, type, count);
}
