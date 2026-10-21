# Шейдеры

[← оглавление](DEVELOPER.md)

Состояние кода: 21.10.2026

Реестр всех файлов `shaders\`: 41 `.hlsl` и 5 `.hlsli`. Для каждого указаны точки входа, константные буферы,
ресурсы с регистрами, место компиляции и рисования в C++. Ссылки вида `File:line` даны по текущим номерам строк.
Если шейдер относится к тематической главе ([exhaust.md](exhaust.md), [atmosphere.md](atmosphere.md),
[water.md](water.md), [rain.md](rain.md), [clouds.md](clouds.md)), здесь приведены только регистры и назначение,
физика описана в главе.

## Содержание

1. [Соглашения](#1-соглашения)
2. [Общая карта регистров](#2-общая-карта-регистров)
3. [Включаемые файлы (`.hlsli`)](#3-включаемые-файлы-hlsli)
4. [Основные проходы сцены](#4-основные-проходы-сцены)
5. [Постобработка и 2D](#5-постобработка-и-2d)
6. [Атмосфера, Солнце, локальные источники](#6-атмосфера-солнце-локальные-источники)
7. [Жидкости и дождь](#7-жидкости-и-дождь)
8. [Облака и ураган](#8-облака-и-ураган)
9. [Струи двигателей, дым, частицы](#9-струи-двигателей-дым-частицы)
10. [Неиспользуемые файлы](#10-неиспользуемые-файлы)
11. [Замечания по коду](#11-замечания-по-коду)

---

## 1. Соглашения

### 1.1. Установка и компиляция

Исходники лежат в `shaders\`. `install.bat:7-8` копирует все `*.hlsl` и `*.hlsli` в `Modules\D3D11Client\`, оттуда они
компилируются во время работы. Каталог задаётся в `Client.cpp:80` (`shaderDir`). Все шейдеры — модель 5.0
(`vs_5_0`, `ps_5_0`, `cs_5_0`, `hs_5_0`, `ds_5_0`), флаг `D3DCOMPILE_OPTIMIZATION_LEVEL3`, без макросов,
`#include "..."` разрешается относительно файла (`D3D_COMPILE_STANDARD_FILE_INCLUDE`). Исключение — `gcIPInterface`
(`Ipi.cpp:52`): пиксельные шейдеры модулей через gcCore компилируются как `ps_4_0` с макросами модуля, их вершинный
шейдер — из строки (`Ipi.cpp:82`, `vs_4_0`); к файлам `shaders\` это не относится.

Пути компиляции:

| Путь | Где | Кэш | Пауза между шейдерами, `NoteShader` | Кто пользуется |
|---|---|---|---|---|
| `CompileFile` | `Client.cpp:463-485` | да | да | `D3D11Client::CreateShaders`, `D3D11Client::CompileShaderFile` (`Client.cpp:461`) |
| `FxCompile` | `Fx.cpp:6-15` | да | нет | `FxAtmo.cpp`, `FxWaves.cpp`, `Exhaust.cpp`, `GpuParticles.cpp`, `Smoke.cpp`, `Spray.cpp`, `VesselExhaust.cpp` |
| `CachedCompileFromFile` напрямую | `Ar.cpp:38-40`, `GpuPad.cpp:36-41` | да | нет | `ar.hlsl`, `sketch.hlsl` |
| `D3DCompileFromFile` напрямую | `HudApi.cpp:47`, `Wake.cpp:121` | нет | нет | `hud.hlsl`, `wavefield.hlsl` |

`CompileFile` выдерживает не менее `kShaderGapMs` = 12 мс между двумя шейдерами (`Client.cpp:468-475`) и отмечает время
последнего в `g_shaderTick`; загрузчик тайлов не берёт задания, пока с этой отметки не прошло 500 мс
(`Client.cpp:592-600`). `NoteShader` (`HangWatch.cpp`) запоминает последние компилируемые шейдеры для `crash.log`.

Когда компилируется:

- **При создании окна рендера** — `D3D11Client::CreateShaders` (`Client.cpp:488-532`, вызов `Client.cpp:138`):
  `mesh`, `planet`, `post`, `blit`, `sky`, `aa` (4 входа), `stars`, `liquid` (`VS`, `PS`), `particles`, `panel`; там же
  создаются раскладки `ilLiquid`, `ilPart`, `ilMesh`, `ilPlanet`. Если не собрался хотя бы один, `shadersOk = false`,
  в журнале строка `shader compilation FAILED - see above; the scene is not drawn` (`Client.cpp:139`), сцена не рисуется.
- **При инициализации эффектов** — `Client.cpp:142`, если основные шейдеры собрались: `FxInit` → `FxSystem::Init`
  (`Fx.cpp`) — `fx_atmo`, `fx_atmo_lut`, `fx_sky_sh`, `fx_waves`; `ExhaustInit` (`Exhaust.cpp:750`) → `ExhaustFx::Init`
  (`Exhaust.cpp:189`) — `exhaust_*`; при отказе строка `exhaust effects FAILED`.
- **Лениво, при первом использовании** — все остальные (`Bloom.cpp:29`, `Ssao.cpp:33`, `Sun.cpp:109`, `Lights.cpp:133`,
  `Clouds.cpp:127`, `Storm.cpp:174` в рабочем потоке `std::async`, `Rain.cpp:74`, `StormRain.cpp:49`,
  `VisorDrops.cpp:186`, `Liquid.cpp:156`, `Liquid.cpp:241`, `GpuParticles.cpp:74`, `Smoke.cpp:74`, `Spray.cpp:40`,
  `HudApi.cpp:43`, `Ar.cpp:36`, `GpuPad.cpp:33`, `Wake.cpp:121`). Отказ ленивого шейдера выключает только свой проход.

Журнал: ошибки и предупреждения компилятора пишутся строкой `shader <путь> <вход>: <текст>` (`Client.cpp:483`,
`Fx.cpp:12`); итог инициализации эффектов — строка `effects: atmosphere <ready|FAILED|off>, waves ...`
(`Fx.cpp:40`); у `ar.hlsl`, `sketch.hlsl`, `wavefield.hlsl`, `hud.hlsl` — собственные строки (`ar.hlsl VS: ...`,
`sketch.hlsl PS: ...`, `wavefield.hlsl <вход>: ...`, `hud.hlsl <вход>: ...`; у этих четырёх — только при неудаче). При попадании в кэш компилятор не вызывается, поэтому
предупреждения появляются в журнале только при первой компиляции данного варианта исходника.

### 1.2. Кэш скомпилированных шейдеров (`ShaderCache.cpp`)

`CachedCompileFromFile` (`ShaderCache.cpp:37-57`):

- Ключ — 64-битный FNV-1a от байтов файла и, рекурсивно, всех файлов из `#include "..."` (относительно папки
  включающего файла, глубина до 16, каждый файл один раз; `HashSource`, `ShaderCache.cpp:15-34`), затем от имени
  входа, цели, флагов и макросов (`ShaderCache.cpp:43-44`). Поиск `#include` текстовый: строки в комментариях
  тоже учитываются.
- Файл — `Modules\D3D11Client\shadercache\<ключ, 16 hex>.cso` (`ShaderCache.cpp:46`). При попадании байт-код читается
  `D3DReadFileToBlob`; при промахе — `D3DCompileFromFile`, успешный результат записывается `D3DWriteBlobToFile`
  (каталог создаётся при необходимости, `ShaderCache.cpp:52-55`).
