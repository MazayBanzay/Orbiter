# Сборка, установка, проверка, расширение

[← оглавление](DEVELOPER.md)

## 1. Сборка — `build.bat`

Требования: Visual Studio 2022 Build Tools (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`),
Windows SDK 10.0.26100, **x86** (`vcvars32.bat`). Orbiter SDK берётся из `..\..\Orbitersdk` (корень Orbiter).

Шаги:
1. `rc` → `build\D3D11Client.res` из `src\D3D11Client.rc` (строки 1000/1001, диалог Advanced).
2. `cl /std:c++17 /O2 /Zi /EHsc /W3 /MD /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DWIN32 /D_WINDOWS /LD` —
   **явный список `.cpp`** + `.res` → `build\D3D11Client.dll` (+ `.pdb`). Компоновка: `Orbiter.lib Orbitersdk.lib user32.lib gdi32.lib`;
   остальные библиотеки — через `#pragma comment(lib, …)` в `Client.cpp` (d3d11, dxgi, d3dcompiler, windowscodecs, ole32, d2d1,
   dwrite, msimg32).
3. `cl /LD shim\shim.c … /link /NOENTRY` → `build\D3D9Client.dll` (прокладка).

Важно:
- **Новый `.cpp` нужно дописать в строку `cl` в `build.bat`** — проектного файла нет.
- `/DNDEBUG` обязателен (assert'ы базового `oapi::Sketchpad`, см. [orbiter-api.md §7](orbiter-api.md#7-ловушки-найденные-при-разработке)).
- `#define ORBITER_MODULE` — ровно в одном файле (`Client.cpp:2`), иначе дублируются символы шаблона модуля.
- Сборка не трогает Orbiter. Собирать можно при запущенном Orbiter; устанавливать — нет (DLL занята).

## 2. Установка — `install.bat`

Копирует `build\D3D11Client.dll` → `Modules\Plugin\`, `shaders\*.hlsl` и `*.hlsli` → `Modules\D3D11Client\`,
`build\D3D9Client.dll` → `Modules\D3D11Client\`. Orbiter должен быть закрыт. Шейдеры можно обновлять и без пересборки
(подхватятся при следующем запуске).

Включение клиента: Launchpad → Modules — снять D3D9Client, отметить D3D11Client; запуск через `Orbiter_ng.exe`.

## 3. Тестовый прогон — `tools\run_test.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_test.ps1 [-Scenario "(Current state)"] [-Seconds 45]
```

Что делает (`tools\run_test.ps1`):
1. Если `Orbiter.exe` / `Orbiter_ng.exe` уже запущен — **не запускается** (`Orbiter is running - test skipped`, код 2):
   иначе восстановление в конце затёрло бы cfg и состояние пользователя.
2. Сохраняет `Scenarios\(Current state).scn` и `Orbiter_NG.cfg` в `%TEMP%`.
3. Убирает из `Orbiter_NG.cfg` строки `XRSound` (без звука) и `D3D11Client` (клиент передаётся ключом `-p`).
4. Копирует сценарий в `Scenarios\Tests\D3D11_test.scn` (этот файл после прогона остаётся).
5. Ставит `D3D11CLIENT_TEST=1` — клиент прячет окно и не берёт фокус (`WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, `SW_HIDE`,
   `Client.cpp:71-78, 650`), `Present` без vsync.
6. Запускает `Modules\Server\Orbiter.exe -p D3D11Client -s "Tests\D3D11_test" --maxsystime=<Seconds> -x` свёрнутым;
   ждёт `Seconds + 120` с, при зависании убивает (`killed (hang)`), печатает код выхода.
7. В `finally` возвращает переменную окружения, сценарий и cfg.

Правило проекта: Orbiter самостоятельно запускается **только** этим скриптом (скрытно и без звука).

## 4. Самопроверка — `Orbiter.log`

Все строки клиента начинаются с `D3D11Client: `. Ожидаемые при нормальном старте:

```
Module D3D11Client.dll ....... [Build ……, API ……]
D3D11Client: extension shim loaded (gcCore, AtmoFX)
D3D11Client: initialised, window 1920x1080
D3D11Client: adapter NVIDIA GeForce RTX 4050 Laptop GPU, 3072 MB
D3D11Client: anti-aliasing: MSAA, 4 sample(s)
D3D11Client: render window 1904x1041, D3D11 device ready
D3D11Client: effects: atmosphere from space on
D3D11Client: planet Titan: R 2575 km, max level 21, archives: surf no, elev no, atmosphere, liquid
D3D11Client: stars: 41057
D3D11Client: session: 32 planets, 2 vessels
D3D11Client: fps 60 | GPU ms: planets 3.07, vessels 0.00, post 13.20 | tiles drawn 26, load queue 0 | vessels 2
D3D11Client: screenshot Modules\D3D11Client\shots\start.png
```
(значения — из прогона 19.10.2026; строки `anti-aliasing` и `effects` появились в срезе позже этого прогона.)

