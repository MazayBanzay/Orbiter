# Архитектура

[← оглавление](DEVELOPER.md)

Состояние кода: 21.10.2026

Глава описывает состав исходников клиента, жизненный цикл модуля, порядок проходов кадра, системы координат, потоки и
владение ресурсами. Ссылки вида `Client.cpp:921` относятся к `src\`, `common.hlsli:74` — к `shaders\`. Номера строк
сверены с файлами на дату выше; при расхождении искать по имени функции. Подробности отдельных подсистем вынесены в
тематические главы: [exhaust.md](exhaust.md), [atmosphere.md](atmosphere.md), [water.md](water.md), [rain.md](rain.md),
[clouds.md](clouds.md).

## 1. Модули и файлы

Сборка — `build.bat`: один вызов `cl` (x86, `/std:c++17 /O2 /MD /utf-8`) компилирует перечисленные в нём `.cpp` в
`build\<D3D11_BUILD_TAG>\D3D11Client.dll` (тег по умолчанию `main`), затем собирается прокладка `shim\shim.c` →
`D3D9Client.dll`; оба DLL и PDB копируются в `build\`. Колонка «В сборке» отражает список файлов в `build.bat`.

### 1.1. Исходники `src\*.cpp`

| Файл | В сборке | Что внутри | Глава |
|---|---|---|---|
| `Client.cpp` | да | `InitModule`/`ExitModule`, `clbkInitialise`, окно и устройство (`CreateDevice`), защита от кэша шейдеров драйвера, компиляция основных шейдеров (`CompileFile`, `CreateShaders`), сессия, потоки-загрузчики тайлов, поверхности и текстуры, потоки частиц (создание), материалы и группы мешей, визуалы кораблей, **кадр** (`clbkRenderScene`, `clbkDisplayFrame`), снимок экрана, экран загрузки (Direct2D), `UpdateFrameCB`, каскадные тени (`RenderShadows`), `LogMemory`, тестовый сценарий `TestScript` | — |
| `AA.cpp` | да | Цели сцены под режим сглаживания (`CreateSceneTargets`), MSAA-резолв, `CopySceneColour`/`ResolveDepth`, джиттер и TAA (`FinishScene`), тон-маппинг + FXAA (`ToneMapAndAA`) | — |
| `Bloom.cpp` | да | Свечение ярких мест: 5 уровней уменьшения вдвое и обратного сложения (`RenderBloom`), результат добавляется в проходе `post.hlsl` | — |
| `Ssao.cpp` | да | Затенение окружения GTAO (`RenderAO`, `gtao.hlsl`): половинное разрешение, умножается только доля света неба | — |
| `Mesh.cpp` | да | `DevMesh::Build` из шаблона Orbiter, `VesselVis` (меши, анимации), `RenderVessel` (основной, зеркальный, теневой проходы; матрицы прошлого кадра для векторов движения), `DirtSet` | — |
| `Planet.cpp` | да | `.tree`-архивы, `Planet` (атмосфера и жидкость из cfg, микротекстуры, классы грунта), загрузка тайла `Planet::Load` (поток-загрузчик), обход дерева `Traverse` с LOD, рисование, `Purge`, `LoadStats` | — |
| `Draw.cpp` | да | 2D: GDI-шрифты/перья/кисти, `GPad` (Sketchpad на DIB или на GPU), DIB-сторона поверхностей, Blt/Fill, слой экрана (`BeginScreenLayer`, `CompositeScreenLayer`), 2D-панели (`clbkRender2DPanel`), профиль 2D (`Prof2DTick`) | — |
| `GpuPad.cpp` | да | Sketchpad на GPU: пакет примитивов (`Sketch.h`) в цель поверхности в гамма-пространстве, `PipeState` (сохранение/восстановление конвейера) | — |
| `HudApi.cpp` | да | Нативное рисование модулей `D3D11Hud` (`D3D11Hud_GetApi`): холсты, штрихи, заливки, текст SDF, пути AR (`ArPath`) | — |
| `Ar.cpp` | да | Проход дополненной реальности в 3D-сцене: метки (`AtmoFX_HudMarker`), пути, линии рельефа (`AtmoFX_HudFrame`) | — |
| `Ipi.cpp` | да | gcCore `gcIPInterface`: пиксельный шейдер модуля над его поверхностями | — |
| `Ext.cpp` | да | gcCore (`gcBindCoreMethod`), экспорты `AtmoFX_*` (кроме HUD), вызов HUD-процедур модулей (`CallRenderProcs`), загрузка прокладки (`LoadShim`) | [extensions.md](extensions.md) |
| `Liquid.cpp` | да | Жидкие поверхности, зеркало (`RenderMirror`), дно под жидкостью (`SaveLiquidBed`), вид из-под поверхности (`RenderUnderwater`); задаёт `g_fxPlanets` | [water.md](water.md) |
| `FxWaves.cpp` | да | Поле волн на жидкости (БПФ, вычислительные шейдеры `fx_waves.hlsl`) | [water.md](water.md) |
| `Wake.cpp` | да | Кильватер и волновое поле от узлов `AtmoFX_SetWake` (`wavefield.hlsl`) | [water.md](water.md) |
| `Spray.cpp` | да | Брызги от колёс в жидкости (флипбук) | [water.md](water.md), [exhaust.md](exhaust.md) |
| `Fx.cpp` | да | Крючки эффектов `FxInit`/`FxFrame`/`FxRenderTransparent`/`FxRelease`/`FxBindLight` → `FxSystem` (атмосфера + волны), `FxCompile`, переключатели `Fx.cfg` | [atmosphere.md](atmosphere.md) |
| `FxAtmo.cpp` | да | FX-атмосфера: таблицы, полноэкранный проход, свет в точке для шейдеров клиента, адаптация глаза | [atmosphere.md](atmosphere.md) |
| `Sun.cpp` | да | Солнце (`RenderSun`) и блик в глазу (`RenderSunGlare`) | [atmosphere.md](atmosphere.md) §6 |
| `Stars.cpp` | да | Звёзды из каталога Orbiter (`oapi::CelestialSphere`), `CreateStars`, `RenderStars` | — |
| `Lights.cpp` | да | Локальные источники кораблей: сбор, кластерный список 16×9×24, тени прожекторов (атлас 2×2), карты видимости неба, лучи в воздухе, линзы и маяки, адаптация глаза к ним | — |
| `Exhaust.cpp` | да | Эффекты двигателей (`ExhaustInit`/`ExhaustRender`/`ExhaustRelease`): струи AtmoFX, свечение металла, марево, факелы | [exhaust.md](exhaust.md) |
| `GpuParticles.cpp` | да | GPU-частицы (пул 65536): выброс грунта, брызги, частицы модулей | [exhaust.md](exhaust.md) |
| `H2Chem.cpp` | да | Химия и оптика водородной струи | [exhaust.md](exhaust.md) |
| `Smoke.cpp` | да | Дым и конденсат как 3D-поток (решатель на GPU) | [exhaust.md](exhaust.md) |
| `VesselExhaust.cpp` | да | Объёмные факелы `VESSEL::AddExhaust` | [exhaust.md](exhaust.md) |
| `Particles.cpp` | да | `PStream` — потоки частиц Orbiter (эмиссия на CPU, рисование), текстура частицы по умолчанию | [exhaust.md](exhaust.md) §12 |
| `Clouds.cpp` | да | Объёмные облачные слои по `Clouds.cfg` (Титан) | [clouds.md](clouds.md) §3 |
| `Storm.cpp` | да | Ураган Земли: шум, погодная карта, тень (`RenderStormShadow`), облака (`RenderStorm`), турбулентность (`StormPreStep`, `StormFrameFallback`), экспорт `StormSoundState` | [clouds.md](clouds.md) §4 |
| `StormRain.cpp` | да | Дождь под ураганом | [rain.md](rain.md) |
| `Rain.cpp` | да | Метановый дождь Титана | [rain.md](rain.md) |
| `VisorDrops.cpp` | да | Капли на стекле шлема: снаружи (`RenderVisorDropsOutside`) и изнутри (`RenderVisorDropsInside`) | [rain.md](rain.md) §4 |
| `Dds.cpp` | да | Загрузчик DDS (BC1–5, DX10, несжатые), генерация недостающих мипов на CPU, `ReadFileBytes` | — |
| `VideoTab.cpp` | да | Вкладка Video в Launchpad (DXGI), диалог Advanced, `ClientCfg::Load/Save` | [data-and-config.md](data-and-config.md) |
| `ShaderCache.cpp` | да | `CachedCompileFromFile`: дисковый кэш байт-кода `Modules\D3D11Client\shadercache\<хеш>.cso` | [shaders.md](shaders.md) |
| `HangWatch.cpp` | да | Сторож зависаний (`hang.log`), отчёт о падении (`crash.log`), кольцо последних скомпилированных шейдеров (`NoteShader`) | — |
| `MemCensus.cpp` | да | Перепись адресного пространства по владельцам (`LogMemoryCensus`) | — |
| `HeapTrack.cpp` | да | Учёт кучи CRT по модулям (`HeapTrackStart`, `HeapTrackReport`), только при `HeapByModule = 1` | — |
| `FxTest.cpp` | да | Тестовые эффекты без игрока (`FxTestFrame`), чтение тестовых ключей из `Client.cfg` (`FxTestNum`, `FxTestShotTime`) | [build-and-test.md](build-and-test.md) |
| `CloudLayer.cpp` | **нет** | 2D-слой облаков Земли из `Cloud.tree` | [clouds.md](clouds.md) §5 |
| `EarthClouds.cpp` | **нет** | Объёмные облака Земли по слою | [clouds.md](clouds.md) §5 |
| `Cyclone.cpp` | **нет** | Погодная карта движущегося циклона | [clouds.md](clouds.md) §5 |

Методы `RenderCloudShadow`, `RenderCloudLayer`, `RenderEarthClouds` объявлены в `Client.h:564-566`, определены только в
файлах вне сборки и нигде не вызываются. В корне `_src\d3d11client\` лежит пустой файл `Sun.cpp` (0 байт); в сборку он не
входит, рабочий — `src\Sun.cpp`.

### 1.2. Заголовки и ресурсы

| Файл | Что внутри |
|---|---|
| `Client.h` | Общие типы: `ClientCfg`, `ExtState`, `FxAtmoState` (`g_fxAtmo`), `Surf`, `PipeState`, `PStream`, `DGroup`/`DevMesh`, `VesselVis`, `TreeArchive`, `ElevQ`, `Tile`, `AtmCfg`, `LiquidCfg`, `Planet`, класс `D3D11Client`, `g_devGen`, `RotToXM`, `LiquidLevelFor`, макрос `LOGC` |
| `Fx.h`, `FxSystem.h`, `FxInternal.h` | Крючки эффектов, объект `FxSystem`, общее для `Fx*.cpp`/`Liquid.cpp` (`g_fxPlanets`, `FxCompile`) |
| `FxAtmo.h`, `FxWaves.h` | Классы `Atmosphere`, `WaveField` |
| `Exhaust.h`, `ExhaustInternal.h`, `H2Chem.h` | Крючки эффектов двигателей; общие структуры `Exhaust.cpp`/`GpuParticles.cpp`; химия H2 |
| `Wake.h` | Кильватер для шейдера жидкости |
| `Ar.h`, `HudAr.h` | Проход AR; пути AR модулей, хранимые `HudApi.cpp` |
| `Sketch.h` | Пакет Sketchpad на GPU (`SkBatch`) |
| `CloudLayer.h` | Интерфейсы `CloudLayer.cpp`/`Cyclone.cpp`/`EarthClouds.cpp` (вне сборки) |
| `D3D11Client.rc`, `resource.h` | Строки 1000/1001 (описание, «Graphics engines»), диалог `IDD_ADVANCED` |

Прочее: `shaders\*.hlsl`, `*.hlsli` — см. [shaders.md](shaders.md); `shim\shim.c`, `shim\shim.def` — прокладка
`D3D9Client.dll`, см. [extensions.md](extensions.md); `tools\run_test.ps1` — тестовый прогон, см.
[build-and-test.md](build-and-test.md).

Глобальные объекты: `g_client` (`Client.cpp:55`), `g_cfg` (`VideoTab.cpp`), `g_ext` (`Ext.cpp`), `g_fxAtmo`
(`Client.h:87`, inline), `g_fxPlanets` (`Liquid.cpp`), `g_devGen` (`Client.cpp:307`). Логирование — `LOGC`
(`Client.h:36`): `oapiWriteLogV("D3D11Client: " …)`.

## 2. Жизненный цикл

```
InitModule                  HeapTrackStart (только при HeapByModule = 1); new D3D11Client; oapiRegisterGraphicsClient;
                            LoadShim
