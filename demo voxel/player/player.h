#pragma once
#include <raylib.h>
#include "../world/world.h"

struct Player {
    Vector3 feet     = { 0.5f, 24.0f, 0.5f };
    Vector3 horizVel = { 0.0f, 0.0f, 0.0f };
    float   vy       = 0.0f;
    bool    onGround = false;
    bool    flying   = true;
};

// yaw передаётся снаружи (обычно VR_GetYaw()), чтобы модуль игрока
// не тянул за собой VR.
void Player_Update(const World& world, Player& p, float yaw, float dt);