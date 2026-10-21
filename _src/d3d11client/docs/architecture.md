# Архитектура

[← оглавление](DEVELOPER.md)

## 1. Модули и файлы

| Файл | Что внутри |
|---|---|
| `src\Client.h` | Все общие типы: `ClientCfg`, `ExtState`, `Surf`, `PStream`, `DGroup`/`DevMesh`, `VesselVis`, `TreeArchive`, `Tile`, `AtmCfg`, `LiquidCfg`, `Planet`, класс `D3D11Client`, `RotToXM` |
| `src\Client.cpp` | `InitModule`/`ExitModule`, `clbkInitialise`, окно и устройство (`CreateDevice`), компиляция шейдеров (`CreateShaders`), сессия, поток-загрузчик, текстуры/поверхности, создание потоков частиц, меши (материалы, текстуры, группы), визуалы, **кадр** (`clbkRenderScene`, `clbkDisplayFrame`), снимок экрана, экран загрузки (Direct2D), `UpdateFrameCB`, тени (`RenderShadows`) |
| `src\AA.cpp` | Сглаживание: цели сцены под выбранный режим (`CreateSceneTargets`), MSAA-резолв, `CopySceneColour`/`ResolveDepth`, джиттер и TAA (`FinishScene`), тон-маппинг + FXAA (`ToneMapAndAA`) |
| `src\Mesh.cpp` | `DevMesh::Build` из шаблона Orbiter, `VesselVis` (создание мешей, иерархические анимации), `RenderVessel` (+ матрицы прошлого кадра для векторов движения) |
| `src\Planet.cpp` | `.tree`-архивы, `Planet` (атмосфера и жидкость из cfg), загрузка тайла (`Planet::Load`), обход дерева с отсечением и LOD, рисование тайла, чистка |
| `src\Draw.cpp` | Всё 2D: GDI-шрифты/перья/кисти, `GPad` (Sketchpad на HDC), DIB-сторона поверхностей, Blt/Fill, слой экрана, 2D-панели |
| `src\Ext.cpp` | gcCore (`gcBindCoreMethod`), экспорты `AtmoFX_*`, вызов HUD-процедур модулей, загрузка прокладки |
| `src\Liquid.cpp` | Жидкие поверхности (моря Титана); задаёт `g_fxPlanets` для эффектов |
| `src\Fx.h`, `src\Fx.cpp` | Четыре крючка эффектов, которые вызывает клиент (`FxInit`, `FxFrame`, `FxRenderTransparent`, `FxRelease`), и `FxCompile`. В срезе `FxRenderTransparent` рисует атмосферу снаружи, `FxFrame` пуст |
| `src\FxInternal.h`, `src\FxAtmo.cpp` | Общее для эффектов (`g_fxPlanets`, `FxCompile`); атмосфера, видимая из космоса и с высоты (`fx_atmo.hlsl`) |
| `src\Particles.cpp` | `PStream` (эмиссия на CPU), рисование частиц, текстура частицы по умолчанию |
| `src\Stars.cpp` | Звёзды из каталога Orbiter через `oapi::CelestialSphere::LoadStars` |
| `src\Dds.cpp` | Загрузчик DDS (BC1–5, DX10, 32/24/16/8 бит), `ReadFileBytes` |
| `src\VideoTab.cpp` | Вкладка Video в Launchpad (DXGI), диалог Advanced, `ClientCfg::Load/Save` |
| `src\D3D11Client.rc`, `src\resource.h` | Строки 1000/1001 (описание, «Graphics engines»), диалог `IDD_ADVANCED` |
| `shaders\*.hlsl`, `common.hlsli` | См. [shaders.md](shaders.md) |
| `shim\shim.c`, `shim\shim.def` | Прокладка `D3D9Client.dll`, см. [extensions.md](extensions.md) |
| `tools\run_test.ps1` | Тестовый прогон, см. [build-and-test.md](build-and-test.md) |

Глобальные объекты: `g_client` (`Client.cpp`), `g_cfg` (`VideoTab.cpp`), `g_ext` (`Ext.cpp`), `g_fxPlanets` (`Liquid.cpp`).
Логирование — макрос `LOGC` (`Client.h:36`): `oapiWriteLogV("D3D11Client: " …)`.

## 2. Жизненный цикл

