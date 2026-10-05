#include "world.h"
#include "FastNoiseLite.h"
#include <raymath.h>
#include "rlgl.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>

/* ============================== Утилиты ============================== */

uint64_t chunkKey(int cx, int cz) {
    return ((uint64_t)(uint32_t)cx << 32) | (uint64_t)(uint32_t)cz;
}

int floordiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int blockIndex(int lx, int y, int lz) {
    return (lx * CHUNK_H + (y - CHUNK_MIN_Y)) * CHUNK_W + lz;
}

/* ================================ Chunk =============================== */

Chunk::Chunk() {
    std::memset(blocks, BLOCK_AIR, sizeof(blocks));
}

World::World() {
    chunks.reserve(121 * 2);   // (2*LOAD_R+1)^2 * 2 при LOAD_R=5
}

BlockId World::getBlock(int x, int y, int z) const {
    if (y < CHUNK_MIN_Y || y > CHUNK_MAX_Y) return BLOCK_AIR;
    int cx = floordiv(x, CHUNK_W);
    int cz = floordiv(z, CHUNK_W);
    auto it = chunks.find(chunkKey(cx, cz));
    if (it == chunks.end()) return BLOCK_AIR;
    if (it->second.state != STATE_GENERATED) return BLOCK_AIR;
    int lx = x - cx * CHUNK_W;
    int lz = z - cz * CHUNK_W;
    return it->second.blocks[blockIndex(lx, y, lz)];
}

bool World::has(int x, int y, int z) const {
    return getBlock(x, y, z) != BLOCK_AIR;
}

/* ========================= Цвета и fake-shading ======================= */

static inline Color blockTopColor(BlockId id) {
    switch (id) {
        case BLOCK_GRASS:   return (Color){  95, 160,  70, 255 };
        case BLOCK_DIRT:    return (Color){ 120,  80,  45, 255 };
        case BLOCK_STONE:   return (Color){ 130, 130, 130, 255 };
        case BLOCK_SAND:    return (Color){ 220, 210, 150, 255 };
        case BLOCK_WATER:   return (Color){  55, 110, 200, 255 };
        case BLOCK_WOOD:    return (Color){ 110,  75,  45, 255 };
        case BLOCK_LEAVES:  return (Color){  55, 130,  55, 255 };
        case BLOCK_BEDROCK: return (Color){  40,  40,  40, 255 };
        default:            return MAGENTA;
    }
}

static inline Color blockSideColor(BlockId id) {
    if (id == BLOCK_GRASS) return (Color){ 120, 80, 45, 255 };
    return blockTopColor(id);
}

constexpr float SHADE_TOP    = 1.00f;
constexpr float SHADE_BOTTOM = 0.50f;
constexpr float SHADE_PX     = 0.75f;
constexpr float SHADE_NX     = 0.65f;
constexpr float SHADE_PZ     = 0.85f;
constexpr float SHADE_NZ     = 0.80f;

static inline Color shade(Color c, float f) {
    return (Color){
        (unsigned char)(c.r * f),
        (unsigned char)(c.g * f),
        (unsigned char)(c.b * f),
        c.a
    };
}

/* ================================ Шум ================================ */

static FastNoiseLite g_terrainN, g_detailN, g_caveN;

void World_InitNoise() {
    g_terrainN.SetSeed(1337);
    g_terrainN.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    g_terrainN.SetFrequency(0.012f);
    g_terrainN.SetFractalType(FastNoiseLite::FractalType_FBm);
    g_terrainN.SetFractalOctaves(4);
    g_terrainN.SetFractalLacunarity(2.0f);
    g_terrainN.SetFractalGain(0.5f);

    g_detailN.SetSeed(99);
    g_detailN.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    g_detailN.SetFrequency(0.09f);
    g_detailN.SetFractalType(FastNoiseLite::FractalType_FBm);
    g_detailN.SetFractalOctaves(2);

    g_caveN.SetSeed(4242);
    g_caveN.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    g_caveN.SetFrequency(0.09f);
    g_caveN.SetFractalType(FastNoiseLite::FractalType_FBm);
    g_caveN.SetFractalOctaves(2);
}

/* ============================ Генерация ============================== */

static int computeSurfaceY(int wx, int wz) {
    float h = g_terrainN.GetNoise((float)wx, (float)wz) * 12.0f
            + g_detailN.GetNoise((float)wx, (float)wz) *  3.0f
            + 2.0f;
    return (int)floorf(h);
}

