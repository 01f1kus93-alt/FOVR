#include "FOVR/FOVR.h"
#include "world/world.h"
#include "player/player.h"
#include <raymath.h>

constexpr float EYE_ABOVE = 1.65f;

int main(void) {
    World_InitNoise();

    VRConfig cfg = VR_DefaultConfig();
    cfg.ipd      = 0.062f;
    cfg.k1       = 0.18f;
    cfg.k2       = 0.15f;
    cfg.lensFill = 0.92f;
    cfg.hfovDeg  = 100.0f;

    FOVR_Init("My VR App", "VR/barrel.frag", &cfg, 5555);

    World  world;
    Player player;
    bool   showWires = false;

    {
        int pcx = floordiv((int)floorf(player.feet.x), CHUNK_W);
        int pcz = floordiv((int)floorf(player.feet.z), CHUNK_W);
        World_SpawnInitial(world, pcx, pcz);
    }

    World_StartWorker();

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F)) showWires = !showWires;

        if (IsKeyPressed(KEY_R)) {
            FOVR_Recalibrate();
            TraceLog(LOG_INFO, "HeadTracker: recalibrate requested");
        }

        if (IsKeyPressed(KEY_G)) {
            player.flying = !player.flying;
            if (player.flying) {
                player.vy = 0.0f;
                player.onGround = false;
                TraceLog(LOG_INFO, "FLY: ON");
            } else {
                TraceLog(LOG_INFO, "FLY: OFF");
            }
        }

        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;

        FOVR_Update();
        Player_Update(world, player, VR_GetYaw(), dt);

        int pcx = floordiv((int)floorf(player.feet.x), CHUNK_W);
        int pcz = floordiv((int)floorf(player.feet.z), CHUNK_W);
        World_UpdateAround(world, pcx, pcz);

        Vector3 eyePos = {
            player.feet.x,
            player.feet.y + EYE_ABOVE,
            player.feet.z
        };
        VR_SetPosition(eyePos);

        Frustum frustum;
        {
            Camera3D camL = VR_GetCameraLeft();
            Camera3D camR = VR_GetCameraRight();
            Camera3D camMid = camL;
            camMid.position = Vector3Scale(Vector3Add(camL.position, camR.position), 0.5f);
            camMid.target   = Vector3Scale(Vector3Add(camL.target,   camR.target  ), 0.5f);

            float aspectEye = (float)(GetScreenWidth() / 2) / (float)GetScreenHeight();
            if (aspectEye <= 0.0f) aspectEye = 1.0f;

            Frustum_FromCamera(camMid, cfg.hfovDeg + 6.0f, aspectEye,
                               cfg.nearPlane, cfg.farPlane, frustum);
        }

        VR_BeginLeftEye();
            World_Draw(world, frustum, showWires);
        VR_EndLeftEye();

        VR_BeginRightEye();
            World_Draw(world, frustum, showWires);
        VR_EndRightEye();

        VR_Present();
    }

    World_StopWorker();
    World_Clear(world);
    FOVR_Shutdown();
    return 0;
}