```
InitModule                  new D3D11Client; oapiRegisterGraphicsClient; LoadShim()
clbkInitialise              базовый Initialise; g_cfg.Load(); InitVideoTab(); размер окна из VIDEODATA
   … Launchpad: LaunchpadVideoWndProc, clbkRefreshVideoData, OpenAdvanced …
clbkCreateRenderWindow      окно (базовый класс) → «полный экран» = окно без рамки на весь монитор; тестовый режим;
                            CreateDevice (+ CreateSceneTargets); CreateShaders; CreateLiquid; CreateParticleTexture;
                            FxInit; запуск потока-загрузчика
clbkSplashLoadMsg           экран загрузки через Direct2D прямо в swap chain (+ Present)
clbkPostCreation            Planet для каждого OBJTP_PLANET; ScanVessels; CreateStars; loadsteps.txt
   [цикл] clbkRenderScene → (Orbiter рисует HUD/инфо через Sketchpad) → clbkDisplayFrame
clbkCloseSession            очередь загрузки очищается; визуалы: UnregisterVisObject + delete
clbkDestroyRenderWindow     остановка и join загрузчика; delete планет; FxRelease; delete потоков частиц;
                            ClearState; освобождение слоя экрана и swap chain; базовый Destroy
ExitModule                  oapiUnregisterGraphicsClient; delete
```
Всё — `Client.cpp`, кроме `LoadShim` (`Ext.cpp`), `InitVideoTab`/`OpenAdvanced` (`VideoTab.cpp`), `CreateSceneTargets`
(`AA.cpp`), `CreateLiquid` (`Liquid.cpp`), `FxInit`/`FxRelease` (`Fx.cpp`).

## 3. Кадр

`D3D11Client::clbkRenderScene` (`Client.cpp`). Сцена рисуется в HDR-цель `hdrTex` (R16G16B16A16_FLOAT) с глубиной
`depthTex` (R32_TYPELESS → DSV D32_FLOAT); при MSAA обе многовыборочные (`CreateSceneTargets`, `AA.cpp`). Затем
одновыборочное изображение (как есть / резолв / TAA) тон-маппится в back buffer (B8G8R8A8, RTV в sRGB-формате).

| # | Проход | Где | Цель / состояние |
|---|---|---|---|
| 0 | Нет устройства/swap chain или шейдеры не скомпилировались — выход (ничего не рисуется) | `clbkRenderScene` | |
| 1 | `FlushDirty()` — изменённые за прошлый кадр поверхности (MFD, дисплеи) заливаются в их текстуры | `Draw.cpp:178` | |
| 2 | `BeginScreenLayer()` — слой экрана очищается ключевым цветом в прямоугольнике прошлого кадра | `Draw.cpp:318` | CPU (DIB) |
| 3 | каждые 32 кадра `ScanVessels()` (новые корабли → визуал, исчезнувшие → удалить) | `Client.cpp` | |
| 4 | Камера: `oapiCameraGlobalPos`, `oapiCameraRotationMatrix`, `oapiCameraAperture` → проекция; копия VP без джиттера (`curVPNoJit`); при TAA — субпиксельный сдвиг проекции `Jitter()` (Halton 2,3 по 8 кадрам) | `clbkRenderScene`, `AA.cpp` | |
| 5 | `FxFrame(...)` — вычислительная работа эффектов до сцены (в срезе пусто) | `Fx.cpp` | |
| 6 | **Тени** `RenderShadows` — карта 2048² от Солнца вокруг фокус-объекта | `Client.cpp` | `shadowDSV`, прямой Z |
| 7 | Очистка HDR (чёрный) и глубины (**0.0** — обратный Z), сэмплеры s0/s1/s2, карта теней в t7 | `clbkRenderScene` | `hdrRTV` + `dsv` |
| 8 | `UpdateFrameCB` — константы кадра b0 (Солнце, атмосфера под камерой, экспозиция, тень) | `Client.cpp` | |
| 9 | **Небо** — полноэкранный треугольник, только если камера внутри атмосферы (`skyOn`) | `clbkRenderScene` | `dsRead` |
| 10 | **Звёзды** `RenderStars` — аддитивно, при `starVis ≥ 0.01` | `Stars.cpp:36` | `bsAdd`, `dsRead` |
| 11 | **Планеты** от дальней к ближней (`Planet::Render`), затем `Purge` | `Planet.cpp:293` | `bsOpaque`, `dsWrite`, `rsNone` |
| 12 | **Корабли** — непрозрачные группы всех визуалов (при TAA вторым RT пишутся вектора движения `mvRTV`), затем прозрачные | `RenderVessel` (`Mesh.cpp`) | `dsWrite` / `dsRead` |
| 13 | **Жидкость** `RenderLiquid` — сначала `CopySceneColour` (копия/резолв цвета + одновыборочная глубина) | `Liquid.cpp`, `AA.cpp` | read-only DSV |
| 14 | `FxRenderTransparent(...)` — в срезе: **атмосфера снаружи** `FxAtmoRender` (полноэкранно на каждую планету в кадре, dual-source blending, глубина как текстура) | `Fx.cpp`, `FxAtmo.cpp` | без DSV |
| 15 | **Частицы** `UpdateRenderParticles` — обновление на CPU и рисование (сначала диффузные, потом излучающие) | `Particles.cpp:81` | `bsAlpha`/`bsAdd`, `dsRead` |
| 16 | `FinishScene` — одновыборочное изображение: как есть / MSAA-резолв / TAA (история, репроекция) | `AA.cpp` | `outTex[0..1]` |
| 17 | `ToneMapAndAA` — тон-маппинг `post.hlsl` (экспозиция) в back buffer; при FXAA — в LDR-текстуру, затем FXAA в back buffer | `AA.cpp` | `bbRTV` (sRGB) / `ldrRTV` → `bbRTVLin` |
| 18 | `Render2DOverlay()` (метод `oapi::GraphicsClient`, `GraphicsAPI.h:1639`) → Orbiter вызывает `clbkRender2DPanel` | `Draw.cpp:392` | `bbRTV` |
| 19 | GPU-таймстемпы (кольцо из 4 кадров, чтение с отставанием 3 кадра), строка статистики раз в 10 с | `clbkRenderScene` | |
| — | *После возврата:* Orbiter рисует HUD, инфо-блоки, стеклянные MFD через `clbkGetSketchpad(NULL)` → слой экрана | `Draw.cpp:202` | CPU (DIB) |
| 20 | `clbkDisplayFrame`: в тестовом режиме — снова спрятать окно | `Client.cpp` | |
| 21 | `CallRenderProcs()` — HUD-процедуры модулей (gcCore `RegisterRenderProc`) на слой экрана | `Ext.cpp:118` | |
| 22 | `CompositeScreenLayer()` — DIB → текстура (только изменённый прямоугольник) → альфа-смешение поверх back buffer | `Draw.cpp:332` | `bsAlpha` |
| 23 | Через 15 с после старта — `shots\start.png` | `clbkDisplayFrame` | |
| 24 | `Present(1)`, или `Present(0)` при выключенном vsync и в тестовом режиме | `clbkDisplayFrame` | |

