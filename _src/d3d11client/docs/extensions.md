# Расширения: gcCore, AtmoFX, прокладка D3D9Client.dll

[← оглавление](DEVELOPER.md)

## 1. Зачем прокладка `D3D9Client.dll`

Модули проекта через заголовки `Orbitersdk\samples\AtmoFX\AtmoFX*.h`, а также
`gcCoreAPI.h::gcGetCoreInterface()` (`gcCoreAPI.h:1159-1169`) ищут расширения клиента **по имени модуля**:
`GetModuleHandle("D3D9Client.dll")` + `GetProcAddress(...)`. Чтобы ни один модуль не пришлось менять, клиент сам грузит
DLL с этим именем, которая только переадресует экспорты в `D3D11Client.dll`.

- Исходник: `shim\shim.c` — только `#pragma comment(linker, "/export:Имя=D3D11Client.Имя")` для 15 экспортов и пустой
  `DllMain`. Сборка: `cl /LD shim\shim.c … /link /NOENTRY` → `build\D3D9Client.dll` (`build.bat:13`).
  Это PE-переадресация (export forwarders): загрузчик Windows разрешает `D3D11Client.Имя` через уже загруженный
  `D3D11Client.dll`; своего кода в прокладке нет.
- `shim\shim.def` содержит тот же список, но **в сборке не используется** (`build.bat` его не передаёт) — держать в
  синхроне с `shim.c` или удалить; при добавлении экспорта править `shim.c`.
- Установка: `Modules\D3D11Client\D3D9Client.dll` (`install.bat:9`) — **не** в `Modules\Plugin`, чтобы Launchpad не
  считал её модулем и чтобы не конфликтовать с настоящим D3D9Client в `Modules\Plugin`.
- Загрузка: `D3D11Client::LoadShim` из `InitModule` после регистрации клиента (`Client.cpp:23`, `Ext.cpp:135-140`).
  Если в процессе уже есть настоящий `D3D9Client.dll`, прокладка не грузится. В лог: `extension shim loaded (gcCore, AtmoFX)`.
- Порядок важен: модуль, который ищет `D3D9Client.dll` в своём `InitModule` раньше, чем загрузился D3D11Client, его не
  найдёт. Заголовки AtmoFX повторяют поиск, пока модуль не найден (`looked = (h != nullptr)`), так что для них это не
  проблема; `gcGetCoreInterface` не кеширует неудачу, но пишет `FAILED` в лог.

## 2. gcCore

Таблица `gcBindCoreMethod` (`Ext.cpp:62-76`). `gcCore2` при создании запрашивает **все** методы по имени
(`gcCoreAPI.h:1000-1054`); на неизвестное имя отдаётся `gc::Null` — функцию `__cdecl`, возвращающую 0/NULL
(`Ext.cpp:58`). Это безопасно для методов, возвращающих `void`, `bool`, `int`, указатели и хэндлы; методы, возвращающие
структуру по значению (`PickGround`), требуют своей заглушки (скрытый указатель на результат) — для обоих такие есть.
`gcGetCoreAPI` (старый интерфейс) возвращает `nullptr` (`Ext.cpp:78`).

| Метод | Состояние | Поведение |
|---|---|---|
| `GetRenderWindow` | ✅ | `HWND` окна рендера |
| `SetMatrix(id, hVessel, mesh, group, M)` | ◐ | `GROUP` — матрица группы (строчные векторы) **заменяет** её текущее состояние анимации; `MESH` — та же матрица во все группы меша; `OFFSET`, `COMBINED` → −4 (`Ext.cpp:14-30`). Последующие анимации Orbiter умножаются поверх (`Mesh.cpp:117`); `ResetAnimations` сбрасывает в единичную |
| `GetMatrix(id, …)` | ◐ | всегда возвращает матрицу **группы** `group`, `id` не учитывается (`Ext.cpp:32-40`) |
| `RegisterRenderProc(proc, id, param)` | ◐ | только `RENDERPROC_HUD_1ST` и `RENDERPROC_HUD_2ND`; `RENDERPROC_DELETE` удаляет по `proc`; прочие id (`PLANETARIUM`, `EXTERIOR`…) → false (`Ext.cpp:42-52`). Обе HUD-группы вызываются в `clbkDisplayFrame` **после** HUD Orbiter, на Sketchpad слоя экрана (`Ext.cpp:118-125`) — «под HUD» для `HUD_1ST` не соблюдается |
| `SketchpadVersion` | ✅ | 1 (Sketchpad v2 не поддержан) |
| `LoadSurface(fname, flags)` | ◐ | `clbkLoadTexture(fname, flags)`; флаги игнорируются |
| `ScanScreen`, `GetTileData` | ◻ | нулевой `PickGround` |
| `RegisterSwap`, `FlipSwap`, `GetRenderTarget`, `ReleaseSwap` | ⛔ | 0 |
| `SetupCustomCamera`, `DeleteCustomCamera`, `CustomCameraOnOff`, `CustomCameraOverlay` | ⛔ | 0 (камеры для MFD/зеркал нет) |
| `CreatePoly`, `CreateTriangles`, `DeletePoly`, `GetTextLength`, `GetCharIndexByPosition`, `CreateSketchpadFont` | ⛔ | 0 |
| `GetMeshMaterial`, `SetMeshMaterial` | ⛔ | 0 — ⚠ для вызывающего 0 может выглядеть как «успех», а выходной `FVECTOR4` не заполнен |
| `GetDevMesh`, `LoadDevMeshGlobal`, `ReleaseDevMesh`, `RenderMesh`, `PickMesh`, `RenderLines` | ⛔ | 0 |
| `GetSystemSpecs`, `GetSurfaceSpecs` | ⛔ | 0 — ⚠ структура вызывающего не заполняется |
| `SaveSurface`, `GetMipSublevel`, `GenerateMipmaps`, `CompressSurface`, `LoadBitmapFromFile` | ⛔ | 0 |
| `RegisterGenericProc`, `StretchRectInScene`, `ClearSurfaceInScene`, `LockSurface`, `ReleaseLock` | ⛔ | 0 |
| `GetPlanetManager`, `SetTileOverlay`, `AddGlobalOverlay`, `GetTile`, `HasTileData`, `SeekTileTexture`, `SeekTileElevation`, `GetElevation` | ⛔ | 0 |
| `CreateIPInterface`, `ReleaseIPInterface` | ⛔ | 0 |

