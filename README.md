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

## Android-трекер (FreePIE IMU)

На телефоне ставится **FreePIE IMU**. Оно читает 
акселерометр, гироскоп и магнитометр и шлёт сырые данные по UDP 
на компьютер, а моя библиотека превращает их в углы.

### Настройка телефона

1. Установить **FreePIE IMU** из Google Play.
2. Включить **Send over UDP**.
3. Указать **IP компьютера** (тот, где запущен твой проект) — 
   например, `192.168.1.42`.
4. Указать **порт** — обычно `5555` (тот же, что в `FOVR_Init`).

Телефон и компьютер должны быть **в одной Wi-Fi-сети**. Мобильный 
интернет не подойдёт — UDP-пакеты не пройдут.

Все проверено и работает в связке Linux(Mint) + Android 14

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