Замечания:
- Таймеры в строке статистики: `planets` = отрезок 0→1 (тени, небо, звёзды, планеты), `vessels` = 1→2
  (корабли, жидкость, эффекты, частицы), `post` = 2→3 (резолв/TAA, тон-маппинг, FXAA, 2D-панели). В логе прогона 19.10
  (до сглаживания) `post` ≈ 11–16 мс при 60 fps — для одного полноэкранного прохода это много; откуда эти миллисекунды
  (ожидание vsync/Present, попадающее в интервал, или что-то ещё) — по коду не ясно, не проверялось.
- Карта теней меняет вьюпорт; `clbkRenderScene` выставляет вьюпорт окна после теней.
- `Render2DOverlay` должен вызываться в конце `clbkRenderScene` — так требует SDK (`GraphicsAPI.h:1586-1591`).
- Режим сглаживания (`AntiAliasing` в `Client.cfg`) применяется только при создании окна: 0 выкл., 1–3 MSAA 2/4/8×
  (с понижением числа выборок, если формат его не поддерживает), 4 FXAA, 5 TAA. Вектора движения есть только у
  непрозрачных кораблей; остальное TAA репроецирует через глубину относительно ближайшего тела (`FinishScene`).

## 4. Системы координат и матрицы

**Глобальная система Orbiter, сдвинутая в камеру.** Все позиции считаются в `double` в глобальной системе и
переводятся во `float` только после вычитания позиции камеры (`oapiCameraGlobalPos`):
- корабль: `rel = gp - cp`, смещение меша `mul(R, ofs)` (`Mesh.cpp:211, 240-243`);
- тайл: вершины хранятся относительно центра тайла в системе планеты, сдвиг центра от камеры считается в double:
  `tc = mul(Rp, t->centre) + Pp - camGlobal` (`Planet.cpp:385`);
- жидкость: сфера строится в вершинном шейдере из малых величин (база под камерой + направления),
  без вычитания чисел планетарного масштаба (`liquid.hlsl:23-38`);
- частицы: `p.pos - cp` на CPU (`Particles.cpp:97`).
Поэтому в шейдерах «мировая» позиция `wp` — это вектор от камеры, а `length(wp)` — расстояние до камеры.