static inline uint32_t hash2D(int x, int z) {
    uint32_t h = (uint32_t)x * 0x9E3779B1u;
    h ^= (uint32_t)z * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// Заполняет ch.blocks. Никаких side-эффектов вроде state — этим управляет вызывающий.
static void generateChunkBlocks(Chunk& ch) {
    const int baseX = ch.cx * CHUNK_W;
    const int baseZ = ch.cz * CHUNK_W;

    for (int lx = 0; lx < CHUNK_W; ++lx) {
        for (int lz = 0; lz < CHUNK_W; ++lz) {
            int wx = baseX + lx;
            int wz = baseZ + lz;

            int surfaceY = computeSurfaceY(wx, wz);
            if (surfaceY > CHUNK_MAX_Y)     surfaceY = CHUNK_MAX_Y;
            if (surfaceY < CHUNK_MIN_Y + 1) surfaceY = CHUNK_MIN_Y + 1;

            const bool beach = (surfaceY <= SEA_LEVEL + 1);

            for (int y = CHUNK_MIN_Y; y <= surfaceY; ++y) {
                BlockId id;
                if      (y == CHUNK_MIN_Y)  id = BLOCK_BEDROCK;
                else if (y == surfaceY)     id = beach ? BLOCK_SAND : BLOCK_GRASS;
                else if (y >= surfaceY - 3) id = beach ? BLOCK_SAND : BLOCK_DIRT;
                else                        id = BLOCK_STONE;

                if (y > CHUNK_MIN_Y + 1 && y < surfaceY - 1 && id != BLOCK_BEDROCK) {
                    float c = g_caveN.GetNoise((float)wx, (float)y, (float)wz);
                    if (c > 0.58f) continue;
                }
                ch.blocks[blockIndex(lx, y, lz)] = id;
            }

            for (int y = surfaceY + 1; y <= SEA_LEVEL; ++y) {
                int idx = blockIndex(lx, y, lz);
                if (ch.blocks[idx] == BLOCK_AIR)
                    ch.blocks[idx] = BLOCK_WATER;
            }
        }
    }

    // --- Деревья ---
    const int margin = 3;
    for (int tx = baseX - margin; tx < baseX + CHUNK_W + margin; ++tx) {
        for (int tz = baseZ - margin; tz < baseZ + CHUNK_W + margin; ++tz) {
            uint32_t h = hash2D(tx, tz);
            if ((h & 0x3F) != 0) continue;

            int surfaceY = computeSurfaceY(tx, tz);
            if (surfaceY <= SEA_LEVEL + 1) continue;
            if (surfaceY < CHUNK_MIN_Y + 1 || surfaceY > CHUNK_MAX_Y) continue;

            int trunkH    = 4 + (int)((h >> 8) % 3);
            int trunkBase = surfaceY + 1;
            int topTrunk  = trunkBase + trunkH - 1;

            for (int i = 0; i < trunkH; ++i) {
                int x = tx, y = trunkBase + i, z = tz;
                if (x < baseX || x >= baseX + CHUNK_W) continue;
                if (z < baseZ || z >= baseZ + CHUNK_W) continue;
                if (y < CHUNK_MIN_Y || y > CHUNK_MAX_Y) continue;
                ch.blocks[blockIndex(x - baseX, y, z - baseZ)] = BLOCK_WOOD;
            }

            auto putLeaf = [&](int x, int y, int z) {
                if (x < baseX || x >= baseX + CHUNK_W) return;
                if (z < baseZ || z >= baseZ + CHUNK_W) return;
                if (y < CHUNK_MIN_Y || y > CHUNK_MAX_Y) return;
                int idx = blockIndex(x - baseX, y, z - baseZ);
                if (ch.blocks[idx] == BLOCK_AIR)
                    ch.blocks[idx] = BLOCK_LEAVES;
            };

            for (int layer = 0; layer < 2; ++layer) {
                int y = topTrunk - 1 + layer;
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (dx == 0 && dz == 0) continue;
                        if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                        putLeaf(tx + dx, y, tz + dz);
                    }
            }
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    putLeaf(tx + dx, topTrunk + 1, tz + dz);
            putLeaf(tx, topTrunk + 2, tz);
            putLeaf(tx + 1, topTrunk + 2, tz);
            putLeaf(tx - 1, topTrunk + 2, tz);
            putLeaf(tx, topTrunk + 2, tz + 1);
            putLeaf(tx, topTrunk + 2, tz - 1);
        }
    }
}

