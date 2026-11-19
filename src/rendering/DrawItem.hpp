/**
 * @file DrawItem.hpp
 * @brief A deferred world draw call, sorted by its foot y so sprites overlap correctly.
 */
#pragma once
#include <functional>
#include <vector>

struct DrawItem
{
    float y;
    std::function<void()> fn;
};

using DrawList = std::vector<DrawItem>;