- Инвалидация — только сменой ключа: любая правка исходника или включаемого файла даёт новый файл кэша. Старые
  `.cso` не удаляются: кода, который чистит `shadercache\`, нет. Если включаемый файл не найден, ключ не строится
  (`keyed = false`): шейдер компилируется каждый запуск и в кэш не пишется.
- Отдельных строк журнала о попадании или промахе нет.

Следствие: правка установленного `.hlsl`/`.hlsli` вступает в силу при следующем создании шейдера (для основных — при
следующем запуске симуляции) без пересборки DLL.

### 1.3. Точки входа

По умолчанию — `VS` и `PS` (так их ищут лямбды `CreateShaders`, `Client.cpp:490-497`). Файлы с несколькими проходами
называют входы по назначению: `PS<Проход>` (`PSMarch`, `PSComposite`, `PSShadow`, `PSApply`), `CS<Шаг>` (`CSSim`,
`CSInit`), `VS<Вариант>` (`VSSprite`, `VSFull`). Исключения: `aa.hlsl` (`ResolveDepthPS`, `TAAPS`, `FXAAPS`),
`hud.hlsl` (`FullVS`, `LightPS`, `DownPS`), `fx_sky_sh.hlsl` (`CS`), `liquid.hlsl` (`VSCP`, `HS`, `DS`, `PSMS`).

### 1.4. Матрицы, системы координат, глубина, цвет

- **Матрицы.** На CPU матрицы DirectXMath транспонируются перед записью в буфер (например `UpdateFrameCB`,
  `Client.cpp:1231`); в HLSL — строчные векторы: `mul(float4(p, 1), M)`. См.
  [architecture.md §4](architecture.md#4-системы-координат-и-матрицы).
- **Мировые позиции** (`wp`, `WPOS`) — глобальная система Orbiter, сдвинутая в камеру (камера в нуле, оси глобальные).
  Матрицы `gW` объектов переводят в неё; `gInvVP` переводит из клипа в неё.
- **Обратная глубина.** Проекция с бесконечной дальней плоскостью, `depth = n / z` (`Client.cpp:973`); буфер глубины
  очищается в 0 (`Client.cpp:987`), тест `GREATER_EQUAL` (`Client.cpp:351`). Глубина 0 — «ничего не нарисовано» (небо);
  направления на бесконечность восстанавливаются через `gInvVP` при `z` ≈ 1e−7. Карта теней — обычная глубина
  (`LESS_EQUAL`, `Client.cpp:402-404`).
- **Цвет.** Сцена — линейный HDR (`R16G16B16A16_FLOAT`). Единица — облучённость Солнца вне атмосферы = 1 (белая
  поверхность под отвесным Солнцем даёт 1). Экспозиция — `gHazeRGB.w`, баланс адаптации — `gAdapt`; тон-маппинг и
  sRGB-кодирование — в `post.hlsl` и RTV back buffer'а. Элементы, которые должны выглядеть как есть (HUD, дисплеи,
  маяки), делят на экспозицию и `gAdapt` сами.
- **Полноэкранные проходы** — треугольник из `SV_VertexID` (3 вершины, без буферов и раскладки).

---

## 2. Общая карта регистров

### 2.1. Глобальные привязки

Ставятся один раз за кадр (или после проходов, которые их перезаписывают) и читаются через включаемые файлы.

| Слот | Ресурс | Объявлен | Стадии | Где ставится |
|---|---|---|---|---|
| `b0` | `Frame` (`FrameCB`, `Client.cpp:1227`) | `common.hlsli:2` | VS, PS | `UpdateFrameCB`, `Client.cpp:1295-1296`; повторно перед собственным кораблём `Client.cpp:1070`; после bloom `Bloom.cpp:89` |
| `b4` | `FxAtmoMedia` (`MediaCB`, `FxAtmo.cpp:10`) | `fx_atmo_light.hlsli:8` | VS, PS | `Atmosphere::BindLight`, `FxAtmo.cpp:349-350`; вызывается из `Atmosphere::Frame` (`FxAtmo.cpp:370`), в конце `Atmosphere::Render` (`FxAtmo.cpp:580`) и из `FxBindLight` (`Fx.cpp:72`) в проходах эффектов. Только при работающей FX-атмосфере; иначе слот пуст и `FxActive()` = false |
| `b9` | `LocalLights` (`LightsCB`, `Lights.cpp:15`) | `lights.hlsli:28` | PS | `LightsBind`, `Lights.cpp:560`; вызовы `Client.cpp:994`, `Client.cpp:1071`, `Lights.cpp:571`, `VisorDrops.cpp:320, 675`. В VS — только `Lights.cpp:642` |
| `t7` | атлас теней Солнца (2×2 плитки по `SHADOW_N` = 2048, `R32_FLOAT`) | `common.hlsli:28` | PS | `Client.cpp:991`, `Client.cpp:1069`; в VS — `Rain.cpp:125` и копия из PS в `GpuParticles.cpp:665-666` |
| `t8` | `tFxLut`: пропускание 256×64 + освещённость от неба 64×32 | `fx_atmo_light.hlsli:22` | PS | `BindLight`, `FxAtmo.cpp:351`; в VS только копия `GpuParticles.cpp:666` |
| `t9` | `tFxMS`: многократное рассеяние 64×64 | `fx_atmo_sky.hlsli:8` | PS | там же |
| `t13` | `tFxSH`: 10 × `float4`, L2-гармоники неба | `fx_atmo_light.hlsli:34` | VS, PS | `BindLight`, `FxAtmo.cpp:345-346` |
| `t17`–`t21` | источники, кластеры, индексы, атлас теней прожекторов, атлас видимости неба | `lights.hlsli:40-44` | PS | `LightsBind`, `Lights.cpp:561`. `t20` снимается в `Lights.cpp:338`, `t21` — в `Lights.cpp:400` (до их перерисовки), возвращаются в `Client.cpp:994` |
| `s0` | `sampAniso`: анизотропный, WRAP; при TAAU — отрицательный `MipLODBias` | — | PS | `Client.cpp:989-990`, `Client.cpp:1067-1068` |
| `s1` | `sampClamp`: анизотропный, CLAMP | — | PS | там же |
| `s2` | `sampShadow`: сравнивающий, `LESS_EQUAL`, BORDER = 1 | `common.hlsli:29` | PS | там же; в VS — `Rain.cpp:126`, `GpuParticles.cpp:666` |

### 2.2. Регистры по проходам

Остальные слоты свои у каждого прохода; проход ставит их сам перед рисованием.

| Слот | Использование |
|---|---|
| `b0` (не `Frame`) | `bloom.hlsl` `Bloom`, `hud.hlsl` `Hud`, `sketch.hlsl` `Sk`, `wavefield.hlsl` `Field` (CS), `ssao.hlsl` (не используется). Frame возвращают `Bloom.cpp:89` и `PipeState` (`GpuPad.cpp:103-126`) |
| `b1` | константы объекта/прохода. Общий динамический буфер `cbObj` (1024 байта, `Client.cpp:390`, `Map(DISCARD)` перед каждым draw) — `mesh`, `planet`, `stars`, `panel`, `particles`, `aa`; собственные буферы — `clouds`, `storm`, `rain`, `storm_rain`, `underwater`, `liquid`, `visor_drops`, `ar`, `lights_vol` (`SpriteCB`), `cloudlayer`, `earthclouds`, `cyclone` |
| `b2` | `post.hlsl` `Post`, `fx_waves.hlsl` `WaveCB` (CS), `visor_drops.hlsl` `VisorOut` |
| `b3` | `fx_atmo.hlsl` `AtmoPass`, `sun.hlsl` `SunPass` |
| `b5` | эффекты: `Glow`, `Plume`, `Shimmer`, `Ex`, `Sim`/`Draw` (частицы), `Smoke`, `Spray`; `liquid.hlsl` `Wakes` |
| `b6` | `mesh.hlsl` `Mirror`; `particles_gpu.hlsl` `Count` (CS) |
| `b7`, `b8` | `mesh.hlsl` `Dirt`, `Emis2` |
| `b10` | `lights_vol.hlsl` `Beam` |
| `b11` | `gtao.hlsl` `Gtao` |
| `t0`–`t6` | основная текстура, глубина сцены, копии сцены, промежуточные цели; у эффектов `t5` — глубина сцены, `t6` — таблица чёрного тела (`exhaust_common.hlsli:7-8`) |
| `t11`, `t12` | `liquid.hlsl`: глубина зеркала, глубина дна |
| `t14`, `t15` | `visor_drops.hlsl` (`PSDown`/`PSDownMS`); `t15` также `planet.hlsl` `tMask` |
| `t16` | `planet.hlsl` `tSdf` (VS и PS) |
| `t22`–`t25` | `smoke.hlsl`: плотность, свет, скорость, занятость блоков |
| `s3` | эффекты: линейный CLAMP (`Exhaust.cpp:718`, `Smoke.cpp:207, 241, 255`); `visor_drops.hlsl` `sLin`; `planet.hlsl` — трилинейный WRAP (`Planet.cpp:801-808`) |
| `u0`–`u5` | вычислительные шейдеры, свои у каждого |

`cbGrp` (256 байт, `Client.cpp:390`) создаётся, но ни к одному слоту не привязывается.

### 2.3. Сводка по файлам

«+» — глобальные слоты, которые шейдер читает через включаемые файлы.

| Файл | b | t | s | u |
|---|---|---|---|---|
| `mesh` | 1, 6, 7, 8 (+0, 4, 9) | 0–5 (+7, 8, 13, 17–21) | 0 (+2) | — |
| `planet` | 1 (+0, 4, 9) | 0–6, 15, 16 (+7, 8, 13, 17–21) | 0, 1, 3 (+2) | — |
| `sky` | +0 | — | — | — |
| `stars` | 1 | 0 (VS) | — | — |
| `particles` | 1 (+0, 4) | 0 (+8, 13) | 1 | — |
| `panel` | 1 | 0 | 1 | — |
| `blit` | — | 0 | 1 | — |
| `post` | 0, 2 | 0, 1 | 1 | — |
| `aa` | 1 | 0–3 | 1 | — |
| `bloom` | 0 (свой) | 0 | 1 | — |
| `gtao` | 11 (+0, 4) | 0, 1 (+7, 8, 13) | (+2) | 0 |
| `hud` | 0 (свой) | 0 | 0 (свой) | — |
| `sketch` | 0 (свой) | 0, 1 | 0, 1 (свои) | — |
| `ar` | 1 (+0) | — | — | — |
| `fx_atmo` | 3, 4 | 0–5, 8, 9 | 1 | 0–3 (CS) |
| `fx_atmo_lut` | 4 | 1, 8 | — | 0 |
| `fx_sky_sh` | 4 | 8, 9 | — | 0 |
| `sun` | 3 (+4 в `PSG`) | 0–5 (+8 в `PSG`) | 0, 1 | — |
| `lights_vol` | 0, 1, 4, 9, 10 | 0, 1, 2, 17, 20 | 1, 2 | 0 (CS) |
| `liquid` | 1, 5 (+0, 4) | 0–6, 11, 12 (+7, 8, 9, 13) | 1 (+2) | — |
| `underwater` | 1 | 0 | — | — |
| `wavefield` | 0 (`Field`, CS) | 0 | — | 0, 1 |
| `fx_waves` | 2 (CS) | 0 | — | 0–3 |
| `spray` | 5 (+0, 4) | 3, 4, 5, 6 (+7, 8, 13) | 3 (+2) | — |
| `rain` | 1 (+0, 4; всё в VS) | +7, 8, 13 (VS) | +2 (VS) | — |
| `storm_rain` | 1 (+0, 4) | 0–4 (+8) | — | 0–3 |
| `visor_drops` | 1, 2 (+0, 4, 9) | 0–6, 14, 15 (+7, 8, 13, 17–20) | 3 (+2) | 0–4 |
| `clouds` | 1 (+0, 4) | 0–3 (+8, 9, 13) | 0, 1 | — |
| `storm` | 1 (+0, 4) | 0–7 (+8, 9, 13) | 0, 1 | — |
| `storm_noise` | — | — | — | 0 |
| `exhaust_glow`, `exhaust_plume` | 5 (+0, 4) | 5, 6 | 3 | — |
| `exhaust_shimmer` | 5 (+0) | 3, 4, 5 | 3 | — |
| `exhaust_vessel` | 5 (+0) | 5, 6 | 3 | — |
| `particles_gpu` | 5, 6 (+0, 4) | 3, 4, 5 (+7, 8, 13) | (+2) | 0–2 |
| `smoke` | 5 (+0, 4) | 0–5, 22–25 (+8, 13) | 3 | 0–5 |
| `cloudlayer`, `earthclouds`, `cyclone` | см. [§8](#8-облака-и-ураган) (вне сборки) | | | |

---

## 3. Включаемые файлы (`.hlsli`)

Цепочка включений: `common.hlsli` → `fx_atmo_light.hlsli`, `lights.hlsli`; `fx_atmo_sky.hlsli` → `fx_atmo_light.hlsli`;
`exhaust_common.hlsli` → `common.hlsli`. У `fx_atmo_light.hlsli`, `fx_atmo_sky.hlsli`, `lights.hlsli`,
`exhaust_common.hlsli` есть защита от повторного включения; у `common.hlsli` её нет (повторное включение даст
переопределение `Frame`).

### 3.1. `common.hlsli`

`cbuffer Frame : register(b0)` (`common.hlsli:2-25`), C++-зеркало — `struct FrameCB` (`Client.cpp:1227`), заполняет
`UpdateFrameCB` (`Client.cpp:1229-1297`) раз в кадр.

| Поле | Строка | Смысл |
|---|---|---|
| `float4x4 gInvVP` | 4 | клип (с джиттером TAA) → сдвинутая в камеру глобальная система |
| `float4 gSunDir` | 5 | направление на Солнце (глобальное); `w` — «день» у камеры 0…1 |
| `float4 gSky0` | 6 | цвет горизонта старого неба; `w` = 1 — камера в атмосфере |
| `float4 gSky1` | 7 | цвет зенита; `w` — сила неба (угасает с высотой) |
| `float4 gAmb` | 8 | свет неба на поверхностях; `w` — экстинкция дымки у земли [1/м]; при активной FX-атмосфере 0 (`Client.cpp:1268`) |
| `float4 gSun` | 9 | цвет прямого Солнца; `w` — высота однородной дымки [м] |
| `float4 gUp` | 10 | местная вертикаль у камеры; `w` — высота камеры [м] |
| `float4 gPlanet` | 11 | центр планеты от камеры; `w` — радиус [м] |
| `float4 gHazeRGB` | 12 | спектральные веса экстинкции; `w` — экспозиция (`Exposure` × экспозиция адаптации, `Client.cpp:1269`) |
| `float4x4 gLightVP` | 13 | глобальная → карта теней (прежняя одиночная карта) |
| `float4 gShadow` | 14 | `x` — тени включены; `y` — 1/`SHADOW_N`; `z` — режим `D3D11CLIENT_SHADOWDBG`; `w` — угловой диаметр Солнца (`Client.cpp:1276`) |
| `float4 gAdapt` | 15 | адаптация глаза: `rgb` — баланс цвета (применяется при тон-маппинге), `w` — множитель яркости, уже входящий в `gHazeRGB.w` |
| `float4 gLiq0` | 17 | жидкость под камерой: центр её планеты от камеры; `w` — радиус уровня (0 — нет) |
| `float4 gLiq1` | 18 | рассеянный цвет жидкости; `w` — экстинкция [1/м] |
| `float4 gLiq2` | 19 | веса каналов; `w` = 1 — включено |
| `float4x4 gLightVP2` | 20 | не используется (комментарий в коде: «unused: the cascades below») |
| `float4x4 gCasc[4]` | 21 | глобальная → каждая плитка атласа теней: 0 — карта по фокусному кораблю, 1–3 — каскады по глубине вида |
| `float4 gCascFar` | 22 | дальний конец каждого каскада [м]; `x` = −1 — плитка 0 задана коробкой фокуса, −2 — плитки 0 нет (`Client.cpp:1382, 1394`) |
| `float4 gCascTex` | 23 | размер текселя каждого каскада [м] (сдвиг по нормали) |
| `float4 gCascRange` | 24 | диапазон глубины каждого каскада [м] (расстояние до заслонителя для полутени) |

Ресурсы: `Texture2D<float> tShadow : t7`, `SamplerComparisonState sShadow : s2` (`common.hlsli:28-29`).

| Функция | Строка | Что делает |
|---|---|---|
| `NotFinite(v)` | 27 | NaN или бесконечность по битам (компилятор выбрасывает `isnan` при правилах по умолчанию) |
| `CascadePCF(a, z, k)` | 42 | 16 аппаратно сравниваемых билинейных выборок 4×4 с шагом `k` текселей |
| `CascadeVis(ci, p, ang, sr, kmax)` | 52 | PCSS в плитке `ci`: поиск заслонителей 4×4, полутень = расстояние × угловой размер источника, затем `CascadePCF`; −1 — точка вне плитки |
| `CascadeTap(ci, p)` | 73 | `CascadeVis` для Солнца (`gShadow.w`, поиск ±3 текселя, шаг ≤ 4) |
| `ShadowAtN(wp, n)` | 74 | видимость Солнца: плитка 0, если точка в ней, иначе каскад по расстоянию; сдвиг по нормали на 1.5 текселя; смешение в следующий каскад на последних 10 % |
| `ShadowAt(wp)` | 94 | `ShadowAtN` без сдвига по нормали |
| `SkyColour(dir)` | 96 | старое небо: горизонт→зенит, подсветка к Солнцу, ореол (`pow` 24 и 400), × день × сила неба |
| `ApplyHaze(col, wp)` | 107 | воздушная перспектива старого неба (Бугер–Ламберт по средней высоте луча); без изменений при `FxActive()` или вне атмосферы |
| `SunLight(wp)`, `SunLight()` | 121, 122 | прямое Солнце: `FxSunLight` при FX-атмосфере, иначе `gSun.rgb` в атмосфере и белый в космосе |
| `SkyLight(n, wp)`, `SkyLight(n)` | 123, 130 | свет неба на нормаль: `FxSkyLight` при FX-атмосфере, иначе `gAmb` × 0.35…1 по наклону; в космосе 0.02 |
| `Aureole()` | 138 | ореол аэрозоля: `x` — угловой диаметр конуса половинной интенсивности ХГ, `yzw` — доля света неба в нём по каналам; 0 без FX-атмосферы |
| `AureoleVis(wp, n, ang)` | 149 | видимость ореола по каскадам 1–2 (дальше ~50 м — 1) |
| `SkyLightShaded(n, wp, ng)` | 159 | `SkyLight` с затенением доли ореола; выключается при `gShadow.z > 9.5` |

Физика ореола — [atmosphere.md §2.12](atmosphere.md#212-ореол-commonhlsli132-166).

### 3.2. `fx_atmo_light.hlsli`

Среда и свет FX-атмосферы планеты, у которой камера. `cbuffer FxAtmoMedia : register(b4)` (`fx_atmo_light.hlsli:8-21`),
C++ — `MediaCB` (`FxAtmo.cpp:10-13`); поля описаны в [atmosphere.md §5.2](atmosphere.md#52-cbuffer-fxatmomedia--registerb4-fx_atmo_lighthlsli8-21-c--mediacb-fxatmocpp10-13-заполняет-planetatmosphereupdate-fxatmocpp127-148).
Ресурсы: `tFxLut : t8` (`:22`), `StructuredBuffer<float4> tFxSH : t13` (`:34`). Собственных сэмплеров нет: выборка
таблиц — `Load` с ручной билинейной интерполяцией. Константы размеров `FX_TW` 256, `FX_TH` 64, `FX_IW` 64, `FX_IH` 32 (`:24`).

| Функция | Строка | Что делает |
|---|---|---|
| `FxAltX(r)`, `FxAltR(x)` | 28, 29 | координата высоты таблиц (корень доли высоты) и обратно |
| `FxActive()` | 31 | `mGeo.z > 0.5`: FX-атмосфера освещает сцену |
| `FxSHIrradiance(d)` | 35 | освещённость по L2-гармоникам `tFxSH[0..8]` |
| `FxBilinear(t, f, org, size)` | 45 | билинейная выборка из прямоугольника текстуры через `Load` |
| `FxMieDensity(h)`, `FxRayDensity(h)`, `FxAbsDensity(h)` | 56, 64, 69 | профили плотности аэрозоля (экспонента или слой), Рэлея (с изломом шкалы высот), поглотителя (шатёр) |
| `FxExtinction(h)` | 70 | суммарная экстинкция на высоте |
| `FxTrans(r, mu)` | 76 | пропускание до верха атмосферы из таблицы (параметризация Брюнетона) |
| `FxTransToSun(r, muS)` | 87 | то же к Солнцу, 0 под горизонтом планеты с мягким краем по диску |
| `FxIrradiance(r, muS)` | 95 | освещённость горизонтальной площадки светом неба (строки 64…95 `tFxLut`) |
| `FxSunLight(wp)` | 101 | прямое Солнце в точке (1 над атмосферой) |
| `FxSkyLight(n, wp)` | 108 | свет неба на нормаль: изотропная часть + отражение от земли; у высоты камеры — `FxSHIrradiance`; + 0.02 к верху атмосферы |

### 3.3. `fx_atmo_sky.hlsli`

Рассеянный свет вдоль луча. Включает `fx_atmo_light.hlsli`; добавляет `tFxMS : t9` (`:8`).

| Функция | Строка | Что делает |
|---|---|---|
| `FxPhaseRayleigh(c)` | 13 | фаза Рэлея |
| `FxPhaseMie(c)` | 14 | Корнетт–Шэнкс по каналам `mMieG` |
| `FxMSLookup(r, muS)` | 19 | многократное рассеяние из `tFxMS` |
| `FxMarch(org, d, tc0, K, t0, t1, NS, L, T, fwd)` | 28 | однократное + многократное рассеяние вдоль отрезка луча, выборки сгущены к ближайшей к планете точке |
| `FxSkyRadianceEx(wp, dir, NS, aboveOnly)` | 64 | яркость неба по направлению (× π), с землёй ниже горизонта |
| `FxSkyRadiance(wp, dir, NS)` | 92 | то же, лучи ниже горизонта поднимаются к горизонту |

Подробно — [atmosphere.md §5.6](atmosphere.md#56-fx_atmo_skyhlsli).

### 3.4. `lights.hlsli`

Локальные источники кораблей (`LightEmitter` Orbiter), заполняются в `Lights.cpp`. Фотометрия: сила света из кривой
Orbiter на четверти дальности × `LightLux`, спад 1/d² с окном Кариса `(1 − (d/R)^4)^2`, единица — облучённость Солнца
(`lights.hlsli:4-15`). Кластеры: 16×9 плиток × 24 среза по экспоненте глубины.

`struct LocalLightG` (`:19-27`): `p` — позиция, `w` — дальность [м]; `c` — цвет × освещённость на 1 м, `w` — биты
(1 прожектор, 2 не светит на собственный кокпит, 4 только кокпит); `d` — направление прожектора, `w` — cos полутени;
`a` — `x` cos тени, `y` радиус лампы, `z` плитка теневого атласа (−1 нет), `w` = 1 — светит в воздухе.

`cbuffer LocalLights : register(b9)` (`:28-39`), C++ — `LightsCB` (`Lights.cpp:15`):

| Поле | Смысл |
|---|---|
| `float4x4 gLVP` | глобальная → клип без джиттера (сетка кластеров) |
| `float4 gLGrid` | плиток по x, y; срезов; число источников |
| `float4 gLZ` | конец среза 0 [м]; срезов на ln(z/x); 1 — включено; тексель атласа теней прожекторов |
| `float4x4 gSpotVP[4]` | глобальная → клип каждой плитки теней прожекторов |
| `float4 gSpotK[4]` | тексель на 1 м, near, far; `w` — в комментарии «0», фактически `[0].w` = номер кадра & 63 (`Lights.cpp:168`) |
| `float4x4 gSkyVP[12]` | глобальная → карта видимости неба по 12 направлениям (атлас 4×4 по 256) |
| `float4 gSkyDir[12]` | направления на небо |
| `float4 gSkyC` | центр карт видимости; `w` — радиус [м] (0 — выключено) |
| `float4 gSkyK` | тексель [м]; тексель атласа (uv) |

Ресурсы: `tLights : t17`, `tLCluster : t18` (`uint2`: смещение, число), `tLIndex : t19`, `tSpotShadow : t20`,
`tSkyVis : t21` (`:40-44`).

| Функция | Строка | Что делает |
|---|---|---|
| `LightIsSpot(L)` | 46 | бит прожектора |
| `gLLOwn` | 48 | статическая переменная: 1 — рисуется собственный кокпит (ставит `mesh.hlsl`) |
| `LocalLightsOn()` | 49 | источники включены и есть |
| `SpotShadow(t, p)` | 52 | тень прожектора: 3×3 сравниваемых выборок в плитке `t` (сэмплер `s2`) |
| `LocalLightE(L, wp, ng, Ldir)` | 68 | освещённость от источника с окном, конусом, тенью и гашением у самой лампы |
| `LocalCluster(wp)` | 98 | кластер точки; вне сетки — признак «перебрать все» |
| `SkyVisibility(wp, ng)` | 112 | доля неба, видимая мимо кораблей вокруг камеры (12 карт глубины) |
| `D_GGX_L(nh, a)` | 133 | распределение GGX |
| `LocalLightShade(...)` | 137 | диффузный + GGX-зеркальный вклад одного источника |
| `LocalLights(...)` | 151 | сумма по кластеру (вне сетки — по первым 64) |
| `LocalLightsDiffuse(...)` | 165 | только диффузная часть (грунт) |

### 3.5. `exhaust_common.hlsli`

Общее для струй, дыма и частиц. Включает `common.hlsli`. Ресурсы: `tExDepth : t5` (одновыборочная глубина сцены),
`tExBB : t6` (строка 256 столбцов, ln(B(T)/B(Солнце)) для T = 200…20000 K), `sExLin : s3` (`:7-9`).

| Функция | Строка | Что делает |
|---|---|---|
| `ExSceneDist(pix, wp)` | 12 | точка сцены за пикселем и расстояние до неё (1e30 — небо) |
| `ExViewDir(pix)` | 23 | луч зрения через пиксель (не вызывается) |
| `ExBlackbodyRel(T)` | 33 | излучение чёрного тела относительно Солнца по каналам |
| `ExHash`, `ExRand`, `ExHash3`, `ExNoise`, `ExFbm` | 42–52 | хэши и value-noise без текстур |
| `ExAirTrans(dist)` | 56 | пропускание воздуха на высоте камеры (1 без FX-атмосферы) |
| `ExCube(vid, n)` | 65 | вершина единичного куба из `SV_VertexID` (36 вершин) |

---

## 4. Основные проходы сцены

### 4.1. `mesh.hlsl` — меши кораблей

- Компиляция: `CreateShaders`, `Client.cpp:499`; раскладка `ilMesh` (`POSITION`, `NORMAL`, `TEXCOORD` — `NTVERTEX`),
  `Client.cpp:525-529`.
- Рисование: `D3D11Client::RenderVessel` (`Mesh.cpp:256`), шейдеры `Mesh.cpp:282-283`, `DrawIndexed` `Mesh.cpp:408/410`.
  Вызовы: непрозрачные корабли `Client.cpp:1030`, прозрачные `Client.cpp:1041`, собственный корабль `Client.cpp:1073-1074`,
  зеркальный проход жидкости `Liquid.cpp:135-136`, каскады теней `Client.cpp:1392/1425` (только VS, PS = null).
- Входы: `VS` (`mesh.hlsl:87`), `PS` (`mesh.hlsl:144`).

| Регистр | Ресурс | Что | Ставит |
|---|---|---|---|
| `b1` | `Obj` (`MeshCB`, `Mesh.cpp:199`) | `gWVP`, `gW`, `gSunM`; материал `gDiff`, `gMAmb`, `gSpec` (`w` — степень), `gEmis`; `gFlags` (x — текстура, y/w — кокпит, z — не освещается); `gCurNJ`, `gPrevNJ` (WVP без джиттера сейчас/прошлый кадр); `gMaps` (наличие карт `_norm`, `_spec`, `_rghn`, `_metl`); `gMaps2` (x `_emis`, y кожа/плотность фильтра, z фильтр, w угловой радиус Солнца) | VS+PS, `Mesh.cpp:284-285` |
| `b6` | `Mirror` | `gMirN` (нормаль плоскости, `w` = 1 — отсечение), `gMirP` | PS, `Liquid.cpp:133`; после зеркала `w` = 0 (`Liquid.cpp:138-139`) |
| `b7` | `Dirt` | `gDirtO`, `gDirtX/Y/Z`, `gDirtC` — система корабля и альбедо грязи | PS, `Mesh.cpp:305` |
| `b8` | `Emis2` | `gEmis2` — emission2 модуля, `w` = 1 — задан | PS, `Mesh.cpp:381` |
| `t0` | `tDiff` | диффузная текстура или `whiteTex` | PS, `Mesh.cpp:383` |
| `t1`–`t5` | `tNorm`, `tSpecM`, `tRghn`, `tMetl`, `tEmisM` | карты материала | PS, `Mesh.cpp:371` |
| `s0` | `sAniso` | | глобально |
| + | `b0`, `b4`, `b9`, `t7`, `t8`, `t13`, `t17`–`t21`, `s2` | | глобально |

- Выход: `SV_Target0` — цвет; `SV_Target1` — вектор движения (пишется, когда при TAA привязан `mvRTV`,
  `Client.cpp:1021-1025`). Смешивание `bsAlpha`, `bsAdd` или `bsFilt` (`Mesh.cpp:386-387`).
- Функции: `DirtHash`/`DirtNoise`/`DirtCover` (`:66/67/74`) — грязь; `PerturbNormal` (`:106`) — карта нормалей без
  касательных; `SpecAAKernel` (`:123`) — сглаживание бликов; `D_GGX_M` (`:129`); `EnvReflect` (`:134`) — небо или
  освещённый грунт в отражении; ветви `PS`: фильтр `:151`, неосвещённый `:165`, PBR `:183`, Фонг `:216`.
  `LiqApparentDepth` (`:46`) не вызывается. Отражения — [water.md §6.2](water.md#62-на-кораблях-и-материалах-meshhlsl).

### 4.2. `planet.hlsl` — тайлы поверхности

- Компиляция: `Client.cpp:500`; раскладка `ilPlanet` (`Client.cpp:530`), вершина `TileVertex` (`Client.h:222`).
- Рисование: `Planet::Render` (`Planet.cpp:759`), шейдеры `Planet.cpp:813-814`; на тайл — `Planet::Draw`
  (`Planet.cpp:895`), `DrawIndexed` около `Planet.cpp:985`. В кадре — `Client.cpp:1014`, после неба и звёзд.
- Входы: `VS` (`planet.hlsl:78`, использует `SV_VertexID`), `PS` (`planet.hlsl:336`).

| Регистр | Ресурс | Что | Ставит |
|---|---|---|---|
| `b1` | `Tile` (`PlanetCB`, `Planet.cpp:755-756`) | `gWVP`, `gSunL`, `gUV`, `gMisc` (x текстура, y уровень, z маска, w `TileDbg`), `gW`, `gRot`, микротекстуры `gMic[3]`/`gMicM[3]`/`gMicF`/`gMicC`, материал грунта `gGm`, высоты и уровень жидкости `gEl`, классы грунта `gGround[3]`, `gMaskUV`, берег `gShore`/`gShore2` | VS+PS, `Planet.cpp:815-816`; заполнение `Planet.cpp:973` |
| `t0` | `tSurf` | текстура тайла или предка | PS, `Planet.cpp:976` |
| `t1`–`t3` | `tMic0..2` | микротекстуры (мелкая…крупная) | PS, `Planet.cpp:785` |
| `t4`–`t6` | `tGc`, `tGs`, `tGround` | булыжник, песок, веса классов | PS, `Planet.cpp:800` |
| `t15` | `tMask` | маска жидкости | PS, `Planet.cpp:950`; в шейдере не читается |
| `t16` | `tSdf` | расстояние до берега | VS+PS, `Planet.cpp:957-958` |
| `s0`, `s1` | `sAniso`, `sClamp` | | глобально |
| `s3` | `sLin` | трилинейный WRAP (поиск parallax) | `Planet.cpp:801-808` |
| + | `b0`, `b4`, `b9`, `t7`, `t8`, `t13`, `t17`–`t21`, `s2` | | глобально |

- Выход: один `SV_Target`, `bsOpaque`, `dsWrite` (`Planet.cpp:818-819`).
- Функции: `ElevAt` (`:39`); `CoastDist` (`:54`, ручная билинейная выборка в VS), `CoastBed`/`CoastElev` (`:65/66`);
  `MicroAcc`/`MicroLevel`/`MicroFade`/`Micro` (`:105/114/121/124`) — трипланарные микротекстуры; `GHeight` (`:155`);
  `GroundDetail` (`:171`) — POM и тени булыжника; `CoastAlbedo` (`:245`); `CobbleVis` (`:262`);
  `LocalLightsGround` (`:277`); `Shade` (`:292`); `TileDbg`/`ShadowDbg` (`:322/329`). Отладочные режимы — `gShadow.z`
  (`D3D11CLIENT_SHADOWDBG`), `planet.hlsl:296-316`.

### 4.3. `sky.hlsl` — старое небо

Полноэкранный; направление из `gInvVP`, цвет `SkyColour` (`common.hlsli:96`). Компиляция `Client.cpp:503`; рисование
в `clbkRenderScene`, `Client.cpp:995-1003` (`Draw(3)`), первым после очистки, только при `skyOn` — при активной
FX-атмосфере выключено (`Client.cpp:1268`). Входы: `VS` (`sky.hlsl:4`), `PS` (`sky.hlsl:10`). Регистры — только `b0`.
`bsOpaque`, `dsRead`.

### 4.4. `stars.hlsl` — звёзды

- Компиляция `Client.cpp:512`; рисование `D3D11Client::RenderStars` (`Stars.cpp:36`), `Draw(nStars*6)` `Stars.cpp:54`,
  вызов `Client.cpp:1005`. Без раскладки. Входы: `VS` (`stars.hlsl:10`, 6 вершин на звезду по `SV_VertexID`),
  `PS` (`stars.hlsl:22`).
- `b1 Stars`: `gSVP` (вид×проекция без переноса), `gSMisc` (2/RW, 2/RH, видимость) — VS, `Stars.cpp:52`.
  `t0` (VS): `StructuredBuffer<Star>` = `StarGPU` (`Stars.cpp:14`), `Stars.cpp:53`.
- Выход `(c·f, f)` при `bsAdd` (`SRC_ALPHA, ONE`), `dsRead`.

### 4.5. `particles.hlsl` — потоки частиц Orbiter

- Компиляция `Client.cpp:518`; раскладка `ilPart` (`POSITION` f3, `TEXCOORD` f2, `COLOR` f4), `Client.cpp:519-523`.
  Квадраты строятся на CPU.
- Рисование: `D3D11Client::UpdateRenderParticles` (`Particles.cpp:144`), draw `Particles.cpp:199`; вызов `Client.cpp:1057`.
- Входы: `VS` (`particles.hlsl:12`), `PS` (`particles.hlsl:17`).
- `b1 Part`: `gPVP`, `gPFlags.x` (1 — излучающие) — VS+PS, `Particles.cpp:194-195`. `t0` — текстура потока,
  `Particles.cpp:198`. `s1` — глобальный (сам проход не ставит). Читает `b0`, `b4`, `t8`, `t13` (`SunLight`, `SkyLight`,
  `ApplyHaze`).
- Выход: излучающие — `bsAdd`, диффузные — `bsAlpha` (`Particles.cpp:185`), `dsRead`. Подробно —
  [exhaust.md §12](exhaust.md#12-потоки-частиц-orbiter-particlescpp).

### 4.6. `gtao.hlsl` — затенение окружения

GTAO (Jimenez 2016): 3 среза × 6 шагов в половинном разрешении, двукратное размытие, умножение сцены на AO и долю неба.

- Компиляция: `D3D11Client::RenderAO`, `Ssao.cpp:33-35`, лениво. Вызов `Client.cpp:1033` (после непрозрачных кораблей,
  до жидкости и воздуха).
- Входы: `CSGTAO` (`gtao.hlsl:51`, 8×8), `CSBlur` (`gtao.hlsl:98`, 8×8), `PSApply` (`gtao.hlsl:116`); VS — `vsAA`
  (`Ssao.cpp:103`). Dispatch `Ssao.cpp:83` и `Ssao.cpp:90` (×2), draw `Ssao.cpp:108`.

| Регистр | Ресурс | Что | Ставит |
|---|---|---|---|
| `b11` | `Gtao` (`AoCB`, `Ssao.cpp:10`) | `aP` (tan полуугла x, y; near; радиус 0.6 м), `aS` (размер сцены, размер AO), `aK` (сила, макс. радиус 256 px, тестовый вид), `aD` (шаг размытия) | CS `Ssao.cpp:78`, PS `Ssao.cpp:105` |
| `t0` | `tDepth` | одновыборочная глубина | CS `Ssao.cpp:81`, PS `Ssao.cpp:106-107` |
| `t1` | `tAO` (`float2`: AO, z вида) | | CS `Ssao.cpp:88`, PS `Ssao.cpp:106-107` |
| `u0` | `uAO` | `R16G16_FLOAT`, половинное разрешение | `Ssao.cpp:82, 89` |
| + | `b0`, `b4`, `t7`, `t8`, `t13`, `s2` (в `PSApply`) | | глобально |

Выход `PSApply` — `hdrRTV` без DSV, `g_bsMul` (умножение, только RGB, `Ssao.cpp:39-45`); тестовый вид
`D3D11CLIENT_AODBG` — `bsOpaque`. Функции: `ViewPos` (`:29`), `ViewNormal` (`:40`).

---

## 5. Постобработка и 2D

### 5.1. `aa.hlsl` — разрешение глубины, TAA(U), FXAA

- Компиляция: `Client.cpp:505-510` (`VS` → `vsAA`; `ResolveDepthPS`, `TAAPS`, `FXAAPS`). `vsAA` используют также
  `Bloom.cpp:64`, `Ssao.cpp:103`.
- Рисование: `ResolveDepthPS` — `D3D11Client::ResolveDepth` (`AA.cpp:94`, draw `AA.cpp:107`), только при MSAA, по
  запросу проходов; `TAAPS` — `D3D11Client::FinishScene` (`AA.cpp:115`, draw `AA.cpp:173`, вызов `Client.cpp:1085`);
  `FXAAPS` — `ToneMapAndAA` (`AA.cpp:217-222`), после `post.hlsl`.
- Входы: `VS` (`aa.hlsl:26`), `ResolveDepthPS` (`:33`), `TAAPS` (`:78`), `FXAAPS` (`:126`). `common.hlsli` не включается.

| Регистр | Ресурс | Что | Ставит |
|---|---|---|---|
| `b1` | `AA` (`AACB`, `AA.cpp:112`) | `gInvVP` (свой, с джиттером), `gPrevVP` (прошлый кадр без джиттера), `gCamDelta` (в шейдере не читается), `gP` (x вес истории 0.9, y история годна, zw 1/размер), `gScene` (RW, RH, джиттер), `gPM0..2` (поворот тела + сдвиг) | PS, `AA.cpp:169` (TAA), `AA.cpp:219` (FXAA, заполнено только `gP`) |
| `t0` | `tDepthMS` / `tCur` | MS-глубина (`ResolveDepthPS`, `AA.cpp:106`); текущий кадр `hdrSRV` (TAA) или `ldrSRV` (FXAA) | `AA.cpp:170-171`, `AA.cpp:220` |
| `t1` | `tDepth` | одновыборочная глубина | `AA.cpp:170-171` |
| `t2` | `tHist` | история `outSRV[prev]` | там же |
| `t3` | `tMV` | вектора движения (`mesh.hlsl` `SV_Target1`) | там же |
| `s1` | `sClamp` | | `AA.cpp:172, 221` |

Выход: `ResolveDepthPS` → `R32_FLOAT`; `TAAPS` → `outRTV[cur]` (`R16G16B16A16_FLOAT`, выходное разрешение);
`FXAAPS` → `bbRTVLin` (UNORM: изображение уже в sRGB). Функции: `Tm`/`InvTm` (`:38/39`), `ToYCoCg`/`FromYCoCg`
(`:41/42`), `HistoryCR` (`:46`, Катмулл–Ром за 5 выборок), `ClipToBox` (`:63`), Ланцош-2 по 3×3 (`:85-95`), `Luma` (`:124`).

Разрешение цвета MSAA выполняется аппаратно (`ResolveSubresource`, `AA.cpp:120`), не шейдером.

### 5.2. `post.hlsl` — тон-маппинг

- Компиляция `Client.cpp:501`; рисование `D3D11Client::ToneMapAndAA` (`AA.cpp:181`), шейдеры `AA.cpp:192-193`,
  draw `AA.cpp:209`; вызов `Client.cpp:1086`.
- Входы: `VS` (`post.hlsl:11`), `PS` (`post.hlsl:37`).
- `b0 Frame` (`gHazeRGB.w`, `gAdapt.rgb`, `AA.cpp:204`); `b2 Post`: `gPost.x` — резкость 0…1, при TAA + 0.6
  (`AA.cpp:194-202`); `t0 tHDR` — одновыборочное изображение (`AA.cpp:206`); `t1 tBloom` — свечение половинного
  размера или null (`AA.cpp:207`); `s1` (`AA.cpp:208`).
- Функции: `ToneMap` (`:21`) — ACES (аппроксимация Хилла), экспозиция × 1.6; `Px` (`:32`) — HDR + 1 % свечения;
  адаптивная резкость `:42-52`.
- Выход: `bbRTV` (sRGB) или `ldrRTV` (sRGB) при FXAA (`AA.cpp:188`), `bsOpaque`.

### 5.3. `bloom.hlsl` — свечение

Пять уровней вниз (13 выборок, среднее Кариса на первом), затем вверх (шатёр 3×3) со сложением.

- Компиляция: `D3D11Client::RenderBloom`, `Bloom.cpp:29-30`, лениво; рисование `Bloom.cpp:76`; вызов из
  `ToneMapAndAA` (`AA.cpp:185`), до `post.hlsl`.
- Входы: `PSDown` (`bloom.hlsl:16`), `PSUp` (`bloom.hlsl:30`); VS — `vsAA`.
- `b0 Bloom` (свой): `bTex` (xy 1/размер источника, z = 1 — среднее Кариса), `Bloom.cpp:65`; Frame возвращается в
  `Bloom.cpp:89`. `t0 tSrc` (`Bloom.cpp:75`), `s1` (`Bloom.cpp:66`).
- Уровни `R11G11B10_FLOAT`; вниз — `bsOpaque`, вверх — `ONE, ONE` (`Bloom.cpp:34, 84`). Функции `S` (`:13`, выборка
  с ограничением 64), `W` (`:14`, вес Кариса).

### 5.4. `blit.hlsl` — копия текстуры

`t0 tSrc`, `s1`; без буферов. Входы `VS` (`blit.hlsl:5`), `PS` (`blit.hlsl:11`). Компиляция `Client.cpp:502`.
Рисование: `D3D11Client::CompositeScreenLayer` (`Draw.cpp:1051`, draw `Draw.cpp:1108`, вызов из `clbkDisplayFrame`,
`Client.cpp:1140`) — слой экрана на back buffer с `bsAlpha`; `D3D11Client::Readback` (`Draw.cpp:744`, draw
`Draw.cpp:767`) — чтение поверхности во временную цель `B8G8R8A8_UNORM_SRGB` с `bsOpaque`.

### 5.5. `panel.hlsl` — 2D-панели

- Компиляция `Client.cpp:524`; рисование `D3D11Client::clbkRender2DPanel` (`Draw.cpp:1120`), шейдеры `Draw.cpp:1138-1139`,
  `DrawIndexed` `Draw.cpp:1163`; из `Render2DOverlay` (`Client.cpp:1090`), после тон-маппинга, в back buffer.
- Входы: `VS` (`panel.hlsl:11`), `PS` (`panel.hlsl:19`). Раскладка `ilMesh` (`NORMAL` не используется).
- `b1 Panel` (`PanelCB`, `Draw.cpp:1113`): `gXf` (m11, m22, m13, m23 матрицы `T`), `gMisc` (2/W, 2/H, альфа) —
  `Draw.cpp:1140-1141`. `t0` — текстура группы (`Draw.cpp:1162`), `s1` (`Draw.cpp:1142`). `bsAdd` или `bsAlpha`
  (`Draw.cpp:1133`).

### 5.6. `hud.hlsl` — холсты модулей (`D3D11Hud`)

- Компиляция: `HudApi.cpp:43-57`, `D3DCompileFromFile` напрямую (без кэша). Раскладка `HudApi.cpp:58-66`: `POSITION`
  f2, `TEXCOORD0` f2, `TEXCOORD1` f4, `TEXCOORD2` f4, `COLOR` unorm4, `KIND` uint, `DEPTH` f.
- Рисование: `Flush` (`HudApi.cpp:215`, draw `HudApi.cpp:255`) и `Resolve` (`HudApi.cpp:261`, draw `HudApi.cpp:278`),
  по вызовам модулей; состояние конвейера сохраняется и восстанавливается `PipeState` (`GpuPad.cpp:103-126`).
- Входы: `VS` (`hud.hlsl:24`), `PS` (`:37`), `FullVS` (`:81`), `LightPS` (`:87`), `DownPS` (`:94`).
- `b0 Hud` (свой): `gSize` (2/размер, полшага глубины, жёсткие края), `gFx` (усиление, гамма), `gFx2` (шум, seed,
  источник через sRGB-view), `gRes` (всегда 0) — VS+PS `HudApi.cpp:251`. `t0` — источник или атлас глифов
  (`HudApi.cpp:253, 276`); `s0` — свой линейный CLAMP (`HudApi.cpp:254, 277`).
- `PS` пишет `SV_Target` и `SV_Depth` (тест `GREATER`). Виды примитивов в `PS`: 0 треугольник, 1 линия-капсула,
  2 эллипс, 3 изображение, 4 глиф SDF (`hud.hlsl:42-64`); ореол `:67-70`; `Hash` `:32`.

### 5.7. `sketch.hlsl` — Sketchpad на GPU

- Компиляция: `GpuPad.cpp:33-44`, `CachedCompileFromFile` напрямую. Раскладка `GpuPad.cpp:45-54`.
- Рисование: `SkBatch::Flush` (`GpuPad.cpp:240`, draw `GpuPad.cpp:283`) — рисование Sketchpad на поверхностях-целях.
- Входы: `VS` (`sketch.hlsl:26`), `PS` (`sketch.hlsl:36`).
- `b0 Sk` (свой, 64 байта, `GpuPad.cpp:258`): `gSize`, `gGamma`, `gMode`, `gKey` (цветовой ключ) — `GpuPad.cpp:278`.
  `t0 tSrc`, `t1 tText` (атлас текста GDI) — `GpuPad.cpp:280`; `s0` свой линейный CLAMP, `s1` свой точечный CLAMP —
  `GpuPad.cpp:282`. Выход `SV_Target` + `SV_Depth`, тест `GREATER` (`GpuPad.cpp:74`). Глобальные `b0`, `s0`, `s1`
  восстанавливает `PipeState` (`GpuPad.cpp:125-126`).

### 5.8. `ar.hlsl` — линии дополненной реальности

- Компиляция: `Ar.cpp:36-42`, `CachedCompileFromFile` напрямую. Раскладка `Ar.cpp:43-50`: `POSITION`, `OTHER`, `SIDE`,
  `END`, `HALFW`, `COLOR`.
- Рисование: `D3D11Client::RenderAr` (`Ar.cpp:150`), два прохода (draw `Ar.cpp:248`); вызов `Client.cpp:1056`.
  Линии — ленты постоянной ширины в пикселях, строятся в VS.
- Входы: `VS` (`ar.hlsl:13`), `PS` (`ar.hlsl:28`).
- `b0 Frame` (`gHazeRGB.w`, `gAdapt.rgb` — компенсация экспозиции); `b1 Ar`: `gVPa`, `gScr` (размер сцены, ширина
  свечения, яркость 0.9 / 0.22) — VS+PS `Ar.cpp:241`.
- Аддитивно, только RGB (`Ar.cpp:54-58`); видимые части — `GREATER_EQUAL`, скрытые — `LESS`, без записи
  (`Ar.cpp:59-61`).

---

## 6. Атмосфера, Солнце, локальные источники

### 6.1. `fx_atmo.hlsl` — проход атмосферы

Полноэкранный проход на каждую планету с атмосферой в кадре (от дальней к ближней) после непрозрачной сцены; добавка
рассеянного света и множитель пропускания через dual-source blending. Внутри воздуха текущей планеты лучи заранее
интегрируются в объём атмосферной перспективы (`CSVol`), из космоса PS интегрирует сам. Физика —
[atmosphere.md §2.6](atmosphere.md#26-однократное-рассеяние-вдоль-луча-fxmarch-fx_atmo_skyhlsli28-60-step-в-fx_atmohlsl92-108),
[§3.3](atmosphere.md#33-объём-атмосферной-перспективы-csvol-fx_atmohlsl110-146), [§5.4](atmosphere.md#54-fx_atmohlsl).

- Компиляция: `FxAtmo.cpp:259-260` (`VS`, `PS`), `FxAtmo.cpp:265` (`CSVol`, необязателен).
- Вызов: `Atmosphere::Render` (`FxAtmo.cpp:484-583`) ← `FxSystem::RenderTransparent` (`Fx.cpp:52`) ← `Client.cpp:1045`.
  `CSVol` — `Dispatch` `FxAtmo.cpp:560`; `Draw(3)` — `FxAtmo.cpp:573`.
- Входы: `VS` (`fx_atmo.hlsl:34-39`), `CSVol` (`:112-146`, 8×8), `PS` (`:176-267`).

| Регистр | Ресурс | Стадия | Ставит |
|---|---|---|---|
| `b3` | `AtmoPass` (`PassCB`, `FxAtmo.cpp:14-17`) | PS, CS (к VS тоже привязан, не читается) | `FxAtmo.cpp:527-528`, `555` |
| `b4` | `FxAtmoMedia` данной планеты | PS, CS | `FxAtmo.cpp:572`, `556` |
| `t0` | `tDepth` | PS | `FxAtmo.cpp:529` |
| `t1`–`t4` | `tApL`, `tApT`, `tEndL`, `tEndT` (объём) | PS | `FxAtmo.cpp:565-566` |
| `t5` | `tDepthMS` | PS (MSAA) | `FxAtmo.cpp:567-568` |
| `t8`, `t9` | таблицы | PS, CS | `FxAtmo.cpp:571`, `557` |
| `s1` | `sClamp` | PS | `FxAtmo.cpp:569` |
| `u0`–`u3` | `uApL`, `uApT`, `uEndL`, `uEndT` | CS | `FxAtmo.cpp:558-559` |

Выход: `SV_Target0` — добавка, `SV_Target1` — множитель; `bsDual` (`ONE`, `SRC1_COLOR`, только RGB,
`FxAtmo.cpp:286-293`); DSV не привязан. В конце `BindLight` возвращает b4/t8/t9 текущей планеты (`FxAtmo.cpp:580`).

### 6.2. `fx_atmo_lut.hlsl` — таблицы атмосферы

Три ядра строят таблицы планеты один раз, при первом кадре с ней (`PlanetAtmosphere::BuildTables`,
`FxAtmo.cpp:150-180`): пропускание 256×64, многократное рассеяние 64×64, освещённость от неба 64×32; затем пропускание и
освещённость копируются в общую `lut` 256×96 (`FxAtmo.cpp:176-177`). См. [atmosphere.md §2.7](atmosphere.md#27-многократное-рассеяние-csms-fx_atmo_luthlsl89-133),
[§2.8](atmosphere.md#28-освещённость-от-неба-csirr-fx_atmo_luthlsl136-178), [§5.5](atmosphere.md#55-fx_atmo_luthlsl).

- Компиляция `FxAtmo.cpp:261-263`; без любого из ядер атмосфера не инициализируется (`FxAtmo.cpp:264`).
- Входы и запуск: `CSTrans` (`fx_atmo_lut.hlsl:41-59`, 8×8; `Dispatch(32, 8)`, `FxAtmo.cpp:158`), `CSMS` (`:91-133`,
  64 потока на направления; `Dispatch(64, 64)`, `FxAtmo.cpp:164`), `CSIrr` (`:161-178`; `Dispatch(64, 32)`, `FxAtmo.cpp:170`).
- `b4` — `FxAtmo.cpp:154`; `t8` — здесь отдельная таблица пропускания `transSRV` (`FxAtmo.cpp:161`); `t1 tMS` — только
  `CSIrr` (`FxAtmo.cpp:167`); `u0 uOut` — `R32G32B32A32_FLOAT` (`FxAtmo.cpp:157, 163, 169`).

### 6.3. `fx_sky_sh.hlsl` — свет неба в гармониках

Свет неба вокруг камеры как 9 коэффициентов L2, свёрнутых с косинусом: 1024 направления (сфера Фибоначчи), выше
горизонта — `FxSkyRadianceEx` (10 шагов), ниже — освещённая земля. Пересчёт каждый кадр, когда камера в воздухе текущей
планеты и таблицы готовы (`FxAtmo.cpp:326-328`). См. [atmosphere.md §2.11](atmosphere.md#211-свет-неба-в-сферических-гармониках-fx_sky_shhlsl).

- Компиляция `FxAtmo.cpp:267-272`; буфер 10 × 16 байт, SRV + UAV.
- Запуск: `Atmosphere::UpdateSkySH` (`FxAtmo.cpp:323-340`), `Dispatch(1, 1, 1)` `FxAtmo.cpp:334`.
- Вход: `CS` (`fx_sky_sh.hlsl:20-52`, 256 потоков; групповая память `gs[256][9]`).
- `b4` (`FxAtmo.cpp:330`), `t8` = `lutSRV`, `t9` = `msSRV` (`FxAtmo.cpp:331-332`), `u0 uSH` (`FxAtmo.cpp:333`).
  Результат: `[0..8]` — коэффициенты, `[9]` = (радиус камеры, 0, 0, 1 — годно); читается как `t13`.

### 6.4. `sun.hlsl` — диск Солнца и блик

Два прохода: диск, хромосфера, протуберанцы, корона (`VS`/`PS`) — квад после жидкости и до прохода атмосферы,
аддитивно, только на небе (глубина 0); блик в глазу (`VSG`/`PSG`) — полноэкранный, в конце кадра, видимая доля диска по
13 выборкам глубины в VS. См. [atmosphere.md §6.1](atmosphere.md#61-проход-солнца-suncpp91-268-sunhlsl),
[§6.2](atmosphere.md#62-модель-sunhlsl), [§6.4](atmosphere.md#64-блик-rendersunglare-sunhlsl255-309).

- Компиляция: `Sun.cpp:109` (`VS`, `PS`), `Sun.cpp:113` (`VSG`, `PSG`), при первом `RenderSun`; после этого
  `g_sunPassOn = true` (`Sun.cpp:129`) и `fx_atmo.hlsl` свой диск не рисует (`FxAtmo.cpp:543`).
- Рисование: `D3D11Client::RenderSun` (`Sun.cpp:91-268`, `Draw(4)` `Sun.cpp:264`, вызов `Client.cpp:1039`);
  `D3D11Client::RenderSunGlare` (`Sun.cpp:273-301`, `Draw(3)` `Sun.cpp:296`, вызов `Client.cpp:1078`).
- Входы: `VS` (`sun.hlsl:51-56`), `PS` (`:311-419`), `VSG` (`:269-283`), `PSG` (`:284-309`).

| Регистр | Ресурс | Стадия | Ставит |
|---|---|---|---|
| `b3` | `SunPass` (`SunCB`, `Sun.cpp:26`) | VS, PS | `Sun.cpp:257`, `293` |
| `b4`, `t8` | FX-атмосфера (`FxTransToSun`) | `PSG` | глобально |
| `t0` / `t1` | `tSunDepth` / `tSunDepthMS` | `VSG` | `Sun.cpp:294-295` (одна из двух null по MSAA) |
| `t2`–`t5` | `tRealWl`, `tRealHa`, `tRealLimb`, `tRealC2` | PS | `Sun.cpp:259-260` |
| `s0`, `s1` | `sampAniso`, `sampClamp` | PS | `Sun.cpp:261-262` |

Выход: аддитивно, только RGB (`Sun.cpp:120-124`); диск — `dsRead` с DSV, блик — без DSV, предел яркости 3·10⁴
(`sun.hlsl:308`).

### 6.5. `lights_vol.hlsl` — локальные источники в воздухе

Видимая часть локальных источников кораблей после воздуха и облаков (`LightsRenderAir`, `Lights.cpp:565-653`,
вызов `Client.cpp:1052`). Ни одна тематическая глава этот файл не описывает.

1. **Лучи фар** (`PSBeams`): однократное рассеяние света прожектора или точечного источника вдоль луча зрения в
   половинном разрешении с ножницами по плиткам кластеров; затем `PSUpsample` добавляет результат в `hdrRTV`.
   Отбор — до `MAXBEAM` = 8 источников с дальностью ≥ 4 м и силой ≥ 300 кд в поле зрения, при активной FX-атмосфере
   или старом небе (`Lights.cpp:451-456`); они стоят первыми в `tLights` (`Lights.cpp:472`), индекс прохода совпадает
   с индексом источника.
2. **Линзы прожекторов и маяки** (`VSSprite`/`PSSprite`): аддитивные квады до `MAXSPR` = 256, закрываемые глубиной сцены.
3. **Зонд адаптации** (`CSProbe`): 16×9 точек кадра для `LightsAdaptEye`.
4. **`VSClearZ`**: очистка одной плитки атласа видимости неба (`Draw(3)` без PS, `Lights.cpp:430-433`).

Компиляция: в `LightsFrame` (`Lights.cpp:107-162`): `VS`, `PSBeams` — `:133`; `VSSprite`, `PSSprite` — `:134`;
`VSClearZ` — `:139`; `PSUpsample` — `:142`; `CSProbe` — `:149`. Каждая часть необязательна.

| Вход | Строки | Вызов |
|---|---|---|
| `VS` | `lights_vol.hlsl:15-20` | полноэкранный треугольник для `PSBeams`, `PSUpsample` |
| `PSBeams` | `:51-122` | `Draw(3)` на каждый луч, `Lights.cpp:604-612` |
| `PSUpsample` | `:235` | `Draw(3)` в `hdrRTV`, `Lights.cpp:620` |
| `VSSprite`, `PSSprite` | `:146-202`, `:204-216` | `DrawInstanced(6, g_nSpr)`, `Lights.cpp:645` |
| `CSProbe` | `:221-230`, 16×9 | `LightsProbe`, `Dispatch(1,1,1)` `Lights.cpp:707` (вызов `Client.cpp:1077`); результат через кольцо из 3 буферов чтения (`Lights.cpp:710`) |
| `VSClearZ` | `:238` | `Lights.cpp:430-433` |

`PSBeams`: точка поверхности из глубины пикселя сцены `pos.xy · gBeam.y`; отрезок луча обрезается сферой дальности и
конусом прожектора (`ConeSpan`, `:27-45`); интеграл 1/r² — равноугловая выборка (Kulla, Fajardo 2012), N = 10 со
сдвигом по номеру кадра `gSpotK[0].w` (`:70-71, 89-116`); в выборке — окно дальности, конус, тень прожектора
(`tSpotShadow`, `s2`), фазы Рэлея и Хеньи–Гринстейна (`g` — среднее `mMieG`), ослабление `exp(−ext·(t + r))`; × π
(`:118`). `VSSprite`: видимость по 9 выборкам глубины (`:153-163`), яркость линзы I/r² с конусом и минимумом 3 %,
ослабление воздухом (`:167-174`), маяки делятся на экспозицию (`:177`), ядро меньше 1.2 пикселя растягивается с
сохранением энергии (`:188`).

| Регистр | Ресурс | Стадия | Ставит |
|---|---|---|---|
| `b0` | `Frame` | PS, VS, CS | `Lights.cpp:581`, `641`, `704` |
| `b1` | `SpriteCB` (`lights_vol.hlsl:133-140`; C++ `Lights.cpp:17`) | VS | `Lights.cpp:640` |
| `b4` | `FxAtmoMedia` | PS, VS | `FxBindLight`, `Lights.cpp:572` |
| `b9` | `LocalLights` | PS (VS — привязан, не читается) | `Lights.cpp:571` → `560`; VS `Lights.cpp:642` |
| `b10` | `Beam`: x — индекс источника, y — пикселей сцены на пиксель цели (2) | PS | `Lights.cpp:603`, заполнение `607-608` |
| `t0` | `tDepth` | PS, VS, CS | `Lights.cpp:601`, `644`, `705` |
| `t1` | `tSprites` (`StructuredBuffer<Sprite>`) | VS | `Lights.cpp:644` |
| `t2` | `tHalf` (половинная цель `R16G16B16A16_FLOAT`) | PS | `Lights.cpp:618` |
| `t17`, `t20` | источники, тени прожекторов | PS | `LightsBind` |
| `s1` | `sampClamp` | PS | `Lights.cpp:619` |
| `s2` | `sampShadow` | PS | `Lights.cpp:583` |
| `u0` | `uProbe` (144 × `float4`) | CS | `Lights.cpp:706` |

Выход: все PS пишут альфу 1 под `bsAdd`; половинная цель очищается в 0 (`Lights.cpp:593-594`); спрайты рисуются в
`hdrRTV` с `dsvRO` и `dsRead` (`Lights.cpp:636`). Ключи `Client.cfg`: `LocalLights`, `LightLux`, `LightShadows`,
`LightBeams`, `Beacons` (`Lights.cpp:25-45`).

---

## 7. Жидкости и дождь

### 7.1. `liquid.hlsl` — поверхность жидкости

Вид сверху: Френель, небо FX в зеркале, отражение в экранном пространстве и из зеркального прохода, преломление по
глубине сцены, поглощение, блик GGX, кильватеры Кельвина; при поле волн — тесселяция и смещение. См.
[water.md §5.1](water.md#51-liquidhlsl), физика — [§2.2–2.5](water.md#22-геометрия-поверхности).

- Компиляция: `VS`, `PS` — `CreateShaders`, `Client.cpp:514`, раскладка `ilLiquid` (`POSITION` R32G32) `Client.cpp:515-516`;
  `VSCP`, `HS`, `DS` — `Liquid.cpp:241-242`; `PSMS` — при MSAA, `Liquid.cpp:260`.
- Рисование: `RenderLiquid` (вызов `Client.cpp:1037`): цвет `Liquid.cpp:362`, второй проход только в глубину без PS
  `Liquid.cpp:371`. Тесселяция — при включённом поле волн и наличии трёх шейдеров (`Liquid.cpp:321`).
- Входы: `VS` (`:87`), `VSCP` (`:110`), `HS` (`:178`, константная функция `HSConst` `:147`), `DS` (`:185`), `PS` (`:442`),
  `PSMS` (`:443`); оба PS вызывают `Shade` (`:299`).

| Регистр | Ресурс | Стадия | Ставит |
|---|---|---|---|
| `b1` | `Liquid` (`LiquidCB`, `Liquid.cpp:79`) | VS, PS, HS, DS | `Liquid.cpp:314-315`, `327-328` |
| `b5` | `Wakes` (`gWk[96]`, 32 источника × 3, `Wake.h:8`) | PS | `Liquid.cpp:353` |
| `t0`, `t1` | копия сцены, глубина | PS | `Liquid.cpp:318` |
| `t2` | `tWave` (уклоны поля волн) | PS, HS, DS | `Liquid.cpp:318`, `329-330` |
| `t3` | `tKelvin` | PS | `Liquid.cpp:354` |
| `t4` | `tRefl` (зеркало) | PS | `Liquid.cpp:357` |
| `t5`, `t6` | `tSceneMS`, `tDepthMS` | `PSMS` | `Liquid.cpp:357` |
| `t11`, `t12` | `tReflDepth`, `tBedDepth` | PS; `t12` и DS | `Liquid.cpp:360`, `332` |
| `s1` | `sClamp` | PS, HS, DS | глобально; HS/DS — `sampLiqClamp`, `Liquid.cpp:333-334` |
| + | `b0`, `b4`, `t7`, `t8`, `t9`, `t13`, `s2` | PS | глобально, `FxBindLight` `Liquid.cpp:311` |

Выход: `hdrRTV`, `bsOpaque`, глубина только для чтения (`dsvRO`, `Liquid.cpp:302`). `s0 sWrap` (`:28`) объявлен и
привязан к VS (`Liquid.cpp:320`), но не читается.

### 7.2. `wavefield.hlsl` — поле волн iWave

Поле 1024×1024 вокруг фокусного корабля: штампы тел, шаг, сдвиг окна, уклоны для `liquid.hlsl`. См.
[water.md §5.2](water.md#52-wavefieldhlsl), [§2.8](water.md#28-поле-волн-iwave-используется-wakecpp-wavefieldhlsl).

- Компиляция: `FieldInit`, `Wake.cpp:121-126`, `D3DCompileFromFile` без кэша.
- Запуск: `Run` (`Wake.cpp:145`, `Dispatch(FN/16, FN/16)` `:153`): `CSShift` `:249`, `CSStamp` `:500`, `CSSim` `:505`
  (до 4 шагов по 1/60 с), `CSSlope` `:506`; цепочка `WakeField` ← `RenderLiquid` (`Liquid.cpp:294`).
- Входы (16×16): `CSStamp` (`:45`), `CSSim` (`:65`), `CSShift` (`:116`), `CSSlope` (`:124`).
- `b0 Field` (`FieldCB`, `Wake.cpp:52`; `gKer[43]` ↔ `ker[172]`); `t0 tIn` (R32G32), `u0 uOut` (R32G32), `u1 uSlope`
  (`R16G16B16A16_FLOAT`) — `Wake.cpp:149-152`.

### 7.3. `underwater.hlsl` — камера в жидкости

Полноэкранный; поглощение и рассеянный свет столба до глубины сцены или выхода из сферы уровня. Компиляция
`Liquid.cpp:156`; рисование `RenderUnderwater`, `Draw(3)` `Liquid.cpp:191` (вызов `Client.cpp:1060`), если камера ниже
уровня не глубже 5 км (`Liquid.cpp:151`). Входы `VS` (`:15`), `PS` (`:22`). `b1 Under` (анонимная структура
`Liquid.cpp:176`, VS+PS `Liquid.cpp:189`), `t0 tDepth` (`Liquid.cpp:190`). Dual-source (`ONE`, `SRC1_COLOR`, только RGB,
`Liquid.cpp:161-164`), без DSV. `common.hlsli` включён, но из него ничего не читается. См.
[water.md §5.3](water.md#53-underwaterhlsl).

### 7.4. `fx_waves.hlsl` — спектральное поле волн (FFT)

FFT 1024² с точным поворотом мод ω² = gk. См. [water.md §5.4](water.md#54-fx_waveshlsl-fft-не-подключён).

- Компиляция: `WaveField::Init`, `FxWaves.cpp:43-46` (из `FxSystem::Init`, `Fx.cpp:39`), если `Fx.cfg` `Waves` ≠ 0
  (без файла или ключа — включено, `Fx.cpp:36`).
- Запуск каждый кадр при жидкости рядом с камерой: `CSSource` `FxWaves.cpp:246`, 4 прохода `CSFFT` и `CSEvolve`
  (`FxWaves.cpp:248-261`), `CSNormals` `FxWaves.cpp:267`.
- Входы: `CSSource` (`:39`, 8×8), `CSFFT` (`:82`), `CSEvolve` (`:111`, 8×8), `CSNormals` (`:140`, 8×8).
- `b2 WaveCB` (`FxWaves.cpp:16`, 4 экземпляра); `u0 uState`, `u2 uImpact` (`CSSource`); `t0 tIn`, `u1 uOut`
  (`CSFFT`, `CSEvolve`); `t0`, `u3 uNormal` (`CSNormals`).
- Результат (`FxWaves()`, `Fx.cpp:70`) никем не читается; `liquid.hlsl` использует `wavefield.hlsl`.

### 7.5. `spray.hlsl` — карточки брызг

Карточки брызг от колёс в жидкости: атлас 8×8 кадров `splash_wheel.dds`, масштаб по подобию Фруда. См.
[water.md §5.5](water.md#55-sprayhlsl-карточки-без-питания).

- Компиляция: `SprayInit` (`Spray.cpp:40`), вызывается только из `SprayRender` (`Spray.cpp:100`) при непустом списке
  карточек. Единственный источник карточек `SprayFeed` (`Spray.cpp:63`) нигде не вызывается
  (`GpuParticles.cpp:367`: карточки выключены). Шейдер в работе не компилируется.
- Входы: `VS` (`:33`), `PS` (`:71`); рисование (недостижимо) `DrawInstanced(6, n)`, `Spray.cpp:153`.
- `b5 Spray` (`Spray.cpp:20`, `:147-148`); `t4 tCards` (VS, `:149`), `t5` глубина и `t6 tSheet` (`:151`), `t3 tBehind`
  (`:152`); `s3` сам не ставит. PS читает также `b0`, `b4`, `t7`, `s2`, `t8`, `t13`. Предумноженная альфа.

### 7.6. `rain.hlsl` — дождь Титана

Процедурные капли из `SV_InstanceID` в коробке 40 м, штрих из 6 вершин, освещение в VS. См.
[rain.md §6.1](rain.md#61-rainhlsl), [§2.1](rain.md#21-титан-rainhlsl-raincpp).

- Компиляция `Rain.cpp:74`; рисование `DrawInstanced(6, n)` `Rain.cpp:128`, n = 4000…90000; вызов `Client.cpp:1058`;
  только Титан, высота < 3 км, интенсивность > 0.
- Входы: `VS` (`:29`), `PS` (`:70`).
- `b1 Rain` (анонимная структура `Rain.cpp:105`, VS+PS `Rain.cpp:124`). VS читает `b0`, `b4`, `t13` (`FxBindLight`,
  `Rain.cpp:122`), `t7` + `s2` (`Rain.cpp:125-126`) и `t8` — последний к VS не привязан (см. [§11](#11-замечания-по-коду)).
  PS ресурсов не читает. Предумноженная альфа (`Rain.cpp:80-83`), `dsRead`.

### 7.7. `storm_rain.hlsl` — дождь под циклоном Земли

Частицы на GPU в коробке 26 м: инициализация, шаг, яркость окружения; штрихи капель и короны брызг. См.
[rain.md §6.2](rain.md#62-storm_rainhlsl), [§2.2](rain.md#22-земля-шторм-storm_rainhlsl-stormraincpp-stormcpp).

- Компиляция `StormRain.cpp:49-52`. Запуск: `CSInit` `:170`, `CSSim` `:174`, `CSEnv` `:191`; рисование
  `DrawInstanced(4, active)` `:207` и `DrawInstanced(4, SPLASH_N)` `:209` (TRIANGLESTRIP, без DSV). Вызов
  `Client.cpp:1059`; выход при интенсивности < 0.05 мм/ч.
- Входы: `CSInit` (`:54`), `CSSim` (`:63`) — 256 потоков; `CSEnv` (`:99`) — 1 поток; `VSDrop` (`:123`), `PSDrop` (`:173`),
  `VSSplash` (`:199`), `PSSplash` (`:224`).
- `b1 StormRain` (`CB`, `StormRain.cpp:32`): CS `:165, 188`, VS/PS `:203`. `b0` привязан к CS (`:166`), но читается
  только `PSDrop`; `PSDrop` читает также `b4`, `t8` (`FxBindLight`, `:202`).
- `u0 uDrops`, `u1 uSplash`, `u2 uCount` (`:168`), `u3 uEnv` (`:190`); `t0 tScene` (`CSEnv` `:189`, `PSDrop`), `t1 tDrops`
  (`VSDrop`), `t2 tSplash` (`VSSplash`), `t3 tDepthS` (`PSDrop`), `t4 tEnv` (оба VS); все пять SRV — к VS и PS (`:205`).
  Предумноженная альфа (`StormRain.cpp:68-71`).

### 7.8. `visor_drops.hlsl` — капли на визоре

Симуляция капель на стекле (сетка 768×480, 4096 капель, ячейка слияния 16), карта высот и линз, вид изнутри, капли на
чужих стёклах снаружи. См. [rain.md §6.3](rain.md#63-visor_dropshlsl), [§4.4](rain.md#44-симуляция-gpu-visor_dropshlsl).

- Компиляция: `VisorInit`, `VisorDrops.cpp:186-195` (21 вход); включение — `Client.cfg` `VisorDrops` и галочка в
  диалоге (`VisorDrops.cpp:258, 356`).
- Запуск: CS `VisorDrops.cpp:572-580`; стирание и бусины `:600-602`; высоты, линзы, размытие `:616-645` (вид от первого
  лица изнутри); уменьшение сцены `:660-662`; `PSInside` `:682` (`RenderVisorDropsInside`, `Client.cpp:1061`); `PSOut`
  `:339` (`RenderVisorDropsOutside`, `Client.cpp:1042`).
- Входы: CS (64 потока) `CSClearGrid` (`:125`), `CSInsert` (`:135`), `CSUpdate` (`:154`), `CSFinalize` (`:235`), `CSSpawn`
  (`:242`); CS 8×8 `CSFilm` (`:257`); VS `VSDrop` (`:269`), `VSFull` (`:310`), `VSOut` (`:485`); PS `PSWipe`/`PSBead`/`PSCap`
  (`:304-306`), `PSCopyFilm` (`:318`), `PSLens` (`:322`), `PSBlurH`/`PSBlurV` (`:349-350`), `PSDown` (`:355`), `PSDownMS`
  (`:362`), `PSInside` (`:422`), `PSOut` (`:496`).
- `b1 Visor` (`VCB`, `VisorDrops.cpp:102`; CS/VS/PS `:556`), `b2 VisorOut` (`OCB`, `:103`; `:318`); `b0` (`:317, 673`);
  `b4`/`t8`/`t13` (`FxBindLight`, `:319, 674`); `b9`, `t17`–`t20` (`LightsBind`, `:320, 675`).
- CS: `u0`–`u3` (`:568-569`), `u4 uFilm` (`:579`), `t1 tSpawn`, `t2 tFilmR` (`:571`). VS: `t0 tDrops`, `t1 tSpawn`
  (`:593, 626`). PS: `t2` (`PSCopyFilm`, `:620`), `t3 tHeight` (`:632`), `t6 tLensIn` (`:639, 643`), `t14`/`t15`
  (`PSDown`/`PSDownMS`, `:660`), `t4 tLens`, `t5 tSceneC` (`:680`), `s3 sLin` (`:678`); `t7` + `s2` — `PSOut`
  (`:321-322`).
- Смешивание (`VisorDrops.cpp:231-234`): `bsMul` — стирание плёнки; `bsMax` — бусины и шапки; без смешивания — копия,
  линза, размытие; предумноженная альфа — `PSInside`, `PSOut`.

---

## 8. Облака и ураган

`CloudLayer.cpp`, `EarthClouds.cpp`, `Cyclone.cpp` не входят в список файлов `build.bat:15`; их функции нигде не
вызываются. Шейдеры `cloudlayer.hlsl`, `earthclouds.hlsl`, `cyclone.hlsl` устанавливаются, но в работе не компилируются.

### 8.1. `clouds.hlsl` — объёмные слои (Титан)

Сферические оболочки из `Clouds.cfg` (до 4 слоёв на тело), марш на половинном разрешении до глубины сцены, свет и
воздух перед облаком — из FX-атмосферы, наложение на `hdrRTV` в полном разрешении. См.
[clouds.md §3](clouds.md#3-объёмные-слои-титан-cloudscpp-cloudshlsl), [§7.1](clouds.md#71-cloudshlsl).

- Компиляция `Clouds.cpp:127` при первом `RenderClouds`; рисование: марш `Clouds.cpp:201`, наложение `Clouds.cpp:209`;
  вызов `Client.cpp:1050`.
- Входы: `VS` (`:30-37`), `PSMarch` (`:128-200`; RT0 `clTex` RGBA16F, RT1 `clTexT` R32F — расстояние до облака),
  `PSComposite` (`:205-223`; `ONE`/`SRC_ALPHA`, только RGB, `Clouds.cpp:134-138`).
- `b1 Clouds` (`:15-23`; `struct k`, `Clouds.cpp:165`, 336 байт, `Clouds.cpp:196`); `b4` (`FxBindLight`, `Clouds.cpp:194`);
  `t0 tShape` 64³, `t1 tDepth` (`Clouds.cpp:197-198`); `t2 tCloud`, `t3 tCloudT` (`Clouds.cpp:207-208`); `s0` =
  `sampAniso`, `s1` = `sampClamp` (`Clouds.cpp:199-200`); `t8`, `t9`, `t13` — глобально.

### 8.2. `storm.hlsl` — ураган Земли

Погодная карта вокруг центра (`PSMap`), тень урагана на сцене до воздуха (`PSShadow`), марш с обновлением одного
текселя из блока 2×2 за кадр (`PSMarch`), временное накопление (`PSTemporal`), билатеральное наложение
(`PSComposite`). См. [clouds.md §4](clouds.md#4-ураган-земли-stormcpp-stormhlsl-storm_noisehlsl), [§7.2](clouds.md#72-stormhlsl).

- Компиляция `Storm.cpp:174-179` в рабочем потоке; объекты — `Storm.cpp:190-195`, когда результат готов.
- Рисование: `PSMap` `Storm.cpp:350` (затем `GenerateMips`); `PSShadow` `Storm.cpp:404` (`RenderStormShadow`,
  `Client.cpp:1044`); `PSMarch` `:461`, `PSTemporal` `:471`, `PSComposite` `:482` (`RenderStorm`, `Client.cpp:1047`).
- Входы: `VS` (`:86-93`), `PSMap` (`:176-180`, 2048² RGBA8), `PSMarch` (`:382-492`, RT0 RGBA16F, RT1 RG32F),
  `PSTemporal` (`:503-549`), `PSComposite` (`:554-584`, `ONE`/`SRC_ALPHA`), `PSShadow` (`:588-628`, `ZERO`/`SRC_COLOR`).
- `b1 Storm` (`:19-37`; `struct CB`, `Storm.cpp:156-161`, 416 байт; `Storm.cpp:349, 399, 456`); `b4` (`Storm.cpp:397, 454`;
  для `PSMap` не ставится).

| Слот | Ресурс | Ставит |
|---|---|---|
| `t0`–`t3` | `tShape` 128³, `tDetail` 32³, `tMap`, `tDepth` | `Storm.cpp:400-401`, `457-458` |
| `t4`, `t5` | `tCur`, `tCurT` (`PSTemporal`) / `tCloud`, `tCloudT` (`PSComposite`) | `Storm.cpp:469-470` / `480-481` |
| `t6`, `t7` | `tHist`, `tHistT` (`PSTemporal`) | `Storm.cpp:469-470` |
| `s0` | `sTri` — собственный трилинейный WRAP (`Storm.cpp:226-228`) | `Storm.cpp:402-403`, `459-460` |
| `s1` | `sampClamp` | там же |

`tHistT` занимает `t7` — слот глобальной карты теней (см. [§11](#11-замечания-по-коду)).

### 8.3. `storm_noise.hlsl` — 3D-шумы урагана

Однократная генерация тайлящихся шумов: форма 128³ (r — Перлин–Уорли, gba — октавы Уорли), детали 32³ (Уорли 2/4/8
ячеек, a = 1). Вынесен отдельно, чтобы правка `storm.hlsl` не меняла ключ кэша шумов (`storm_noise.hlsl:1-3`). См.
[clouds.md §4.4](clouds.md#44-3d-шумы-storm_noisehlsl).

- Компиляция `Storm.cpp:180-181`; запуск в `Prepare` один раз на устройство: `CSShape` `Dispatch(32,32,32)`
  `Storm.cpp:314`, `CSDetail` `Dispatch(8,8,8)` `Storm.cpp:317`, затем `GenerateMips` (`Storm.cpp:319`).
- Входы (4×4×4): `CSShape` (`:53-65`), `CSDetail` (`:66-71`). Функции `Hash3`, `Hash33`, `Remap`, `WorleyT`, `GradT`,
  `PerlinT` (`:5-50`).
- `u0 uNoise` (`RWTexture3D<unorm float4>`, `:52`); C++ привязывает к `u0` текстуры `sShape` и `sDetail`
  (`Storm.cpp:313, 316`). `common.hlsli` не включается, буферов нет.

### 8.4. `cloudlayer.hlsl` — 2D-слой облаков (вне сборки)

Сфера `R + CloudAlt` с покрытием из `Cloud.tree`, для дальнего вида: `PSLayer` — слой после воздуха, `PSShadow` — тень
до воздуха. Компиляция `CloudLayer.cpp:123-124`; рисование `CloudLayer.cpp:196` (`RenderCloudShadow`), `:225`
(`RenderCloudLayer`). Входы `VS` (`:27-34`), `PSLayer` (`:79-121`), `PSShadow` (`:124-141`). `b1 CloudLayer`
(`CloudLayer.cpp:157`), `b4`; `t0 tCov`, `t1 tDepth`, `t2 tCyc` (карта циклона, читается канал r), `s0`, `s1`
(`CloudLayer.cpp:192-195, 221-224`). См. [clouds.md §5.1](clouds.md#51-2d-слой-cloudlayercpp-cloudlayerhlsl).

### 8.5. `earthclouds.hlsl` — объёмные облака Земли (вне сборки)

Марш на половинном разрешении в оболочке `[R, R + 16.5 км]` по покрытию 2D-слоя и карте циклона. Компиляция
`EarthClouds.cpp:105-106`; марш `:168`, наложение `:175`. Входы `VS` (`:35-42`), `PSMarch` (`:127-212`), `PSComposite`
(`:216-233`). `b1 EarthClouds` (`EarthClouds.cpp:137`), `b4`; `t0 tShape` 64³, `t1 tDepth`, `t2 tCov`, `t3 tCyc`
(`:164-165`), `t4`, `t5` (`:173-174`); `s0`, `s1`. См. [clouds.md §5.2](clouds.md#52-объёмные-облака-земли-по-слою-earthcloudscpp-earthcloudshlsl).

### 8.6. `cyclone.hlsl` — карта циклона (вне сборки)

Квадрат 512² RGBA16F каждый кадр: r — покрытие, g — доля конвекции, b — верх/16 км, a — осадки/30 мм·ч⁻¹. Компиляция
`Cyclone.cpp:67`; рисование `Cyclone.cpp:101` (`CycloneUpdate` ← `CloudLayer.cpp:178`). Входы `VS` (`:25-32`), `PSGen`
(`:173-186`). `b1 Cyc` (`cA`, `cB`, `cC`; `Cyclone.cpp:88-91, 100`); текстур нет, шум хэшевый; `common.hlsli` не
включается. См. [clouds.md §5.3](clouds.md#53-циклон-cyclonecpp-cyclonehlsl).

---

## 9. Струи двигателей, дым, частицы

Все шесть файлов компилирует `FxCompile` и включают `exhaust_common.hlsli`. Проходы `ExhaustFx::Render`
(`Exhaust.cpp:685-743`) окружены сохранением состояния конвейера (`Exhaust.cpp:697`). Общие привязки: `t5` глубина
и `t6` таблица чёрного тела — PS, `Exhaust.cpp:717` (повторно `:722`, снятие `:730`); `s3` — `Exhaust.cpp:718`.
Сводка регистров — [exhaust.md §13.1](exhaust.md#131-регистры).

### 9.1. `exhaust_glow.hlsl` — свечение горячего металла

Коробка вокруг каждого горячего узла у среза сопла; PS восстанавливает точку и нормаль сцены и добавляет тепловое
излучение стенки. См. [exhaust.md §7](exhaust.md#7-свечение-горячего-металла-exhaust_glowhlsl).
Компиляция `Exhaust.cpp:195`; `DrawGlow`, `DrawInstanced(36, n)` `Exhaust.cpp:532` (n ≤ 32). Входы `VS` (`:23-32`),
`PS` (`:34-67`). `b5 Glow` (`GlowCB`, `Exhaust.cpp:78`: `gNode[96]`, `gGlow`, `gVP`; `Exhaust.cpp:531`); `b0`, `b4`
(`ExAirTrans`); `t5`, `t6`, `s3`. Выход аддитивный (`Exhaust.cpp:205-206`).

### 9.2. `exhaust_plume.hlsl` — объёмный факел AtmoFX

Марш луча через коробку струи до глубины сцены: поле газа двух режимов (нерасчётная струя и источниковое течение),
излучение `B(T)·(1 − e^−τ)`, синий континуум водорода. См. [exhaust.md §5](exhaust.md#5-объёмный-факел-exhaust_plumehlsl),
[§6.4](exhaust.md#64-свечение-в-шейдере-синий-континуум), поля буфера — [§13.2](exhaust.md#132-plumecb-exhaustcpp79-exhaust_plumehlsl22-37).
Компиляция `Exhaust.cpp:196`; `DrawPlumes`, `Draw(36)` на струю `Exhaust.cpp:654`. Входы `VS` (`:41-50`), `PS`
(`:155-208`); функции `OH2Pdf` (`:68-78`), `Field` (`:82-151`). `b5 Plume` (`PlumeCB`, `Exhaust.cpp:79`;
`Exhaust.cpp:617`); `b0`, `b4`; `t5`, `t6`, `s3`. Dual-source: `SV_Target0` — излучение, `SV_Target1` — пропускание,
`ONE`/`SRC1_COLOR` (`Exhaust.cpp:207-208`).

### 9.3. `exhaust_shimmer.hlsl` — тепловое марево

Два прохода до тон-маппинга: квады вдоль горячих столбов пишут смещение uv в `R16G16_FLOAT`, затем полноэкранный
проход читает копию сцены со смещением. См. [exhaust.md §8](exhaust.md#8-тепловое-марево).
Компиляция `Exhaust.cpp:197-198`; `DrawShimmer`: `DrawInstanced(6, n)` `Exhaust.cpp:596`, `Draw(3)` `:605`.
Входы `VSProxy` (`:25-42`), `PSProxy` (`:44-65`), `VSFull` (`:70-74`), `PSComposite` (`:76-82`). `b5 Shimmer`
(`ShimCB`, `Exhaust.cpp:80`; `:595`, `:598`); `b0`; `PSProxy` — `t5` (`:586`); `PSComposite` — `t3 tOffset`,
`t4 tScene` (`:603`), `s3`. `PSProxy` — аддитивно в `offRTV`; `PSComposite` — `bsOpaque`.

### 9.4. `exhaust_vessel.hlsl` — объёмные факелы `AddExhaust`

Цилиндр на каждый стандартный факел Orbiter, марш 32 шага до выхода или глубины сцены, плотность и цвет — из
текстуры факела; излучение делится на `gAdapt`. См. [exhaust.md §11](exhaust.md#11-собственные-факелы-кораблей-vesselexhaustcpp).
Компиляция `VesselExhaust.cpp:26`; `RenderVesselExhausts`, `Draw(32*12)` на факел `VesselExhaust.cpp:97` (вызов
`Exhaust.cpp:728`). Входы `VS` (`:24-37`), `PS` (`:41-86`). `b5 Ex` (`CB`, `VesselExhaust.cpp:13`; `:93-94`); `b0`;
`t6 tExTex` — текстура факела (`VesselExhaust.cpp:96`), тот же слот, что `tExBB`; `t5`, `s3` — от `Exhaust.cpp:717-718`.
Предумноженная альфа (`VesselExhaust.cpp:32-33`).

### 9.5. `particles_gpu.hlsl` — GPU-частицы

Пул 65536 частиц (грунт, комья, капли, туман, лёд, конденсат): compute-эмиссия и полёт, рисование
`DrawInstancedIndirect` по списку живых, свет — в VS. См. [exhaust.md §10](exhaust.md#10-gpu-частицы-выброс-грунта-под-струёй-и-конденсат).

- Компиляция `GpuParticles.cpp:74` (`CSInit`, `CSEmit`, `CSSim`), `:78` (`VS`, `PS`).
- Запуск: `CSInit` `:575`, `CSEmit` `:591`, `CSSim` `:601`; `CopyStructureCount` `:605`; `DrawInstancedIndirect` `:668`.
- Входы: `CSInit` (`:67-74`, 256), `CSEmit` (`:97-112`, 64), `CSSim` (`:182-275`, 256), `VS` (`:307-411`), `PS` (`:413-517`).
- Буферы: `b5 Sim` (`SimCB`, `GpuParticles.cpp:13`; CS `:569`); `b6 Count` (`uint4 cDead`; `CopyStructureCount` `:582`,
  CS `:589`); `b5 Draw` (`DrawCB`, `GpuParticles.cpp:14`; VS/PS `:656-657`).
- CS: `u0 uPool`, `u1 uDeadC`/`uDeadA`, `u2 uAlive` (`:573, 585, 599`); `t4 tNew` (`:586`); `t3 tSceneC` всегда null
  (`:588`, `sceneCopy = false`).
- VS: `t3 tAlive`, `t4 tPool` (`:659`); `t7`, `t8`, `t9`, `s2`, `b4` скопированы из PS (`:664-666`), `t13` — `BindLight`.
- PS: `t5` (`:667`). Предумноженная альфа (`:114-115`).

### 9.6. `smoke.hlsl` — дым и конденсат как 3D-поток

Несжимаемый поток на сетке 128×64×128 (Stam 1999), объём света 64×32×64, занятость блоков 8³; рисование — марш с
пропуском пустых блоков; отдельный PS — марево по полю температуры. См.
[exhaust.md §9](exhaust.md#9-дым-и-конденсат-как-3d-поток).

- Компиляция `Smoke.cpp:74-79`: 8 CS, `VSDraw`, `PSDraw`, `PSShimmer`.
- CS через `SmokeFx::Pass` (`Smoke.cpp:111-124`; SRV `t0`–`t4`, UAV `u0`–`u5`), все с `b5` (`:206`) и `s3` (`:207`):

| Вход | Строки | Группа | Вызов | Ресурсы |
|---|---|---|---|---|
| `CSAdvect` | `:49-70` | 8³ | `Smoke.cpp:211` | t0, t1 → u0, u1 |
| `CSCurl` | `:73-84` | 8³ | `:212` | t0 → u2 |
| `CSForces` | `:89-162` | 8³ | `:213` | t0, t1, t2 → u0, u1 |
| `CSDiv` | `:165-179` | 8³ | `:214` | t0 → u3 |
| `CSJacobi` | `:188-198` | 8³ | `:217`, × iters | t3, t4 → u3 |
| `CSProject` | `:200-213` | 8³ | `:219` | t0, t3 → u0 |
| `CSLight` | `:217-239` | 8³ | `:222` | t1 → u4 |
| `CSOcc` | `:244-256` | 4³ | `:223` | t0, t1 → u5 |

- Рисование: `SmokeFx::Render`, `Draw(36)` `Smoke.cpp:242`; `SmokeFx::Shimmer`, `Draw(36)` `:256` (из `Exhaust.cpp:597`).
  Входы `VSDraw` (`:279-287`), `PSDraw` (`:291-341`), `PSShimmer` (`:356-395`).
- `b5 Smoke` (`CB`, `Smoke.cpp:16-22`; `:238`, `:252`). `PSDraw`: `t22 tDenR`, `t23 tLightR`, `t25 tOccR`
  (`Smoke.cpp:239-240`), `s3 sLin3` (`:241`), `t5`; `b0`, `b4`, `t8`, `t13` (`FxBindLight`, `:232`); предумноженная
  альфа (`:92-93`). `PSShimmer`: `t24 tVelR`, `t25 tOccR` (`:253-254`), `s3` (`:255`), `t5`; пишет смещение в `offRTV`.

---

## 10. Неиспользуемые файлы

Поиск по `src\` не находит ни имён этих файлов, ни их точек входа. `install.bat:7` всё равно копирует их в
`Modules\D3D11Client\`.

| Файл | Что | Чем заменён | Регистры |
|---|---|---|---|
| `resolve.hlsl` | `PS` (`:10`): разрешение MSAA цвета HDR с весом Кариса | аппаратный `ResolveSubresource` (`AA.cpp:120`; причина — `AA.cpp:119`) | `t0` `Texture2DMS<float4>`; `b0` |
| `skypano.hlsl` | `VS` (`:13`), `PS` (`:22`): панорама неба над жидкостью | `liquid.hlsl` считает небо через `FxSkyRadiance` | `b1 Pano` (`pUp`, `pE1`, `pE2`); `b0`, `b4`, `t8`, `t9`, `t13` |
| `ssao.hlsl` | `CSAO` (`:28`, 8×8), `PSApply` (`:67`): горизонтный SSAO | `gtao.hlsl` (`Ssao.cpp:1, 33-35`) | `b0 Ssao` (собственный, в слоте `Frame`), `t0`, `t1`, `u0` |

Кроме того, вне сборки — `cloudlayer.hlsl`, `earthclouds.hlsl`, `cyclone.hlsl` ([§8](#8-облака-и-ураган)); собирается,
но не вызывается — `spray.hlsl` ([§7.5](#75-sprayhlsl--карточки-брызг)); работает, но результат не читается —
`fx_waves.hlsl` ([§7.4](#74-fx_waveshlsl--спектральное-поле-волн-fft)).

---

## 11. Замечания по коду

**Пересечения регистров**

- `storm.hlsl:501` `tHistT : t7` совпадает с `tShadow : t7` (`common.hlsli:28`). `Storm.cpp:470` перезаписывает PS `t7`
  историей, `Storm.cpp:483` обнуляет `t0`–`t7`. Карта теней возвращается только перед собственным кораблём
  (`Client.cpp:1069`) и в `VisorDrops.cpp:321, 676`. Проходы между `RenderStorm` (`Client.cpp:1047`) и собственным
  кораблём, которые берут `t7` из PS, получают пустой слот; так, `GpuParticles.cpp:665-666` копирует PS `t7` в VS.
  Как выглядит результат, по коду не ясно.
- `wavefield.hlsl` объявляет в `b0` буфер `Field`; конфликта нет (CS, буфер ставится перед каждым запуском).
- Собственные буферы в `b0` у `bloom`, `hud`, `sketch` восстанавливаются после прохода; у неиспользуемого `ssao.hlsl`
  при возвращении в работу заняли бы `Frame`.
- Несколько объявлений на одном регистре в одном файле (используется одно на вход): `aa.hlsl` `t0`;
  `exhaust_vessel.hlsl:19` `tExTex` и `tExBB` на `t6`; `spray.hlsl:18` `tSheet` и `tExBB` на `t6`; `smoke.hlsl:39`
  `sLin3` и `sExLin` на `s3`; `particles_gpu.hlsl` — `t3`, `t4`, `u1`, два буфера в `b5`.

**Ошибки привязки**

- `rain.hlsl`: VS вызывает `SunLight`/`SkyLight` (`rain.hlsl:64-66`) → `tFxLut` (`t8`), но `BindLight` ставит `t8` только
  в PS (`FxAtmo.cpp:351`), а `Rain.cpp:125` — только `t7`. При активной FX-атмосфере VS читает нули: `FxSunLight` даёт 0,
  от света неба остаётся часть из `t13`.
- `particles_gpu.hlsl` `CSEmit` → `GroundAlbedo` (`:78-95`) обращается к `t3`, `t5`, `b0`, `t7`, `s2`, `b4`, `t8`, `t13`
  в CS, где они не привязаны; ветка сейчас недостижима (`sLiq.w = 0`, `GpuParticles.cpp:558`).
- `exhaust_glow`/`exhaust_plume` не ставят `b4` сами и читают то, что оставили предыдущие проходы (`BindLight` в конце
  `Atmosphere::Render`, `FxAtmo.cpp:580`, и `FxBindLight` последующих проходов).

**Устаревшие комментарии**

- `common.hlsli:31`, `Client.cpp:994`, `Lights.cpp:6`: «t17..t20» — привязывается пять SRV, `t17`–`t21`.
- `exhaust_common.hlsli:2`: «b5, b6, t4..t6, s3 — none of the client's» — эти слоты используют также `mesh` (`b6`),
  `planet` (`t5`, `t6`, `s3`), `liquid` (`b5`, `t5`, `t6`), `visor_drops` (`t5`, `s3`).
- `lights.hlsli:34`: `gSpotK` «w: 0» — фактически номер кадра (`Lights.cpp:168`); `lights_vol.hlsl:7` «12 a light» —
  в коде N = 10 (`:70`).
- `common.hlsli:14`: `gShadow` «x: on, y: texel» — используются также `z` (режим отладки) и `w` (угловой диаметр Солнца).
- `stars.hlsl:5`: «2/W, 2/H» — передаётся 2/RW, 2/RH (`Stars.cpp:42`).
- `clouds.hlsl:22`: `cSize` «size, 1 / size» — C++ передаёт `(w, h, CloudDbg, 1/h)` (`Clouds.cpp:179`);
  `clouds.hlsl:159` «6 steps» — в коде 4 пробы.
- `fx_sky_sh.hlsl:1-2` «every frame» — только в воздухе текущей планеты с готовыми таблицами.
- `resolve.hlsl:1` ссылается на `FinishScene`, который использует `ResolveSubresource`.
- `FxInternal.h:9`, `Fx.cpp:2`: «Liquid.cpp reads it through FxWaves()» — не читает; `Spray.cpp:8` «Fed by
  GpuParticles::SpawnSource» — не питается.

**Заполняемое, но не читаемое**

`aa.hlsl` `gCamDelta`; `hud.hlsl` `gRes`; `planet.hlsl` `tMask` (`t15`) и `gMisc.z`; `mesh.hlsl` `gFlags.y`, интерполянт
`o.liq` и функция `LiqApparentDepth`; `exhaust_glow` `gGlow.y`; `exhaust_vessel` `eToG`, `eP.w`; `exhaust_shimmer`
привязывает `t6`, не читая его; `fx_waves.hlsl` `u2 uImpact` без писателя; `ExViewDir` не вызывается; `cbGrp` не
привязывается; лишние привязки `b9` к VS (`Lights.cpp:642`), `b3` к VS (`FxAtmo.cpp:527`), `b0` к CS в `storm_rain`
(`StormRain.cpp:166`), `t2` и `s0` к VS в `liquid` (`Liquid.cpp:319-320`).

**Прочее**

- `particles.hlsl:21` и `stars.hlsl:25` возвращают цвет, уже умноженный на альфу, при смешении `SRC_ALPHA, ONE`
  (итог × a²); задумано ли так, по коду не ясно.
- `hud.hlsl` и `wavefield.hlsl` компилируются мимо кэша; `ar.hlsl`, `sketch.hlsl`, `hud.hlsl`, `wavefield.hlsl` и все
  шейдеры `FxCompile` — без паузы между шейдерами и без `NoteShader`.
- `fx_waves.hlsl` без `Fx.cfg` или ключа `Waves` включается по умолчанию и выполняет 7 проходов 1024² в кадр впустую.
- `Ar.cpp:52` создаёт буфер на 96 байт, `UpdateSubresource` без рамки (`Ar.cpp:246`) копирует размер буфера из
  80-байтной структуры (`Ar.cpp:235`); выход за конец структуры по коду вероятен, не проверено.
- `Cyclone.cpp:94-102` (вне сборки) не задаёт смешивание, глубину и растеризатор перед `PSGen`.
- `UpdateSkySH` ставит UAV на буфер (`FxAtmo.cpp:333`), который привязан как `t13` с прошлого кадра; среда выполнения
  снимает SRV сама, `BindLight` возвращает его следом.