/* ======================= Асинхронная генерация ======================= */

struct GenTask   { int cx, cz; };
struct GenResult {
    int cx, cz;
    BlockId blocks[CHUNK_W * CHUNK_H * CHUNK_W];
};

static std::queue<GenTask>     g_genTasks;
static std::queue<GenResult>   g_genResults;
static std::mutex              g_genMutex;
static std::condition_variable g_genCV;
static std::atomic<bool>       g_genRun{false};
static std::thread             g_genThread;

static void workerLoop() {
    Chunk scratch;
    while (g_genRun.load()) {
        GenTask t;
        {
            std::unique_lock<std::mutex> lk(g_genMutex);
            g_genCV.wait(lk, [] {
                return !g_genTasks.empty() || !g_genRun.load();
            });
            if (!g_genRun.load() && g_genTasks.empty()) break;
            t = g_genTasks.front();
            g_genTasks.pop();
        }

        std::memset(scratch.blocks, BLOCK_AIR, sizeof(scratch.blocks));
        scratch.cx = t.cx;
        scratch.cz = t.cz;
        generateChunkBlocks(scratch);

        GenResult r;
        r.cx = t.cx;
        r.cz = t.cz;
        std::memcpy(r.blocks, scratch.blocks, sizeof(r.blocks));

        {
            std::lock_guard<std::mutex> lk(g_genMutex);
            g_genResults.push(std::move(r));
        }
    }
}

void World_StartWorker() {
    if (g_genThread.joinable()) return;   // уже запущен
    g_genRun.store(true);
    g_genThread = std::thread(workerLoop);
}

void World_StopWorker() {
    {
        std::lock_guard<std::mutex> lk(g_genMutex);
        g_genRun.store(false);
        std::queue<GenTask> empty;
        std::swap(g_genTasks, empty);
    }
    g_genCV.notify_all();
    if (g_genThread.joinable()) g_genThread.join();
}

/* ================================ Меш ================================ */

static void addFace(std::vector<float>&          verts,
                    std::vector<unsigned char>&  cols,
                    std::vector<unsigned short>& idx,
                    Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                    Color color)
{
    unsigned short base = (unsigned short)(verts.size() / 3);
    Vector3 corners[4] = { a, b, c, d };
    for (int i = 0; i < 4; ++i) {
        verts.push_back(corners[i].x);
        verts.push_back(corners[i].y);
        verts.push_back(corners[i].z);
        cols.push_back(color.r);
        cols.push_back(color.g);
        cols.push_back(color.b);
        cols.push_back(color.a);
    }
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 1); idx.push_back(base + 3); idx.push_back(base + 2);
}