**Строчные векторы.** DirectXMath и шейдеры используют `v' = v · M`. Матрицы перед записью в константный буфер
транспонируются (`XMMatrixTranspose`), а HLSL умножает `mul(float4(p,1), M)` при упаковке column-major по умолчанию —
итог эквивалентен строчному соглашению на CPU.

**`RotToXM(R)`** (`Client.h:478`): матрица поворота Orbiter `MATRIX3` действует на столбцы
(`v_global = R · v_local`); для строчного вектора нужна `Rᵀ` — `RotToXM` кладёт столбцы `R` в строки XMMATRIX.

**Вид.** `V` собирается из `cr = oapiCameraRotationMatrix` построчно (`Client.cpp:578`) — это `crᵀ` в строчном
соглашении, т.е. перевод из глобальных осей в оси камеры: x вправо, y вверх, z вперёд (левая система, как у Orbiter;
см. комментарий `Planet.cpp:339`).

**Проекция: обратный Z, бесконечная дальняя плоскость** (`Client.cpp:556-562`):
```
tanY = tan(oapiCameraAperture())          // апертура Orbiter = половина вертикального угла
P = | 1/(tanY·W/H)  0       0  0 |
    | 0             1/tanY  0  0 |
    | 0             0       0  1 |
    | 0             0       n  0 |        n = 0.1 м
clip = (x·a, y·b, n, z)  →  depth = n / z  (1 на ближней плоскости, → 0 на бесконечности)
```
Глубина очищается в 0, тест `GREATER_EQUAL` (`Client.cpp:117, 573`). Один буфер D32F покрывает всё от 0.1 м до
планет, поэтому планеты и корабли не разделяются на отдельные проходы глубины; порядок «дальние планеты первыми» нужен
только для раннего отсечения. Небо и звёзды рисуются при `z = 0` (бесконечность) с `dsRead`. Восстановление позиции
по глубине — через `gInvVP` (b0) или `gLInvVP` (жидкость).

**Система планеты.** `oapiGetRotationMatrix(hPlanet)` и `oapiGetGlobalPos` дают `global = Rp · local + Pp`.
Точка на широте/долготе: `(r·cos lat·cos lng, r·sin lat, r·cos lat·sin lng)` (`Planet.cpp:236, 245`).

**Тени** — отдельная, обычная (не обратная) глубина: `XMMatrixLookToLH` от точки «фокус + 2000 м к Солнцу»,
ортопроекция `2rs × 2rs`, `rs = clamp(4·size(focus), 25, 400)` м, глубина 1…4000 м; очистка в 1.0, тест `LESS_EQUAL`,
depth bias 200 + slope 2.0 (`Client.cpp:156-171, 773-807`). Матрица `lightVP` переводит из сдвинутой в камеру глобальной
системы в пространство карты; PCF 3×3 в `common.hlsli:20` (`ShadowAt`).

## 5. Потоки

| Поток | Что делает |
|---|---|
| Главный (поток Orbiter) | Все `clbk*`, весь `ID3D11DeviceContext`, GDI, AtmoFX/gcCore-вызовы модулей |
| Загрузчик тайлов `LoaderMain` (`Client.cpp:315`) | Чтение файлов/`.tree` (+ `oapiInflate`), разбор `.elv`, DDS → текстура (`dev->CreateTexture2D` с начальными данными), вершинный буфер тайла |

- `ID3D11Device` свободно-поточный, контекст — нет. Поэтому загрузчик создаёт только ресурсы с начальными данными и
  сам строит mip-цепочку несжатых текстур на CPU (`Dds.cpp:140-167`) — генерировать мипы на GPU он не может.
- Очередь `loadQ` под `loadMx` + `loadCv`. Из очереди берётся тайл **самого грубого уровня** (линейный поиск,
  `Client.cpp:323-326`): ребёнку нужны высоты родителя.
- Состояние тайла `Tile::state` (`std::atomic<int>`, `Client.h:175`): 0 новый → 1 в очереди (CAS в `QueueTile`,
  `Client.cpp:310`) → 2 готов / 3 ошибка (`Planet.cpp:284-287`). Поля `tex/elev/vb` пишутся загрузчиком до записи
  `state = 2`; главный поток читает их только после того, как увидел 2.
- Дети ставятся в очередь только из `Traverse` уже готового (state 2) родителя (`Planet.cpp:326, 362`), так что у
  загрузчика высоты родителя всегда есть.
- `Purge` (каждые 64 кадра, тайлы без использования > 600 кадров) не трогает поддерево, где есть тайл в состоянии 1
  (`Planet.cpp:408-424`).