clbkInitialise              базовый Initialise; g_cfg.Load(); InitVideoTab(); размер окна из VIDEODATA;
                            каталог шейдеров Modules\D3D11Client\
   … Launchpad: LaunchpadVideoWndProc, clbkRefreshVideoData, OpenAdvanced …
clbkCreateRenderWindow      окно (базовый класс); «полный экран» = окно без рамки на весь монитор;
                            D3D11CLIENT_TEST_RES; std::set_terminate(LogTerminate); тестовый режим (скрытое окно);
                            CreateDevice → CreateShaders → CreateLiquid → CreateParticleTexture →
                            FxInit + ExhaustInit (если шейдеры собрались) → потоки-загрузчики → HangWatchStart
clbkSplashLoadMsg           экран загрузки через Direct2D прямо в swap chain (+ Present(0))
clbkPostCreation            Planet для каждого OBJTP_PLANET; ScanVessels; CreateStars; loadsteps.txt
   [цикл] clbkPreStep (StormPreStep) … clbkRenderScene → (Orbiter рисует HUD через Sketchpad) → clbkDisplayFrame
   clbkNewVessel / clbkDeleteVessel — визуалы и данные модулей для корабля
clbkCloseSession            очистка очереди загрузки; визуалы: UnregisterVisObject + delete
clbkDestroyRenderWindow     остановка и join загрузчиков; HangWatchStop; delete планет; FxRelease; ExhaustRelease;
                            delete потоков частиц; ClearState + Flush; освобождение D2D, слоя экрана, back buffer,
                            swap chain; базовый Destroy