static Mesh buildChunkMesh(const World& world, int cx, int cz) {
    auto it = world.chunks.find(chunkKey(cx, cz));
    if (it == world.chunks.end()) return Mesh{0};
    const Chunk& ch = it->second;
    if (ch.state != STATE_GENERATED) return Mesh{0};

    // Кэш 3x3 соседей (только сгенерированные)
    const Chunk* neigh[3][3];
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            auto nit = world.chunks.find(chunkKey(cx + dx, cz + dz));
            neigh[dx + 1][dz + 1] =
                (nit != world.chunks.end() && nit->second.state == STATE_GENERATED)
                    ? &nit->second : nullptr;
        }
    }

    auto getBlockFast = [&](int wx, int wy, int wz) -> BlockId {
        if (wy < CHUNK_MIN_Y || wy > CHUNK_MAX_Y) return BLOCK_AIR;
        int ncx = floordiv(wx, CHUNK_W) - cx + 1;
        int ncz = floordiv(wz, CHUNK_W) - cz + 1;
        if (ncx < 0 || ncx > 2 || ncz < 0 || ncz > 2) return BLOCK_AIR;
        const Chunk* c = neigh[ncx][ncz];
        if (!c) return BLOCK_AIR;
        int lx = wx - (cx + ncx - 1) * CHUNK_W;
        int lz = wz - (cz + ncz - 1) * CHUNK_W;
        return c->blocks[blockIndex(lx, wy, lz)];
    };
    auto solidFast = [&](int wx, int wy, int wz) -> bool {
        return getBlockFast(wx, wy, wz) != BLOCK_AIR;
    };

    std::vector<float>          verts;
    std::vector<unsigned char>  cols;
    std::vector<unsigned short> idx;
    verts.reserve(20000 * 3);
    cols.reserve(20000 * 4);
    idx.reserve(30000);

    const int baseX = cx * CHUNK_W;
    const int baseZ = cz * CHUNK_W;

    for (int lx = 0; lx < CHUNK_W; ++lx) {
        for (int lz = 0; lz < CHUNK_W; ++lz) {
            int wx = baseX + lx;
            int wz = baseZ + lz;
            for (int y = CHUNK_MIN_Y; y <= CHUNK_MAX_Y; ++y) {
                BlockId id = ch.blocks[blockIndex(lx, y, lz)];
                if (id == BLOCK_AIR) continue;

                Color cTop  = shade(blockTopColor(id),  SHADE_TOP);
                Color cBot  = shade(blockTopColor(id),  SHADE_BOTTOM);
                Color cSide = blockSideColor(id);
                Color cXP   = shade(cSide, SHADE_PX);
                Color cXN   = shade(cSide, SHADE_NX);
                Color cZP   = shade(cSide, SHADE_PZ);
                Color cZN   = shade(cSide, SHADE_NZ);

                const float x0 = (float)wx - 0.5f, x1 = (float)wx + 0.5f;
                const float y0 = (float)y  - 0.5f, y1 = (float)y  + 0.5f;
                const float z0 = (float)wz - 0.5f, z1 = (float)wz + 0.5f;

                if (!solidFast(wx - 1, y, wz))
                    addFace(verts, cols, idx,
                        {x0, y0, z0}, {x0, y0, z1},
                        {x0, y1, z0}, {x0, y1, z1}, cXN);
                if (!solidFast(wx + 1, y, wz))
                    addFace(verts, cols, idx,
                        {x1, y0, z1}, {x1, y0, z0},
                        {x1, y1, z1}, {x1, y1, z0}, cXP);
                if (!solidFast(wx, y - 1, wz))
                    addFace(verts, cols, idx,
                        {x0, y0, z0}, {x1, y0, z0},
                        {x0, y0, z1}, {x1, y0, z1}, cBot);
                if (!solidFast(wx, y + 1, wz))
                    addFace(verts, cols, idx,
                        {x0, y1, z1}, {x1, y1, z1},
                        {x0, y1, z0}, {x1, y1, z0}, cTop);
                if (!solidFast(wx, y, wz - 1))
                    addFace(verts, cols, idx,
                        {x1, y0, z0}, {x0, y0, z0},
                        {x1, y1, z0}, {x0, y1, z0}, cZN);
                if (!solidFast(wx, y, wz + 1))
                    addFace(verts, cols, idx,
                        {x0, y0, z1}, {x1, y0, z1},
                        {x0, y1, z1}, {x1, y1, z1}, cZP);
            }
        }
    }

    Mesh mesh = { 0 };
    mesh.vertexCount   = (int)(verts.size() / 3);
    mesh.triangleCount = (int)(idx.size()  / 3);
    if (mesh.vertexCount == 0) return mesh;

    mesh.vertices = (float*)MemAlloc(verts.size() * sizeof(float));
    memcpy(mesh.vertices, verts.data(), verts.size() * sizeof(float));

    mesh.colors = (unsigned char*)MemAlloc(cols.size());
    memcpy(mesh.colors, cols.data(), cols.size());

    mesh.indices = (unsigned short*)MemAlloc(idx.size() * sizeof(unsigned short));
    memcpy(mesh.indices, idx.data(), idx.size() * sizeof(unsigned short));

    UploadMesh(&mesh, false);
    return mesh;
}

/* ======================= Управление чанками ========================= */

static void unloadChunkModel(Chunk& ch) {
    if (ch.hasModel) {
        UnloadModel(ch.model);
        ch.hasModel = false;
    }
}

static void markNeighborsDirty(World& world, int cx, int cz) {
    const int offs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (auto& o : offs) {
        auto it = world.chunks.find(chunkKey(cx + o[0], cz + o[1]));
        if (it != world.chunks.end() && it->second.state == STATE_GENERATED)
            it->second.dirty = true;
    }
}

static void generateChunkSync(World& world, int cx, int cz) {
    Chunk& ch = world.chunks[chunkKey(cx, cz)];
    ch.cx = cx; ch.cz = cz;
    ch.state = STATE_EMPTY;
    ch.dirty = false;
    generateChunkBlocks(ch);
    ch.state = STATE_GENERATED;
}