- Остановка: `clbkCloseSession` очищает очередь; `clbkDestroyRenderWindow` ставит `loaderRun=false`, будит и
  `join`-ит поток до удаления планет (`Client.cpp:288-294`).
- Общие структуры загрузчика: `fileCache` под `fileMx`; `TreeArchive::Read` под мьютексом архива; `toc` после `Open`
  только читается (так что `Has/Index` из главного потока без блокировки безопасны).
- Мелочь: `Planet::tilesLoaded++` выполняется в потоке загрузчика без атомарности (`Planet.cpp:286`); счётчик нигде не
  выводится.
- Потокобезопасность `oapiInflate` из чужого потока в SDK не оговорена; на практике работает (используется для всех
  `.tree`).
- `g_ext.mx` защищает карты AtmoFX (`jets/wakes/heat`); `renderProcs` без блокировки (меняются и читаются из главного потока).

## 6. Владение ресурсами

**`Surf` (SURFHANDLE)** — `Client.h:71`.
- Счётчик `refs` начинается с 1. `clbkIncrSurfaceRef` — `++`; `clbkReleaseSurface`/`clbkReleaseTexture` — `--`,
  при 0 удаляется из `texCache` и из `dirty`, затем `delete` (`Client.cpp:366-377`).
- Текстуры из файлов кешируются по имени файла (`texCache`); повторная загрузка того же имени отдаёт тот же `Surf` с `refs++`
  (`Client.cpp:346-347`). Такие `Surf` помечены `immutable` (GPU-текстура IMMUTABLE).
- DIB-сторона (GDI) создаётся лениво при первом Sketchpad/GetDC/Blt (`EnsureDIB`) и живёт до удаления `Surf`.
  Для текстур из файла пиксели один раз читаются обратно с GPU (`Readback`, `Draw.cpp:135`), и при первой заливке
  IMMUTABLE-текстура заменяется на DEFAULT (`Draw.cpp:191-198`).

**`DevMesh` и текстуры.** `DevMesh::Build` берёт **собственную ссылку** на каждую текстуру шаблона
(`Mesh.cpp:11-12`) и отпускает их в `~DevMesh` (`Mesh.cpp:51-54`). Причина: шаблон (или временная копия из
`CopyMeshFromTemplate`, удаляемая сразу после сборки, `Mesh.cpp:86-91`) может освободить текстуры раньше, чем меш
корабля. `clbkSetMeshTexture` берёт ссылку на новую и отпускает старую (`Client.cpp:420-421`).
Слот `tex[0]` — «нет текстуры»; группа хранит слот = индекс Orbiter + 1.

**`VesselVis`.** Создаётся в `ScanVessels` и регистрируется `RegisterVisObject` (`Client.cpp:527-532`). Исчезнувший
из симуляции корабль удаляется без `UnregisterVisObject` (`Client.cpp:535`) — к этому моменту Orbiter объект уже удалил;
нужно ли всё же вызывать Unregister, по SDK не ясно. В `clbkCloseSession` все визуалы снимаются с регистрации и удаляются.

**`PStream`.** Создаётся клиентом в `clbkCreate*Stream`, кладётся в `streams` и отдаётся Orbiter. Клиент же и удаляет:
поток, который корабль отсоединил (`level == nullptr`), удаляется, когда у него не осталось частиц
(`PStream::Expired`, `Client.h:103`; `Particles.cpp:83-88`). Остальные удаляются в `clbkDestroyRenderWindow`
(«корабли к этому моменту уже удалены», `Client.cpp:296`). Текстура потока `spec.tex` используется без собственной ссылки
(`Particles.cpp:131`) — её время жизни на совести модуля.

**Планеты и тайлы.** `Planet` создаются в `clbkPostCreation`, удаляются в `clbkDestroyRenderWindow` после остановки
загрузчика. Тайлы создаются по требованию (`Planet::Child`), освобождаются `Purge`. Высоты — `shared_ptr<vector<float>>`.

**D3D-объекты** — `ComPtr`. Шейдеры компилируются один раз при создании окна; при ошибке компиляции `shadersOk=false`,
текст ошибки в `Orbiter.log`, сцена не рисуется (`Client.cpp:80-81, 213, 545`).

**Слой экрана** (`screen`) — `Surf` без GPU-текстуры (только DIB), создаётся лениво в `BeginScreenLayer`; отдельная
текстура `screenTex` для композиции. Освобождается в `clbkDestroyRenderWindow`. Размер — `W×H` окна на момент создания;
смена размера окна во время сессии не обрабатывается (см. [roadmap.md](roadmap.md)).