ExitModule                  oapiUnregisterGraphicsClient; delete
```

Код — `Client.cpp:57-154, 534-577`, кроме `LoadShim` (`Ext.cpp:141`), `InitVideoTab`/`OpenAdvanced` (`VideoTab.cpp:118, 247`),
`CreateSceneTargets` (`AA.cpp:15`), `CreateLiquid` (`Liquid.cpp:54`), `CreateParticleTexture` (`Particles.cpp:204`),
`CreateStars` (`Stars.cpp:17`), `FxInit`/`FxRelease` (`Fx.cpp`), `ExhaustInit`/`ExhaustRelease` (`Exhaust.cpp:750, 752`),
`HangWatchStart`/`HangWatchStop` (`HangWatch.cpp`), `HeapTrackStart` (`HeapTrack.cpp:248`).

### 2.1. `clbkCreateRenderWindow` (`Client.cpp:109-154`)

- Окно создаёт базовый класс; при `fullscreen` стиль меняется на `WS_POPUP` с размером монитора. `W`, `H` берутся из
  клиентской области окна, в тестовом прогоне их может заменить `D3D11CLIENT_TEST_RES` (`Client.cpp:120-127`).
- `RenderWndProc` (`Client.cpp:103`) отбрасывает `WM_SYSCOMMAND SC_KEYMENU`: отпущенный одиночный Alt иначе вводит окно в
  модальный цикл меню, и цикл Orbiter останавливается.
- `std::set_terminate(LogTerminate)` (`Client.cpp:129`): необработанное C++-исключение пишет в `Orbiter.log` строку
  `FATAL terminate` и стек «модуль+смещение» (`Client.cpp:25-44`), затем `abort`.
- Тестовый режим (`D3D11CLIENT_TEST=1`): окно скрыто, `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`.
- Отказ `CreateDevice` — выход без устройства (сцена не рисуется). Отказ `CreateShaders` — `shadersOk = false`,
  `FxInit`/`ExhaustInit` не вызываются, кадр не рисуется (`Client.cpp:923`).
- Загрузчики тайлов: `clamp(hardware_concurrency − 2, 2, 6)` потоков `LoaderMain` (`Client.cpp:144-149`); верхний предел
  обоснован в комментарии рабочими буферами потока («~10 MB at most») в 32-битном адресном пространстве.
- `HangWatchStart` (`Client.cpp:150`): фильтр необработанных исключений (`crash.log`) и поток-сторож (`hang.log`), §5, §6.4.

### 2.2. `CreateDevice` (`Client.cpp:308-458`)

Порядок:
1. `g_devGen++` (`Client.cpp:310`).
2. `TrimDriverCache` (`Client.cpp:163`): файлы кэша драйвера из `Modules\D3D11Client\drivercache.txt` (записан при прошлом
   старте) больше `DriverCacheMB` переносятся в `…\NVIDIA\DXCache_Orbiter_old`.
3. `DriverCacheGuardBefore` (`Client.cpp:278`): при `DriverCacheOff = 1` загружается `NvMemMapStorage.dll` (путь из
   `nvmemmap.txt` или из реестра драйвера, `FindNvMemMapLib`), его импорт `CreateFileW` подменяется на
   `NoNvphCreateFileW`, отказывающий в открытии `*.nvph`.
4. `D3D11CreateDevice`: аппаратный адаптер по умолчанию, уровень 11_0, `BGRA_SUPPORT`; при `DriverThreads = 0` ещё
   `PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` (`Client.cpp:314-316`).
5. `NoteDriverCache` (`Client.cpp:188`): отображённые в процесс файлы `\NVIDIA\DXCache\` → `drivercache.txt` и строка
   журнала; `DriverCacheGuardAfter` (`Client.cpp:294`): путь `NvMemMapStorage.dll` → `nvmemmap.txt`, число отказов.
6. Журнал: адаптер, видеопамять, версия драйвера, режим потоков драйвера, процессор и число логических ядер.
7. Swap chain: `CreateSwapChainForHwnd`, B8G8R8A8_UNORM, 2 буфера, `FLIP_DISCARD`, `DXGI_MWA_NO_ALT_ENTER`; RTV back
   buffer в формате `_SRGB` (`Client.cpp:337-345`).
8. `CreateSceneTargets` (`AA.cpp:15`), §3.1.
9. Общие состояния: `rsBack`, `rsNone`, `dsWrite`/`dsRead` (`GREATER_EQUAL`), `bsOpaque`, `bsAlpha`, `bsAdd`.
10. Direct2D/DirectWrite для экрана загрузки, `loadsteps.txt`.
11. Сэмплеры `sampAniso` (wrap) и `sampClamp`; при TAA mip bias `log2(renderScale) − 1` (`Client.cpp:382`).
12. Константные буферы `cbObj` (1024 Б), `cbGrp` (256), `cbFrame` (1024).
13. Атлас теней `2·SHADOW_N × 2·SHADOW_N` R32 (`SHADOW_N = 2048`, `Client.h:673`), сэмплер сравнения, `dsShadow`,
    `rsShadow` (`Client.cpp:392-408`).
14. Белая текстура 1×1; общий индексный буфер тайлов (сетка 64×64 с юбкой и та же сетка через узел, `tileIdxCount2`).
15. Запросы GPU-времени: кольцо из 4 кадров (`qDis`, `qTs[4]`, `qMk[12]`, `qSub[7]`).

**Поколение устройства `g_devGen`** (`Client.h:214-217`). Orbiter создаёт окно и устройство заново для каждого сценария.
Модуль, хранящий объекты устройства в статических переменных, обязан пересоздать их при смене устройства: объекты старого
устройства, переданные контексту нового, по комментарию в `Client.h` вызвали исключение GPU (Xid 13) и потерю устройства.
Механизмы в коде — §6.3.

**Компиляция шейдеров** (`CompileFile`, `Client.cpp:470-486`): последовательно под мьютексом, с паузой не менее
`kShaderGapMs = 12` мс между шейдерами (по комментарию — против пачки компиляций в потоках драйвера при старте);
время последней компиляции — `g_shaderTick` (загрузчики тайлов ждут 0.5 с после неё, `Client.cpp:608-614`); каждая
компиляция записывается в кольцо `NoteShader` (для `crash.log`) и идёт через `CachedCompileFromFile` (`ShaderCache.cpp`):
ключ — FNV-хеш исходника со всеми `#include "…"`, точки входа, цели, флагов и макросов. `CreateShaders`
(`Client.cpp:488-532`) собирает только базовый набор (`mesh`, `planet`, `post`, `blit`, `sky`, `aa`, `stars`, `liquid`,
`particles`, `panel`); остальные модули компилируют свои шейдеры лениво при первом использовании
(`CompileShaderFile` или `FxCompile`).

### 2.3. Закрытие

- `clbkCloseSession` (`Client.cpp:546`): очередь загрузки очищается под `loadMx` (состояния тайлов не сбрасываются;
  планеты удаляются позже), визуалы снимаются с регистрации и удаляются.
- `clbkDestroyRenderWindow` (`Client.cpp:556-577`): `loaderRun = false`, `notify_all`, `join` всех загрузчиков,
  `HangWatchStop`; затем `delete` планет (после остановки загрузчиков — они работают с тайлами), `FxRelease`,
  `ExhaustRelease` (вместе с `SprayRelease`, `VesselExhaustRelease`, `SmokeRelease`), удаление `PStream`, `ClearState` +
  `Flush`, освобождение `d2rt`, слоя экрана, `bbRTV`, `bbTex`, swap chain; базовый `clbkDestroyRenderWindow`.
