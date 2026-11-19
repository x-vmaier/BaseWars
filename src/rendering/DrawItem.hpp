#pragma once
// A deferred world draw call, sorted by its foot y so sprites overlap correctly.
#include <functional>
#include <vector>

struct DrawItem
{
    float y;
    std::function<void()> fn;
};

using DrawList = std::vector<DrawItem>;
