# Что не реализовано (дорожная карта)

[← оглавление](DEVELOPER.md)

Состояние кода: 21.10.2026

Составлено по коду `src\`, `shaders\`, `shim\`, по SDK и по разделам «Ограничения» тематических глав. «Нет в коде»
означает, что в `src\` нет кода для этого. Пункты §4 — то, что по коду выглядит ошибкой или расхождением; вживую они
не проверялись, если не сказано иное.

## 1. Сцена

| Что | Состояние |
|---|---|
| Наземные базы (здания, ВПП, площадки) | нет в коде: рисуются только тайлы. Orbiter запрашивает текстуры баз (отсюда строки `texture not loaded: …` в журнале), меши баз клиент не рисует |
| Планета меньше пикселя | не рисуется (`Planet.cpp:768`); точек планет на небе нет |
| Ночные огни поверхности, кольца, модификации высот (`Elev_mod`), подписи и метки на поверхности | нет в коде |
| Маска воды | читается только из папок `Textures\<планета>\Mask\…` (`Planet.cpp:474`, `:590`); архив `Mask.tree` не открывается (открываются только `Surf.tree`, `Elev.tree`, `Planet.cpp:67-68`). Используется только для береговой линии жидкостей |
| Облака | объёмные слои по `Clouds.cfg` (Титан, `Clouds.cpp`) и ураган Земли (`Storm.cpp`). Облачный слой Orbiter (`Cloud.tree`), объёмные облака Земли по слою и движущийся циклон написаны (`CloudLayer.cpp`, `EarthClouds.cpp`, `Cyclone.cpp`), но не входят в `build.bat` и не вызываются. Подробно — [clouds.md §5](clouds.md#5-код-вне-сборки-2d-слой-облака-земли-по-слою-циклон), [§10](clouds.md#10-ограничения-замечания-подозрительное) |
| Тени | каскадная карта Солнца: плитка фокус-корабля и три каскада до 300 м (`RenderShadows`, `Client.cpp:1300-1430`); отбрасывают только корабли. Теней от рельефа, затмений планетами, теней дальше 300 м нет. Тени локальных прожекторов — до четырёх (`Lights.cpp`) |
| Меши: сортировка прозрачных групп; прозрачность по альфе текстуры | нет: прозрачность группы = альфа материала < 0.99, порядок — как в меше (`Mesh.cpp:328-329`) |
| Меши: `DGroup::flags`, `zBias`, `MESHVIS_EXTPASS`, `MESHPROPERTY_MODULATEMATALPHA` | не используются |
| Материалы: `MatProp` `Reflect`, `Smooth`, `Metal`, `Fresnel`, `SpecialFX` | не хранятся (`clbkSetMeshMaterialEx` → 3); карты `_norm/_spec/_rghn/_metl/_emis` по имени текстуры — есть ([orbiter-api.md §4.2](orbiter-api.md#42-материалы)) |
| Потоки частиц Orbiter | реализованы на CPU; `PARTICLESTREAMSPEC::flags` не используется; тени частиц на грунт нет |
| Атмосфера | FX-атмосфера по таблицам (`FxAtmo.cpp`); ограничения модели — [atmosphere.md §9.1](atmosphere.md#91-ограничения-модели): нет атмосферной перспективы на том, что рисуется после прохода воздуха (собственный корабль, потоки частиц), один объём перспективы только для планеты камеры, экспозиция по горизонтальной площадке, не по кадру |
| Струи двигателей | ограничения модели — [exhaust.md §16.1](exhaust.md#161-ограничения-модели): γ = 1.3 для всех, состав только H2/H2O, нет дисков Маха, нет взаимодействия факела с грунтом, нет выброса грунта над жидкостью, сажа не подаётся в 3D-поток |
| Жидкости | ограничения модели — [water.md §8.1](water.md#81-ограничения-модели): только глубокая жидкость, нет ветрового волнения, поле волн только около фокуса, нет каустик, брызги от струй над жидкостью и карточки от колёс выключены; FFT-поле волн (`FxWaves.cpp`) не подключено |
| Пена (`AtmoFX_SetFoam`), колеи и следы (`AtmoFX_Mark*`) | пустые экспорты (`Ext.cpp:112`, `:117-120`); проект колей — [ruts.md](ruts.md) |
| Дождь | Титан, ураган Земли, капли на визоре; ограничения — [rain.md §8.1](rain.md#81-ограничения-и-расхождения): нет ветра у дождя Титана, визор знает только дождь Титана, нет видимости/экстинкции в дожде, капли без векторов движения |
| Сглаживание | MSAA 2/4/8×, FXAA, TAA с понижением разрешения. Векторы движения пишут только непрозрачные корабли (остальное репроецируется через глубину, `Client.cpp:1020-1031`); DLSS нет |

## 2. 2D, окна, вывод

| Что | Состояние |
|---|---|
| HUD и MFD виртуальной кабины (`GetVCHUDSurface`, `GetVCMFDSurface`) | нет в коде; MFD 2D-панелей работают (`Draw.cpp:1149`) |
| Отрисовка 3D в поверхность (камеры для MFD и зеркал, `OAPISURFACE_RENDER3D`, gcCore `SetupCustomCamera`) | нет |
| Планетарий (`oapi::CelestialSphere`: созвездия, сетки, метки) | нет: используется только загрузчик каталога звёзд (`Stars.cpp:7-15`) |
| Экранные подписи (`clbkCreateAnnotation`) | поведение SDK; рисуются ли, по коду не ясно |
| Sketchpad v2 | переопределены `QuickPen`, `QuickBrush`, `SetBlendState`, `SetWorldTransform2D`, `SetBrightness`, `SetRenderParam` (только гамма), `CopyRect`, `StretchRect`, `ColorKey`, `Lines`; `SetColorMatrix` пустой; 3D-часть (матрицы, `DrawMeshGroup`, `DrawPoly`, `Clipper`), `RotateRect`, `TextEx`, `ColorFill`, `GradientFillRect`, `Clear` и др. — нет ([orbiter-api.md §3.1](orbiter-api.md#31-sketchpad-какие-методы-есть)) |
| `BLT_TGTCOLORKEY`; группы блиттинга | нет / заглушки |
| Флаги и атрибуты `clbkLoadTexture`/`clbkLoadSurface`; атрибуты `OAPISURFACE_*` кроме `ALPHA`, `RENDERTARGET`, `SKETCHPAD` | не используются |
| `clbkSaveSurfaceToImage` (снимки Orbiter), `clbkCopyBitmap` | поведение SDK (false / тело в `Orbiter.lib`) |
| Изменение размера окна во время сессии (swap chain, цели сцены) | нет: всё создаётся по `W×H` в `clbkCreateRenderWindow`; `RenderWndProc` не обрабатывает `WM_SIZE` |
| Выбор адаптера из Launchpad (`VIDEODATA::deviceidx`) | выбирается в диалоге (`VideoTab.cpp:175-187`), но `D3D11CreateDevice(nullptr, …)` берёт адаптер по умолчанию (`Client.cpp:316`) |
| Эксклюзивный полноэкранный режим, частота из `modeidx` | нет: «полный экран» — окно без рамки на весь монитор (`Client.cpp:112-117`) |
| Потеря устройства (TDR, `DXGI_ERROR_DEVICE_REMOVED`) | не обрабатывается; результат `Present` не проверяется (`Client.cpp:1164`) |

## 3. API

- Не переопределены (поведение SDK): `clbkUpdate`, `clbkOptionChanged`, `clbkDebugString`, `clbkPreOpenPopup`,
  `clbkSaveSurfaceToImage`, `clbkCopyBitmap`, `clbkCreateSurface(HBITMAP)`, `clbkGetDeviceColour`,
  `clbkStoreMeshPersistent`, `clbkCreateAnnotation`, `clbkFilterElevation`; из `oapi::Module` — все, кроме
  `clbkPreStep`, `clbkNewVessel`, `clbkDeleteVessel`.
- Частично: `RenderWndProc`, `clbkLoadTexture`, `clbkLoadSurface`, `clbkCreateSurfaceEx`, `clbkCreateSurface`,
  `clbkBlt`/`clbkScaleBlt`, `clbkCreateFont`/`clbkCreateFontEx`, `clbkVisEvent`, `clbkGetMeshGroup`,
  `clbkSetMeshTexture`, `clbkSetMeshMaterial`/`clbkMeshMaterial`, `clbkSetMeshMaterialEx`/`clbkMeshMaterialEx`,
  `clbkCreateReentryStream`, `clbkCloseSession`.
- Заглушки: `clbkSetMeshProperty`, `clbkBeginBltGroup`, `clbkEndBltGroup`.
- Подробно — [orbiter-api.md](orbiter-api.md).
- gcCore: в таблице 10 имён из 52, из них 8 работают (`GetRenderWindow`, `SetMatrix`, `GetMatrix`,
  `RegisterRenderProc`, `SketchpadVersion`, `LoadSurface`, `CreateIPInterface`, `ReleaseIPInterface`), 2 — нулевые
  заглушки (`ScanScreen`, `GetTileData`); список — [extensions.md §2](extensions.md#2-gccore).
- AtmoFX: заглушки `AtmoFX_SetFoam`, `AtmoFX_Mark`, `AtmoFX_MarkPut`, `AtmoFX_MarkCount`, `AtmoFX_MarkGet`.
- D3D11Hud: все 20 функций таблицы реализованы ([extensions.md §4](extensions.md#4-d3d11hud)).

## 4. Возможные ошибки и расхождения

### 4.1. API и общий код

1. **`clbkSetMeshTexture`: индекс.** SDK говорит то «texidx ≥ 0» (`GraphicsAPI.h:548`), то «1..n» (`oapiSetTexture`,
   `OrbiterAPI.h:4714-4719`); код считает индекс слотом клиента 1…n, `texidx = 0` принимает без видимого эффекта
   (`Client.cpp:723-731`). → [orbiter-api.md §4](orbiter-api.md#4-меши-и-визуалы)
2. **`clbkGetMeshGroup`** отдаёт в `TexIdx` слот клиента (индекс + 1, 0 = нет), а не индекс Orbiter / `SPEC_DEFAULT`
   (`Client.cpp:810`). → [orbiter-api.md §4](orbiter-api.md#4-меши-и-визуалы)
3. **Коды возврата материалов.** `clbkSetMeshMaterial`/`clbkMeshMaterial`/`…Ex` при `NULL`-меше возвращают 2
   («не поддерживается» по SDK) вместо 3; `…Ex` на неподдерживаемое свойство возвращает 3 («плохой меш» по SDK)
   (`Client.cpp:736`, `:745`, `:756`, `:768`). → [orbiter-api.md §4](orbiter-api.md#4-меши-и-визуалы)
4. **gcCore.** `GetMatrix` не учитывает `MatrixId`; `GetMeshMaterial`/`SetMeshMaterial` через `Null` возвращают 0 —
   код успеха без заполнения результата; `GetSystemSpecs` не заполняет структуру. → [extensions.md §2](extensions.md#2-gccore)
5. **`RENDERPROC_HUD_1ST`** рисуется поверх HUD Orbiter, а не под ним (`Ext.cpp:124-131`, `Client.cpp:1139`).
   → [extensions.md §2](extensions.md#2-gccore)
6. **HUD-процедуры модулей не снимаются** ни в `clbkCloseSession`, ни при выгрузке модуля: если модуль не вызвал
   `RENDERPROC_DELETE`, в `renderProcs` остаётся указатель на код выгруженной DLL. Выгружаются ли такие модули между
   сценариями, по коду не ясно. → [extensions.md §5](extensions.md#5-extstate-и-потребители)
7. **Состояние модулей между сценариями.** `g_ext`, грязь (`Mesh.cpp:242-254`), метки AR (`Ar.cpp`), пути D3D11Hud
   (`HudApi.cpp:501-510`) — статические и в `clbkCloseSession` не очищаются. Грязь не удаляется и при удалении корабля.
   Вызывает ли ядро `clbkDeleteVessel` для каждого корабля при закрытии сессии, по коду не ясно. Проход AR вызывает
   `oapiGetObjectType(p.body)` для хэндлов из прошлых сценариев (`Ar.cpp:216`). → [extensions.md §5](extensions.md#5-extstate-и-потребители)
8. **Статические ресурсы по указателю устройства.** `Mesh.cpp:210` (карты материалов), `:288` (буфер грязи),
   `:376` (буфер `emis2`), `:386` (состояние фильтра), `AA.cpp:195`, `Planet.cpp:801` пересоздают ресурсы при смене
   `ID3D11Device*`, а не `g_devGen`. Новое устройство, созданное по тому же адресу, получит объекты старого — та же
   ошибка, от которой защищает `g_devGen` (`Client.h:214-217`). → [orbiter-api.md §7, п. 8](orbiter-api.md#7-ловушки)
9. **Излучающие частицы: альфа дважды.** `particles.hlsl` возвращает `rgb·a` с альфой `a`, `bsAdd` умножает на
   `SRC_ALPHA` — итог `rgb·a²` (`Client.cpp:356-361`, `Particles.cpp:185`). → [orbiter-api.md §5](orbiter-api.md#5-потоки-частиц)
10. **Поток частиц без `Attach`.** `PStream::Expired` истинно при `level == NULL` и пустом списке частиц;
    поток из `clbkCreateParticleStream`, не прикреплённый до ближайшего `UpdateRenderParticles`, удаляется клиентом,
    хотя указатель остаётся у создателя (`Particles.cpp:146-150`). Прикрепляет ли ядро поток сразу после создания, по
    коду не ясно. Ссылка на `spec.tex` не берётся. → [orbiter-api.md §5](orbiter-api.md#5-потоки-частиц)
11. **Базовые реализации `oapi::Module`.** `clbkPreStep` и `clbkDeleteVessel` переопределены без вызова базовых
    (`ModuleAPI.h:152`, `:234`, тела в `Orbiter.lib`); делают ли базовые что-либо, по коду не ясно.
12. **Подписи интервалов строки статистики.** `planets` (`qTs[0]…qTs[1]`, `Client.cpp:944-1017`) включает также
    вычисления эффектов, тени, локальные огни, небо и звёзды; `vessels` (`:1017-1079`) — корабли, AO, жидкость,
    Солнце, воздух, облака, ураган, струи, частицы, дождь, собственный корабль; `post` (`:1079-1091`) — резолв/TAA,
    тон-маппинг с bloom и 2D-панели (`Render2DOverlay`). Подробная разбивка — во второй и третьей строках
    (`Client.cpp:1121-1125`).
13. **`cbGrp`** создаётся (`Client.cpp:390`) и нигде не используется.
14. **`shim\shim.def`** в сборке не участвует и расходится с `shim.c` (нет `D3D11Hud_GetApi`). → [extensions.md §1](extensions.md#1-прокладка-d3d9clientdll)
15. **Файлы вне сборки.** `CloudLayer.cpp`, `EarthClouds.cpp`, `Cyclone.cpp` не перечислены в `build.bat`;
    `Client.h:564-566` объявляет `RenderCloudShadow`, `RenderCloudLayer`, `RenderEarthClouds`, которые в собранной
    DLL не определены (линковка проходит, так как вызовов нет). В корне `_src\d3d11client\` лежит пустой `Sun.cpp`
    (0 байт) рядом с `src\Sun.cpp`. → [clouds.md §5](clouds.md#5-код-вне-сборки-2d-слой-облака-земли-по-слою-циклон)
16. **Устаревший комментарий `AA.cpp:1-5`**: «vessel motion vectors … come later», хотя непрозрачные корабли пишут
    векторы движения (`Client.cpp:1020-1031`, `Mesh.cpp:333-338`).
17. **D3D11Hud.** Заголовок `D3D11Hud.h` предлагает gcCore `SetupCustomCamera`/`CustomCameraOnOff` для видов с камер —
    не реализованы; `ArPath` «до выгрузки модуля» — очистки нет, `id` общий для всех модулей.
    → [extensions.md §4](extensions.md#4-d3d11hud)
18. **Выбор адаптера** из Launchpad не влияет на создание устройства (`Client.cpp:316`); на машине с двумя GPU
    используется адаптер по умолчанию.
19. **`tools\run_test.ps1`** копирует сценарий в `Scenarios\Tests\D3D11_test.scn` (`run_test.ps1:32`) и не удаляет его.
20. **Камера под поверхностью** (Луна, 20.10.2026): снизу видна изнанка тайлов — «вторая Луна» над головой. Тайлы
    рисуются без отсечения граней ради юбок (`Planet.cpp:817`, `rsNone`), под грунтом обзор нечему закрыть. Варианты:
    юбки отдельным вызовом без отсечения, тайлы с отсечением задних граней; не пускать камеру под грунт.
21. **Тёмная пыль из-под колёс на Луне** (20.10.2026): причина по коду не установлена. Возможные связи: цвет грунта для
    частиц берётся по таблице альбедо тела, захват цвета сцены выключен (`GpuParticles.cpp:558`,
    [exhaust.md §16.2](exhaust.md#162-замеченные-несоответствия-кода-и-комментариев)); свет Солнца на частицах через
    `ShadowAt`.

### 4.2. Из тематических глав

22. **Ураган снимает карту теней `t7`** (`Storm.cpp:483`): проходы между `RenderStorm` и собственным кораблём
    (GPU-частицы, дождь Титана, брызги) при активном урагане читают пустую карту теней — по коду их солнечный свет
    полностью в тени. → [clouds.md §10](clouds.md#10-ограничения-замечания-подозрительное)
23. **`g_stormLight`** (`FxAtmo.cpp:380`) нигде не присваивается; ослабление света ураганом в экспозиции берётся
    прямым вызовом `StormCameraLight()` (`FxAtmo.cpp:397`) — при появлении присваивания применится дважды.
    → [atmosphere.md §9.2](atmosphere.md#92-расхождения-и-подозрительные-места), [clouds.md §10](clouds.md#10-ограничения-замечания-подозрительное)
24. **Длина штрихов GPU-частиц всегда 0**: `camU.w = 0` (`GpuParticles.cpp:628`), шейдер читает его как выдержку
    кадра. → [exhaust.md §16.2](exhaust.md#162-замеченные-несоответствия-кода-и-комментариев)
25. **Условие тестовых струй** в `Exhaust.cfg` (`D3D11CLIENT_TEST=1`) не совпадает с кодом
    (`D3D11CLIENT_EXHAUST_TEST`); `SmokeEnabled()` кеширует ключ на процесс, `SmokeFx::ReadCfg` — на устройство.
    → [exhaust.md §16.2](exhaust.md#162-замеченные-несоответствия-кода-и-комментариев)
26. **FFT-поле волн** при `Waves = 1` считается каждый кадр без потребителя (`Fx.cpp:70`); плотность жидкости в
    `Wake.cpp`, `FxWaves.cpp`, `Spray.cpp` — 520 / 470 / 450 кг/м³; `SprayFeed` не вызывается; поле iWave и табличный
    кильватер Кельвина в окне поля складываются. → [water.md §8.2](water.md#82-найденное-в-коде)
27. **Стоимость жидкости** — 5–7 мс GPU при MSAA 8× (марш неба на пиксель, SSR, преломление) при цели 144 к/с.
    → [water.md §8.2](water.md#82-найденное-в-коде)
28. **Дождь.** Число колец на жидкости — за кадр, а не за секунду (`Rain.cpp:138`); `Rain` урагана по умолчанию в коде
    выключен, в `Storm.cfg` включён; при шаге ≥ 1 с капли урагана не движутся; при выключенном HUD шлема капли
    изнутри визора не рисуются. → [rain.md §8.1](rain.md#81-ограничения-и-расхождения)
29. **Облака Титана**: у шума один мип-уровень (возможное зерно вдали), азимут ветра не используется, возможный шов на
    180° долготы; `Debug = 2` урагана не обрабатывается шейдером; значения по умолчанию `Storm.cpp` расходятся с
    `Storm.cfg`. → [clouds.md §10](clouds.md#10-ограничения-замечания-подозрительное)
30. **Атмосфера**: устаревшие размеры таблиц в комментариях шейдеров; угловой радиус Солнца в `b4` — константа
    `6.957e8 м`, а не `oapiGetSize` звезды; порог `rho0 < 1e-4` совпадает со значением у Тритона.
    → [atmosphere.md §9.2](atmosphere.md#92-расхождения-и-подозрительные-места)