- Сами `dev`, `ctx` и объекты-члены (`hdrTex`, шейдеры, буферы) здесь не освобождаются; они заменяются при следующем
  `CreateDevice`/`CreateShaders` (присваивание через `ComPtr::operator&` освобождает прежний объект) или в деструкторе.
- Порядок `clbkCloseSession` → `clbkDestroyRenderWindow` задаёт Orbiter; клиент его не проверяет.

## 3. Кадр

### 3.1. Цели сцены (`CreateSceneTargets`, `AA.cpp:15-68`)

| Цель | Формат | Размер | Назначение |
|---|---|---|---|
| `hdrTex` / `hdrRTV` / `hdrSRV` | R16G16B16A16_FLOAT, MSAA при режимах 1–3 | `RW × RH` | Цвет сцены (HDR) |
| `depthTex` / `dsv` / `dsvRO` | R32_TYPELESS → D32_FLOAT | `RW × RH` | Глубина (обратный Z); `dsvRO` — только чтение |
| `depthSRV` | R32_FLOAT | `RW × RH` | Одновыборочная глубина (при MSAA — `depthRes`, заполняется `ResolveDepth`) |
| `outTex[0..1]` | R16G16B16A16_FLOAT | `W × H` | Резолв MSAA или выход TAA (два — для истории) |
| `mvTex` | R16G16_FLOAT | `RW × RH` | Векторы движения (только TAA) |
| `ldrTex` | B8G8R8A8 (RTV sRGB) | `W × H` | Промежуточное изображение для FXAA |
| back buffer | B8G8R8A8, RTV `_SRGB` (`bbRTV`) и линейный (`bbRTVLin`) | `W × H` | Выход |

`RW × RH` = `W × H`, кроме TAA с `renderScale < 1` (0.5…1). Число выборок MSAA понижается, пока формат его не
поддерживает. Режим `AntiAliasing`: 0 выкл., 1–3 MSAA 2/4/8×, 4 FXAA, 5 TAA; применяется только при создании окна.

### 3.2. `clbkRenderScene` (`Client.cpp:921-1133`)

Колонка «Цель / состояние» указывает то, что выставляется на шаге или требуется от него; «hdr+dsv» — `hdrRTV` с
записываемой глубиной, «hdr, глубина текстурой» — `hdrRTV` без DSV, глубина читается через `depthSRV`.

| № | Шаг | Функция | Файл | Цель / состояние | Назначение |
|---|---|---|---|---|---|
| 0 | Проверка | — | `Client.cpp:923` | — | Нет устройства, swap chain или `shadersOk == false` — выход |
| 1 | Тестовые действия | `TestScript`, `FxTestFrame`; удержание/падение на кадре 600 | `Client.cpp:925-932` | — | Только `testMode` |
| 2 | След памяти | `LogMemory("frame N")` | `Client.cpp:933-938` | — | 60 кадров после действия сценария, при изменении private ≥ 2 MB |
| 3 | Поверхности | `FlushDirty` | `Draw.cpp:791` | CPU → текстуры | Изменённые DIB (MFD, дисплеи) заливаются в текстуры; `Prof2DTick` |
| 4 | Слой экрана | `BeginScreenLayer` | `Draw.cpp:1034` | DIB | Очистка прямоугольника прошлого кадра ключевым цветом |
| 5 | Корабли | `ScanVessels` раз в 32 кадра | `Client.cpp:899` | — | Запасной путь к `clbkNewVessel`/`clbkDeleteVessel` |
| 6 | GPU-время | `Begin(qDis)`, `End(qTs[0])`, `GpuMark 0` | `Client.cpp:942-945` | — | Начало кадра |
| 7 | Камера | `oapiCameraGlobalPos`, `oapiCameraRotationMatrix`, `oapiCameraAperture` | `Client.cpp:947-976` | — | Проекция (§4), `curVPNoJit`, джиттер TAA `Jitter` (`AA.cpp:71`: Halton 2,3, 8 кадров; 16 при `RW < W`); тест `TestLookSun` |
| 8 | Эффекты: вычисления | `FxFrame` → `Atmosphere::Frame`, `WaveField::Frame` | `Fx.cpp`, `FxAtmo.cpp:354`, `FxWaves.cpp:110` | CS | Таблицы атмосферы, константы и свет, адаптация глаза; поле волн |
| 9 | Тени Солнца | `RenderShadows` | `Client.cpp:1306` | `shadowDSV`, прямой Z, `rsShadow`/`dsShadow` | 4 тайла атласа: фокус-корабль + 3 каскада (§4) |
| 10 | Локальный свет | `LightsFrame` | `Lights.cpp:102` | свои DSV | Сбор источников, кластеры, тени прожекторов, карты видимости неба (у земли) |
| 11 | Подготовка сцены | вьюпорт `RW×RH`, очистка цвета (0,0,0,1) и глубины **0.0**, сэмплеры s0–s2, тень в t7 | `Client.cpp:983-991` | hdr+dsv | — |
| 12 | Константы кадра | `UpdateFrameCB` | `Client.cpp:1229` | b0 (VS, PS) | Солнце, старая атмосфера, экспозиция и адаптация (вкл. `LightsAdaptEye`), каскады, жидкость под камерой |
| 13 | Локальный свет: привязка | `LightsBind` | `Lights.cpp:556` | b9, t17–t20 | Для `mesh.hlsl`, `planet.hlsl` |
| 14 | Старое небо | полноэкранный треугольник `sky.hlsl` при `skyOn` | `Client.cpp:995-1004` | `bsOpaque`, `dsRead` | Только без FX-атмосферы (`skyOn = false` при `g_fxAtmo.active`) |
| 15 | Звёзды | `RenderStars` | `Stars.cpp:36` | аддитивно, `dsRead` | Видимость `starVis` |
| 16 | Планеты | `Planet::Render` от дальней к ближней; `Purge` | `Planet.cpp:759, 1058` | hdr+dsv | Обход тайлов, постановка в очередь загрузки |
| 17 | Дно жидкости | `SaveLiquidBed` | `Liquid.cpp:32` | копия глубины | Глубина рельефа без кораблей; только если жидкость рисовалась в прошлом кадре |
| 18 | Корабли: непрозрачные | `RenderVessel(…, false)` для всех, кроме своего корабля изнутри | `Mesh.cpp:256` | hdr (+ `mvRTV` при TAA) + dsv | Векторы движения пишутся вторым RT |
| 19 | AO | `RenderAO` | `Ssao.cpp:22` | CS, половинное разрешение, умножение | Только при `AmbientOcclusion`; до жидкости и воздуха |
| 20 | Жидкость | `RenderLiquid` (внутри — `RenderMirror`, `CopySceneColour`) | `Liquid.cpp:197` | hdr + `dsvRO` | Преломление, отражение, волны; см. [water.md](water.md) §1 |
| 21 | Солнце | `RenderSun` | `Sun.cpp:91` | на глубине неба | До воздуха: атмосфера ослабляет его вдоль луча |
| 22 | Корабли: прозрачные | `RenderVessel(…, true)` | `Mesh.cpp:256` | hdr, `dsRead` | — |
| 23 | Капли снаружи | `RenderVisorDropsOutside` | `VisorDrops.cpp:255` | hdr+dsv | Капли на стёклах шлемов других скафандров |
| 24 | Тень урагана | `RenderStormShadow` | `Storm.cpp:381` | hdr, глубина текстурой | До воздуха |
| 25 | Воздух | `FxRenderTransparent` → `Atmosphere::Render` | `FxAtmo.cpp:484` | hdr, глубина текстурой, dual-source | Небо и воздушная перспектива; см. [atmosphere.md](atmosphere.md) §1 |
| 26 | Ураган | `RenderStorm`; `StormFrameFallback` | `Storm.cpp:412, 650` | половинное разрешение → hdr | Объёмные облака; турбулентность, если Orbiter не вызывает `clbkPreStep` |
| 27 | Облачные слои | `RenderClouds` | `Clouds.cpp:112` | половинное разрешение → hdr | См. [clouds.md](clouds.md) §2 |
| 28 | Свет в воздухе | `LightsRenderAir` | `Lights.cpp:565` | hdr | Лучи фар, линзы, маяки |
| 29 | Двигатели | `ExhaustRender` | `Exhaust.cpp:685` | hdr, глубина текстурой; `PipeState` восстанавливает конвейер | GPU-частицы, 3D-поток, свечение, марево, факелы, брызги, факелы `AddExhaust`; см. [exhaust.md](exhaust.md) §2 |
| 30 | Привязка | `OMSetRenderTargets(hdrRTV, dsv)` | `Client.cpp:1055` | hdr+dsv | — |
| 31 | AR | `RenderAr` | `Ar.cpp:150` | hdr, глубина | Метки и пути шлемного дисплея |
| 32 | Частицы Orbiter | `UpdateRenderParticles` | `Particles.cpp:144` | `bsAlpha`/`bsAdd`, `dsRead` | Обновление на CPU, удаление истёкших потоков, рисование |
| 33 | Дождь Титана | `RenderRain` | `Rain.cpp:46` | hdr+dsv | См. [rain.md](rain.md) §1 |
| 34 | Дождь урагана | `RenderStormRain` | `StormRain.cpp:82` | hdr | — |
| 35 | Под водой | `RenderUnderwater` | `Liquid.cpp:145` | hdr | Камера внутри жидкости |
| 36 | Капли изнутри | `RenderVisorDropsInside` | `VisorDrops.cpp:353` | свои цели → hdr | Капли на своём визоре, под его слоями |
| 37 | Свой корабль | повторная привязка (вьюпорт, сэмплеры, t7, b0, `LightsBind`); `ownPass = true`; `RenderVessel` непрозр. + прозр. | `Client.cpp:1063-1076` | hdr+dsv | Кабина/шлем изнутри — последним, чтобы их меши не обрывали лучи воздуха и эффектов |
| 38 | Замер света | `LightsProbe` | `Lights.cpp:696` | — | Для адаптации глаза к локальному свету |
| 39 | Блик Солнца | `RenderSunGlare` | `Sun.cpp:273` | hdr, глубина текстурой | Поверх всего, включая свою кабину |
| 40 | Изображение | `FinishScene` | `AA.cpp:115` | `outTex` | Как есть / MSAA-резолв / TAA (история, репроекция через глубину и векторы движения) |
| 41 | Тон-маппинг | `ToneMapAndAA` → `RenderBloom` → `post.hlsl` (+ резкость b2) → FXAA | `AA.cpp:181`, `Bloom.cpp:18` | `bbRTV` (sRGB) или `ldrRTV` → `bbRTVLin` | Экспозиция, свечение, резкость; FXAA только в режиме 4 |
| 42 | 2D-панели | `Render2DOverlay` (метод `oapi::GraphicsClient`, `GraphicsAPI.h:1639`) → `clbkRender2DPanel` | `Draw.cpp:1120` | `bbRTV` | По требованию SDK — в конце `clbkRenderScene` |
| 43 | GPU-время | `End(qTs[3])`, `GpuMark 11`, `End(qDis)`; чтение кадра `frameNo − 3` | `Client.cpp:1091-1113` | — | Скользящее среднее 0.9/0.1 |
| 44 | Журнал | строки статистики раз в 10 с | `Client.cpp:1115-1132` | — | §3.4 |

