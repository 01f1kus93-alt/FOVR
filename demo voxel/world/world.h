#pragma once
#include <raylib.h>
#include <cstdint>
#include <unordered_map>

/* ============================ Размеры мира ============================ */

constexpr int CHUNK_W     = 16;
constexpr int CHUNK_MIN_Y = -12;
constexpr int CHUNK_MAX_Y =  40;
constexpr int CHUNK_H     = (CHUNK_MAX_Y - CHUNK_MIN_Y + 1);   // 53
constexpr int WORLD_R     = 4;              // радиус отрисовки в чанках
constexpr int LOAD_R      = WORLD_R + 1;    // радиус генерации
constexpr int SEA_LEVEL   = 0;

// Бюджет пересборки мешей за кадр. 0 = без ограничения.
constexpr int MESH_REBUILD_BUDGET = 4;

/* ================================ Блоки =============================== */

enum BlockId : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_BEDROCK,
    BLOCK_STONE,
    BLOCK_DIRT,
    BLOCK_GRASS,
    BLOCK_SAND,
    BLOCK_WATER,
    BLOCK_WOOD,
    BLOCK_LEAVES,
};

enum ChunkState : uint8_t {
    STATE_EMPTY = 0,
    STATE_LOADING,      // поставлен в очередь воркеру, блоки ещё не валидны
    STATE_GENERATED,    // блоки заполнены
};

/* ================================ Чанк ================================ */

struct Chunk {
    int cx = 0, cz = 0;
    BlockId blocks[CHUNK_W * CHUNK_H * CHUNK_W];
    Model model = {0};
    bool  hasModel   = false;
    ChunkState state = STATE_EMPTY;
    bool  dirty      = false;   // нужна пересборка меша

    Chunk();
};

/* ================================ Мир ================================= */

struct World {
    std::unordered_map<uint64_t, Chunk> chunks;

    World();

    BlockId getBlock(int x, int y, int z) const;
    bool    has(int x, int y, int z) const;
};

/* ============================== Утилиты =============================== */

uint64_t chunkKey(int cx, int cz);
int      floordiv(int a, int b);
int      blockIndex(int lx, int y, int lz);

/* =========================== Жизненный цикл =========================== */

void World_InitNoise();                                       // задать сиды/частоты шума (один раз)
void World_StartWorker();                                     // запустить поток генерации
void World_StopWorker();                                      // остановить и join
void World_SpawnInitial(World& world, int pcx, int pcz);      // синхронно сгенерировать LOAD_R и собрать WORLD_R
void World_UpdateAround(World& world, int pcx, int pcz);      // тик: приём результатов, выгрузка, отправка, бюджетный ремеш
void World_Clear(World& world);                               // выгрузить все модели и очистить map

/* =========================== Frustum culling ========================== */

struct Frustum { Vector4 planes[6]; };

void Frustum_FromCamera(const Camera3D& cam, float hfovDeg, float aspect,
                        float nearP, float farP, Frustum& out);
bool Frustum_ChunkVisible(const Frustum& f, int cx, int cz);

/* ============================== Отрисовка ============================= */

// Рисует все видимые чанки мира. Вызывать между Begin/EndMode3D (или
// BeginTextureMode внутри BeginMode3D) — сам по себе ничего не открывает.
void World_Draw(const World& world, const Frustum& f, bool showWires);