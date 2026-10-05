#include "player.h"
#include <raymath.h>
#include <cmath>

constexpr float PLAYER_HALF     = 0.3f;
constexpr float PLAYER_HEIGHT   = 1.8f;
constexpr float GRAVITY         = -25.0f;
constexpr float JUMP_SPEED      = 8.0f;
constexpr float MOVE_SPEED      = 4.5f;
constexpr float FLY_SPEED       = 12.0f;
constexpr float FLY_VERT_SPEED  = 8.0f;
constexpr float ACCEL_SHARPNESS = 12.0f;

static bool playerHitsWorld(const World& w, Vector3 feet) {
    const float xmin = feet.x - PLAYER_HALF, xmax = feet.x + PLAYER_HALF;
    const float ymin = feet.y,               ymax = feet.y + PLAYER_HEIGHT;
    const float zmin = feet.z - PLAYER_HALF, zmax = feet.z + PLAYER_HALF;

    const int bx0 = (int)floorf(xmin - 0.5f);
    const int bx1 = (int)ceilf (xmax + 0.5f);
    const int by0 = (int)floorf(ymin - 0.5f);
    const int by1 = (int)ceilf (ymax + 0.5f);
    const int bz0 = (int)floorf(zmin - 0.5f);
    const int bz1 = (int)ceilf (zmax + 0.5f);

    for (int bx = bx0; bx <= bx1; ++bx)
    for (int by = by0; by <= by1; ++by)
    for (int bz = bz0; bz <= bz1; ++bz) {
        if (!w.has(bx, by, bz)) continue;
        const float hb = 0.5f;
        if (xmax <= bx - hb || xmin >= bx + hb) continue;
        if (ymax <= by - hb || ymin >= by + hb) continue;
        if (zmax <= bz - hb || zmin >= bz + hb) continue;
        return true;
    }
    return false;
}

void Player_Update(const World& w, Player& p, float yaw, float dt) {
    const Vector3 fwdFlat   = {  sinf(yaw), 0.0f, -cosf(yaw) };
    const Vector3 rightFlat = {  cosf(yaw), 0.0f,  sinf(yaw) };

    Vector3 want = { 0.0f, 0.0f, 0.0f };
    if (IsKeyDown(KEY_W)) want = Vector3Add     (want, fwdFlat);
    if (IsKeyDown(KEY_S)) want = Vector3Subtract(want, fwdFlat);
    if (IsKeyDown(KEY_D)) want = Vector3Add     (want, rightFlat);
    if (IsKeyDown(KEY_A)) want = Vector3Subtract(want, rightFlat);

    const float horizSpeed = p.flying ? FLY_SPEED : MOVE_SPEED;
    if (Vector3Length(want) > 0.0001f) {
        want = Vector3Normalize(want);
        want = Vector3Scale(want, horizSpeed);
    }

    const float alpha = 1.0f - expf(-ACCEL_SHARPNESS * dt);
    p.horizVel = Vector3Lerp(p.horizVel, want, alpha);

    { Vector3 t = p.feet; t.x += p.horizVel.x * dt;
      if (!playerHitsWorld(w, t)) p.feet = t; else p.horizVel.x = 0.0f; }
    { Vector3 t = p.feet; t.z += p.horizVel.z * dt;
      if (!playerHitsWorld(w, t)) p.feet = t; else p.horizVel.z = 0.0f; }

    if (p.flying) {
        float wantVy = 0.0f;
        if (IsKeyDown(KEY_SPACE))       wantVy += FLY_VERT_SPEED;
        if (IsKeyDown(KEY_LEFT_SHIFT) ||
            IsKeyDown(KEY_RIGHT_SHIFT)) wantVy -= FLY_VERT_SPEED;
        p.vy = wantVy;
    } else {
        if (p.onGround && IsKeyPressed(KEY_SPACE)) {
            p.vy = JUMP_SPEED;
            p.onGround = false;
        }
        p.vy += GRAVITY * dt;
        if (p.vy < -50.0f) p.vy = -50.0f;
    }

    {
        Vector3 t = p.feet;
        t.y += p.vy * dt;
        if (playerHitsWorld(w, t)) {
            if (p.vy < 0.0f) p.onGround = true;
            p.vy = 0.0f;
        } else {
            p.feet = t;
            if (!p.flying) p.onGround = false;
        }
    }
}