*После возврата* `clbkRenderScene` Orbiter рисует HUD, инфо-блоки и стеклянные MFD через `clbkGetSketchpad(NULL)`
(`Draw.cpp:849`): Sketchpad на слое экрана (DIB). Поверхности, созданные как цели рисования, получают Sketchpad на GPU
(`GpuPad.cpp`, `SkBatch`) — в любой момент кадра, с сохранением и восстановлением конвейера (`PipeState`).

### 3.3. `clbkDisplayFrame` (`Client.cpp:1135-1166`)

| № | Шаг | Функция | Назначение |
|---|---|---|---|
| 1 | Окно | `ShowWindow(SW_HIDE)` в тестовом режиме | Окно остаётся скрытым |
| 2 | HUD модулей | `CallRenderProcs` (`Ext.cpp:124`) | Процедуры gcCore `RegisterRenderProc` (`RENDERPROC_HUD_1ST/2ND`) рисуют на слой экрана |
| 3 | Слой экрана | `CompositeScreenLayer` (`Draw.cpp:1051`) | DIB → `screenTex` (только изменённый прямоугольник, ключевой цвет → прозрачность) → `blit.hlsl` с `bsAlpha` поверх `bbRTV` |
| 4 | Снимок | `SaveShot` (`Client.cpp:1168`) | `shots\start.png` через `D3D11CLIENT_TEST_SHOT` / `TestShot` / 15 с после старта; в тесте ещё `start_<pid>.png` и серия `burst_<pid>_<k>.png` (`D3D11CLIENT_TEST_BURST`) |
| 5 | Present | `sc->Present(sync, 0)` | `sync = 0` при выключенном vsync и в тестовом режиме, иначе 1 |

### 3.4. GPU-время и строки журнала

Отметки (`GpuMark k`, `Client.h:617-627`) ставятся после шагов: 1 — `FxFrame`; 2 — тени и `LightsFrame`; 3 — небо и
звёзды; 4 — планеты и `SaveLiquidBed`; 5 — непрозрачные корабли и AO; 6 — жидкость; 7 — Солнце … свет в воздухе;
8 — двигатели, AR, частицы, дожди, вид под водой, капли изнутри; 9 — свой корабль, `LightsProbe`, блик; 10 —
`FinishScene` + `ToneMapAndAA`; 11 — 2D-панели. Дополнительные отметки `GpuSub 0…6` делят интервалы 5 и 7: до AO, после
Солнца, после прозрачных кораблей и наружных капель, после тени урагана, воздуха, урагана, облаков. Крупные интервалы
`qTs`: «planets» — начало → после планет, «vessels» — → после блика Солнца, «post» — → после 2D-панелей. Результаты
читаются с отставанием 3 кадра (`frameNo ≥ 4`, `pf = (frameNo − 3) % 4`).

При `StatsLog` раз в 10 с (`Client.cpp:1116-1132`):

```
fps … | GPU ms: planets …, vessels …, post … | tiles drawn …, load queue … | vessels …
GPU ms by pass: fx compute …, shadows …, sky …, planets …, vessels opaque …, liquid …, transparent + air …,
                exhaust/AR/particles …, own vessel …, resolve + post …, 2D …
GPU ms within: AO … | the Sun …, transparent vessels …, storm shadow …, air …, storm …, clouds …, lights in the air …
<Planet::LoadStats: загрузки тайлов и время стадий>
memory now: …                         (LogMemory, §6.4)
heap by module: …                     (HeapTrackReport, только при HeapByModule = 1)
memory census: …                      (LogMemoryCensus, только при MemCensus = N > 0: на 2-й и на N-й строке)
```