На что смотреть:
- `FAILED` в любой строке клиента (`device creation FAILED`, `shader compilation FAILED`, `extension shim NOT loaded`,
  `effects: … FAILED`); строки `shader <файл> <вход>: …` — ошибки/предупреждения компилятора.
- Строка `fps …` раз в 10 с (если `StatsLog = 1`): `load queue` должен опускаться к 0; `tiles drawn` — число
  нарисованных тайлов; GPU-времена — см. [architecture.md §3](architecture.md#3-кадр) (смысл интервала `post` под вопросом).
- `texture not loaded: <имя>` — текстура меша не найдена ни в `Textures2\`, ни в `Textures\`.
- `atmosphere from space, <планета>: …` — параметры дымки эффекта (`FxAtmo.cpp`).

Снимок: через 15 с после создания устройства — `Modules\D3D11Client\shots\start.png` (back buffer, альфа = 255). Один на
запуск; перезаписывается следующим.

## 5. Как добавить проход

1. **Шейдер** — `shaders\<имя>.hlsl`, входы `VS`/`PS`; `#include "common.hlsli"`, если нужны `b0`, дымка, свет, тени.
   `install.bat` скопирует его сам.
2. **Где компилировать.** Обязательная часть сцены — в `CreateShaders` (`Client.cpp`): при ошибке сцена не рисуется
   вовсе. Необязательный эффект — через `FxCompile` в своём `Fx*.cpp` (при ошибке выключается только он).
3. **Ресурсы** — создавать в `CreateDevice` / `CreateSceneTargets` (`AA.cpp`) или в своём `…Init`. Учитывать сглаживание:
   при MSAA цель сцены многовыборочная (`samples > 1`); для чтения цвета/глубины сцены — `CopySceneColour(dst)`
   (резолв цвета + обновление одновыборочной глубины `depthSRV`). Своя цель рендера в сцене должна совпадать по числу
   выборок.
4. **Константы** — `b1` через общий `cbObj` (≤ 512 байт, `Map(WRITE_DISCARD)` перед каждым draw), либо свой буфер на другом
   слоте (как `b3` у `fx_atmo`). `b0` не трогать.
5. **Вызов** — в нужное место `clbkRenderScene` (порядок — [architecture.md §3](architecture.md#3-кадр)) или в крючок
   `FxFrame` / `FxRenderTransparent` для эффектов (их тела — в `Fx.cpp`, у сессии эффектов).
6. **Состояние.** Каждый проход сам выставляет всё нужное (RT/DSV, blend, depth, rasterizer, input layout, топологию,
   шейдеры, CB, SRV, сэмплеры) — предположений о состоянии от предыдущего прохода в коде нет. После прохода —
   отвязать свои SRV (особенно если ресурс потом станет целью) и вернуть RT/DSV сцены, если менял.
   Глубина: обратный Z (`dsWrite`/`dsRead` — `GREATER_EQUAL`, очистка 0).
7. **Сборка** — новый `.cpp` дописать в `build.bat`.
8. **Документация** — строка в таблице кадра, раздел в [shaders.md](shaders.md).

## 6. Как добавить `clbk`

1. Найти виртуальный метод в `Orbitersdk\include\GraphicsAPI.h` и прочитать его doc-комментарий (коды возврата,
   кто владеет памятью, вызывается ли на паузе).
2. Объявить `override` в `Client.h` с **точной** сигнатурой (включая `const` и аргументы по умолчанию — у многих 2D-методов
   они `const`).
3. Реализовать: всё в главном потоке; `SURFHANDLE` → `Surf*` (`NULL` у 2D-методов = слой экрана, см. `Target()`),
   `DEVMESHHANDLE` → `DevMesh*`, `VISHANDLE` → `VesselVis*`. Не возвращать NULL там, где ядро не проверяет
   (как с потоками частиц).
4. Если метод меняет поверхность — `MarkDirty`, чтобы она ушла на GPU в начале следующего кадра.
5. Обновить таблицу в [orbiter-api.md](orbiter-api.md) и, если что-то не сделано, [roadmap.md](roadmap.md).
