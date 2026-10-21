# Отображение на API Orbiter

[← оглавление](DEVELOPER.md)

Состояние кода: 21.10.2026

Источник требований — `Orbitersdk\include\GraphicsAPI.h` (класс `oapi::GraphicsClient`, наследник `oapi::Module` из
`ModuleAPI.h`), `OrbiterAPI.h`, `VesselAPI.h`, `DrawAPI.h` (`oapi::Sketchpad`), `CelSphereAPI.h`. Объявления
переопределений клиента — `Client.h:375-443` (публичные `clbk*`), `Client.h:586-594` (защищённые), `Client.h:658-659`
(`clbkNewVessel`, `clbkDeleteVessel`).

Статусы: **реализовано**; **частично** — переопределено, но часть параметров или случаев не обрабатывается;
**SDK** — не переопределено, работает реализация базового класса; **заглушка** — переопределено пустой функцией или
постоянным ответом.

## 1. Модуль, окно, Launchpad

| clbk / точка входа | Orbiter ждёт (SDK) | Клиент | Статус |
|---|---|---|---|
| `InitModule` / `ExitModule` | создать клиент, `oapiRegisterGraphicsClient` / снять регистрацию | `HeapTrackStart` (только при `HeapByModule = 1`), создание, регистрация, загрузка прокладки `D3D9Client.dll` (`Client.cpp:57-68`) | реализовано |
| `clbkInitialise` | подготовка до Launchpad; базовая версия обязательна | базовая версия, `g_cfg.Load()`, `InitVideoTab()`, размер окна из `VIDEODATA` (по умолчанию 1280×800), каталог шейдеров `Modules\D3D11Client\` (`Client.cpp:73-83`) | реализовано |
| `clbkUseLaunchpadVideoTab` | true — показывать вкладку Video | `true` (`Client.h:379`) | реализовано |
| `LaunchpadVideoWndProc` | сообщения вкладки | переключатели «полный экран»/«окно», смена адаптера → список режимов, кнопка `IDC_VID_INFO` → диалог Advanced (`VideoTab.cpp:189-201`, диалог — `VideoTab.cpp:247`) | реализовано |
| `clbkRefreshVideoData` | записать выбор в `VIDEODATA` | адаптер, режим, fullscreen, novsync, размеры окна (не меньше 640×480; в полноэкранном режиме — из выбранного режима) (`VideoTab.cpp:175-187`) | реализовано |
| `clbkFullscreenMode` | true в полноэкранном режиме | `VIDEODATA::fullscreen` (`Client.h:376`) | реализовано |
| `clbkGetViewportSize` | размер области рендера | `W×H` (`Client.h:377`): клиентская область окна или `D3D11CLIENT_TEST_RES` в тестовом прогоне (`Client.cpp:118-127`) | реализовано |
| `clbkGetRenderParam` | значения `RP_*` | 32-бит цвет, 32-бит Z, трафарет 0, `RP_MAXLIGHTS` 8, TL 1, степень двойки не нужна (`Client.cpp:85-96`) | реализовано |
| `clbkCreateRenderWindow` | создать окно и устройство | окно — базовым классом; «полный экран» — окно `WS_POPUP` на весь монитор, настоящего exclusive fullscreen нет; `set_terminate(LogTerminate)`; скрытое окно в тестовом режиме; `CreateDevice`, `CreateShaders`, `CreateLiquid`, текстура частиц, `FxInit`, `ExhaustInit`; потоки загрузчика тайлов (2…6); `HangWatchStart` (`Client.cpp:109-154`) | реализовано |
| `RenderWndProc` | сообщения окна рендера | `WM_SYSCOMMAND/SC_KEYMENU` отбрасывается (иначе одиночный Alt переводит окно в модальный режим меню и останавливает цикл Orbiter), остальное — базовой версии (`Client.cpp:99-107`). Изменение размера окна не обрабатывается | частично |
| `clbkPostCreation` | доинициализация после загрузки сценария | `Planet` для каждого `OBJTP_PLANET`, `ScanVessels`, звёзды, запись `loadsteps.txt` (`Client.cpp:534-544`) | реализовано |
| `clbkSplashLoadMsg` | показать сообщение загрузки, true если показано | экран Direct2D с полосой прогресса; ожидаемое число шагов — из `Modules\D3D11Client\loadsteps.txt` (`Client.cpp:1200-1224`, файл читается в `CreateDevice`, `Client.cpp:374-375`) | реализовано |
| `clbkCloseSession` | конец сессии | очистка очереди тайлов, `UnregisterVisObject` и удаление всех визуалов (`Client.cpp:546-554`). `g_ext`, грязь кораблей (`Mesh.cpp:242-254`), HUD-процедуры не очищаются | частично |
| `clbkDestroyRenderWindow` | разрушить окно и 3D-ресурсы; базовая версия обязательна | остановка загрузчиков и `HangWatchStop`, удаление планет, `FxRelease`, `ExhaustRelease`, удаление потоков частиц, `ClearState/Flush`, слой экрана, swap chain, базовая версия (`Client.cpp:556-577`) | реализовано |
| `clbkRenderScene` | нарисовать сцену, в конце вызвать `Render2DOverlay` | порядок проходов — [architecture.md §3](architecture.md#3-кадр); `Render2DOverlay` — `Client.cpp:1090` (`Client.cpp:921-1133`) | реализовано |
| `clbkDisplayFrame` | показать кадр | в тестовом режиме прячет окно, HUD-процедуры модулей (`CallRenderProcs`), композиция слоя экрана, снимки `start.png` / `burst_*.png`, `Present(1 или 0)` по `novsync` (`Client.cpp:1135-1166`). Результат `Present` не проверяется | реализовано |
| `clbkUpdate(bool)` | покадровое уведомление | — | SDK |
| `clbkOptionChanged`, `clbkDebugString`, `clbkPreOpenPopup` | уведомления | — | SDK |
| `clbkFilterElevation` | фильтр высот для физики | — (`false`) | SDK |
| `clbkStoreMeshPersistent` | сохранить шаблон меша | — | SDK |

### 1.1. Обратные вызовы `oapi::Module` (`ModuleAPI.h`)

| clbk | Клиент | Статус |
|---|---|---|
| `clbkPreStep` | `StormPreStep` — турбулентность урагана Земли для кораблей (`Client.h:594`, `Storm.cpp:640-648`). Если за 60 кадров вызова не было, ту же работу делает `StormFrameFallback` из кадра (`Client.cpp:1048`, `Storm.cpp:650-658`). Базовая `Module::clbkPreStep` не вызывается | реализовано |
| `clbkNewVessel` | `AddVisual`: визуал, меши, `RegisterVisObject` (`Client.cpp:885`, `:864-873`) | реализовано |
| `clbkDeleteVessel` | `RemoveVisual` (`UnregisterVisObject`, удаление визуала), удаление записей корабля из `g_ext.jets/wakes/heat`, `WakeForget` (`Client.cpp:887-897`). Базовая `Module::clbkDeleteVessel` (тело в `Orbiter.lib`) не вызывается; делает ли она что-либо, по коду не ясно | реализовано |
| `clbkSimulationStart/End`, `clbkPostStep`, `clbkTimeJump`, `clbkFocusChanged`, `clbkTimeAccChanged`, `clbkVesselJump`, `clbkPause`, `clbkProcess*` | — | SDK |

Запасной путь: каждые 32 кадра `ScanVessels` (`Client.cpp:941`, `:899-916`) добавляет визуалы новым кораблям и снимает
визуалы ушедших (или тех, чей хэндл теперь принадлежит другому `VESSEL`) через `RemoveVisual`, т. е. с
`UnregisterVisObject`.

## 2. Текстуры и поверхности

| clbk | Orbiter ждёт | Клиент | Статус |
|---|---|---|---|
| `clbkLoadTexture(fname, flags)` | загрузить текстуру | DDS из `Textures2\`, затем `Textures\`, затем путь как есть; sRGB; кеш по имени (`texCache`), повторный запрос увеличивает счётчик ссылок (`Client.cpp:647-663`). `flags` не используются | частично |
| `clbkLoadSurface(fname, attrib, bPath)` | загрузить поверхность с атрибутами | `clbkLoadTexture(fname, 0)`; `attrib`, `bPath` не используются (`Client.cpp:665-668`) | частично |
| `clbkReleaseTexture` | освободить | `clbkReleaseSurface` (`Client.cpp:670`) | реализовано |
| `clbkCreateSurfaceEx(w, h, attrib)` | поверхность с атрибутами `OAPISURFACE_*` | `MakeSurf` (`Client.cpp:624-645`): `B8G8R8A8_TYPELESS`, SRV sRGB, очистка на GPU (альфа 0 при `OAPISURFACE_ALPHA`, иначе 1), без CPU-копии. Из атрибутов используются `OAPISURFACE_ALPHA` (альфа при заливке, блиттинге, выгрузке DIB) и `OAPISURFACE_RENDERTARGET`/`OAPISURFACE_SKETCHPAD` (Sketchpad на GPU, `Draw.cpp:901-904`); остальные не различаются (`Client.cpp:693`) | частично |
| `clbkCreateSurface(w, h, tpl)` | поверхность | `MakeSurf(…, 0)`; шаблон не используется (`Client.cpp:694`) | частично |
| `clbkCreateTexture(w, h)` | текстура | `MakeSurf(…, OAPISURFACE_ALPHA)` (`Client.cpp:695`) | реализовано |
| `clbkCreateSurface(HBITMAP)` | поверхность из битмапа | по SDK — поверхность размера битмапа и копирование через `clbkCopyBitmap` (`GraphicsAPI.h:1105-1114`) | SDK |
| `clbkIncrSurfaceRef` / `clbkReleaseSurface` | счётчик ссылок | `Client.cpp:684`, `:672-682`; при нуле — удаление из кеша и из списка грязных, учёт в `LogMemory` | реализовано |
| `clbkGetSurfaceSize` | размер | `Client.cpp:686-691` | реализовано |
| `clbkSetSurfaceColourKey` | ключевой цвет | запоминается (`Draw.cpp:966-972`), используется при `BLT_SRCCOLORKEY` и в `Sketchpad::ColorKey` | реализовано |
| `clbkGetDeviceColour` | упаковка RGB | `0x00RRGGBB` по SDK (`GraphicsAPI.h:1172-1173`); совпадает с `DevToRef` клиента (`Draw.cpp:917`) | SDK |
| `clbkSaveSurfaceToImage` | сохранить поверхность в файл или буфер обмена | — (SDK возвращает false; у клиента есть только `SaveShot` back buffer'а, `Client.cpp:1168-1197`) | SDK |
| `clbkCopyBitmap` | битмап → поверхность с растяжением | тело в `Orbiter.lib`; пишет ли оно через `clbkGetSurfaceDC` клиента и помечается ли поверхность грязной, по коду не ясно | SDK |

Поверхность (`Surf`, `Client.h:90-109`) имеет GPU-сторону (текстура, sRGB-SRV; по запросу — UNORM-SRV/RTV для работы в
гамма-пространстве, `Draw.cpp:828-844`) и, по требованию, CPU-сторону — 32-битный DIB (`EnsureDIB`, `Draw.cpp:697-726`).
Флаги: `gpuNewer` — GPU писал после DIB (при следующем `EnsureDIB` пиксели читаются назад, `Readback`,
`Draw.cpp:744-777`); `cpuPref` — поверхность запрашивалась через GDI (`clbkGetSurfaceDC`, `Sketchpad::GetDC`), её
Sketchpad'ы дальше идут на DIB; `immutable` — загружена из файла. Изменённые DIB раз в кадр выгружаются в текстуры
(`FlushDirty`, `Draw.cpp:791-796`; `UploadSurface`, `:798-818`).

## 3. 2D: Blt, Sketchpad, GDI, панели

| clbk | Клиент | Статус |
|---|---|---|
| `clbkBlt` (2 варианта), `clbkScaleBlt` | `BltImpl` (`Draw.cpp:974-1009`). Цель на GPU (`GpuSurface` и `gpuNewer` или нет DIB): четырёхугольник через `SkBatch`, режим «копия», ключ источника (`kind 5`), точечная выборка при масштабе 1:1. Иначе GDI на DIB: `TransparentBlt` (ключ источника), `BitBlt`, `StretchBlt(HALFTONE)`; цель помечается грязной. `BLT_TGTCOLORKEY` не поддержан (`Draw.cpp:1011-1026`) | частично |
| `clbkBeginBltGroup` / `clbkEndBltGroup` | возвращают 0 (`Draw.cpp:1028-1029`); по SDK `EndBltGroup` без начатой группы возвращает −2 | заглушка |
| `clbkFillSurface` (2 варианта) | цель на GPU — `ClearRenderTargetView`/`ClearView` (альфа цвета только при `OAPISURFACE_ALPHA`); прочие — прямое заполнение DIB (альфа 0 хранится как 1); слой экрана — GDI `FillRect` (`Draw.cpp:919-964`) | реализовано |
| `clbkGetSketchpad` / `clbkReleaseSketchpad` | `GPad`. Поверхность на GPU (`GpuSurface`: атрибут `RENDERTARGET` или `SKETCHPAD`, не из файла, без `cpuPref`, не слой экрана) — пакет `SkBatch` (`GpuPad.cpp`); иначе HDC DIB-а с `SaveDC/RestoreDC`; `NULL` — слой экрана (`Draw.cpp:849-883`) | реализовано |
| `clbkCreateFont`, `clbkCreateFontEx`, `clbkReleaseFont` | GDI `CreateFontA` с `ANTIALIASED_QUALITY`; «Sans/Serif/Fixed» → Arial/Times New Roman/Courier New; пустое имя → Arial или Courier New по `prop`; в `Ex` `width`, `spacing` не используются, `prop` всегда true, `weight ≥ 600` → жирный (`Draw.cpp:11-27`, `:885-893`) | частично |
| `clbkCreatePen` / `clbkCreateBrush` (+ `Release`) | перо: стиль 0 — `NULL_PEN`, 2 — точки, иначе сплошное; кисть — сплошная (`Draw.cpp:29-48`, `:894-897`) | реализовано |
| `clbkGetSurfaceDC` / `clbkReleaseSurfaceDC` | HDC DIB-а, поверхность получает `cpuPref`; на release — `MarkDirty` (`Draw.cpp:906-914`) | реализовано |
| `clbkRender2DPanel` (2 варианта) | меш панели на GPU прямо в back buffer: из `T` берутся `m11, m22` (масштаб) и `m13, m23` (сдвиг); `alpha` → `misc.z`, `additive` → `bsAdd`; группы с `UsrFlag & 2` пропускаются; `TexIdx ≥ TEXIDX_MFD0` — поверхность MFD (`GetMFDSurface`), иначе `hSurf[TexIdx]` или текстура меша; вершины и индексы каждой группы копируются в динамические буферы каждый вызов (`Draw.cpp:1115-1165`) | реализовано |

### 3.1. Sketchpad: какие методы есть

`GPad` (`Draw.cpp:193-627`) работает в одном из двух режимов: на DIB (GDI плюс программная растеризация `Raster`,
`Draw.cpp:77-170`, для цветов с альфой, `QuickPen/QuickBrush` и мировой 2D-трансформации) или на GPU (`gb != nullptr`:
пакет `SkBatch`, `Sketch.h`, `GpuPad.cpp`, шейдер `sketch.hlsl`).

| Метод | DIB | GPU |
|---|---|---|
| `SetFont`, `SetPen`, `SetBrush`, `SetTextAlign`, `SetTextColor`, `SetBackgroundColor`, `SetBackgroundMode`, `GetCharSize`, `GetTextWidth`, `SetOrigin`, `GetOrigin` | GDI | состояние в `GPad`; измерение текста — во вспомогательном DC `g_txt` |
| `Text`, `TextW` | `TextOutA/W` | текст рисуется GDI белым по чёрному во вспомогательный DIB и передаётся пакету как глиф (`GText`, `Draw.cpp:495-516`) |
| `Pixel`, `MoveTo`, `LineTo`, `Line`, `Lines`, `Rectangle`, `Ellipse`, `Polygon`, `Polyline`, `PolyPolygon`, `PolyPolyline` | GDI; при `QuickPen/QuickBrush` или повороте/масштабе — `Raster` со сглаживанием | отрезки, эллипсы, треугольники (заливка многоугольника — отсечение ушей, `GFillPoly`) |
| `GetDC` | HDC DIB-а | пакет завершается, поверхность получает `cpuPref` и DIB, остаток сеанса — на DIB (`Draw.cpp:330-345`) |
| `QuickPen`, `QuickBrush` (v2) | `Raster` | пакет |
| `SetBlendState` (v2) | `ALPHABLEND`, `COPY`, `COPY_ALPHA`, `COPY_COLOR`; `FILTER_POINT` | то же, `SkBatch::SetBlend` |
| `SetWorldTransform2D` (v2) | масштаб, поворот вокруг центра, сдвиг (`Tr`, `Draw.cpp:547-552`) | то же |
| `SetBrightness` (v2) | множитель в `StretchRect` | множитель (`tint`) в `GStretch` |
| `SetRenderParam` (v2) | только `PRM_GAMMA` | только `PRM_GAMMA` |
| `SetColorMatrix` (v2) | пустая функция | пустая функция |
| `CopyRect`, `StretchRect` (v2) | программная выборка с билинейным фильтром, взвешенным по альфе (`Draw.cpp:631-691`) | четырёхугольник с текстурой источника |
| `ColorKey` (v2) | `TransparentBlt` — без трансформации, яркости и режима смешения | четырёхугольник с ключом (`kind 5`) |

Не переопределены (в базовом классе — `assert(false)`, `DrawAPI.h:1363-1697`): `GetRenderSurfaceSize`,
`SetGlobalLineScale`, `SetWorldTransform`, `ViewMatrix`, `ProjectionMatrix`, `GetViewProjectionMatrix`, `SetViewMode`,
`ClipRect`, `Clipper`, `DepthEnable`, `DrawMeshGroup`, `RotateRect`, `TextEx`, `DrawPoly`, `SetViewMatrix`,
`SetProjectionMatrix`, `GetColorMatrix`, `GetRenderParam`, `PushWorldTransform`, `PopWorldTransform`,
`SetWorldScaleTransform2D`, `GradientFillRect`, `ColorFill`, `StretchRegion`, `CopyTetragon`, `FillTetragon`,
`ColorCompatibility`, `Clear`, `SetClipDistance`, `SetWorldBillboard`. Сборка с `/DNDEBUG` делает их пустыми; методы,
возвращающие указатель (`ViewMatrix`, `GetColorMatrix` и др.), возвращают `NULL`. Без `assert`: `TextBox` (реализация
SDK), `GetWorldTransform` (единичная `FMATRIX4`), `GetVersion` (1). gcCore `SketchpadVersion` также возвращает 1
(`Ext.cpp:56`).

### 3.2. Sketchpad на GPU (`GpuPad.cpp`)

- Ресурсы (`InitRes`, `GpuPad.cpp:27-88`) создаются при первом использовании и пересоздаются при смене `g_devGen`:
  шейдеры `sketch.hlsl`, динамический VB, состояния смешения `[режим][альфа доступна]`, общий буфер глубины (по
  глубине разделяются примитивы, чтобы перекрытия одного примитива смешивались один раз; шаг `ZSTEP = 2⁻²²`), атлас
  текста 2048×1024.
- Рисование идёт в UNORM-RTV поверхности (те же байты, что в DIB). Состояние конвейера сохраняется и восстанавливается
  вокруг каждого вывода (`PipeState`, `GpuPad.cpp:103-134`): Sketchpad'ы вызываются модулями посреди кадра.
- Источник для `StretchRect/ColorKey/Blt` — SRV поверхности; рисование поверхности самой в себя на GPU пропускается
  (`GStretch`, `Draw.cpp:519`).

### 3.3. Слой экрана

DIB размером `W×H` (`BeginScreenLayer`, `Draw.cpp:1034-1049`), каждый кадр область прошлого кадра заливается ключевым
цветом `0x00010001`; Orbiter (HUD, информационные блоки) и HUD-процедуры модулей рисуют в него через Sketchpad `NULL`.
`CompositeScreenLayer` (`Draw.cpp:1051-1110`) выгружает объединение прямоугольников этого и прошлого кадра и накладывает
слой на back buffer: ключ → прозрачно, альфа 0 или 255 → непрозрачно, промежуточная альфа (цвета Sketchpad v2) —
как есть.

## 4. Меши и визуалы

| clbk | Orbiter ждёт | Клиент | Статус |
|---|---|---|---|
| `clbkVisEvent` | события визуала, 1 если обработано | `INSMESH` → удалить меш с индексом из контекста и создать недостающие; `DELMESH` (−1 — все); `RESETANIM`, `CLEARANIM`, `NEWANIM`, `DELANIM` → `ResetAnimations`; `MODMESHGROUP`, `MESHVISMODE`, `MESHOFS` — ничего (режим видимости и смещение читаются каждый кадр); всегда 1 (`Client.cpp:842-855`). Контекст `CLEARANIM` (сбрасывать ли анимации) не учитывается | частично |
| `clbkGetMesh(vis, idx)` | хэндл меша визуала (для `VESSEL::GetDevMesh`) | указатель на `DevMesh` (`Client.cpp:857-862`) | реализовано |
| `clbkGetMeshGroup` | вершины/индексы группы | из CPU-копии группы, с `VtxPerm/IdxPerm`; `MtrlIdx` — индекс Orbiter (или `(DWORD)-1`), `TexIdx` — слот клиента (индекс+1, 0 = нет), а не индекс Orbiter (`Client.cpp:790-812`) | частично |
| `clbkEditMeshGroup` | изменить группу | пользовательский флаг (замена, добавление или снятие — один за вызов), вершины — замена или прибавление по компонентам; затем `UploadGroup` (`Client.cpp:814-839`, `Mesh.cpp:64-70`) | реализовано |
| `clbkSetMeshTexture(hMesh, texidx, tex)` | заменить текстуру | `tex[texidx]` с учётом ссылок (`Client.cpp:723-731`). Индекс трактуется как слот клиента (1…n); `texidx = 0` принимается и записывает слот «нет текстуры» без видимого эффекта. SDK: `GraphicsAPI.h:548` — «texidx ≥ 0», `oapiSetTexture` (`OrbiterAPI.h:4714-4719`) — «1..n» | частично |
| `clbkSetMeshMaterial` / `clbkMeshMaterial` | 0 успех, 3 плохой меш, 4 индекс вне диапазона | 0 / **2** при `NULL`-меше / 4 (`Client.cpp:733-749`). Код 2 по SDK означает «клиент не поддерживает» | частично |
| `clbkSetMeshMaterialEx` / `clbkMeshMaterialEx` | свойства материала по одному (`MatProp`) | `Diffuse` (rgba), `Ambient`, `Specular` (+ `power` в `.a`), `Light` (= `emissive`), `Emission` (отдельный массив `emis2`, вывод = `max(освещённое, emission)`); `Reflect`, `Smooth`, `Metal`, `Fresnel`, `SpecialFX` → код 3; `NULL`-меш → 2 (`Client.cpp:751-788`) | частично |
| `clbkSetMeshProperty` | `MESHPROPERTY_MODULATEMATALPHA` | `false` (`Client.h:393`) | заглушка |

### 4.1. Путь меша корабля на экран

1. `VesselVis::CreateMeshes` (`Mesh.cpp:81-96`): сначала `ResetAnimations`; для каждого индекса без меша —
   `GetMeshTemplate`, иначе `CopyMeshFromTemplate` (временная копия удаляется сразу после сборки).
2. `DevMesh::Build` (`Mesh.cpp:8-52`): все группы в один VB/IB (`NTVERTEX`, 16-битные индексы), CPU-копии вершин и
   индексов; `SPEC_DEFAULT`/`SPEC_INHERIT` для текстуры и материала; собственная ссылка на каждую текстуру.
3. Анимации (`Mesh.cpp:98-196`): перед непрозрачным проходом (не в зеркальном) `UpdateAnimations` применяет разницу
   `ANIMATION::state` против `animApplied`. Компонент умножает **матрицы групп** (`DGroup::xf`), двигает локальные
   списки вершин (`LOCALVERTEXLIST`) и — вниз по иерархии — опорные точки и оси детей. Вершины в GPU не меняются.
4. `RenderVessel` (`Mesh.cpp:256-413`): отсечение меньше половины пикселя (кроме теней); видимость `MESHVIS_*`
   (своя кабина — `COCKPIT` или `VC` в виртуальной кабине; снаружи, в тенях и в зеркале жидкости — `EXTERNAL`);
   прозрачность группы — `material.diffuse.a < 0.99` (альфа текстуры на выбор прохода не влияет, сортировки нет);
   `DGroup::flags`, `zBias`, `MESHVIS_EXTPASS` не используются.

### 4.2. Материалы

- Цвета `MATERIAL` → `cbObj` (`MeshCB`, `Mesh.cpp:199`); `emis2` → `b8`; грязь корабля (`AtmoFX_SetDirt`) → `b7`.
- Дополнительные карты ищутся рядом с текстурой группы по имени, как у D3D9Client: `<имя>_norm`, `_spec`, `_rghn`,
  `_metl`, `_emis` `.dds` в `Textures2\`, затем `Textures\` (`MapsFor`, `Mesh.cpp:201-232`); `_spec` и `_emis` — sRGB.
  Загружаются один раз на имя текстуры, привязываются к `t1…t5`, наличие — в `c.maps`/`c.maps2`. При находке — строка
  журнала `materials: <текстура> with normal specular smoothness metalness emission` (перечисляются найденные).
  `D3D11CLIENT_NOMAPS=1` отключает карты (тестовые прогоны).
- «Кожа» (подповерхностное рассеяние в `mesh.hlsl`) — если имя текстуры содержит `skin`.
- Биты `UsrFlag` группы: `2` — не рисовать; `4` — без освещения (в собственном проходе кабины — без записи глубины);
  `8` — аддитивное смешение; `0x100` — фильтр (умножение цели на пропускание; участвует в блике Солнца);
  `0x200` вместе с `0x100` — H-alpha фильтр (`Sun.cpp`).

## 5. Потоки частиц

| clbk | Orbiter ждёт | Клиент | Статус |
|---|---|---|---|
| `clbkCreateParticleStream` | новый поток (по SDK — `NULL`) | `PStream` в список `streams` (`Client.cpp:698`) | реализовано |
| `clbkCreateExhaustStream` (указатели / ссылки) | поток, прикреплённый к кораблю | `PStream` + `Attach(hVessel, ref, dir, lvl)`, `engine = true` (`Client.cpp:700-712`) | реализовано |
| `clbkCreateReentryStream` | поток входа в атмосферу | `PStream`, прикреплён в центр корабля, направление −Z, уровень — статическая 1.0 (`Client.cpp:714-720`) | частично |

- Эмиссия на CPU в глобальной системе (`PStream::Update`, `Particles.cpp:33-139`): `srcrate` (интервал укорачивается
  со скоростью относительно воздуха), `v0`, `srcspread` (поперёк струи — треть), `lifetime` (±30 %), `growthrate`
  (размер растёт линейно), `atmslowdown` (торможение к скорости ветра, `oapiGetWindVector`), тяжесть и всплытие,
  растекание по грунту, `levelmap` (FLAT/LIN/SQRT/PLIN/PSQRT), `atmsmap` (PLIN/PLOG); не более 4000 частиц на поток.
- Поток, уровень которого совпадает с уровнем факела корабля (`EXHAUSTSPEC::level`), рождает частицы в конце факелов
  поочерёдно (до 16) и проявляет их на длине смешения (`tIn`) — подробно в [exhaust.md §12](exhaust.md#12-потоки-частиц-orbiter-particlescpp).
- Рисование (`UpdateRenderParticles`, `Particles.cpp:144-202`): квадраты лицом к камере; диффузные — смешение и свет
  Солнца и неба (`particles.hlsl`), излучающие — сложение. Поток `engine` не рисуется, если выхлоп его корабля несёт
  3D-поток (`g_exModelled`). При `tex = NULL` — своя «мягкая точка» (`Particles.cpp:204-219`).
- `PARTICLESTREAMSPEC::flags` не используется; ссылка на `spec.tex` не увеличивается.
- Время жизни: поток удаляется клиентом, когда у него нет уровня (`level == NULL`, т. е. отсоединён) и не осталось
  частиц (`PStream::Expired`, `Client.h:151`; `Particles.cpp:146-150`).

## 6. Прочее

| Что | Клиент | Статус |
|---|---|---|
| `clbkCreateAnnotation` | по SDK создаётся базовый `ScreenAnnotation` (`GraphicsAPI.h:786`); его `Render` — в `Orbiter.lib`, рисует ли он через Sketchpad клиента, по коду не ясно | SDK |
| `GetMFDSurface` (метод базового класса) | используется для групп панели с `TexIdx ≥ TEXIDX_MFD0` (`Draw.cpp:1149`) | — |
| `GetVCHUDSurface`, `GetVCMFDSurface` | не используются: HUD и MFD виртуальной кабины не накладываются на меши кабины | — |
| `oapi::CelestialSphere` | только загрузчик каталога звёзд (`Stars.cpp:7-15`); созвездия, сетки, метки не рисуются | — |

---

## 7. Ловушки

**1. NULL вместо потока частиц.** По SDK `clbkCreate*Stream` по умолчанию возвращают `NULL`
(`GraphicsAPI.h:722-778`), но ядро Orbiter 2024 обращается к возвращённому указателю без проверки — с `NULL` процесс
падает (известно из разработки; в коде не прокомментировано). Все четыре метода **всегда** возвращают живой
`PStream`; при `pss == NULL` конструктор подставляет значения по умолчанию (`Particles.cpp:11-19`).

**2. NDEBUG и assert'ы Sketchpad.** Почти все виртуальные методы `oapi::Sketchpad` в базовом классе —
`{ assert(false); … }` (`DrawAPI.h:1032-1697`), тела в заголовке, поэтому компилируются в DLL клиента. Модуль,
вызвавший метод, которого `GPad` не переопределил (§3.1), без `NDEBUG` остановил бы Orbiter. `build.bat` собирает с
`/DNDEBUG` — не убирать. `SketchpadVersion` (gcCore) и `Sketchpad::GetVersion` возвращают 1, поэтому модули, проверяющие
версию, не вызывают методы v2.

**3. Строка 1001 «Graphics engines».** Launchpad показывает DLL из `Modules\Plugin` среди графических движков, только
если строка 1001 в её ресурсах равна `Graphics engines` (`src\D3D11Client.rc:4-8`). Без `.rc` клиент виден как
обычный плагин.

**4. 16-битные текстуры.** Текстуры Orbiter встречаются в A4R4G4B4, R5G6B5, A1R5G5B5 и 8-битной яркости; в D3D11
этих форматов нет или они необязательны. Загрузчик расширяет каждый канал по маске в RGBA8 (`Dds.cpp:342-357`).

**5. GDI и альфа.** GDI пишет в 32-битный DIB альфу 0. При создании DIB все нулевые альфы поднимаются до 1
(`Draw.cpp:720-723`); после рисования `FixGdiAlpha` в прямоугольнике `GetBoundsRect` превращает 0 в 255
(`Draw.cpp:728-742`). При выгрузке поверхности без `OAPISURFACE_ALPHA` и не из файла альфа принудительно 255
(`Draw.cpp:804-806`). Слой экрана заливается ключом `0x00010001` (почти чёрный — сглаженные края GDI-текста остаются
тёмными); при композиции ключ → прозрачно (`Draw.cpp:1032`, `:1087-1089`). Цвет Sketchpad с альфой 0 считается
непрозрачным (совместимость с D3D9Client: `GPad::Comp`, `Draw.cpp:542-543`; так же в D3D11Hud, `HudApi.cpp:164`).

**6. Время жизни текстур меша.** Шаблон меша (или временная копия из `CopyMeshFromTemplate`) отпускает текстуры раньше,
чем живёт меш корабля. `DevMesh::Build` берёт собственную ссылку (`Mesh.cpp:13-14`), деструктор её отдаёт
(`Mesh.cpp:54-57`).

**7. 32-битная сборка.** Orbiter 2024 и `Orbitersdk\lib\Orbiter.lib` — x86; `build.bat` вызывает `vcvars32.bat`.
Адресное пространство 2 ГБ ограничивает: поверхности очищаются на GPU без CPU-копии (`Client.cpp:630-631`); загрузчиков
тайлов не больше 6 (`Client.cpp:144-146`); высоты тайлов хранятся 16-битными и разделяются между тайлами, а не
копируются (`Client.h:224-243`, `:260-264`).

**8. Поколение устройства.** Каждый сценарий создаёт окно и устройство заново; `g_devGen` увеличивается в
`CreateDevice` (`Client.cpp:307-310`, `Client.h:214-217`). Объекты старого устройства, переданные контексту нового,
дают исключение GPU (Xid 13) и потерю устройства. Статические ресурсы модулей клиента пересоздаются по смене
`g_devGen` (`GpuPad.cpp:29`, `HudApi.cpp:37`, `Ar.cpp:32`, `Bloom.cpp:22`, `Ssao.cpp:26`, `Wake.cpp:114`). Часть
статических ресурсов сравнивает указатель `ID3D11Device*` (`Mesh.cpp:210`, `:288`, `:376`, `:386`; `AA.cpp:195`;
`Planet.cpp:801`) — см. [roadmap.md §4](roadmap.md#4-возможные-ошибки-и-расхождения).

**9. Хэндлы объектов между сценариями.** Статический `OBJHANDLE` Солнца, сохранённый в первом сценарии, указывал на
освобождённый объект после загрузки следующего (падение в `Orbiter.exe`); Солнце ищется при каждом вызове
(`Mesh.cpp:267`). Корабли появляются и исчезают посреди кадра (например, блок полёта, удаляемый при надевании) — визуал
и всё связанное удаляются в `clbkDeleteVessel` (`Client.h:656-659`).

**10. Дисковый кеш шейдеров драйвера.** Драйвер NVIDIA отображает файл кеша `%LOCALAPPDATA%\NVIDIA\DXCache\*.nvph`
в процесс целиком (256, 512, 1024 МБ, растёт при работе). Перед созданием устройства файл больше `DriverCacheMB`
(по умолчанию 600) переносится в `DXCache_Orbiter_old` (`TrimDriverCache`, `Client.cpp:156-185`); список файлов —
`Modules\D3D11Client\drivercache.txt` (`NoteDriverCache`, `:187-217`). При `DriverCacheOff = 1` в таблице импорта
`NvMemMapStorage.dll` `CreateFileW` подменяется функцией, отказывающей в открытии `*.nvph` (`Client.cpp:219-305`); путь
к библиотеке — из `nvmemmap.txt` или из ключа реестра класса видеоадаптеров. Изменение действует только в процессе
Orbiter.

**11. Компиляция шейдеров драйвером.** Пачка из ~120 шейдеров на старте загружала все ядра компилятором драйвера, на
одной машине это стабильно роняло `nvgpucomp32.dll`. Шейдеры создаются по одному с промежутком не меньше 12 мс
(`kShaderGapMs`), загрузчики тайлов ждут, пока идёт создание (`g_shaderTick`) (`Client.cpp:463-486`, `:609-610`).
`DriverThreads = 0` создаёт устройство с `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`
(`Client.cpp:315`, `Client.h:55-57`). Байт-код кешируется на диске (`ShaderCache.cpp`, `Modules\D3D11Client\shadercache`).

**12. Alt и режим меню.** Отпущенный одиночный Alt переводит окно в модальный цикл меню Windows, и цикл Orbiter
останавливается; `RenderWndProc` отбрасывает `SC_KEYMENU` (`Client.cpp:99-107`).

**13. Необработанное исключение.** `std::terminate` завершает процесс без дампа (fast fail `0xC0000409`);
`LogTerminate` пишет в `Orbiter.log` тип исключения и стек «модуль+смещение» (`Client.cpp:15-44`). Зависание дольше 4 с
пишет стеки всех потоков в `Modules\D3D11Client\hang.log` (`HangWatch.cpp`).

**14. Первый кадр повторного сценария.** Текстура слоя экрана могла не создаться — `UpdateSubresource` с `NULL` падал;
теперь кадр 2D пропускается с записью в журнал (`Draw.cpp:1068-1079`).

**15. Ближняя плоскость.** Обратный Z с бесконечной дальней плоскостью: ближняя плоскость 0.1 м, изнутри корабля
(`oapiCameraInternal`) — 0.015 м; проходы читают её из проекции (`P._43`) или обратной матрицы, а не из константы
(`Client.cpp:966-973`).

**16. Прочее из кода.**
- `oapiCameraAperture()` — **половина** вертикального угла обзора (`Client.cpp:964`).
- Цвета неба AtmoFX в `Config\GC\*.atm.cfg` подобраны в гамма-пространстве D3D9-клиента; клиент работает в линейном
  пространстве — цвет дымки из параметров планеты возводится в степень 2.2 (`Planet.cpp:88`).
- В тестовом режиме окно прячется не только при создании, но и в каждом `clbkDisplayFrame`, если оно оказалось
  видимым (`Client.cpp:1138`); кто его показывает, в коде не записано.
- Прокладка `D3D9Client.dll` не грузится, если в процессе уже есть настоящий D3D9Client (`Ext.cpp:143`).
- Ядро может не вызывать `clbkPreStep` графического клиента — для этого случая есть `StormFrameFallback`
  (`Client.cpp:1048`); вызывается ли он на самом деле, по коду не ясно.