Свои строки пишут и подсистемы (эффекты двигателей при `StatsLog`, жидкость раз в 10 с, 2D раз в 5 с —
`2D ms/s: …`, `diag 2D: …`). Форматы строк подсистем — в тематических главах.

## 4. Системы координат и матрицы

**Глобальная система Orbiter, сдвинутая в камеру.** Позиции считаются в `double` в глобальной системе и переводятся во
`float` только после вычитания позиции камеры (`oapiCameraGlobalPos`):
- корабль: `rel = gp − cp` (`Mesh.cpp:263`), смещение меша `rel + mul(R, ofs)` (`Mesh.cpp:317-318`);
- тайл: вершины хранятся относительно центра тайла в системе планеты; сдвиг центра от камеры — в double:
  `tc = mul(Rp, t->centre) + Pp − camGlobal` (`Planet.cpp:908`);
- жидкость: точка сферы строится в вершинном шейдере из малых величин — база под камерой плюс направления
  (`liquid.hlsl:87-102`), без вычитания чисел планетарного масштаба;
- частицы Orbiter: `p.pos − cp` на CPU (`Particles.cpp:161`).

В шейдерах «мировая» позиция `wp` — вектор от камеры, `length(wp)` — расстояние до камеры. Матрица вида `V` не содержит
переноса.

**Строчные векторы.** DirectXMath и шейдеры используют `v' = v · M`. Матрицы перед записью в константный буфер
транспонируются (`XMMatrixTranspose`), HLSL умножает `mul(float4(p,1), M)` при упаковке column-major по умолчанию —
итог эквивалентен строчному соглашению на CPU.

**`RotToXM(R)`** (`Client.h:679`): `MATRIX3` Orbiter действует на столбцы (`v_global = R · v_local`); для строчного
вектора нужна `Rᵀ` — `RotToXM` кладёт столбцы `R` в строки `XMMATRIX`.

**Вид.** `V` собирается из `cr = oapiCameraRotationMatrix` построчно (`Client.cpp:992`, та же запись в `Client.cpp:975,
1084`) — это `crᵀ` в строчном соглашении, перевод глобальных осей в оси камеры: x вправо, y вверх, z вперёд (левая
система, как у Orbiter; см. комментарий о сетке тайла `Client.cpp:420`).

**Проекция: обратный Z, бесконечная дальняя плоскость** (`Client.cpp:964-976`):
```
tanY = tan(oapiCameraAperture())          // апертура Orbiter = половина вертикального угла
P = | 1/(tanY·W/H)  0       0  0 |
    | 0             1/tanY  0  0 |
    | 0             0       0  1 |
    | 0             0       n  0 |        n = 0.015 м при oapiCameraInternal(), иначе 0.1 м
clip = (x·a, y·b, n, z)  →  depth = n / z  (1 на ближней плоскости, → 0 на бесконечности)
```
Ближняя плоскость читается проходами из `P._43` или из обратной матрицы (комментарий `Client.cpp:966-968`, пример —
`FxAtmo.cpp:510`). При TAA в третью строку записывается сдвиг `(jx, jy, 0, 1)` (`Client.cpp:976`); копия без сдвига —
`curVPNoJit` и `projNoJit` (история TAA).

Глубина очищается в 0, тест `GREATER_EQUAL` (`Client.cpp:351, 987`). Один буфер D32F покрывает всё от ближней плоскости
до планет; планеты и корабли не разделяются на отдельные проходы глубины, порядок «дальние планеты первыми»
(`Client.cpp:1009-1014`) нужен только для раннего отсечения. Звёзды и небо рисуются на бесконечности с `dsRead`.
Восстановление позиции по глубине — через `gInvVP` (b0, `common.hlsli:4`) или собственные обратные матрицы проходов
(например, `gLInvVP` в `liquid.hlsl:14`).

**TAA-репроекция** (`AA.cpp:128-155`): движение камеры считается относительно ближайшего тела; точка, неподвижная на
теле, переводится из текущей сдвинутой системы в прошлую: `p_prev = M p + t`, `M = Rb_prev · Rb_nowᵀ`. История
сбрасывается при смене опорного тела или скачке больше 1e5 м. Векторы движения есть только у непрозрачных кораблей.

**Система планеты.** `oapiGetRotationMatrix(hPlanet)` и `oapiGetGlobalPos` дают `global = Rp · local + Pp`.
Точка на широте/долготе: `(r·cos lat·cos lng, r·sin lat, r·cos lat·sin lng)` (`Planet.cpp:702`).

**Тени Солнца** (`RenderShadows`, `Client.cpp:1300-1430`) — отдельная обычная (не обратная) глубина, атлас 2×2 тайла
по `SHADOW_N = 2048`:
- тайл 0 — фокус-корабль: ортопроекция по его сфере `r = max(1.02·size, 0.5)` м, источник в `r + 30` м к Солнцу, дальняя
  граница `back0 + r + 60` м (`Client.cpp:1367-1394`); в шейдер передаётся `cascFar[0] = −1` («по объёму»), `−2` — тайла нет;
- тайлы 1–3 — каскады по глубине вида 0.3…300 м, схема разбиения с λ = 0.85; каскад вписан в ограничивающую сферу своего
  среза (радиус квантуется до 1/16 м), источник в 300 м к Солнцу от центра (`Client.cpp:1396-1427`);
- центр карты сдвигается целыми текселями в системе, связанной с ближайшей планетой при высоте менее 20 км, иначе — с
  фокус-кораблём; ось света удерживается, пока Солнце смещается менее чем на 0.5° (`Client.cpp:1320-1353`);
- матрица каждого тайла — `XMMatrixLookToLH` × `XMMatrixOrthographicLH`, из сдвинутой в камеру глобальной системы;
  в `cbFrame` — `casc[4]`, `cascFar`, `cascTex` (тексель, м), `cascRange`, `lightVP = casc[0]`, `lightVP2 = casc[1]`,
  угловой диаметр Солнца `sunDiam` для полутени;
- отбрасывают тень только корабли (`RenderVessel` с `lightVP`, только внешние меши); очистка 1.0, тест `LESS_EQUAL`,
  растеризатор: `SlopeScaledDepthBias 1.5`, `DepthBiasClamp 0.002` (`Client.cpp:405-407`) плюс смещение по нормали в
  шейдере (`ShadowAtN`, `common.hlsli:74`); сэмплер сравнения `LESS_EQUAL`, граница 1.0.

Тени локальных прожекторов и карты видимости неба — собственные атласы `Lights.cpp` (прожекторы: до 4, тайлы по 1024;
небо: 12 карт по 256, только ниже 2000 м над грунтом).

## 5. Потоки

