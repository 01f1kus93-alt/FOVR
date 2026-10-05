#pragma once
#include <raylib.h>

/* ============================================================
 *  FOVR - VR + head tracking
 *  author: f1kus
 * ============================================================ */

typedef struct VRConfig {
    float sensitivity;
    float ipd;
    float moveSpeed;
    float eyeHeight;

    float hfovDeg;
    float nearPlane;
    float farPlane;
    float supersample;

    float k1;
    float k2;
    float chroma;
    float vignette;
    float lensFill;
    float gapFrac;

    int   targetFps;
} VRConfig;

VRConfig VR_DefaultConfig(void);

void VR_Init(const char* title, const char* shaderPath, const VRConfig* cfg);
void VR_Shutdown(void);
void VR_Update(void);

void VR_BeginLeftEye(void);
void VR_EndLeftEye(void);
void VR_BeginRightEye(void);
void VR_EndRightEye(void);

void VR_Present(void);

Camera3D VR_GetCameraLeft(void);
Camera3D VR_GetCameraRight(void);

Vector3 VR_GetPosition(void);
void    VR_SetPosition(Vector3 pos);

float   VR_GetYaw(void);
void    VR_SetYaw(float yaw);

float   VR_GetPitch(void);
void    VR_SetPitch(float pitch);

float   VR_GetRoll(void);
void    VR_SetRoll(float roll);

// When true, VR_Update ignores mouse and expects angles from an external source.
void    VR_SetHeadTracking(bool active);

/* ============================================================
 *  HeadTracker (UDP)
 * ============================================================ */

// Packet: [u8 hdr0][u8 hdr1][f32 yaw][f32 roll][f32 pitch] LE, radians.
void HeadTracker_Init(int port);
void HeadTracker_Update(void);
void HeadTracker_Recalibrate(void);
void HeadTracker_DumpStats(void);
void HeadTracker_Shutdown(void);

/* ============================================================
 *  Unified FOVR entry points
 * ============================================================ */

void FOVR_Init(const char* title, const char* shaderPath, const VRConfig* cfg, int udpPort = 5555);
void FOVR_Update(void);
void FOVR_Shutdown(void);
void FOVR_Recalibrate(void);