void World_SpawnInitial(World& world, int pcx, int pcz) {
    for (int dx = -LOAD_R; dx <= LOAD_R; ++dx)
        for (int dz = -LOAD_R; dz <= LOAD_R; ++dz)
            generateChunkSync(world, pcx + dx, pcz + dz);

    for (int dx = -WORLD_R; dx <= WORLD_R; ++dx) {
        for (int dz = -WORLD_R; dz <= WORLD_R; ++dz) {
            int cx = pcx + dx, cz = pcz + dz;
            auto it = world.chunks.find(chunkKey(cx, cz));
            if (it == world.chunks.end()) continue;
            Mesh m = buildChunkMesh(world, cx, cz);
            if (m.vertexCount > 0) {
                it->second.model    = LoadModelFromMesh(m);
                it->second.hasModel = true;
            }
            it->second.dirty = false;
        }
    }
}

void World_UpdateAround(World& world, int pcx, int pcz) {
    // 0) Приём результатов от воркера
    {
        std::queue<GenResult> local;
        {
            std::lock_guard<std::mutex> lk(g_genMutex);
            std::swap(local, g_genResults);
        }
        while (!local.empty()) {
            GenResult& r = local.front();
            auto it = world.chunks.find(chunkKey(r.cx, r.cz));
            if (it != world.chunks.end()) {
                int dx = std::abs(r.cx - pcx);
                int dz = std::abs(r.cz - pcz);
                if (dx <= LOAD_R && dz <= LOAD_R) {
                    std::memcpy(it->second.blocks, r.blocks, sizeof(r.blocks));
                    it->second.state = STATE_GENERATED;
                    it->second.dirty = true;
                    markNeighborsDirty(world, r.cx, r.cz);
                }
            }
            local.pop();
        }
    }

    // 1) Выгрузка за пределами LOAD_R
    for (auto it = world.chunks.begin(); it != world.chunks.end(); ) {
        int dx = std::abs(it->second.cx - pcx);
        int dz = std::abs(it->second.cz - pcz);
        if (dx > LOAD_R || dz > LOAD_R) {
            int cx = it->second.cx, cz = it->second.cz;
            unloadChunkModel(it->second);
            it = world.chunks.erase(it);
            markNeighborsDirty(world, cx, cz);
        } else {
            ++it;
        }
    }

    // 2) Постановка новых задач генерации (плейсхолдеры STATE_LOADING)
    {
        std::vector<GenTask> batch;
        batch.reserve(32);
        for (int dx = -LOAD_R; dx <= LOAD_R; ++dx) {
            for (int dz = -LOAD_R; dz <= LOAD_R; ++dz) {
                int cx = pcx + dx, cz = pcz + dz;
                uint64_t key = chunkKey(cx, cz);
                if (world.chunks.find(key) != world.chunks.end()) continue;

                Chunk& ch = world.chunks[key];
                ch.cx = cx; ch.cz = cz;
                ch.state = STATE_LOADING;
                ch.dirty = false;
                batch.push_back({cx, cz});
            }
        }
        if (!batch.empty()) {
            {
                std::lock_guard<std::mutex> lk(g_genMutex);
                for (auto& t : batch) g_genTasks.push(t);
            }
            g_genCV.notify_one();
        }
    }

    // 3) Скрыть модели за пределами WORLD_R
    for (auto& kv : world.chunks) {
        Chunk& ch = kv.second;
        if (!ch.hasModel) continue;
        if (std::abs(ch.cx - pcx) > WORLD_R || std::abs(ch.cz - pcz) > WORLD_R)
            unloadChunkModel(ch);
    }

    // 4) Бюджетная пересборка мешей внутри WORLD_R.
    //    Строим и тогда, когда меша ещё нет (например, кольцо LOAD_R\WORLD_R
    //    при спавне), иначе при подъезде будут дырки.
    int rebuilt = 0;
    bool budgetHit = false;
    for (int dx = -WORLD_R; dx <= WORLD_R && !budgetHit; ++dx) {
        for (int dz = -WORLD_R; dz <= WORLD_R; ++dz) {
            if (MESH_REBUILD_BUDGET > 0 && rebuilt >= MESH_REBUILD_BUDGET) {
                budgetHit = true;
                break;
            }
            int cx = pcx + dx, cz = pcz + dz;
            auto it = world.chunks.find(chunkKey(cx, cz));
            if (it == world.chunks.end()) continue;
            Chunk& ch = it->second;
            if (ch.state != STATE_GENERATED) continue;
            if (ch.hasModel && !ch.dirty) continue;

            unloadChunkModel(ch);
            Mesh m = buildChunkMesh(world, cx, cz);
            if (m.vertexCount > 0) {
                ch.model    = LoadModelFromMesh(m);
                ch.hasModel = true;
            }
            ch.dirty = false;
            ++rebuilt;
        }
    }
}