| Поток | Где | Что делает |
|---|---|---|
| Главный (поток Orbiter) | — | Все `clbk*`, весь `ID3D11DeviceContext`, GDI, Direct2D, вызовы модулей (gcCore, AtmoFX, `D3D11Hud`) |
| Загрузчики тайлов `LoaderMain`, 2…6 шт. | `Client.cpp:588-621` | `Planet::Load`: чтение файлов/`.tree` (+ `oapiInflate`), разбор `.elv`, DDS → текстура, нормали, поле берега, вершинный буфер тайла — только через `ID3D11Device` |
| Микротекстуры `Planet::MicroLoad` | `Planet.cpp:780` | Один поток на планету, запускается при подлёте ближе 100 км к поверхности; `join` в `~Planet` (`Planet.cpp:159`) |
| Сторож зависаний | `HangWatch.cpp:170` | Раз в 250 мс сравнивает `frameNo`; кадра нет 4 с при `frameNo > 300` — однократный дамп стеков всех потоков в `hang.log` |
| Фильтр исключений | `HangWatch.cpp`, `CrashFilter` | Выполняется в упавшем потоке: `crash.log` |
| Потоки драйвера | — | Компиляция шейдеров драйвером; при `DriverThreads = 0` устройство создаётся с `PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` |

Правила и синхронизация:
- `ID3D11Device` свободнопоточный, контекст — нет. Загрузчики создают только ресурсы с начальными данными; мипы
  несжатых и DXT-текстур без мипов строятся на CPU (`Dds.cpp`), генерировать мипы на GPU загрузчик не может.
- Очередь `loadQ` (deque) под `loadMx` + `loadCv`. Загрузчик сначала убирает из очереди тайлы, не запрошенные дольше
  30 кадров (`state = 0`), затем берёт тайл с наименьшим `prio` (`Planet::LoadPrio`, `Planet.cpp:826`: расстояние до
  камеры в размерах тайла; обновляется каждым кадром в `Traverse`). Пока `g_shaderTick` моложе 0.5 с, тайлы не берутся.
- Состояние `Tile::state` (`std::atomic<int>`, `Client.h:250`): 0 новый → 1 в очереди (CAS в `QueueTile`,
  `Client.cpp:583`) → 2 готов (`Planet.cpp:751`) / 3 ошибка (`Planet.cpp:747`). Поля тайла пишутся загрузчиком до
  `state = 2`; главный поток читает их после того, как увидел 2. `Tile::prio` — `std::atomic<float>`.
- Дети ставятся в очередь только из `Traverse` готового родителя (`Planet.cpp:837, 877`), поэтому высоты родителя у
  загрузчика есть.
- `Purge` (каждые 64 кадра, тайлы без использования более 600 кадров) не освобождает поддерево с тайлом в состоянии 1
  (`Planet.cpp:1058-1074`).
- Общие данные загрузчиков: `fileCache` под `fileMx` (`Planet.cpp:326`); `TreeArchive::Read` под мьютексом архива
  (`Planet.cpp:46`); кэш масок берега под `sdfMx`; `toc` после `Open` только читается; счётчик `tilesLoaded` —
  `std::atomic<int>` (`Client.h:326`). Рабочие буферы — `static thread_local` (`Planet.cpp:43, 472, 577-578, 678, 704`;
  `Dds.cpp:181, 233, 318`).
- `Tile::lastUsed` и `frameNo` — обычные `DWORD`: пишутся главным потоком, читаются загрузчиками (под `loadMx`, без
  синхронизации с записью) и сторожем.
- Потокобезопасность `oapiInflate` из чужого потока в SDK не оговорена.
- `g_ext.mx` защищает карты AtmoFX `jets/wakes/heat` (`Ext.cpp:93, 106`; `Client.cpp:891`; `Exhaust.cpp:294`;
  `FxWaves.cpp:147`); `renderProcs` без блокировки (меняются и читаются в главном потоке). Свои мьютексы — у `Ar.cpp`
  (`mx`) и `GpuParticles.cpp` (`mx`, `crownMx`).
- Сторож и `CrashFilter` не загружают символы, не берут loader lock и не пишут `Orbiter.log` (комментарий
  `HangWatch.cpp:1-6`); кольцо `NoteShader` пополняется через `InterlockedIncrement`.
- Остановка: `clbkCloseSession` очищает очередь; `clbkDestroyRenderWindow` ставит `loaderRun = false`, будит и
  `join`-ит загрузчики, останавливает сторожа до удаления планет (`Client.cpp:558-566`).

## 6. Владение ресурсами и память

### 6.1. Поверхности, меши, потоки частиц

**`Surf` (SURFHANDLE)** — `Client.h:90-109`.
- `refs` начинается с 1. `clbkIncrSurfaceRef` — `++`; `clbkReleaseSurface`/`clbkReleaseTexture` — `--`, при 0 —
  удаление из `texCache` и `dirty`, уменьшение счётчика клиента, `delete` (`Client.cpp:672-684`).
