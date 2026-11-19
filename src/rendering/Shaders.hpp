#pragma once
// Shader loading with graceful fallback, and a uniform setter that ignores missing uniforms.
#include <raylib.h>

Shader loadShader(const char* name, bool& ok);   // assets/shaders/<name>; ok == false -> plain rendering
void setU(Shader& s, const char* n, const void* v, int type, int count = 1);