Доступ к мешу: `D3D11Client::VesselMesh(hVessel, idx)` (`Ext.cpp:127-132`) — `DevMesh` из визуала, `nullptr`, если
визуала или меша нет.

## 3. Экспорты AtmoFX, `D3D11Hud_GetApi` и `ExtState`

Сигнатуры — `Ext.cpp` (все `extern "C"`, `__cdecl`); смысл параметров — комментарии в
`Orbitersdk\samples\AtmoFX\AtmoFX*.h` (заголовки написаны для D3D9-форка AtmoFX; D3D11Client принимает те же вызовы).
Состояние на 21.10.2026.

| Экспорт | Заголовок / назначение | В клиенте |
|---|---|---|
| `AtmoFX_SetJet(h, id, exitPos[3], jetDir[3], thrust, ve, exitT, ground, wet, rExit)` | `AtmoFXJet.h`: струя у грунта или жидкости; позиции в системе корабля, вызывать каждый кадр, `thrust = 0` убирает, не обновлённое 0.5 с сим. времени отбрасывается | запись `g_ext.jets[{h,id}]` с отметкой `t = oapiGetSimTime()`. Читают: `Exhaust.cpp` (факел, марево, выброс грунта), `Wake.cpp` (полости в жидкости), `FxWaves.cpp`. См. [exhaust.md](exhaust.md), [water.md](water.md) |
| `AtmoFX_SetWake(h, id, pos[3], r, strength)` | `AtmoFXWake.h`: кильватер и кольца на жидкости | `WakeSet` (`Wake.cpp`): узлы источников волн жидкости. `g_ext.wakes` больше не заполняется. См. [water.md](water.md) |
| `AtmoFX_SetHeatSource(h, id, x, y, z, r, dT)` | `AtmoFXHeat.h`: горячие узлы для марева | `g_ext.heat`; `dT ≤ 0` удаляет. Читает `Exhaust.cpp` (марево) |
| `AtmoFX_SetDirt(h, amount, r, g, b, topY)` | `AtmoFXDirt.h`: грязь на корпусе | `DirtSet` (`Mesh.cpp`) |
| `AtmoFX_Emit(...)`, `AtmoFX_Box(...)` | `AtmoFXPart.h`: свободные частицы и область, где они гаснут | `ExhaustEmit`, `ExhaustBox` (GPU-частицы, [exhaust.md](exhaust.md)) |
| `AtmoFX_HudFrame`, `AtmoFX_HudMarker` | `AtmoFXHud.h`: шлемный HUD в сцене (изолинии рельефа, метки) | `Ar.cpp` |
| `AtmoFX_SetFoam` | `AtmoFXFoam.h`: пузыри и пена | ◻ пусто |
| `AtmoFX_Mark`, `AtmoFX_MarkPut` | `AtmoFXMark.h`: колеи и следы | ◻ пусто (проект — [ruts.md](ruts.md)) |
| `AtmoFX_MarkCount`, `AtmoFX_MarkGet` | чтение колей для сохранения в сценарий | ◻ возвращают 0 |
| `D3D11Hud_GetApi(version)` | `D3D11Hud.h`: таблица функций собственного HUD шлема | `HudApi.cpp` |

Все экспорты переадресованы прокладкой (`shim\shim.c`).

`ExtState` (`Client.h`): `jets`, `wakes`, `heat` — `std::map` по ключу `{OBJHANDLE, id}`, под `g_ext.mx`;
`renderProcs` — вектор HUD-процедур gcCore. При удалении корабля его записи `jets/wakes/heat` удаляются
(`Client.cpp`, удаление визуала). Запись струи с `thrust = 0` не удаляется: потребители проверяют тягу и отметку
времени сами.

Экспорт `AtmoFX_Box` заголовок `AtmoFXPart.h:54` ищет только по `GetModuleHandleA("D3D9Client.dll")` — с
прокладкой это работает так же, как остальные.

## 4. Как добавить экспорт или метод gcCore

- **Метод gcCore:** функция `__cdecl` в `namespace gc` (`Ext.cpp`) с точной сигнатурой указателя из
  `gcCoreAPI.h:937-987`, строка в таблице `gcBindCoreMethod`. Прокладку менять не нужно (`gcBindCoreMethod` уже
  переадресован).
- **Новый экспорт** (например, новый `AtmoFX_*`): `DLLCLBK` в `Ext.cpp`, строка `#pragma comment(linker, "/export:…")` в
  `shim\shim.c` (и в `shim.def`, если его оставить), пересобрать обе DLL, `install.bat`.
