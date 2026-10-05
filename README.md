# FOVR

Модуль VR-рендера с head tracking для raylib. Один заголовок, одна реализация.

## Возможности

- **Стерео-рендер** — два render target'а с суперсэмплингом, asymmetric frustum 
  под IPD, корректный сдвиг камер вдоль правого вектора с учётом roll.
- **Линзовая дисторсия** — barrel distortion + chromatic aberration + vignette 
  через фрагментный шейдер, параметры (`k1`, `k2`, `chroma`, `vignette`, `lensFill`) 
  настраиваются через `VRConfig`.
- **Head tracking по UDP** — приём yaw/pitch/roll, кольцевой буфер сэмплов 
  с интерполяцией и задержкой воспроизведения, экспоненциальное сглаживание, 
  dead-zone против шума, авто- и ручная калибровка.
- **Fallback на мышь** — если трекер не подключён, yaw/pitch читаются с мыши.

## Использование

```cpp
#include "FOVR/FOVR.h"

VRConfig cfg = VR_DefaultConfig();
cfg.ipd     = 0.062f;
cfg.hfovDeg = 100.0f;

FOVR_Init("App", "VR/barrel.frag", &cfg, 5555);

while (!WindowShouldClose()) {
    FOVR_Update();

    VR_SetPosition(cameraPos);

    VR_BeginLeftEye();
        // ... draw scene ...
    VR_EndLeftEye();

    VR_BeginRightEye();
        // ... draw scene ...
    VR_EndRightEye();

    VR_Present();
}

FOVR_Shutdown();
