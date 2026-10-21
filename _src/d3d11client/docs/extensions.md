# Расширения: прокладка D3D9Client.dll, gcCore, AtmoFX, D3D11Hud

[← оглавление](DEVELOPER.md)

Состояние кода: 21.10.2026

## 1. Прокладка `D3D9Client.dll`

Модули проекта находят расширения графического клиента **по имени модуля** `D3D9Client.dll`:
`gcCoreAPI.h::gcGetCoreInterface()` (`gcCoreAPI.h:1159-1169`), заголовки `Orbitersdk\samples\AtmoFX\AtmoFX*.h` и
`Orbitersdk\include\D3D11Hud.h` вызывают `GetModuleHandle("D3D9Client.dll")` и `GetProcAddress(...)`. Чтобы модули не
менять, клиент загружает DLL с этим именем, которая только переадресует экспорты в `D3D11Client.dll`.

- Исходник: `shim\shim.c` — строки `#pragma comment(linker, "/export:Имя=D3D11Client.Имя")` и пустой `DllMain`.
  Сборка: `cl /LD shim\shim.c … /link /NOENTRY` → `build\<tag>\D3D9Client.dll`, копия в `build\` (`build.bat`).
  Это PE-переадресация (export forwarders): загрузчик Windows разрешает `D3D11Client.Имя` через уже загруженный
  `D3D11Client.dll`; кода в прокладке нет (`DllMain` при `/NOENTRY` не вызывается).
- Список экспортов `shim.c` (16):

  | Экспорт | Реализация |
  |---|---|
  | `gcBindCoreMethod`, `gcGetCoreAPI` | `Ext.cpp:68-86` |
  | `AtmoFX_SetJet`, `AtmoFX_SetWake`, `AtmoFX_SetHeatSource`, `AtmoFX_SetFoam`, `AtmoFX_SetDirt`, `AtmoFX_Emit`, `AtmoFX_Box`, `AtmoFX_Mark`, `AtmoFX_MarkCount`, `AtmoFX_MarkGet`, `AtmoFX_MarkPut` | `Ext.cpp:91-120` |
  | `AtmoFX_HudFrame`, `AtmoFX_HudMarker` | `Ar.cpp:87-104` |
  | `D3D11Hud_GetApi` | `HudApi.cpp:532-537` |

- `shim\shim.def` в сборке не участвует (`build.bat` его не передаёт) и отстаёт: в нём 15 экспортов, нет
  `D3D11Hud_GetApi`. Источник истины — `shim.c`.
- Установка: `Modules\D3D11Client\D3D9Client.dll` (`install.bat`) — не в `Modules\Plugin`, чтобы Launchpad не считал её
  модулем и чтобы не конфликтовать с настоящим D3D9Client.
- Загрузка: `D3D11Client::LoadShim` из `InitModule` после регистрации клиента (`Client.cpp:62`, `Ext.cpp:141-146`). Если
  в процессе уже есть `D3D9Client.dll`, прокладка не грузится. Журнал: `extension shim loaded (gcCore, AtmoFX)` или
  `extension shim NOT loaded`.
- Порядок: модуль, который ищет `D3D9Client.dll` раньше загрузки D3D11Client, его не найдёт. Заголовки AtmoFX (кроме
  `AtmoFXHud.h`, `AtmoFXMark.h::Proc`) повторяют поиск, пока модуль не найден (`looked = (h != nullptr)`); `AtmoFXHud.h`
  запоминает неудачный `GetProcAddress` только при найденном модуле. `gcGetCoreInterface` не кеширует неудачу, но пишет
  `gcGetCoreInterface() FAILED` в журнал. `D3D11Hud::Get()` (`D3D11Hud.h`) ищет один раз и неудачу запоминает.

## 2. gcCore

`gcCore2` в конструкторе запрашивает **все** методы по имени (`gcCoreAPI.h:1000-1054`, 52 имени). Таблица клиента —
`gcBindCoreMethod` (`Ext.cpp:68-84`); на неизвестное имя отдаётся `gc::Null` — `__cdecl`-функция, возвращающая
`nullptr`/0 (`Ext.cpp:60`). Для методов, возвращающих `void`, `bool`, `int`, указатель или хэндл, это безопасно
(аргументы снимает вызывающий). Методы, возвращающие структуру по значению (`ScanScreen`, `GetTileData` → `PickGround`),
имеют собственные заглушки (скрытый указатель на результат). Старый интерфейс `gcGetCoreAPI` возвращает `nullptr`
(`Ext.cpp:86`).

| Метод (порядок `gcCoreAPI.h:1002-1053`) | Статус | Поведение |
|---|---|---|
| `RegisterSwap`, `FlipSwap`, `GetRenderTarget`, `ReleaseSwap` | нет (`Null`) | 0 / `NULL`; дополнительные окна вывода не поддержаны |
| `DeleteCustomCamera`, `CustomCameraOnOff`, `CustomCameraOverlay`, `SetupCustomCamera` | нет (`Null`) | `SetupCustomCamera` → `NULL`, `DeleteCustomCamera` → 0; камер для MFD, зеркал, видов на поверхность нет |
| `SketchpadVersion` | реализовано | 1 (`Ext.cpp:56`) |
| `CreatePoly`, `CreateTriangles`, `DeletePoly` | нет (`Null`) | `NULL`; `Sketchpad::DrawPoly` тоже не переопределён |
| `GetTextLength`, `GetCharIndexByPosition` | нет (`Null`) | 0 |
| `RegisterRenderProc(proc, id, param)` | частично | только `RENDERPROC_HUD_1ST` и `RENDERPROC_HUD_2ND`; `RENDERPROC_DELETE` удаляет все записи с этим `proc`; прочие id (`PLANETARIUM`, `EXTERIOR`, …) → false (`Ext.cpp:44-54`). Обе HUD-группы вызываются в `clbkDisplayFrame` после HUD Orbiter на Sketchpad слоя экрана (`CallRenderProcs`, `Ext.cpp:124-131`) — «под HUD» для `HUD_1ST` не соблюдается |
| `CreateSketchpadFont` | нет (`Null`) | `NULL` |
| `GetMeshMaterial`, `SetMeshMaterial` | нет (`Null`) | 0 — для вызывающего это код успеха, выходной `FVECTOR4` не заполняется. Тот же доступ даёт `oapiSetMaterialEx/oapiMeshMaterialEx` → `clbkSetMeshMaterialEx/clbkMeshMaterialEx` ([orbiter-api.md §4](orbiter-api.md#4-меши-и-визуалы)) |
| `GetMatrix(id, …)` | частично | всегда матрица **группы** `group`; `id` не учитывается (`Ext.cpp:34-42`) |
| `SetMatrix(id, hVessel, mesh, group, M)` | частично | `GROUP` — матрица группы (строчные векторы) **заменяет** её текущее состояние анимации; `MESH` — та же матрица во все группы меша; `OFFSET`, `COMBINED` → −4 (`Ext.cpp:16-32`). Последующие анимации Orbiter умножаются поверх (`Mesh.cpp:120`); `ResetAnimations` сбрасывает в единичную |
| `GetDevMesh`, `LoadDevMeshGlobal`, `ReleaseDevMesh`, `RenderMesh`, `PickMesh`, `RenderLines` | нет (`Null`) | 0 / `NULL` |
| `GetSystemSpecs` | нет (`Null`) | структура вызывающего не заполняется |
| `GetSurfaceSpecs` | нет (`Null`) | false |
| `LoadSurface(fname, flags)` | частично | `clbkLoadTexture(fname, flags)`; флаги не используются (`Ext.cpp:57`) |
| `SaveSurface`, `GetMipSublevel`, `GenerateMipmaps`, `CompressSurface`, `LoadBitmapFromFile` | нет (`Null`) | false / `NULL` |
| `GetRenderWindow` | реализовано | `HWND` окна рендера (`Ext.cpp:14`) |
| `RegisterGenericProc`, `StretchRectInScene`, `ClearSurfaceInScene` | нет (`Null`) | false |
| `ScanScreen` | заглушка | нулевой `PickGround` (`Ext.cpp:58`) |
| `LockSurface`, `ReleaseLock` | нет (`Null`) | false |
| `GetPlanetManager`, `SetTileOverlay`, `AddGlobalOverlay` | нет (`Null`) | `NULL` |
| `GetTileData` | заглушка | нулевой `PickGround` (`Ext.cpp:59`) |
| `GetTile`, `HasTileData`, `SeekTileTexture`, `SeekTileElevation`, `GetElevation` | нет (`Null`) | `NULL` / false / 0 |
| `CreateIPInterface`, `ReleaseIPInterface` | реализовано | `IpiCreate`, `IpiRelease` (`Ipi.cpp:298-307`), см. §2.1 |

Итого: 10 имён в таблице из 52; из них 2 — нулевые заглушки (`ScanScreen`, `GetTileData`).

Доступ к мешу из gcCore: `D3D11Client::VesselMesh(hVessel, idx)` (`Ext.cpp:133-138`) — `DevMesh` визуала или
`nullptr`, если визуала или меша нет.

### 2.1. `gcIPInterface` (`Ipi.cpp`)

Пиксельный шейдер модуля над его поверхностями, как в D3D9Client. Шейдеры модулей написаны для D3D9 (`sampler`,
`tex2D`, `: COLOR`) и компилируются как `ps_4_0` в режиме обратной совместимости; вершинный шейдер собирается под
входы пиксельного. Поверхности читаются и пишутся в гамма-пространстве (UNORM-виды, `SurfGpuViews`,
`Draw.cpp:828-844`). Выполнение — `D3D11Client::RunIPI` (`Ipi.cpp:170`).

| Метод | Статус |
|---|---|
| `CompileShader`, `Activate`, `SetFloat`, `SetInt`, `SetBool` (скаляры и массивы), `SetStruct`, `SetTexture`, `SetOutput`, `SetOutputRegion`, `IsOK`, `Execute` (оба варианта) | реализовано (`Ipi.cpp:251-295`); у `Execute` параметры `bool` и `ipitemplate` не используются |
| `SetVSFloat`, `SetVSInt`, `SetVSBool`, `SetVSStruct` | пустые (`Ipi.cpp:271-277`) |
| `SetMesh` | пустой (`Ipi.cpp:293`) |
| `FindDefine` | 0 (`Ipi.cpp:296`) |

## 3. Экспорты AtmoFX

Сигнатуры — в `Ext.cpp` и `Ar.cpp` (все `extern "C"` через `DLLCLBK`, `__cdecl`); смысл параметров — комментарии в
`Orbitersdk\samples\AtmoFX\AtmoFX*.h` (заголовки написаны для D3D9-форка AtmoFX; D3D11Client принимает те же вызовы).

| Экспорт | Заголовок / назначение | В клиенте | Статус |
|---|---|---|---|
| `AtmoFX_SetJet(h, id, exitPos[3], jetDir[3], thrust, ve, exitT, ground, wet, rExit)` | `AtmoFXJet.h`: струя у грунта или жидкости; позиции в системе корабля, вызывать каждый кадр | запись `g_ext.jets[{h, id}]` с отметкой `t = oapiGetSimTime()` (`Ext.cpp:91-97`); `thrust = 0` запись не удаляет. Читают: `Exhaust.cpp:294-300` (тяга > 0, не старше 0.5 с сим. времени, `oapiIsVessel`), `Wake.cpp:418-419` (полости в жидкости), `FxWaves.cpp:147-148` (FFT-поле, не подключено). См. [exhaust.md §3.1](exhaust.md#31-экспорты-atmofx-extcpp) | реализовано |
| `AtmoFX_SetWake(h, id, pos[3], r, strength)` | `AtmoFXWake.h`: узлы кильватера и колец на жидкости | `WakeSet` (`Ext.cpp:99-102`, `Wake.cpp:173`) — источники поля волн и кильватера Кельвина. `g_ext.wakes` не заполняется. См. [water.md §3.3](water.md#33-узлы-следа-от-модулей-atmofx_setwake) | реализовано |
| `AtmoFX_SetHeatSource(h, id, x, y, z, r, dT)` | `AtmoFXHeat.h`: горячие узлы для марева | `g_ext.heat`; `dT ≤ 0` удаляет (`Ext.cpp:104-110`). Читает `Exhaust.cpp:302-306` (свечение, марево) | реализовано |
| `AtmoFX_SetDirt(h, amount, r, g, b, topY)` | `AtmoFXDirt.h`: грязь на корпусе | `DirtSet` (`Ext.cpp:113-114`, `Mesh.cpp:248-254`); `amount ≤ 0` удаляет; рисует `mesh.hlsl` (`b7`) | реализовано |
| `AtmoFX_Emit(h, id, p, v, cone, spread, kgps, kind, ground, wet, life, jetJ)`, `AtmoFX_Box(h, id, c, half)` | `AtmoFXPart.h`: свободные частицы и область, где они гаснут | `ExhaustEmit`, `ExhaustBox` (`Ext.cpp:115-116`, `Exhaust.cpp:753-757`) → GPU-частицы ([exhaust.md §10](exhaust.md#10-gpu-частицы-выброс-грунта-под-струёй-и-конденсат)) | реализовано |
| `AtmoFX_HudFrame(flags, r, g, b, step)` | `AtmoFXHud.h`: включение шлемного HUD в сцене (изолинии рельефа, метки) на 0.5 с системного времени | `Ar.cpp:87-92`; изолинии — в шейдере планеты, метки — проход `RenderAr` (`Ar.cpp:150`) | реализовано |
| `AtmoFX_HudMarker(id, body, lng, lat, alt, type, size, heading)` | `AtmoFXHud.h`: метка (цель, площадка, ворота глиссады, точка маршрута); `type < 0` удаляет | `Ar.cpp:94-104` | реализовано |
| `AtmoFX_SetFoam` | `AtmoFXFoam.h`: пузыри и пена | пустая функция (`Ext.cpp:112`) | заглушка |
| `AtmoFX_Mark`, `AtmoFX_MarkPut` | `AtmoFXMark.h`: колеи и следы | пустые функции (`Ext.cpp:117`, `:120`); проект — [ruts.md](ruts.md) | заглушка |
| `AtmoFX_MarkCount`, `AtmoFX_MarkGet` | чтение колей для сохранения в сценарий | возвращают 0 (`Ext.cpp:118-119`) | заглушка |

`AtmoFX_Box` заголовок `AtmoFXPart.h:54` ищет по `GetModuleHandleA("D3D9Client.dll")` — с прокладкой это работает так же,
как остальные.

## 4. D3D11Hud

Собственный API рисования для дисплеев модулей (шлемный HUD, MFD скафандра, панели). Заголовок —
`Orbitersdk\include\D3D11Hud.h` (только заголовок, без зависимости при линковке); реализация — `src\HudApi.cpp`,
шейдер — `shaders\hud.hlsl`. Модуль получает таблицу функций через `D3D11Hud::Get()` → экспорт `D3D11Hud_GetApi`
прокладки. Под другим клиентом экспорта нет, `Get()` возвращает `NULL`, модуль остаётся на Sketchpad.

- `D3D11Hud_GetApi(version)` (`HudApi.cpp:532-537`): `version < 1` или нет клиента → `NULL`; иначе таблица `API`
  (`HudApi.cpp:512-519`, `VERSION = 1`, `size = sizeof(Api)`) и строка журнала
  `D3D11Hud: table version 1 handed out (module asked N)`.
- Ресурсы (`Init`, `HudApi.cpp:35-100`) создаются при первом использовании и пересоздаются при смене `g_devGen`.
- Холст — поверхность клиента `OAPISURFACE_TEXTURE | RENDERTARGET | NOMIPMAPS` (+ `ALPHA` для `ALPHA`/`LIGHT`),
  результат `CanvasSurface` — обычный `SURFHANDLE` (для `oapiSetTexture` или блиттинга). Для `LIGHT` и `SUPERSAMPLE`
  есть рабочая текстура RGBA8 (×2 для `SUPERSAMPLE`), сводимая в результат в `End` (`Resolve`, `HudApi.cpp:261-279`).
- Примитивы копятся в вершинный пакет и рисуются в `End`/при смене состояния (`Flush`, `HudApi.cpp:215-258`);
  сглаживание — по точному расстоянию до фигуры; перекрытия одного вызова смешиваются один раз (буфер глубины).

| Функция таблицы | Реализация | Примечание |
|---|---|---|
| `CanvasCreate(w, h, flags)` | `HudApi.cpp:282-300` | флаги `SOLID`, `ALPHA`, `LIGHT`, `SUPERSAMPLE` |
| `CanvasRelease` | `HudApi.cpp:301-307` | отпускает поверхность результата |
| `CanvasSurface` | `HudApi.cpp:308` | ссылку не увеличивает |
| `Begin(c, clear, col)` | `HudApi.cpp:310-326` | сбрасывает состояние; false — если ресурсы не созданы |
| `End(c)` | `HudApi.cpp:327-335` | вывод, сведение, `gpuNewer` |
| `SetBlend`, `SetTransform`, `SetClip`, `SetGlow` | `HudApi.cpp:336-351` | |
| `Polyline`, `Polygon`, `Rect`, `Ellipse`, `Arc` | `HudApi.cpp:353-420` | |
| `Image(c, src, sr, dr, tint, fx)` | `HudApi.cpp:421-435` | `ImageFx`: усиление по каналам, гамма, шум |
| `FontLoad`, `FontRelease`, `TextWidth`, `Text` | `HudApi.cpp:437-498` | шрифты SDF: атлас + таблица (`SDF <em> <spread>`, строки глифов) |
| `ArPath(id, body, lngLatAlt, n, width, col, flags)` | `HudApi.cpp:503-510` | хранится в `arPaths`, рисуется проходом `RenderAr` (`Ar.cpp:212-221`) с глубиной; `n ≤ 0` удаляет |

Все 20 функций таблицы реализованы. Расхождения с заголовком:
- `D3D11Hud.h` предлагает для видов с камер gcCore `SetupCustomCamera`/`CustomCameraOnOff`; в клиенте эти методы не
  реализованы (§2).
- По заголовку пути `ArPath` «остаются до удаления или выгрузки модуля»; очистки при выгрузке модуля или конце сессии
  в коде нет; `id` общий для всех модулей.

## 5. `ExtState` и потребители

`ExtState` (`Client.h:68-82`, экземпляр `g_ext` — `Ext.cpp:9`):

| Поле | Кто пишет | Кто читает |
|---|---|---|
| `jets` — `std::map<{OBJHANDLE, id}, Jet>` | `AtmoFX_SetJet` | `ExhaustFx::Collect` (`Exhaust.cpp:294-300`), `Wake.cpp:418-419`, `FxWaves.cpp:147-148` |
| `wakes` — `std::map<{OBJHANDLE, id}, Wake>` | никто (с переходом `AtmoFX_SetWake` на `WakeSet`) | `FxWaves.cpp:171` (FFT-поле, не подключено) |
| `heat` — `std::map<{OBJHANDLE, id}, Heat>` | `AtmoFX_SetHeatSource` | `ExhaustFx::Collect` (`Exhaust.cpp:302-306`) |
| `renderProcs` — `std::vector<RenderProc>` | gcCore `RegisterRenderProc` | `CallRenderProcs` (`Ext.cpp:124-131`) |
| `mx` — мьютекс | — | защищает `jets`, `wakes`, `heat`; `renderProcs` используется без него (главный поток) |

- При удалении корабля его записи `jets/wakes/heat` удаляются в `clbkDeleteVessel` (`Client.cpp:887-897`), там же
  `WakeForget` (узлы волн). Запись струи с `thrust = 0` не удаляется: потребители проверяют тягу и отметку времени.
- Вне `ExtState` состояние модулей хранят: грязь (`Mesh.cpp:242-254`, map по `OBJHANDLE`, при удалении корабля не
  очищается), метки и изолинии AR (`Ar.cpp:14-28`), пути D3D11Hud (`HudApi.cpp:501-502`), узлы волн (`Wake.cpp`), частицы
  `AtmoFX_Emit` (`Exhaust.cpp`). `clbkCloseSession` ничего из этого не очищает.

## 6. Как добавить метод gcCore или экспорт

- **Метод gcCore.** Функция `__cdecl` в `namespace gc` (`Ext.cpp`) с точной сигнатурой указателя из
  `gcCoreAPI.h:937-988`, строка `{ "Имя", (void*)gc::Имя }` в таблице `gcBindCoreMethod` (`Ext.cpp:70-81`). Если метод
  возвращает структуру по значению, заглушка `Null` для него непригодна — нужна собственная функция. Прокладку менять не
  нужно (`gcBindCoreMethod` уже переадресован).
- **Новый экспорт** (например, `AtmoFX_*`): функция `DLLCLBK` в `Ext.cpp` (или в файле эффекта), строка
  `#pragma comment(linker, "/export:Имя=D3D11Client.Имя")` в `shim\shim.c`, пересборка обеих DLL (`build.bat`),
  `install.bat`. `shim.def` в сборке не участвует.
- **Новая функция D3D11Hud.** Добавлять только в конец `struct Api` (`D3D11Hud.h`) и в конец инициализатора `API`
  (`HudApi.cpp:512-519`): модуль проверяет наличие новых полей по `size`. При несовместимом изменении — увеличить
  `VERSION`.
- Данные, переданные модулем для корабля, удалять в `clbkDeleteVessel` (`Client.cpp:887-897`) — корабли исчезают
  посреди кадра.
