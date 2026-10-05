#include "FOVR.h"
#include <raymath.h>
#include "rlgl.h"
#include <math.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

/* ============================================================
 *  VR
 *  author: f1kus
 * ============================================================ */

VRConfig VR_DefaultConfig(void) {
    VRConfig c;
    c.sensitivity = 0.0025f;
    c.ipd         = 0.063f;
    c.moveSpeed   = 3.5f;
    c.eyeHeight   = 1.65f;

    c.hfovDeg     = 100.0f;
    c.nearPlane   = 0.05f;
    c.farPlane    = 200.0f;
    c.supersample = 1.5f;

    c.k1          = 0.22f;
    c.k2          = 0.22f;
    c.chroma      = 0.010f;
    c.vignette    = 0.25f;
    c.lensFill    = 1.0f;
    c.gapFrac     = 0.015f;

    c.targetFps   = 0;
    return c;
}

struct VRState {
    int SW, SH, EYE_W;
    int RW, RH;
    RenderTexture2D eyeL, eyeR;
    Shader barrel;
    bool   ready;

    Camera3D base;
    Camera3D camL, camR;

    float yaw, pitch, roll;

    double frusRight, frusTop;
    int GAP_W;

    bool headTracking;

    VRConfig cfg;
};

static VRState vr = {0};

static void SetEyeProjection(void) {
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlFrustum(-vr.frusRight, vr.frusRight, -vr.frusTop, vr.frusTop,
              vr.cfg.nearPlane, vr.cfg.farPlane);
    rlMatrixMode(RL_MODELVIEW);
}

