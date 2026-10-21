# Отображение на API Orbiter

[← оглавление](DEVELOPER.md)

Источник требований — `Orbitersdk\include\GraphicsAPI.h` (класс `oapi::GraphicsClient`), `OrbiterAPI.h`,
`VesselAPI.h`, `DrawAPI.h` (`oapi::Sketchpad`), `CelSphereAPI.h`. Объявления переопределений клиента — `Client.h:245-414`.

Обозначения: ✅ реализовано; ◐ частично / упрощённо; ⛔ не переопределено (работает поведение SDK по умолчанию);
◻ переопределено заглушкой.

## 1. Модуль, окно, Launchpad

| clbk / точка входа | Orbiter ждёт (SDK) | Поведение клиента | |
|---|---|---|---|
| `InitModule` / `ExitModule` | создать клиент, `oapiRegisterGraphicsClient` | создаём, регистрируем, грузим прокладку `D3D9Client.dll` (`Client.cpp:19-29`) | ✅ |
| `clbkInitialise` | подготовка до Launchpad; базовая версия обязательна | базовая + `g_cfg.Load()`, `InitVideoTab()`, размер окна из `VIDEODATA` (`Client.cpp:34-44`) | ✅ |
| `clbkUseLaunchpadVideoTab` | true — показывать вкладку Video | `true` (`Client.h:249`) | ✅ |
| `LaunchpadVideoWndProc` | обработка сообщений вкладки | полный экран/окно, смена адаптера → список режимов, кнопка «Advanced» (`VideoTab.cpp:165-178`) | ✅ |
| `clbkRefreshVideoData` | записать выбор в `VIDEODATA` | адаптер, режим, fullscreen, novsync, размеры окна (`VideoTab.cpp:151-163`) | ✅ |
| `clbkFullscreenMode` | true в полноэкранном режиме | `VIDEODATA::fullscreen` (`Client.h:246`) | ✅ |
| `clbkGetViewportSize` | размер области рендера | `W×H` клиентской области (`Client.h:247`) | ✅ |
| `clbkGetRenderParam` | `RP_*` | 32-бит цвет, 32-бит Z, 0 трафарет, 8 источников, TL=1, pow2 не нужен (`Client.cpp:46-57`) | ✅ |
| `clbkCreateRenderWindow` | создать окно и устройство | окно — базовым классом; «полный экран» = окно без рамки на весь монитор (настоящего exclusive fullscreen нет); устройство, шейдеры, ресурсы, загрузчик (`Client.cpp:60-90`) | ✅ |
| `clbkPostCreation` | доинициализация после загрузки сцены | планеты, визуалы кораблей, звёзды (`Client.cpp:264-274`) | ✅ |
| `clbkSplashLoadMsg` | показать сообщение загрузки, вернуть true если показано | Direct2D-экран с полосой прогресса, ожидаемое число шагов из `loadsteps.txt` (`Client.cpp:695-719`) | ✅ |
| `clbkCloseSession` | конец сессии | очистка очереди тайлов, снятие визуалов (`Client.cpp:276-284`) | ✅ |
| `clbkDestroyRenderWindow` | разрушить окно и 3D-ресурсы; базовая версия обязательна | см. [architecture.md §2](architecture.md#2-жизненный-цикл) (`Client.cpp:286-304`) | ✅ |
| `clbkRenderScene` | нарисовать сцену, в конце вызвать `Render2DOverlay` | см. [architecture.md §3](architecture.md#3-кадр) | ✅ |
| `clbkDisplayFrame` | показать кадр | HUD-процедуры модулей, композиция слоя экрана, снимок, `Present` (`Client.cpp:647-661`) | ✅ |
| `clbkUpdate(bool running)` | покадровое уведомление (и на паузе) | — | ⛔ |
| `clbkOptionChanged`, `clbkDebugString`, `clbkPreOpenPopup` | уведомления | — | ⛔ |
| `RenderWndProc` | сообщения окна рендера | базовая версия (смена размера окна не обрабатывается нами) | ⛔ |

## 2. Текстуры и поверхности

| clbk | Orbiter ждёт | Поведение клиента | |
|---|---|---|---|
| `clbkLoadTexture(fname, flags)` | загрузить текстуру | DDS из `Textures2\`, затем `Textures\`, затем путь как есть; sRGB; кеш по имени (`Client.cpp:342-357`). `flags` игнорируются | ◐ |
| `clbkLoadSurface(fname, attrib, bPath)` | загрузить поверхность с атрибутами | то же, что `clbkLoadTexture(fname, 0)`; `attrib`/`bPath` игнорируются (`Client.cpp:359-362`) | ◐ |
| `clbkReleaseTexture` | освободить | = `clbkReleaseSurface` (`Client.cpp:364`) | ✅ |
| `clbkCreateSurfaceEx(w,h,attrib)` | поверхность с атрибутами `OAPISURFACE_*` | BGRA8 sRGB, SRV + RTV; из атрибутов используется только `OAPISURFACE_ALPHA` (при заливке DIB) (`Client.cpp:333-340, 386`) | ◐ |
| `clbkCreateSurface(w,h,tpl)` | поверхность | как Ex с attrib 0; шаблон игнорируется (`:384`) | ◐ |
| `clbkCreateTexture(w,h)` | текстура | как Ex с `OAPISURFACE_ALPHA` (`:385`) | ✅ |
| `clbkCreateSurface(HBITMAP)` | поверхность из битмапа | не переопределён. По SDK базовая версия создаёт поверхность размера битмапа и копирует его через `clbkCopyBitmap` (`GraphicsAPI.h:1105-1114`) | ⛔ |
| `clbkIncrSurfaceRef` / `clbkReleaseSurface` | счётчик ссылок | ✅ (`Client.cpp:366-377`) | ✅ |
| `clbkGetSurfaceSize` | размер | ✅ (`Client.cpp:379-384`) | ✅ |
| `clbkSetSurfaceColourKey` | ключевой цвет | запоминается, используется в `Blt` с `BLT_SRCCOLORKEY` (`Draw.cpp:269-275, 283-284`) | ✅ |
| `clbkGetDeviceColour` | упаковка RGB | по умолчанию SDK (`0x00RRGGBB`) — совпадает с `DevToRef` клиента (`Draw.cpp:247`) | ⛔ |
| `clbkSaveSurfaceToImage` | сохранить поверхность в файл | — (в клиенте есть только `SaveShot` back buffer'а) | ⛔ |
| `clbkCopyBitmap` | битмап → поверхность (с растяжением) | не переопределён; базовая реализация сидит в `Orbiter.lib` — пишет ли она через `clbkGetSurfaceDC` клиента и помечается ли поверхность грязной, **не проверялось** | ⛔ |

## 3. 2D: Blt, Sketchpad, GDI, панели

| clbk | Поведение клиента | |
|---|---|---|
| `clbkBlt` (2 варианта), `clbkScaleBlt` | `BltImpl`: на DIB через GDI — `TransparentBlt` (ключ источника), `BitBlt`, `StretchBlt(HALFTONE)`; цель помечается грязной (`Draw.cpp:277-310`). `BLT_TGTCOLORKEY` не поддержан | ◐ |
| `clbkBeginBltGroup` / `clbkEndBltGroup` | заглушки `return 0` (`Draw.cpp:312-313`) | ◻ |
| `clbkFillSurface` (2 варианта) | `FillRect` на DIB (`Draw.cpp:249-267`) | ✅ |
| `clbkGetSketchpad` / `clbkReleaseSketchpad` | `GPad` поверх HDC DIB; `NULL` = слой экрана; `SaveDC/RestoreDC` (`Draw.cpp:205-221`) | ✅ |
| `clbkCreateFont`, `clbkCreateFontEx`, `clbkReleaseFont` | GDI `CreateFontA`; «Sans/Serif/Fixed» → Arial/Times New Roman/Courier New; в Ex `width`/`spacing` игнорируются, `weight ≥ 600` → жирный (`Draw.cpp:9-25, 223-231`) | ◐ |
| `clbkCreatePen` / `clbkCreateBrush` (+ Release) | GDI-перо (стиль 0 = NULL_PEN, 2 = точки, иначе сплошное), сплошная кисть (`Draw.cpp:27-45`) | ✅ |
| `clbkGetSurfaceDC` / `clbkReleaseSurfaceDC` | HDC DIB-а; на release — грязная (`Draw.cpp:237-244`) | ✅ |
| `clbkRender2DPanel` (2 варианта) | меш панели рисуется на GPU прямо в back buffer: `T` даёт масштаб `m11,m22` и сдвиг `m13,m23`; группы с `UsrFlag & 2` пропускаются; вершины каждой группы копируются в динамический буфер каждый кадр (`Draw.cpp:387-432`) | ✅ |

`GPad` (`Draw.cpp:47-91`) переопределяет: `SetFont/SetPen/SetBrush`, `SetTextAlign`, `SetTextColor`,
`SetBackgroundColor`, `SetBackgroundMode`, `GetCharSize`, `GetTextWidth`, `SetOrigin/GetOrigin`, `Text`, `TextW`,
`Pixel`, `MoveTo/LineTo/Line`, `Rectangle`, `Ellipse`, `Polygon`, `Polyline`, `PolyPolygon`, `PolyPolyline`, `GetDC`.
Всё остальное из `DrawAPI.h` (Sketchpad v2: `QuickPen/QuickBrush`, `CopyRect`, `StretchRect`, `RotateRect`, `ColorKey`,
`TextEx`, `DrawPoly`, `Lines`, `ColorFill`, `GradientFillRect`, матрицы мира/вида, `DrawMeshGroup`, `Clear`, …)
остаётся базовой реализацией с `assert(false)` — см. ловушку «NDEBUG» ниже. `TextBox` — базовая реализация SDK.

## 4. Меши и визуалы

| clbk | Orbiter ждёт | Поведение клиента | |
|---|---|---|---|
| `clbkVisEvent` | события визуала, 1 если обработано | `INSMESH` → удалить и пересоздать недостающие меши; `DELMESH` (−1 = все); `RESETANIM/CLEARANIM/NEWANIM/DELANIM` → `ResetAnimations`; `MODMESHGROUP`, `MESHVISMODE`, `MESHOFS` — ничего (режим видимости и смещение читаются каждый кадр); всегда возвращает 1 (`Client.cpp:496-509`). Контекст `CLEARANIM` (сбрасывать или нет) игнорируется | ◐ |
| `clbkGetMesh(vis, idx)` | DEVMESHHANDLE меша визуала (для `VESSEL::GetDevMesh`) | указатель на `DevMesh` клиента (`Client.cpp:511-516`) | ✅ |
| `clbkGetMeshGroup` | прочитать вершины/индексы группы | из CPU-копии группы, с `VtxPerm/IdxPerm`; `MtrlIdx` — индекс Orbiter, **`TexIdx` — слот клиента (индекс+1, 0 = нет)**, а не индекс Orbiter (`Client.cpp:444-466`) | ◐ |
| `clbkEditMeshGroup` | изменить группу | все флаги `GRPEDIT_*` SDK (пользовательский флаг — замена/добавление/снятие, только один за вызов; вершины — замена/прибавление по осям), затем перезаливка вершин группы в GPU (`Client.cpp:468-493`, `Mesh.cpp:61-67`) | ✅ |
| `clbkSetMeshTexture(hMesh, texidx, tex)` | заменить текстуру | `tex[texidx]` с учётом ссылок (`Client.cpp:416-424`). ⚠ SDK противоречив: `GraphicsAPI.h:548` пишет «texidx ≥ 0», `oapiSetTexture` (`OrbiterAPI.h:4714-4719`) — «1..n». Код считает, что приходит 1-based индекс (совпадает со слотом клиента). Не проверялось вживую | ◐ |
| `clbkSetMeshMaterial` / `clbkMeshMaterial` | 0 успех, 3 плохой меш, 4 индекс вне диапазона | 0 / **2** (нет меша) / 4 (`Client.cpp:426-442`). Код 2 по SDK значит «клиент не поддерживает» | ◐ |
| `clbkSetMeshMaterialEx` / `clbkMeshMaterialEx` | PBR-свойства материала | — (возвращают 2) | ⛔ |
| `clbkSetMeshProperty` | `MESHPROPERTY_MODULATEMATALPHA` | заглушка `false` (`Client.h:261`) | ◻ |
| `clbkStoreMeshPersistent` | сохранить шаблон меша | — | ⛔ |

Как меш корабля попадает на экран:
1. `VesselVis::CreateMeshes` (`Mesh.cpp:78-93`): для каждого индекса `GetMeshTemplate`, иначе
   `CopyMeshFromTemplate` (временная копия удаляется сразу после сборки).
2. `DevMesh::Build` (`Mesh.cpp:6-49`): все группы в один VB/IB (16-бит индексы, `NTVERTEX`), CPU-копии вершин/индексов
   для правок и запросов; `SPEC_DEFAULT`/`SPEC_INHERIT` для текстуры и материала.
3. Анимации (`Mesh.cpp:95-193`): каждый кадр перед непрозрачным проходом `UpdateAnimations` применяет разницу состояний
   `ANIMATION::state` против `animApplied`. Компонент двигает **матрицы групп** (`DGroup::xf`), локальные списки вершин
   (`LOCALVERTEXLIST`) и — вниз по иерархии — опорные точки и оси детей. Вершины в GPU не меняются.
4. `RenderVessel` (`Mesh.cpp:204-277`): отсечение меньше пикселя; видимость `MESHVIS_*` (внутри кабины фокус-корабля —
   `COCKPIT` или `VC` в виртуальной кабине; снаружи — `EXTERNAL`; тень отбрасывают только внешние); группы с
   `UsrFlag & 2` пропускаются; прозрачность группы = `material.diffuse.a < 0.99` (альфа текстуры на выбор прохода не
   влияет); `Flags` и `zBias` группы не используются; `MESHVIS_EXTPASS` не учитывается.

## 5. Потоки частиц

| clbk | Orbiter ждёт | Поведение клиента | |
|---|---|---|---|
| `clbkCreateParticleStream` | новый поток (по умолчанию SDK — NULL) | `PStream`, в список `streams` (`Client.cpp:391`) | ✅ |
| `clbkCreateExhaustStream` (указатели / ссылки) | поток, прикреплённый к кораблю | `PStream` + `Attach(hVessel, ref, dir, lvl)` (`Client.cpp:393-405`) | ✅ |
| `clbkCreateReentryStream` | поток входа в атмосферу | `PStream`, прикреплён в центр корабля, направление −Z, уровень 1.0 (`Client.cpp:407-413`) | ◐ |

Эмиссия на CPU в глобальной системе (`Particles.cpp:31-76`): `srcrate`, `v0`, `srcspread`, `lifetime` (±30 %),
`growthrate`, `atmslowdown` (к скорости опорного тела), `levelmap` (FLAT/LIN/SQRT/PLIN/PSQRT), `atmsmap` (PLIN/PLOG),
не более 4000 частиц на поток. Рисование — квадраты лицом к камере, диффузные смешиваются и освещаются, излучающие
складываются. Размер частицы растёт линейно (`size += growthrate·dt`); `ATM_FLAT` — без множителя; `PARTICLESTREAMSPEC::flags`
не используется; при `tex = NULL` — своя «мягкая точка» (`Particles.cpp:139-155`). Насколько это совпадает с моделью
частиц самого Orbiter/D3D9Client — не сверялось.

## 6. Прочее

| clbk | |
|---|---|
| `clbkCreateAnnotation` — экранные подписи | ⛔ по умолчанию SDK создаёт базовый `ScreenAnnotation`; рисуется ли он в клиенте — **не проверялось** |
| `clbkFilterElevation` — фильтр высот для физики | ⛔ (`false`) |
| `GetVCHUDSurface`, `GetMFDSurface`, `GetVCMFDSurface` (не clbk, методы базового класса) | не используются: MFD и HUD виртуальной кабины не рисуются (см. [roadmap.md](roadmap.md)) |

---

## 7. Ловушки, найденные при разработке

**1. NULL вместо потока частиц роняет ядро.** По SDK `clbkCreate*Stream` по умолчанию возвращают NULL
(`GraphicsAPI.h:717-778`), но ядро Orbiter 2024 обращается к возвращённому указателю без проверки — с NULL оно падает
(найдено при разработке; в коде это не прокомментировано). Поэтому все четыре метода **всегда** возвращают живой `PStream`, даже если
`pss == NULL` — конструктор подставляет разумные значения (`Particles.cpp:9-17`).

**2. NDEBUG и assert'ы Sketchpad.** В `DrawAPI.h` почти все виртуальные методы `oapi::Sketchpad` в базовом классе —
`{ assert(false); … }` (`DrawAPI.h:1032-1697`), и они компилируются **в DLL клиента** (тела в заголовке). Модули, вызывающие
метод, который `GPad` не переопределил, без `NDEBUG` уронили бы Orbiter. `build.bat` собирает с `/DNDEBUG` — не убирать.
gcCore `SketchpadVersion` возвращает 1, чтобы аккуратные модули не звали методы Sketchpad v2 (`Ext.cpp:54`).

**3. Строковый ресурс 1001 «Graphics engines».** Launchpad показывает DLL из `Modules\Plugin` в разделе графических
движков, только если строка 1001 в её ресурсах равна `Graphics engines` (`src\D3D11Client.rc:4-8`). Без `.rc` клиент
виден как обычный плагин.

**4. 16-битные текстуры (A4R4G4B4 и др.).** Orbiter-текстуры встречаются в A4R4G4B4, R5G6B5, A1R5G5B5 и 8-бит
luminance; в D3D11 этих форматов либо нет, либо они необязательны. Загрузчик расширяет каждый канал по маске в RGBA8
(`Dds.cpp:113-129`).

**5. GDI и альфа.** GDI пишет в 32-битный DIB альфу 0. Чтобы отличить «GDI здесь рисовал» от «прозрачно»:
при создании DIB все нулевые альфы поднимаются до 1 (почти прозрачно) (`Draw.cpp:112-115`); после рисования
`FixGdiAlpha` в прямоугольнике `GetBoundsRect` превращает альфу 0 в 255 (`Draw.cpp:120-133`). Поверхности без
`OAPISURFACE_ALPHA` и не из файла при заливке делаются полностью непрозрачными (`Draw.cpp:188-190`). Слой экрана устроен
иначе: он заливается ключевым цветом `0x00010001` (почти чёрный, чтобы сглаженные края GDI-текста оставались тёмными), при
композиции ключ → прозрачно, остальное → непрозрачно (`Draw.cpp:316, 362`).

**6. Время жизни текстур меша.** Шаблон меша (или временная копия из `CopyMeshFromTemplate`, которая сразу удаляется) отпускает
свои текстуры раньше, чем живёт меш корабля; без собственной ссылки `DevMesh` держал бы указатели на освобождённые `Surf`. Решение —
`clbkIncrSurfaceRef` в `Build` и release в деструкторе (`Mesh.cpp:11-12, 51-54`).

**7. 32-битная сборка.** Orbiter 2024 и `Orbitersdk\lib\Orbiter.lib` — x86. `build.bat` вызывает `vcvars32.bat`;
x64-DLL Orbiter не загрузит. Отсюда же ограничение адресного пространства (кеш тайлов, буферы) — держать в уме.

**8. Прочее из кода.**
- `oapiCameraAperture()` — **половина** вертикального угла обзора (`Client.cpp:556`).
- Цвета неба AtmoFX в `Config\GC\*.atm.cfg` подобраны в гамма-пространстве D3D9-клиента; в клиенте линейный конвейер —
  их надо линеаризовать (отсюда отдельный `Config\D3D11Client\Titan.cfg`, а цвет дымки из параметров планеты
  возводится в степень 2.2, `Planet.cpp:82`).
- В тестовом режиме окно прячется не только при создании, но и в каждом `clbkDisplayFrame`, если оно оказалось видимым
  (`Client.cpp:650`) — кто именно его показывает, в коде не записано.
- Прокладка `D3D9Client.dll` не грузится, если в процессе уже есть настоящий D3D9Client (`Ext.cpp:137`).
