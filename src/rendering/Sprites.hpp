#pragma once
// Sprite drawing with the optional outline (hover) and dissolve (destroyed) shaders.
#include "rendering/Assets.hpp"

enum SpriteMode
{
    SM_Plain = 0,
    SM_Outline = 1,
    SM_Dissolve = 2
};

void loadSpriteShaders(); // outline.fs + dissolve.fs (cached uniform locations)
void drawSprite(Texture2D t, Rectangle src, Rectangle dst, Color tint, int mode, Color oc = WHITE, float prog = 0, float sd = 0);
// Draws an animation frame with its foot point at (footWX, footWY).
void drawAnim(const Anim &a, int frame, float footWX, float footWY, bool flip, float scale, Color tint = WHITE, int mode = 0, Color oc = WHITE, float prog = 0, float sd = 0);
int frameFor(const Anim &a, double t, float phase = 0);