void VR_Init(const char* title, const char* shaderPath, const VRConfig* cfg) {
    vr.cfg = cfg ? *cfg : VR_DefaultConfig();
    vr.ready = false;
    vr.headTracking = false;

    SetConfigFlags(FLAG_FULLSCREEN_MODE | FLAG_VSYNC_HINT);
    InitWindow(0, 0, title);

    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "VR: InitWindow failed");
        return;
    }

    if (vr.cfg.targetFps > 0) SetTargetFPS(vr.cfg.targetFps);

    vr.SW    = GetScreenWidth();
    vr.SH    = GetScreenHeight();
    vr.EYE_W = vr.SW / 2;
    DisableCursor();

    vr.RW = (int)(vr.EYE_W * vr.cfg.supersample);
    vr.RH = (int)(vr.SH    * vr.cfg.supersample);
    vr.eyeL = LoadRenderTexture(vr.RW, vr.RH);
    vr.eyeR = LoadRenderTexture(vr.RW, vr.RH);
    SetTextureWrap  (vr.eyeL.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap  (vr.eyeR.texture, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(vr.eyeL.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(vr.eyeR.texture, TEXTURE_FILTER_BILINEAR);

    vr.barrel = LoadShader(0, shaderPath);
    if (vr.barrel.id == 0) {
        TraceLog(LOG_ERROR, "VR: failed to load shader '%s'", shaderPath);
        return;
    }

    int k1Loc     = GetShaderLocation(vr.barrel, "k1");
    int k2Loc     = GetShaderLocation(vr.barrel, "k2");
    int chromaLoc = GetShaderLocation(vr.barrel, "chroma");
    int vignLoc   = GetShaderLocation(vr.barrel, "vignette");
    int fillLoc   = GetShaderLocation(vr.barrel, "lensFill");

    SetShaderValue(vr.barrel, k1Loc,     &vr.cfg.k1,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(vr.barrel, k2Loc,     &vr.cfg.k2,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(vr.barrel, chromaLoc, &vr.cfg.chroma,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(vr.barrel, vignLoc,   &vr.cfg.vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(vr.barrel, fillLoc,   &vr.cfg.lensFill, SHADER_UNIFORM_FLOAT);

    vr.base.position   = (Vector3){ 0.0f, vr.cfg.eyeHeight, 0.0f };
    vr.base.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    vr.base.fovy       = 0.0f;
    vr.base.projection = CAMERA_PERSPECTIVE;

    vr.yaw   = 0.0f;
    vr.pitch = 0.0f;
    vr.roll  = 0.0f;

    const float aspectEye = (float)vr.EYE_W / (float)vr.SH;
    const float hfovRad   = vr.cfg.hfovDeg * DEG2RAD;
    vr.frusRight = (double)vr.cfg.nearPlane * tanf(hfovRad * 0.5f);
    vr.frusTop   = vr.frusRight / (double)aspectEye;

    vr.GAP_W = (int)(vr.SW * vr.cfg.gapFrac);

    vr.ready = true;
}

void VR_Shutdown(void) {
    if (vr.ready) {
        UnloadShader(vr.barrel);
        UnloadRenderTexture(vr.eyeL);
        UnloadRenderTexture(vr.eyeR);
        vr.ready = false;
    }
    CloseWindow();
}

void VR_Update(void) {
    if (!vr.ready) return;
    if (vr.headTracking) return;

    Vector2 m = GetMouseDelta();
    vr.yaw   += m.x * vr.cfg.sensitivity;
    vr.pitch -= m.y * vr.cfg.sensitivity;
    vr.pitch  = Clamp(vr.pitch, -1.5f, 1.5f);
}

Vector3 VR_GetPosition(void) { return vr.base.position; }

float VR_GetYaw(void)   { return vr.yaw; }
float VR_GetPitch(void) { return vr.pitch; }
float VR_GetRoll(void)  { return vr.roll; }

void VR_SetYaw(float yaw) {
    if (!vr.ready) return;
    vr.yaw = yaw;
}

void VR_SetPitch(float pitch) {
    if (!vr.ready) return;
    vr.pitch = Clamp(pitch, -1.5f, 1.5f);
}

void VR_SetRoll(float roll) {
    if (!vr.ready) return;
    vr.roll = Clamp(roll, -1.5708f, 1.5708f);
}

void VR_SetHeadTracking(bool active) {
    vr.headTracking = active;
}

void VR_SetPosition(Vector3 pos) {
    if (!vr.ready) return;

    vr.base.position = pos;

    Vector3 lookDir = {
        cosf(vr.pitch) * sinf(vr.yaw),
        sinf(vr.pitch),
        -cosf(vr.pitch) * cosf(vr.yaw)
    };
    vr.base.target = Vector3Add(vr.base.position, lookDir);

    Vector3 fwd = lookDir;

    Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3CrossProduct(fwd, worldUp);
    float rlen = Vector3Length(right);
    if (rlen < 1e-4f) {
        right = (fabsf(fwd.z) > 0.9f) ? (Vector3){1, 0, 0} : (Vector3){0, 0, 1};
    }
    right = Vector3Normalize(right);
    Vector3 up = Vector3CrossProduct(right, fwd);

    float cr = cosf(vr.roll);
    float sr = sinf(vr.roll);

    Vector3 rightR = {
        right.x * cr + up.x * sr,
        right.y * cr + up.y * sr,
        right.z * cr + up.z * sr
    };
    Vector3 upR = {
        -right.x * sr + up.x * cr,
        -right.y * sr + up.y * cr,
        -right.z * sr + up.z * cr
    };

    vr.base.up = upR;

    float halfIpd = vr.cfg.ipd * 0.5f;

    vr.camL = vr.base;
    vr.camR = vr.base;

    vr.camL.position = Vector3Subtract(vr.base.position, Vector3Scale(rightR, halfIpd));
    vr.camL.target   = Vector3Subtract(vr.base.target,   Vector3Scale(rightR, halfIpd));
    vr.camL.up       = upR;

    vr.camR.position = Vector3Add     (vr.base.position, Vector3Scale(rightR, halfIpd));
    vr.camR.target   = Vector3Add     (vr.base.target,   Vector3Scale(rightR, halfIpd));
    vr.camR.up       = upR;
}

void VR_BeginLeftEye(void) {
    if (!vr.ready) return;
    BeginTextureMode(vr.eyeL);
        ClearBackground(SKYBLUE);
        BeginMode3D(vr.camL);
            SetEyeProjection();
}

void VR_EndLeftEye(void) {
    if (!vr.ready) return;
        EndMode3D();
    EndTextureMode();
}

void VR_BeginRightEye(void) {
    if (!vr.ready) return;
    BeginTextureMode(vr.eyeR);
        ClearBackground(SKYBLUE);
        BeginMode3D(vr.camR);
            SetEyeProjection();
}

void VR_EndRightEye(void) {
    if (!vr.ready) return;
        EndMode3D();
    EndTextureMode();
}

void VR_Present(void) {
    if (!vr.ready) return;
    BeginDrawing();
        ClearBackground(BLACK);

        BeginShaderMode(vr.barrel);
            DrawTexturePro(
                vr.eyeL.texture,
                (Rectangle){ 0, 0,
                             (float)vr.eyeL.texture.width,
                            -(float)vr.eyeL.texture.height },
                (Rectangle){ 0, 0, (float)vr.EYE_W, (float)vr.SH },
                (Vector2){ 0, 0 }, 0.0f, WHITE);

            DrawTexturePro(
                vr.eyeR.texture,
                (Rectangle){ 0, 0,
                             (float)vr.eyeR.texture.width,
                            -(float)vr.eyeR.texture.height },
                (Rectangle){ (float)vr.EYE_W, 0, (float)vr.EYE_W, (float)vr.SH },
                (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndShaderMode();

        int covered = vr.EYE_W * 2;
        int gapW    = vr.GAP_W;
        if (covered < vr.SW) {
            DrawRectangle(covered, 0, vr.SW - covered, vr.SH, BLACK);
        }
        if (gapW > 0)
            DrawRectangle(vr.EYE_W - gapW / 2, 0, gapW, vr.SH, BLACK);

        DrawFPS(10, 10);
    EndDrawing();
}

Camera3D VR_GetCameraLeft (void) { return vr.camL; }
Camera3D VR_GetCameraRight(void) { return vr.camR; }


/* ============================================================
 *  HeadTracker
 *  author: f1kus
 * ============================================================ */

static int  g_sock        = -1;
static bool g_initialized = false;

static constexpr int SAMPLE_BUF = 16;

struct Sample {
    double t;
    float  yaw;
    float  pitch;
    float  roll;
};

static Sample g_buf[SAMPLE_BUF];
static int    g_bufHead  = 0;
static int    g_bufCount = 0;

// Playback delay for interpolation (compensates jitter).
static constexpr double INTERP_DELAY = 0.025;

// Smoothing time constants (seconds).
static constexpr float TAU_YAW   = 0.030f;
static constexpr float TAU_PITCH = 0.035f;
static constexpr float TAU_ROLL  = 0.050f;

// Dead zones to suppress sensor noise.
static constexpr float DEAD_YAW   = 0.0020f;
static constexpr float DEAD_PITCH = 0.0025f;
static constexpr float DEAD_ROLL  = 0.0030f;

static bool  g_recalRequested = false;
static bool  g_hasBaseline    = false;
static float g_pitchBaseline  = 0.0f;
static float g_rollBaseline   = 0.0f;

static float g_rawYaw   = 0.0f;
static float g_rawPitch = 0.0f;
static float g_rawRoll  = 0.0f;

static float g_smoothYaw   = 0.0f;
static float g_smoothPitch = 0.0f;
static float g_smoothRoll  = 0.0f;
static bool  g_smoothInit  = false;

struct Stats {
    long   packetsTotal      = 0;
    long   packetsInLastSec  = 0;
    long   packetsPrevSec    = 0;
    double lastSecondMark    = 0.0;
    double maxGap            = 0.0;
    double lastPacketTime    = 0.0;
    double jitterSum         = 0.0;
    long   jitterCount       = 0;
    long   framesSinceStat   = 0;
    double framesTimeStart   = 0.0;
};
static Stats g_stats;

void HeadTracker_Init(int port) {
    if (g_initialized) return;

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { perror("HeadTracker: socket"); return; }

    int flags = fcntl(g_sock, F_GETFL, 0);
    fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);

    int rcvBuf = 1024 * 1024;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(rcvBuf));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(g_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("HeadTracker: bind");
        close(g_sock); g_sock = -1;
        return;
    }

    g_initialized = true;
    printf("HeadTracker: listening on UDP port %d\n", port);
    fflush(stdout);
}

void HeadTracker_Recalibrate() {
    g_recalRequested = true;
}

void HeadTracker_DumpStats() {
    double avgDt = 0.0;
    if (g_stats.packetsTotal > 1 && g_stats.lastPacketTime > 0.0) {
        avgDt = (g_stats.lastPacketTime - g_stats.framesTimeStart)
              / (double)(g_stats.packetsTotal - 1);
    }
    double avgJitter = (g_stats.jitterCount > 0)
                     ? g_stats.jitterSum / (double)g_stats.jitterCount
                     : 0.0;

    printf("\n===== HeadTracker stats =====\n");
    printf("  packets total    : %ld\n", g_stats.packetsTotal);
    printf("  packets in last s: %ld\n", g_stats.packetsPrevSec);
    printf("  avg dt between   : %.3f ms (%.1f Hz)\n",
           avgDt * 1000.0, avgDt > 0 ? 1.0 / avgDt : 0.0);
    printf("  max gap          : %.3f ms\n", g_stats.maxGap * 1000.0);
    printf("  avg jitter       : %.3f ms\n", avgJitter * 1000.0);
    printf("  interpol delay   : %.1f ms\n", INTERP_DELAY * 1000.0);
    printf("  buffer fill      : %d / %d\n", g_bufCount, SAMPLE_BUF);
    printf("==============================\n\n");
    fflush(stdout);
}

static inline float wrapPi(float a) {
    while (a >  (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a <= -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

static void applyBaselineIfNeeded() {
    if (g_recalRequested) {
        g_pitchBaseline  = g_rawPitch;
        g_rollBaseline   = g_rawRoll;
        g_hasBaseline    = true;
        g_recalRequested = false;

        g_bufCount   = 0;
        g_bufHead    = 0;
        g_smoothInit = false;

        printf("[HT] recalibrated: pitch_base=%7.4f  roll_base=%7.4f\n",
               g_pitchBaseline, g_rollBaseline);
        fflush(stdout);
        return;
    }
    if (!g_hasBaseline) {
        g_pitchBaseline = g_rawPitch;
        g_rollBaseline  = g_rawRoll;
        g_hasBaseline   = true;

        printf("[HT] auto-baseline: pitch=%7.4f  roll=%7.4f\n",
               g_pitchBaseline, g_rollBaseline);
        fflush(stdout);
    }
}

static void pushSample(double t, float yaw, float pitch, float roll) {
    g_buf[g_bufHead].t     = t;
    g_buf[g_bufHead].yaw   = yaw;
    g_buf[g_bufHead].pitch = pitch;
    g_buf[g_bufHead].roll  = roll;
    g_bufHead = (g_bufHead + 1) % SAMPLE_BUF;
    if (g_bufCount < SAMPLE_BUF) g_bufCount++;
}

static const Sample& getSample(int i) {
    int start = (g_bufHead - g_bufCount + SAMPLE_BUF) % SAMPLE_BUF;
    return g_buf[(start + i) % SAMPLE_BUF];
}

static void sampleAt(double t, float& yaw, float& pitch, float& roll) {
    if (g_bufCount == 0) {
        yaw = pitch = roll = 0.0f;
        return;
    }
    if (g_bufCount == 1) {
        const Sample& s = getSample(0);
        yaw = s.yaw; pitch = s.pitch; roll = s.roll;
        return;
    }

    int i1 = g_bufCount - 1;
    while (i1 > 0 && getSample(i1).t > t) i1--;
    int i0 = (i1 > 0) ? i1 - 1 : 0;

    const Sample& s0 = getSample(i0);
    const Sample& s1 = getSample(i1);

    double dt = s1.t - s0.t;
    if (dt < 1e-9) {
        yaw = s1.yaw; pitch = s1.pitch; roll = s1.roll;
        return;
    }

    float u = (float)((t - s0.t) / dt);
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    // Yaw wraps, so interpolate along shortest arc.
    float dYaw = wrapPi(s1.yaw - s0.yaw);
    yaw   = wrapPi(s0.yaw + dYaw * u);
    pitch = s0.pitch + (s1.pitch - s0.pitch) * u;
    roll  = s0.roll  + (s1.roll  - s0.roll ) * u;
}

void HeadTracker_Update() {
    if (!g_initialized) return;

    double now = GetTime();

    // Drain the socket.
    for (;;) {
        char buffer[256];
        sockaddr_in sender{};
        socklen_t senderLen = sizeof(sender);

        ssize_t n = recvfrom(g_sock, buffer, sizeof(buffer), 0,
                             (sockaddr*)&sender, &senderLen);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }

        if (n < 14) continue;

        float y, r, p;
        std::memcpy(&y, buffer + 2,  4);
        std::memcpy(&r, buffer + 6,  4);
        std::memcpy(&p, buffer + 10, 4);

        if (!std::isfinite(y) || !std::isfinite(p) || !std::isfinite(r)) continue;

        g_rawYaw   = y;
        g_rawPitch = p;
        g_rawRoll  = r;

        if (g_stats.lastPacketTime > 0.0) {
            double gap = now - g_stats.lastPacketTime;
            if (gap > g_stats.maxGap) g_stats.maxGap = gap;
            if (gap < 0.2) {
                g_stats.jitterSum += gap;
                g_stats.jitterCount++;
            }
        }
        g_stats.lastPacketTime = now;
        g_stats.packetsTotal++;
        g_stats.packetsInLastSec++;

        applyBaselineIfNeeded();

        float adjYaw   = y;
        float adjPitch = -(p - g_pitchBaseline);
        float adjRoll  =  (r - g_rollBaseline);

        pushSample(now, adjYaw, adjPitch, adjRoll);
    }

    if (g_stats.lastSecondMark == 0.0) g_stats.lastSecondMark = now;
    if (now - g_stats.lastSecondMark >= 1.0) {
        g_stats.packetsPrevSec   = g_stats.packetsInLastSec;
        g_stats.packetsInLastSec = 0;
        g_stats.lastSecondMark   = now;
    }

    if (g_bufCount == 0) return;

    double tPlay = now - INTERP_DELAY;
    float iYaw, iPitch, iRoll;
    sampleAt(tPlay, iYaw, iPitch, iRoll);

    float dt = GetFrameTime();
    if (dt > 0.1f)  dt = 0.1f;
    if (dt < 1e-6f) dt = 1e-6f;

    if (!g_smoothInit) {
        g_smoothYaw   = iYaw;
        g_smoothPitch = iPitch;
        g_smoothRoll  = iRoll;
        g_smoothInit  = true;
    } else {
        float kY = 1.0f - expf(-dt / TAU_YAW);
        float kP = 1.0f - expf(-dt / TAU_PITCH);
        float kR = 1.0f - expf(-dt / TAU_ROLL);

        float dYaw = wrapPi(iYaw - g_smoothYaw);
        if (fabsf(dYaw)   > DEAD_YAW)   g_smoothYaw   += kY * dYaw;
        if (fabsf(iPitch - g_smoothPitch) > DEAD_PITCH) g_smoothPitch += kP * (iPitch - g_smoothPitch);
        if (fabsf(iRoll  - g_smoothRoll)  > DEAD_ROLL)  g_smoothRoll  += kR * (iRoll  - g_smoothRoll);

        g_smoothYaw = wrapPi(g_smoothYaw);
    }

    VR_SetYaw  (g_smoothYaw);
    VR_SetPitch(g_smoothPitch);
    VR_SetRoll (g_smoothRoll);
}

void HeadTracker_Shutdown() {
    if (g_sock >= 0) { close(g_sock); g_sock = -1; }
    g_initialized = false;
}

/* ============================================================
 *  Unified FOVR
 *  author: f1kus
 * ============================================================ */

void FOVR_Init(const char* title, const char* shaderPath, const VRConfig* cfg, int udpPort) {
    VR_Init(title, shaderPath, cfg);
    HeadTracker_Init(udpPort);
    VR_SetHeadTracking(true);
}

void FOVR_Update(void) {
    // HeadTracker drives angles first; VR_Update then no-ops in headTracking mode.
    HeadTracker_Update();
    VR_Update();
}

void FOVR_Shutdown(void) {
    HeadTracker_Shutdown();
    VR_Shutdown();
}

void FOVR_Recalibrate(void) {
    HeadTracker_Recalibrate();
}