- Текстуры из файлов (`clbkLoadTexture`, `Client.cpp:647-663`: `Textures2\`, `Textures\`, путь как есть) кешируются по
  имени; повторная загрузка отдаёт тот же `Surf` с `refs++`. Такие `Surf` помечены `immutable`.
- `MakeSurf` (`Client.cpp:624-645`) создаёт текстуру B8G8R8A8 с RTV и очищает её на GPU (без CPU-копии всей
  поверхности); такие поверхности учитываются в `g_surfLive`/`g_surfMB`.
- DIB-сторона создаётся лениво (`EnsureDIB`, `Draw.cpp:697`) и живёт до удаления `Surf`. Изменённые DIB заливаются в
  текстуру в `FlushDirty`; IMMUTABLE-текстура при первой заливке заменяется на DEFAULT (`UploadSurface`, `Draw.cpp:798-818`).
  Гамма-представления `srvLin`/`rtvLin` для IPI и GPU-Sketchpad создаются по требованию (`SurfGpuViews`).

**`DevMesh` и текстуры.** `DevMesh::Build` берёт собственную ссылку на каждую текстуру шаблона (`Mesh.cpp:14`) и
отпускает их в `~DevMesh` (`Mesh.cpp:56`): шаблон или временная копия `CopyMeshFromTemplate` (удаляется сразу после
сборки, `Mesh.cpp:90-94`) может освободить текстуры раньше меша корабля. `clbkSetMeshTexture` берёт ссылку на новую
текстуру и отпускает старую (`Client.cpp:723-731`). Слот `tex[0]` — «нет текстуры»; группа хранит индекс Orbiter + 1.

**`VesselVis`.** Создаётся в `AddVisual` (из `clbkNewVessel` или `ScanVessels`), регистрируется `RegisterVisObject`
(`Client.cpp:864-873`). `RemoveVisual` вызывает `UnregisterVisObject`, если визуал зарегистрирован. `clbkDeleteVessel`
дополнительно удаляет из `g_ext` струи, кильватеры и источники тепла корабля и вызывает `WakeForget`
(`Client.cpp:887-897`). `ScanVessels` пересоздаёт визуал, если по тому же `OBJHANDLE` теперь другой `VESSEL`.

**`PStream`.** Создаётся в `clbkCreate*Stream` (`Client.cpp:698-720`), кладётся в `streams`, отдаётся Orbiter. Удаляет
его клиент: отсоединённый поток (`level == nullptr`) — когда не осталось частиц (`PStream::Expired`, `Client.h:151`;
`Particles.cpp:149`); остальные — в `clbkDestroyRenderWindow`. Текстура `spec.tex` используется без собственной ссылки
(`Particles.cpp:196`).

**Планеты и тайлы.** `Planet` создаются в `clbkPostCreation`, удаляются в `clbkDestroyRenderWindow` после остановки
загрузчиков. Тайлы создаются по требованию (`Planet::Child`), освобождаются `Purge`/`FreeTile`.

**Слой экрана** (`screen`) — `Surf` без GPU-текстуры (только DIB) размером `W×H`, создаётся в `BeginScreenLayer`; для
композиции — отдельная `screenTex`. Освобождается в `clbkDestroyRenderWindow`. `BeginScreenLayer` пересоздаёт слой при
смене `W×H`, но `W`, `H` меняются только в `clbkCreateRenderWindow`, а `ResizeBuffers` в коде нет: изменение размера окна
во время сессии не обрабатывается (см. [roadmap.md](roadmap.md)).

### 6.2. Объекты D3D

Все объекты — `ComPtr`. Базовые шейдеры компилируются при каждом создании окна; при ошибке `shadersOk = false`, текст
ошибки в `Orbiter.log`, сцена не рисуется.

### 6.3. Смена устройства

Объекты, созданные на старом устройстве, не должны попадать в контекст нового. В коде три способа:

| Способ | Файлы |
|---|---|
| Явное освобождение в `clbkDestroyRenderWindow` (`FxRelease`, `ExhaustRelease`) | `FxAtmo.cpp` (`Atmosphere::Release`), `FxWaves.cpp`, `Exhaust.cpp` (+ `GpuParticles`), `Spray.cpp`, `VesselExhaust.cpp`, `Smoke.cpp` |
| Сравнение с `g_devGen`, пересоздание при смене | `Ar.cpp:32`, `Bloom.cpp:22`, `GpuPad.cpp:29`, `HudApi.cpp:37`, `Ssao.cpp:26`, `Wake.cpp:114` (и `CloudLayer.cpp:65` вне сборки) |
| Сравнение с указателем на устройство | `Sun.cpp:106, 209`, `Rain.cpp:71`, `StormRain.cpp:96`, `Clouds.cpp:122`, `Storm.cpp:359`, `Lights.cpp:107`, `VisorDrops.cpp:182`, `Liquid.cpp` (`lqDev`), `AA.cpp:196` (`cbPost`) |
| Члены `D3D11Client` | пересоздаются в `CreateDevice`, `CreateShaders`, `CreateLiquid`, `CreateParticleTexture`, `CreateStars` |

Правило для нового кода по `Client.h:214-217`: модуль со статическими объектами устройства проверяет `g_devGen` (или
освобождает их вместе с окном).

### 6.4. Адресное пространство 32-битного процесса

Orbiter 2024 — 32-битный процесс; по комментарию `MemCensus.cpp:2` адресное пространство (2 GB) кончается раньше
оперативной памяти, а большие выделения зависят от наибольшего свободного блока. Цифры ниже взяты только из комментариев
и форматов кода.

| Потребитель | Что в коде | Источник |
|---|---|---|
| Кэш шейдеров драйвера NVIDIA (`*.nvph`) | Файл отображается в процесс целиком и растёт удвоением (256, 512, 1024 MB); 1 GB — половина адресного пространства. `DriverCacheMB` (по умолчанию 600): файл больше — в сторону при следующем старте. `DriverCacheOff = 1`: драйверу в этом процессе запрещено открывать `*.nvph` | `Client.cpp:156-305`, `Client.h:54-58` |
| Куча CRT | По комментарию — крупнейший владелец (~400 MB на Титане) | `HeapTrack.cpp:2` |
| Загрузчики тайлов | До 6 потоков, рабочие буферы `thread_local` (~10 MB на поток; вершины тайла ~140 KB) | `Client.cpp:144-146`, `Planet.cpp:704` |
| Высоты тайлов | `ElevQ`: 16 бит на отсчёт, 134 KB на тайл; `elev`/`elevSrc` сбрасываются, когда детям они не нужны (`Planet.cpp:676`); `nodes` 65×65 (8 KB) хранятся всегда; у тайлов без своих данных — ссылка на данные предка, без копии | `Client.h:224-269` |
| Поверхности | Очистка на GPU без CPU-копии; учёт `g_surfLive`, `g_surfMB`, `g_dibLive`, `g_dibMB` | `Client.cpp:22-24, 624-645` |

Инструменты:
- **`LogMemory`** (`Client.cpp:1434-1476`) — при каждой строке статистики (`memory now`), после действий тестового
  сценария (`after the action`) и при следе по кадрам (`frame N`). Формат:
  ```
  memory <when>: private <MB> MB, address space free <MB> MB, largest free block <MB> MB | used: image <MB>,
  mapped <MB>, private <MB>, reserved <MB> MB | client surfaces <n> (<MB> MB on the GPU), CPU copies <n> (<MB> MB)
  ```
  Один раз за прогон, если отображённые виды занимают больше 256 MB, — список 15 крупнейших с файлами (`memory: … mapped
  views …`, `memory:   mapped …`). Рост от строки к строке указывает на утечку (комментарий `Client.cpp:1128`).
- **`LogMemoryCensus`** (`MemCensus.cpp:46`) — каждое выделение адресного пространства относится к владельцу: `image`
  (модули), `heap` (сегменты куч), `heap big` (блоки куч напрямую из `VirtualAlloc`, больше ~512 KB), `stack` (стеки
  потоков, резерв 1 MB по умолчанию), `other` (прямые `VirtualAlloc`: драйверы, рантаймы). Включается ключом
  `MemCensus = N` в `Client.cfg` (читается `FxTestNum`); выполняется на 2-й строке `memory now` и на N-й. Строки
  `memory census: …`.
- **`HeapTrack`** (`HeapTrack.cpp`) — `HeapByModule = 1` в `Client.cfg`, читается в `InitModule` до создания клиента.
  Подменяет импорты `malloc/calloc/realloc/free` и `LoadLibrary*` во всех модулях, ведёт таблицу живых блоков вне кучи
  CRT (`VirtualAlloc`, 32 MB адресов, по комментарию). Отчёт `heap by module: …`, `heap live: …`, `heap site: …` с каждой
  строкой `memory now`. Блоки, выделенные до подмены, не учитываются. Только для диагностики.
- **`hang.log`** (`HangWatch.cpp:37-90`, `Dump`) — кадра нет 4 с (после кадра 300): заголовок `=== hh:mm:ss no frame for 4 s:
  frame N ===`, база `D3D11Client.dll`, для каждого потока `eip модуль+смещение` и до 28 адресов возврата.
  Смещения клиента разрешаются по `build\<tag>\D3D11Client.map`.
- **`crash.log`** (`HangWatch.cpp:115-161`, `CrashFilter`) — необработанное SEH-исключение любого потока: код, `модуль+смещение`, поток,
  кадр, адрес нарушения доступа, регистры, базы `D3D11Client` и модулей `nv*`, до 40 адресов возврата, последние 24
  скомпилированных шейдера (`shaders made last …`). Затем исключение передаётся прежнему фильтру; в тестовом прогоне
  процесс завершается без диалога.
- **`FATAL terminate`** в `Orbiter.log` — необработанное C++-исключение главного потока (`LogTerminate`).
- **След выделений** (`g_traceAlloc`, включается действием тестового сценария): строки `trace: surface …`,
  `trace: texture …` для поверхностей от 1 MB и текстур от 512K текселей.
- **Кэш шейдеров драйвера**: строки `driver shader cache: …` при каждом создании устройства (размер отображённых
  файлов, перенос, число отказов при `DriverCacheOff`).