void World_Clear(World& world) {
    for (auto& kv : world.chunks) unloadChunkModel(kv.second);
    world.chunks.clear();
}

/* =========================== Frustum culling ========================= */

void Frustum_FromCamera(const Camera3D& cam, float hfovDeg, float aspect,
                        float nearP, float farP, Frustum& out)
{
    Vector3 eye = cam.position;

    Vector3 fwd = Vector3Subtract(cam.target, cam.position);
    if (Vector3Length(fwd) < 1e-6f) fwd = (Vector3){0, 0, -1};
    fwd = Vector3Normalize(fwd);

    Vector3 worldUp = cam.up;
    Vector3 right = Vector3CrossProduct(fwd, worldUp);
    float rlen = Vector3Length(right);
    if (rlen < 1e-4f) {
        right = (fabsf(fwd.x) > 0.9f) ? (Vector3){0, 0, 1} : (Vector3){1, 0, 0};
    }
    right = Vector3Normalize(right);
    Vector3 up = Vector3CrossProduct(right, fwd);

    const float tanH = tanf(hfovDeg * DEG2RAD * 0.5f);
    const float tanV = tanH / aspect;

    auto setPlane = [&](int i, Vector3 n, float d) {
        float len = Vector3Length(n);
        if (len > 1e-6f) { n = Vector3Scale(n, 1.0f / len); d /= len; }
        out.planes[i] = (Vector4){ n.x, n.y, n.z, d };
    };

    { Vector3 n = Vector3Add(right, Vector3Scale(fwd, tanH));
      setPlane(0, n, -Vector3DotProduct(n, eye)); }                                   // left
    { Vector3 n = Vector3Add(Vector3Negate(right), Vector3Scale(fwd, tanH));
      setPlane(1, n, -Vector3DotProduct(n, eye)); }                                   // right
    { Vector3 n = Vector3Add(up, Vector3Scale(fwd, tanV));
      setPlane(2, n, -Vector3DotProduct(n, eye)); }                                   // bottom
    { Vector3 n = Vector3Add(Vector3Negate(up), Vector3Scale(fwd, tanV));
      setPlane(3, n, -Vector3DotProduct(n, eye)); }                                   // top
    setPlane(4, fwd, -Vector3DotProduct(fwd, eye) - nearP);                           // near
    { Vector3 n = Vector3Negate(fwd);
      setPlane(5, n, Vector3DotProduct(fwd, eye) + farP); }                           // far
}

static inline bool aabbOutsidePlane(const Vector4& p, Vector3 bmin, Vector3 bmax) {
    Vector3 pv = {
        p.x >= 0.0f ? bmax.x : bmin.x,
        p.y >= 0.0f ? bmax.y : bmin.y,
        p.z >= 0.0f ? bmax.z : bmin.z
    };
    return (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w) < 0.0f;
}

bool Frustum_ChunkVisible(const Frustum& f, int cx, int cz) {
    const float fx0 = (float)(cx * CHUNK_W) - 0.5f;
    const float fz0 = (float)(cz * CHUNK_W) - 0.5f;
    Vector3 bmin = { fx0,           (float)CHUNK_MIN_Y - 0.5f, fz0 };
    Vector3 bmax = { fx0 + CHUNK_W, (float)CHUNK_MAX_Y + 0.5f, fz0 + CHUNK_W };
    for (int i = 0; i < 6; ++i)
        if (aabbOutsidePlane(f.planes[i], bmin, bmax)) return false;
    return true;
}

/* ============================== Отрисовка ============================ */

void World_Draw(const World& world, const Frustum& f, bool showWires) {
    rlEnableBackfaceCulling();
    for (const auto& kv : world.chunks) {
        const Chunk& ch = kv.second;
        if (!ch.hasModel) continue;
        if (!Frustum_ChunkVisible(f, ch.cx, ch.cz)) continue;
        DrawModel(ch.model, (Vector3){0, 0, 0}, 1.0f, WHITE);
        if (showWires) DrawModelWires(ch.model, (Vector3){0, 0, 0}, 1.0f, BLACK);
    }
}