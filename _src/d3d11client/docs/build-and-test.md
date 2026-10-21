# Сборка, установка, проверка, расширение

[← оглавление](DEVELOPER.md)

Состояние кода: 21.10.2026

Ссылки вида `Файл:строка` — на `_src\d3d11client\src\`, если не указано иное.

## Содержание

1. [Сборка — `build.bat`](#1-сборка--buildbat)
2. [Установка и включение](#2-установка-и-включение)
3. [Тестовый прогон — `tools\run_test.ps1`](#3-тестовый-прогон--toolsrun_testps1)
4. [Самопроверка](#4-самопроверка)
5. [Как добавить проход](#5-как-добавить-проход)
6. [Как добавить `clbk`](#6-как-добавить-clbk)
7. [Как добавить ключ `Client.cfg`](#7-как-добавить-ключ-clientcfg)
8. [Как добавить шейдер](#8-как-добавить-шейдер)

## 1. Сборка — `build.bat`

Требования: Visual Studio 2022 Build Tools (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`),
Windows SDK 10.0.26100, **x86** (`vcvars32.bat`, `build.bat:4`). Orbiter SDK — `..\..\Orbitersdk` от папки клиента
(`ORB`, `build.bat:12`). Проектного файла нет.

**Папка сборки.** Объектные файлы, PDB компилятора и результат пишутся в `build\<тег>\`, где тег — переменная
окружения `D3D11_BUILD_TAG` (по умолчанию `main`, `build.bat:8-10`): параллельные сборки с разными тегами не делят
объектные файлы и PDB. Готовые `D3D11Client.dll`, `D3D11Client.pdb`, `D3D9Client.dll` копируются в `build\`
(`build.bat:18-20`) — оттуда их берёт `install.bat`. Файл карты `D3D11Client.map` остаётся в `build\<тег>\`.

**Шаги:**

| Шаг | Строка | Команда | Результат |
|---|---|---|---|
| 1 | `build.bat:11` | `rc /fo %OUT%\D3D11Client.res src\D3D11Client.rc` | ресурсы: строки 1000/1001 (1001 = «Graphics engines» — Launchpad показывает DLL как графический движок), диалог `IDD_ADVANCED` |
| 2 | `build.bat:13-16` | `cl` + `link` | `build\<тег>\D3D11Client.dll`, `.pdb`, `.map` |
| 3 | `build.bat:17` | `cl /O2 /LD shim\shim.c … /link /NOENTRY` | `build\<тег>\D3D9Client.dll` — прокладка, все экспорты переадресованы в `D3D11Client.dll` (`shim\shim.c`, `shim\shim.def`) |
| 4 | `build.bat:18-20` | `copy` | копии в `build\` |

**Флаги компилятора** (`build.bat:13-14`): `/std:c++17 /O2 /Zi /EHsc /W3 /MD /utf-8 /D_CRT_SECURE_NO_WARNINGS
/DNDEBUG /DWIN32 /D_WINDOWS /LD`, `/I"%ORB%\Orbitersdk\include"`, `/Fo%OUT%\ /Fd%OUT%\`.
**Флаги компоновщика** (`build.bat:16`): `/DEBUG /MAP /OPT:REF /OPT:ICF`, `/LIBPATH:"%ORB%\Orbitersdk\lib"`.

**Библиотеки:** в командной строке — `Orbiter.lib Orbitersdk.lib user32.lib gdi32.lib`; через `#pragma comment(lib)` —
`psapi`, `advapi32`, `d3d11`, `dxgi`, `d3dcompiler`, `windowscodecs`, `ole32`, `d2d1`, `dwrite`, `msimg32`
(`Client.cpp:11-12, 46-53`), `dxguid` (`Ipi.cpp:9`); `psapi` повторно в `HangWatch.cpp:10`, `HeapTrack.cpp:12`,
`MemCensus.cpp:14`; `windowscodecs`, `ole32` в `Wake.cpp:19-20`.

**Компилируемые файлы** (`build.bat:15`, 39 файлов): `Client`, `Mesh`, `Planet`, `Dds`, `Draw`, `Ext`, `Liquid`,
`VideoTab`, `Particles`, `Stars`, `Fx`, `FxAtmo`, `FxWaves`, `AA`, `Ipi`, `GpuPad`, `Ar`, `Ssao`, `ShaderCache`,
`HangWatch`, `MemCensus`, `HeapTrack`, `Bloom`, `HudApi`, `Wake`, `Exhaust`, `H2Chem`, `Smoke`, `GpuParticles`,
`Rain`, `Spray`, `VesselExhaust`, `Clouds`, `Storm`, `StormRain`, `FxTest`, `Sun`, `Lights`, `VisorDrops` (`.cpp`).

**В `src\` есть, но не собирается:** `CloudLayer.cpp` (2D-слой облаков), `Cyclone.cpp` (движущийся циклон,
читает `Cyclone.cfg`), `EarthClouds.cpp` (объёмные облака Земли по слою, читает `EarthCloudSteps`, `CloudDbg`);
`CloudLayer.h` подключают только они. См. [clouds.md §5](clouds.md#5-код-вне-сборки-2d-слой-облака-земли-по-слою-циклон).

**Шейдеры, на которые не ссылается собираемый код** (копируются `install.bat`, но не компилируются):
`cloudlayer.hlsl`, `cyclone.hlsl`, `earthclouds.hlsl` (только для файлов вне сборки), `resolve.hlsl`, `skypano.hlsl`,
`ssao.hlsl` (ссылок нет ни в одном `.cpp`). Файлы `*.hlsli` подключаются шейдерами через `#include`.

Требования к коду:

- Новый `.cpp` нужно дописать в строку `build.bat:15`.
- `/DNDEBUG` обязателен (assert'ы базового `oapi::Sketchpad`, см.
  [orbiter-api.md §7](orbiter-api.md#7-ловушки-найденные-при-разработке)).
- `#define ORBITER_MODULE` — ровно в одном файле (`Client.cpp:2`).
- Сборка не обращается к Orbiter и возможна при запущенном Orbiter; установка — нет (DLL занята).

## 2. Установка и включение

`install.bat` (`ORB` = `..\..`, `install.bat:4`):

| Строка | Откуда | Куда |
|---|---|---|
| `install.bat:6` | `build\D3D11Client.dll` | `Modules\Plugin\D3D11Client.dll` |
| `install.bat:7-8` | `shaders\*.hlsl`, `shaders\*.hlsli` | `Modules\D3D11Client\` |
| `install.bat:9` | `build\D3D9Client.dll` | `Modules\D3D11Client\D3D9Client.dll` |

PDB и карта не копируются. Orbiter должен быть закрыт. Клиент читает шейдеры только из `Modules\D3D11Client\`:
правка в `shaders\` действует после `install.bat`. Без пересборки DLL новые шейдеры подхватываются при следующем
создании окна рендера (следующий запуск симуляции): кеш (`ShaderCache.cpp`) ключуется хешем исходника.

**Включение.** Клиент — модуль-плагин из `Modules\Plugin\`, который Launchpad показывает в категории «Graphics
engines» (строка 1001 ресурсов). Активные модули записаны в `Orbiter_NG.cfg` в блоке `ACTIVE_MODULES` (строка
`D3D11Client`); вместе с ним не должен быть активен `D3D9Client`. Клиентом пользуется сервер NG
(`Modules\Server\Orbiter.exe`, запускается через `Orbiter_ng.exe`). Для теста клиент передаётся ключом `-p D3D11Client`
(§3).

## 3. Тестовый прогон — `tools\run_test.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_test.ps1 [-Scenario "(Current state)"] [-Seconds 45] [-AA n]
    [-Set "Key = value", "Key2 = value2"] [-Script "t action vessel arg; ..."]
```

### 3.1. Параметры (`run_test.ps1:4`)

| Параметр | По умолчанию | Действие |
|---|---|---|
| `-Scenario` | `(Current state)` | имя сценария относительно `Scenarios\` без `.scn` (например `Tests\Cyclone_KSC`); копируется в `Scenarios\Tests\D3D11_test.scn` |
| `-Seconds` | 45 | `--maxsystime=<Seconds>` для Orbiter; ожидание процесса — `Seconds + 120` с |
| `-AA` | −1 (не менять) | ≥ 0: строка `AntiAliasing = n` в `Client.cfg` на время прогона (прежняя строка `AntiAliasing…` удаляется) |
| `-Set` | — | массив строк `"Ключ = значение"`: каждая заменяет строку с тем же ключом (регулярное выражение `^\s*Ключ\s*=`) или дописывается в `Client.cfg`; ключи — см. [data-and-config.md §1.4](data-and-config.md#14-тестовые-и-диагностические-ключи) |
| `-Script` | — | значение переменной `D3D11CLIENT_TEST_SCRIPT` (§3.4) |

Путь к Orbiter зашит: `$orb = "C:\Games\Orbiter-2024"` (`run_test.ps1:5`).

### 3.2. Порядок работы

1. Именованный мьютекс `Local\D3D11ClientTestRun` — в каждый момент выполняется один прогон; ожидание до 15 мин,
   иначе `another test is still running - test skipped`, код 2 (`run_test.ps1:8-9`).
2. Если запущен `Orbiter` или `Orbiter_ng` — `Orbiter is running - test skipped`, код 2 (`run_test.ps1:10`):
   восстановление в конце затёрло бы cfg и состояние запущенной игры.
3. Копии в `%TEMP%`: `Scenarios\(Current state).scn` → `d3d11client_current_state.scn`, `Orbiter_NG.cfg` →
   `d3d11client_Orbiter_NG.cfg`, `Config\D3D11Client\Client.cfg` → `d3d11client_Client.cfg` (если есть)
   (`run_test.ps1:11-20`).
4. Изменения `Client.cfg` по `-AA` и `-Set` (`run_test.ps1:22-30`, запись в ASCII).
5. Из `Orbiter_NG.cfg` удаляются строки, состоящие только из `XRSound` или `D3D11Client` (без звука; клиент
   передаётся ключом `-p`) (`run_test.ps1:31`).
6. Сценарий копируется в `Scenarios\Tests\D3D11_test.scn` (файл остаётся после прогона) (`run_test.ps1:32`).
7. `D3D11CLIENT_TEST=1`, при `-Script` — `D3D11CLIENT_TEST_SCRIPT` (`run_test.ps1:33-34`).
8. Запуск `Modules\Server\Orbiter.exe -p D3D11Client -s "Tests\D3D11_test" --maxsystime=<Seconds> -x`, окно свёрнуто,
   рабочая папка — корень Orbiter (`run_test.ps1:35-36`). Не дождавшись выхода за `Seconds + 120` с, процесс
   убивается: `killed (hang)` (`run_test.ps1:37`). Печатается `exit code: <код Orbiter>`.
9. Копии результатов в `tools\logs\` (`run_test.ps1:40-47`): `Orbiter.log` → `<ЧЧММСС>_<сценарий>.log` (в имени
   сценария все символы, кроме латиницы и цифр, заменены на `_`); собственный снимок прогона
   `Modules\D3D11Client\shots\start_<pid>.png` **переносится** в `<ЧЧММСС>_<сценарий>.png`; если его нет — копируется
   `shots\start.png` с предупреждением `shot: no own shot, start.png copied (may be another run's)`. Печатается
   `log: <путь>`.
10. В `finally` (`run_test.ps1:48-55`): удаляются `D3D11CLIENT_TEST` и `D3D11CLIENT_TEST_SCRIPT`; возвращаются
    `(Current state).scn`, `Orbiter_NG.cfg` и `Client.cfg`. Если `Client.cfg` не было, а `-AA`/`-Set` заданы, файл
    заменяется одной строкой-комментарием (не удаляется).

Коды выхода скрипта: 2 — прогон пропущен (пп. 1, 2); в остальных случаях явный код не задаётся. Код самого Orbiter —
в строке `exit code:`.

Общие файлы: `Orbiter.log`, `shots\start.png`, `Scenarios\Tests\D3D11_test.scn`, `Client.cfg` — общие для всех
прогонов и обычной игры. Снимок прогона определяется по `start_<pid>.png`; принадлежность лога и снимка проверяется
по строке `screenshot …` и по планете/сценарию в копии лога. Обычный запуск Orbiter во время прогона увидит
изменённые `Client.cfg` и `Orbiter_NG.cfg`.

### 3.3. Переменные окружения клиента

Скрипт ставит только `D3D11CLIENT_TEST` и `D3D11CLIENT_TEST_SCRIPT`; остальные переменные наследуются процессом Orbiter
из окружения, в котором запущен скрипт, и скриптом не снимаются.

| Переменная | Значение | Только при `D3D11CLIENT_TEST=1` | Где | Действие |
|---|---|---|---|---|
| `D3D11CLIENT_TEST` | `1` | — | `Client.cpp:131-136` | тестовый режим (`testMode`): окно скрыто и не берёт фокус (`WS_EX_NOACTIVATE \| WS_EX_TOOLWINDOW`, `SW_HIDE`, повторно в `Client.cpp:1138`); `Present` без vsync (`Client.cpp:1164`); `TestScript` и `FxTestFrame` каждый кадр (`Client.cpp:932`); `diagFrames` на кадрах 300–301 (строки `diag cockpit …`, `local lights …`, дамп `Ipi.cpp:242`); снимок `start_<pid>.png`; строки `elev check` (`Planet.cpp:821`); фильтр падений завершает процесс без диалога (`HangWatch.cpp:161`) |
| `D3D11CLIENT_TEST_SCRIPT` | сценарий действий (§3.4) | да | `TestScript`, `Client.cpp:1478-1553` | — |
| `D3D11CLIENT_TEST_RES` | `ШxВ` (≥ 320×200) | нет | `Client.cpp:120-127` | размер цепочки обмена независимо от окна; строка `test: frame forced to …` |
| `D3D11CLIENT_TEST_SHOT` | секунды (≥ 1) | нет | `Client.cpp:1142-1146` | время снимка `start.png`; приоритет над ключом `TestShot` |
| `D3D11CLIENT_TEST_BURST` | n (0…32) | да | `Client.cpp:1156-1163` | n кадров подряд после снимка: `shots\burst_<pid>_<nn>.png` |
| `D3D11CLIENT_TEST_HANG` | любое | да | `Client.cpp:926-928` | кадр 600 задерживается на 6 с (проверка `hang.log`) |
| `D3D11CLIENT_TEST_CRASH` | любое | да | `Client.cpp:929-930` | запись по адресу 0x10 на кадре 600 (проверка `crash.log`) |
| `D3D11CLIENT_TEST_AR` | `1` | нет | `Ar.cpp:122-127` | тестовая сцена дисплея шлема перед фокусным кораблём |
| `D3D11CLIENT_TEST_WAVES` | `1` | нет | `Wake.cpp:266-269` | источник волн, идущий через жидкость в 3 м от фокуса |
| `D3D11CLIENT_TEST_JET` | `1` | нет | `Wake.cpp:453-456` | два сопла по 87 Н в 1.2 м над жидкостью |
| `D3D11CLIENT_FXWAVETEST` | `1` | нет | `FxWaves.cpp:208-211` | то же для поля волн FFT |
| `D3D11CLIENT_EXHAUST_TEST` | путь к файлу | нет | `Exhaust.cpp:42` | ключи `TestJet`, `TestHeat` из этого файла ([exhaust.md §3.3](exhaust.md#3-входные-данные-откуда-берутся-струи)) |
| `D3D11CLIENT_RAIN` | мм/ч | нет | `Rain.cpp:40`, `VisorDrops.cpp:77` | интенсивность дождя Титана поверх `Fx.cfg`/`Client.cfg` |
| `D3D11CLIENT_LIQDBG` | n | нет | `Liquid.cpp:285` | отладочный вид жидкости |
| `D3D11CLIENT_AODBG` | любое | нет | `Ssao.cpp:63` | только затенение GTAO |
| `D3D11CLIENT_SHADOWDBG` | n | нет | `Client.cpp:1273-1275` | 1: только видимость Солнца (`planet.hlsl`, `mesh.hlsl`) |
| `D3D11CLIENT_NOMIPGEN` | `1` | нет | `Dds.cpp:175` | без построения мипов у DXT-текстур без мипов |
| `D3D11CLIENT_NOMAPS` | `1` | нет | `Mesh.cpp:208` | без сопутствующих карт материалов |
| `D3D11CLIENT_FINETILES` | любое | нет | `Planet.cpp:979` | всегда полная сетка тайла |
| `D3D11CLIENT_NOSKIRTS` | любое | нет | `Planet.cpp:984` | тайлы без «юбок» (замер) |

### 3.4. Сценарий действий (`D3D11CLIENT_TEST_SCRIPT`)

Формат: `t действие корабль [арг]; …`, `t` — секунды системного времени от первого кадра (`TestScript`,
`Client.cpp:1478-1553`). Аргумент `in`/`out` превращается в 1/0. Каждое действие пишет строку
`test script: t <t> <действие> <корабль> <арг>`, затем `memory after the action: …`, и включает трассировку крупных
выделений на 60 кадров (`trace: …`, `memory frame N: …` при изменении на ≥ 2 МБ).

| Действие | Аргумент | Действие |
|---|---|---|
| `lift` | метры | корабль поднят над текущей точкой (`DefSetStateEx`, `status = 0`) |
| `cam` | `in` / `out` | камера на корабль, внутренняя / внешняя |
| `focus` | — | фокус на корабль |
| `dist` | метры | внешняя камера на этом расстоянии |
| `polar`, `azim` | градусы | поворот внешней камеры |
| `fov` | градусы | поле зрения |
| `look` | градусы | поворот взгляда из кабины |
| `orbit` | °/с | с этого момента внешняя камера вращается по азимуту |
| `sun` | поле зрения, ° | камера кабины ищет Солнце (по шагу за кадр) и приближает; по окончании `test script: camera on the Sun, … deg off` |

Корабль ищется по имени; если его нет — `test script: … - no vessel …`. Комментарий над функцией перечисляет не все
действия (нет `fov`, `look`, `orbit`, `sun`).

### 3.5. Другие скрипты `tools\`

| Скрипт | Назначение |
|---|---|
| `sun_fetch.py` | загрузка реальных снимков Солнца (SDO/HMI, SOHO, GONG, LASCO через Helioviewer) и подготовка карт `wl.bin`, `ha.bin`, `halimb.bin`, `c2.bin`, `meta.txt` в `Modules\D3D11Client\sun\<ГГГГММДД>\`; загрузки хранятся в `tools\sun_cache` |
| `bake_splash_blender.py` | запекание кадров брызг (Mantaflow FLIP в Blender) для колеса или шага; параметры через переменные `SPLASH_OUT`, `SPLASH_KIND`, `SPLASH_RES`, `SPLASH_FPS` |
| `pack_splash.py` | упаковка 64 кадров запекания в атлас 8×8 `Textures\D3D11Client\splash_<вид>.dds` (RGBA8) |
| `bake_spray.py` | собственная симуляция (numpy, scipy) листа жидкости с колеса → `Textures\D3D11Client\spray_sheet.dds` (DX10, RGBA8) |

Папка `tools\logs\` — копии логов и снимков прогонов; `tools\sun_cache\` — загруженные `.jp2`.

## 4. Самопроверка

Все строки клиента в `Orbiter.log` начинаются с `D3D11Client: ` (макрос `LOGC`, `Client.h:36`). Журнал Orbiter
буферизован: при падении процесса последние строки могут не попасть в файл (комментарий `HangWatch.cpp:93-97`); для
падений и зависаний предназначены отдельные файлы (§4.4, §4.5).

### 4.1. Строки при старте

Порядок — по коду (значения условные):

```
D3D11Client: heap by module: on (N modules patched)            ; только HeapByModule = 1 (InitModule, HeapTrack.cpp:267)
D3D11Client: extension shim loaded (gcCore, AtmoFX)             ; LoadShim, Ext.cpp:145; иначе "NOT loaded"
D3D11Client: initialised, window 1920x1080                      ; clbkInitialise, Client.cpp:81
-- окно рендера (clbkCreateRenderWindow, каждый запуск симуляции) --
D3D11Client: test: frame forced to WxH (window WxH)             ; D3D11CLIENT_TEST_RES
D3D11Client: test mode: hidden window                           ; D3D11CLIENT_TEST=1
D3D11Client: driver shader cache 1024 MB (over 600): set aside, the driver starts a new one   ; TrimDriverCache
D3D11Client: driver shader cache: off for this process (...)    ; DriverCacheGuardBefore, DriverCacheOff = 1
D3D11Client: driver shader cache: N file(s), M MB mapped (set aside at the next start above 600 MB)   ; NoteDriverCache
D3D11Client: driver shader cache: N cache file open(s) refused  ; DriverCacheOff = 1
D3D11Client: adapter <имя>, <МБ> MB, driver a.b.c.d, driver threads on   ; Client.cpp:328
D3D11Client: processor <имя>, N logical cores                   ; Client.cpp:334
D3D11Client: anti-aliasing: MSAA, 8 sample(s), scene WxH of WxH ; CreateSceneTargets, AA.cpp:67
D3D11Client: shader <путь> <вход>: <сообщение компилятора>      ; только при ошибках/предупреждениях
D3D11Client: effects: atmosphere ready, waves off (Config\D3D11Client\Fx.cfg)   ; Fx.cpp:40
D3D11Client: exhaust effects: glow off, plumes on, ...          ; Exhaust.cpp:237
D3D11Client: tile loader: N threads                             ; Client.cpp:148
D3D11Client: render window WxH, D3D11 device ready              ; Client.cpp:152
-- clbkPostCreation --
D3D11Client: planet <имя>: R <км> km, max level N, archives: surf yes, elev yes, atmosphere, liquid, micro textures   ; Planet.cpp:152
D3D11Client: stars: N                                           ; Stars.cpp:27
D3D11Client: session: N planets, M vessels                      ; Client.cpp:542
-- далее по первому использованию --
D3D11Client: atmosphere <планета>: ... ; atmosphere <планета> tables: ...   ; FxAtmo.cpp:118, 200
D3D11Client: local lights: on (lux per unit 10.0, ...)          ; Lights.cpp:160
D3D11Client: visor drops: on (inside 1, outside 1, ...)         ; VisorDrops.cpp:55
D3D11Client: bloom: on (5 levels) / ambient occlusion: on (...) / clouds: ... / storm: ... / rain: ...
D3D11Client: screenshot Modules\D3D11Client\shots\start.png     ; SaveShot, Client.cpp:1193
```

Признаки неисправности:

- `FAILED` / `NOT` в строке клиента: `device creation FAILED`, `D3D11CreateDevice failed 0x…`, `swap chain failed 0x…`,
  `shader compilation FAILED - see above; the scene is not drawn` (ошибка в обязательных шейдерах `CreateShaders`,
  `Client.cpp:488-532`: сцена не рисуется вовсе), `extension shim NOT loaded`, `<эффект>: shaders FAILED`,
  `… NOT available`.
- `shader <путь> <вход>: …` — сообщения компилятора (выводятся и предупреждения).
- `texture not loaded: <имя>` — текстура меша не найдена ни в `Textures2\`, ни в `Textures\`.
- `surface WxH not made (attrib 0x…)` — не создана поверхность (обычно нехватка памяти).
- `FATAL terminate: <тип>: <what>` и строки `  #NN <модуль>+0x<смещение>` — необработанное исключение C++ в потоке
  рендера (`LogTerminate`, `Client.cpp:25-44`; без этого процесс завершился бы `abort` без дампа).

### 4.2. Периодические строки

Раз в 10 с при `StatsLog = 1` (`Client.cpp:1114-1132`):

```
fps 144 | GPU ms: planets 1.20, vessels 3.40, post 0.90 | tiles drawn 180, load queue 0 | vessels 3
GPU ms by pass: fx compute …, shadows …, sky …, planets …, vessels opaque …, liquid …, transparent + air …, exhaust/AR/particles …, own vessel …, resolve + post …, 2D …
GPU ms within: AO … | the Sun …, transparent vessels …, storm shadow …, air …, storm …, clouds …, lights in the air …
tile loads: N in T s (r/s) | ms each: surface read …, decode + mips …, mask + coast …, elevation …, mesh …
memory now: …                                                    ; §4.3
```

- `GPU ms: planets, vessels, post` — три крупных интервала по меткам времени `qTs` (`Client.cpp:944, 1017, 1079,
  1091`): от начала кадра до конца планет (включая вычисления эффектов, тени, небо); от кораблей до ореола Солнца
  (корабли, жидкость, воздух, эффекты, собственный корабль); резолв, тон-маппинг и 2D. Сглаживание 0.9/0.1, задержка 3
  кадра.
- `GPU ms by pass` — разности соседних меток `GpuMark` 0…11 (`Client.cpp:945-1092`); `GPU ms within` — метки
  `GpuSub` внутри интервалов 5 и 7 (`Client.cpp:1107-1110`).
- `load queue` — длина очереди тайлов; в покое должна опускаться к 0. `tiles drawn` — тайлы, нарисованные в кадре.
- `tile loads` — только если за интервал были загрузки (`Planet::LoadStats`).

Строки модулей: `exhaust: GPU … ms | …` (10 с, только при `StatsLog`, `Exhaust.cpp:662, 680`), `visor drops: … | GPU ms …`
(10 с, `VisorDrops.cpp:716-720`), `storm: GPU ms …` (10 с, `Storm.cpp:499`), `storm rain: …` (20 с), `storm rain map: …`
(20 с), `rain: … km from the area's centre …` (30 с), `local lights: …` (раз в 600 кадров), `local lights: the eye …`
(раз в 600 кадров), `2D ms/s: …` и `diag 2D: …` (раз в 5 с, при заметной нагрузке, `Draw.cpp:59-72`). В тестовом
режиме — `elev check …`, `elev view …` раз в 300 кадров.

### 4.3. Строка `memory now`

`LogMemory` (`Client.cpp:1432-1476`) при каждой строке `fps` (и `memory after the action`, `memory frame N` в
сценариях):

```
memory now: private 812 MB, address space free 690 MB, largest free block 240 MB | used: image 180, mapped 75, private 1010, reserved 160 MB | client surfaces 42 (63 MB on the GPU), CPU copies 12 (9 MB)
```

| Поле | Источник | Смысл |
|---|---|---|
| `private` | `PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` | закоммиченная частная память процесса |
| `address space free` | сумма областей `MEM_FREE` (`VirtualQuery`) | свободные адреса процесса |
| `largest free block` | наибольшая область `MEM_FREE` | предел для одного крупного выделения или отображения |
| `image` | `MEM_IMAGE` | код и данные загруженных модулей |
| `mapped` | `MEM_MAPPED` | отображённые файлы и секции (в т.ч. дисковый кеш шейдеров драйвера) |
| `private` (в `used`) | закоммиченные частные области | кучи, буферы |
| `reserved` | `MEM_RESERVE` | только зарезервированные адреса (резервы куч, стеки потоков, драйвер) |
| `client surfaces` | `g_surfLive`, `g_surfMB` (`MakeSurf`, `Client.cpp:624-645`) | поверхности клиента на GPU |
| `CPU copies` | `g_dibLive`, `g_dibMB` | DIB-копии поверхностей в памяти процесса |

Чтение: Orbiter 2024 — 32-битный процесс; адресное пространство кончается раньше физической памяти. Утечка выглядит
как устойчивый рост `private`/`used` и падение `free` от строки к строке. Падение `largest free block` при достаточном
`free` — фрагментация: крупное выделение (текстура, рост файла кеша драйвера) не находит непрерывного блока. Если
`mapped` больше 256 МБ, один раз за запуск выводится список: `memory: N mapped views, K of them without a file (M MB)` и
до 15 строк `memory:   mapped <МБ> MB at <адрес>  <файл или (no file)>` (`Client.cpp:1454-1475`); файл
`…\NVIDIA\DXCache\….nvph` в списке означает отображённый кеш драйвера (§4.6).

Дополнительно: `MemCensus = N` — перепись адресного пространства по владельцам (`memory census: …`: образы модулей,
кучи, крупные блоки куч, стеки, прочее; `MemCensus.cpp`) при 2-й и N-й строке; `HeapByModule = 1` — после каждой строки
`heap by module: …`, `heap live: …`, `heap site: …` (`HeapTrackReport`, `HeapTrack.cpp:271-335`).

### 4.4. `hang.log` — зависание

`HangWatchStart` (`HangWatch.cpp:165-180`, вызов `Client.cpp:150`) запускает поток, который каждые 250 мс сравнивает
номер кадра. Если после 300-го кадра новых кадров нет 4 с, один раз на каждую остановку в
`Modules\D3D11Client\hang.log` **дописывается** (`Dump`, `HangWatch.cpp:37-91`):

```
=== ЧЧ:ММ:СС no frame for 4 s: frame <номер> ===
module D3D11Client.dll at <база hex>
thread <tid>: eip <модуль>+<смещение hex> | <модуль>+<смещение> <модуль>+<смещение> ...
...
```

Для каждого потока (кроме самого сторожа): `eip` и до 28 адресов возврата, найденных сканированием копии стека (до
128 КБ) — значения внутри кода модулей, перед которыми стоит инструкция `CALL` (`AfterCall`, `HangWatch.cpp:25-35`).
Сканирование эвристическое: в списке могут быть устаревшие адреса из старых кадров стека. Поток не загружает
символы, не берёт loader lock и не пишет в `Orbiter.log`. Проверка механизма — `D3D11CLIENT_TEST_HANG`.

### 4.5. `crash.log` — падение

`CrashFilter` (`HangWatch.cpp:115-163`) устанавливается через `SetUnhandledExceptionFilter` при первом
`HangWatchStart`. Первое необработанное исключение из любого потока **дописывает** в
`Modules\D3D11Client\crash.log`:

```
=== ГГГГ-ММ-ДД ЧЧ:ММ:СС crash: code <код hex> at <модуль>+<смещение hex>, thread <tid>, frame <номер> ===
access violation: reading|writing|executing address <адрес>        ; только для 0xC0000005
eax … ebx … ecx … edx … esi … edi … ebp … esp …
module D3D11Client.dll at <база>                                   ; и все модули, имя которых начинается с "nv"
stack: <модуль>+<смещение> ...                                      ; до 40 адресов возврата от esp
shaders made last (oldest first, of N): [кадр] <файл> <вход>; ...   ; последние 24 компиляции
```

Список шейдеров заполняет `NoteShader` (`HangWatch.cpp:104-111`), вызываемый из `CompileFile` (`Client.cpp:481`):
драйвер компилирует новый шейдер в своих потоках, и падение в его компиляторе не имеет кода клиента на стеке. Вызовы
`FxCompile` и прямые вызовы `CachedCompileFromFile` в список не попадают (§8).

Затем исключение передаётся прежнему фильтру (обычная обработка Windows); в тестовом режиме фильтр возвращает
`EXCEPTION_EXECUTE_HANDLER` — процесс завершается без диалога ошибки. Проверка механизма — `D3D11CLIENT_TEST_CRASH`.

### 4.6. Защита от кеша шейдеров драйвера

Драйвер NVIDIA при создании устройства отображает в процесс весь файл дискового кеша шейдеров
(`%LOCALAPPDATA%\NVIDIA\DXCache\*.nvph`); файл растёт удвоением (256, 512, 1024 МБ). Две независимые меры
(`Client.cpp:156-305`), обе — в `CreateDevice` до `D3D11CreateDevice`:

| Мера | Ключ | Как работает | Строки журнала |
|---|---|---|---|
| Перенос большого кеша | `DriverCacheMB` (600; 0 — выкл.) | после создания устройства пути отображённых файлов кеша пишутся в `Modules\D3D11Client\drivercache.txt` (`NoteDriverCache`); при следующем старте файл больше порога переносится в `…\NVIDIA\DXCache_Orbiter_old\` с заменой (`TrimDriverCache`); драйвер начинает новый | `driver shader cache <МБ> MB (over N): set aside…` / `could not be moved (in use?)`; `driver shader cache: N file(s), M MB mapped (…)` |
| Запрет кеша | `DriverCacheOff` (0) | до создания устройства загружается `NvMemMapStorage.dll` — путь из `Modules\D3D11Client\nvmemmap.txt` (записан при прошлом старте, `DriverCacheGuardAfter`) или, при его отсутствии, из реестра драйвера (`UserModeDriverNameWow` в классе видеоадаптеров, `FindNvMemMapLib`); её импорт `CreateFileW` из `KERNEL32.dll` заменяется функцией, отказывающей в открытии `*.nvph` (`ERROR_ACCESS_DENIED`). Действует только в этом процессе; системные настройки не меняются | `driver shader cache: off for this process (…)`; `… off asked, no NVIDIA cache library found …`; `… could not be loaded (driver updated?) - the cache stays on`; `… the library's import was not found - the cache stays on`; после создания — `N cache file open(s) refused` |

Дополнительно компиляции шейдеров разнесены во времени: не чаще одной в 12 мс (`kShaderGapMs`, `Client.cpp:463-486`),
а загрузчики тайлов ждут 500 мс после последней компиляции (`g_shaderTick`, `Client.cpp:606-614`). `DriverThreads = 0`
запрещает драйверу собственные рабочие потоки компиляции (`Client.cpp:315`).

### 4.7. Разбор падения

1. Найти последний блок `crash.log` (время, кадр). Если падение в потоке рендера по исключению C++ — строки
   `FATAL terminate` в `Orbiter.log`.
2. **Модуль `D3D11Client.dll`:** перевести смещение в функцию. В `build\<тег>\D3D11Client.map` (тот тег, которым собрана
   установленная DLL) — адрес загрузки по умолчанию (`Preferred load address`) в начале файла; адрес = база по
   умолчанию + смещение; функция — ближайшая запись с меньшим `Rva+Base` в таблице `Publics by Value`. PDB —
   `build\D3D11Client.pdb` (копия от последней сборки любого тега) или `build\<тег>\D3D11Client.pdb`; PDB должен быть от
   той же сборки, что и `Modules\Plugin\D3D11Client.dll` (сверка по времени файлов).
3. **Модуль драйвера (`nv*.dll`):** `nvgpucomp32.dll` — компилятор шейдеров драйвера: смотреть
   `shaders made last`; `NvMemMapStorage.dll` — кеш шейдеров драйвера: смотреть строки `memory now` перед падением
   (`mapped`, `largest free block`) и включить `DriverCacheOff = 1` (§4.6).
4. **Нехватка адресов** (`bad_alloc`, `surface … not made`, падение при выделении): строки `memory now`; при
   необходимости — прогон с `-Set "MemCensus = 1"` или `"HeapByModule = 1"` (§4.3).
5. Зависание без падения — `hang.log` (§4.4), смещения переводятся так же.

## 5. Как добавить проход

1. **Шейдер** — `shaders\<имя>.hlsl` (§8). `#include "common.hlsli"`, если нужны `b0` (`cbFrame`), дымка, свет, тени.
   Регистры — [shaders.md](shaders.md#регистры).
2. **Компиляция.**
   - Обязательная часть сцены — в `CreateShaders` (`Client.cpp:488-532`): при ошибке `shadersOk = false`, сцена не
     рисуется (`Client.cpp:923`).
   - Необязательный эффект — ленивая инициализация в своём модуле при первом использовании через
     `CompileShaderFile` (`Client.cpp:461` → `CompileFile`, `Client.cpp:470-486`): кеш, интервал 12 мс, запись в список
     `crash.log`, пауза загрузчиков тайлов. При ошибке — строка `<эффект>: shaders FAILED` и выключение только этого
     эффекта. `FxCompile` (`Fx.cpp:7-16`) и прямой вызов `CachedCompileFromFile` (`Ar.cpp`, `GpuPad.cpp`, `HudApi.cpp`,
     `Wake.cpp`, `Ipi.cpp`) используют кеш, но без интервала и без записи в список.
3. **Объекты устройства в `static` — поколение устройства `g_devGen`** (`Client.h:214-217`, `Client.cpp:307-310`).
   Устройство и окно создаются заново при каждом запуске симуляции; объекты старого устройства, переданные контексту
   нового, вызывали ошибку GPU и потерю устройства. Образец (`Bloom.cpp:21-28`):
   ```cpp
   static unsigned gen = 0;
   if (g_tried && gen != g_devGen) { /* Reset() всех объектов */ g_tried = false; }
   if (!g_tried) { g_tried = true; gen = g_devGen; /* создание */ }
   ```
   Так же устроены `Ar.cpp:32-33`, `GpuPad.cpp:29-30`, `HudApi.cpp:37-38`, `Wake.cpp:114-115`, `Ssao.cpp:26-32`,
   `CloudLayer.cpp:65`. Часть модулей сравнивает указатель на устройство (`Clouds.cpp`, `Lights.cpp:107`,
   `VisorDrops.cpp:182`, `Mesh.cpp:210`, сэмплер в `Planet.cpp:801-807`); указатель нового устройства может совпасть со
   старым, поэтому для нового кода — `g_devGen`.
4. **Ресурсы и сглаживание.** Сцена рисуется в `hdrRTV`/`dsv` размера `RW×RH` (при TAA — `RenderScale` от окна
   `W×H`) с `samples` выборками (`SceneSamples`, `AA.cpp:10-13`; цели — `CreateSceneTargets`, `AA.cpp:15-69`). При TAA
   непрозрачные корабли пишут ещё `mvRTV` (векторы движения). Для чтения цвета/глубины сцены — `CopySceneColour(dst)`
   (`AA.cpp:87`: одновыборочная копия цвета и резолв `depthSRV`). Собственная цель рендера в сцене должна совпадать по
   числу выборок. Размеры, зависящие от окна, пересоздаются при их изменении.
5. **Константы.** `b0` — `cbFrame` (1024 байта, не трогать). `b1` — общий `cbObj` (1024 байта, `Client.cpp:390`;
   `Map(WRITE_DISCARD)` перед каждым draw). Свои буферы — на других слотах (`b3` — проход атмосферы, `b4` —
   `FxAtmoMedia`, `b9` — локальные источники; см. [shaders.md](shaders.md#регистры)).
6. **Вызов** — в нужное место `clbkRenderScene` (`Client.cpp:921-1133`; порядок —
   [architecture.md §3](architecture.md#3-кадр)) или в крючки `FxFrame` / `FxRenderTransparent` (`Fx.cpp`). Время
   прохода попадёт в охватывающий интервал `GPU ms by pass`; собственный замер — своими запросами
   `D3D11_QUERY_TIMESTAMP` (как в `Storm.cpp`, `VisorDrops.cpp`, `Exhaust.cpp`).
7. **Состояние.** Проход сам выставляет всё нужное: RT/DSV, viewport (`RW×RH`), blend, depth, rasterizer, input layout,
   топологию, шейдеры, CB, SRV, сэмплеры. Глубина — обратный Z с бесконечной дальней плоскостью (`dsWrite`/`dsRead` —
   `GREATER_EQUAL`, очистка 0); ближняя плоскость 0.1 м, изнутри корабля 0.015 м (`Client.cpp:969`), значение — из
   `P._43`. После прохода — отвязать свои SRV и вернуть RT/DSV сцены, если менялись. Сэмплеры сцены: `s0` —
   анизотропный wrap, `s1` — clamp, `s2` — сравнение для теней; `t7` — карта теней (`Client.cpp:989-991`).
8. **Сборка** — новый `.cpp` в `build.bat:15`.
9. **Документация** — порядок кадра в [architecture.md](architecture.md), раздел в [shaders.md](shaders.md), строки
   журнала — в тематическую главу.

## 6. Как добавить `clbk`

1. Найти виртуальный метод в `Orbitersdk\include\GraphicsAPI.h` и прочитать его комментарий (коды возврата, владение
   памятью, вызов на паузе).
2. Объявить `override` в `Client.h` с точной сигнатурой (включая `const` и аргументы по умолчанию).
3. Реализовать: всё в главном потоке; `SURFHANDLE` → `Surf*` (`NULL` у 2D-методов — слой экрана), `DEVMESHHANDLE` →
   `DevMesh*`, `VISHANDLE` → `VesselVis*`. Не возвращать NULL там, где ядро не проверяет результат.
4. Метод, меняющий поверхность, вызывает `MarkDirty` — поверхность уходит на GPU в начале следующего кадра
   (`FlushDirty`, `Client.cpp:939`).
5. Метод, создающий объекты устройства, учитывает пересоздание устройства (§5 п. 3) и освобождение в
   `clbkDestroyRenderWindow` (`Client.cpp:556-577`).
6. Обновить таблицу в [orbiter-api.md](orbiter-api.md) и, если что-то не сделано, [roadmap.md](roadmap.md).

## 7. Как добавить ключ `Client.cfg`

**Обычный ключ (настройка игрока):**

1. Поле с инициализатором (значение по умолчанию) в `ClientCfg`, `Client.h:42-65`.
2. Разбор в `ClientCfg::Load` (`VideoTab.cpp:21-41`). Имя ключа — только латинские буквы (шаблон
   `" %63[A-Za-z] = %lf"`); ограничение диапазона — здесь же (`std::clamp`), если значение вне диапазона опасно.
3. Запись в `ClientCfg::Save` (`VideoTab.cpp:46-73`) с комментарием. Без этого кнопка OK диалога Advanced удалит ключ
   из файла.
4. Элемент диалога (по необходимости): идентификатор в `resource.h`, элемент в `IDD_ADVANCED` (`D3D11Client.rc`),
   заполнение в `WM_INITDIALOG` и чтение по `IDOK` в `AdvProc` (`VideoTab.cpp:209-238`).
5. Использование — через `g_cfg.<поле>`. Чтение каждый кадр даёт действие со следующего запуска симуляции; чтение при
   создании ресурсов — с пересоздания устройства.
6. Документация — [data-and-config.md §1.2](data-and-config.md#12-основные-ключи-clientcfg).

Собственный читатель модуля (как `Lights.cpp:25-44`, `VisorDrops.cpp:32-56`) не попадает в `Save`: такие ключи
теряются при OK в диалоге (см. [data-and-config.md §1.3](data-and-config.md#13-ключи-модулей-читаются-своими-читателями)).

**Тестовый или диагностический ключ:** `double FxTestNum(const char* key)` (`FxTest.cpp:41-45`; 0 — ключа нет).
Функция читает файл при каждом вызове — значение кешируется в `static const` при первом обращении. Ограничение
тестовым режимом — явной проверкой `testMode`, если ключ не должен действовать в обычной игре. Ставится в прогоне
через `run_test.ps1 -Set "Ключ = значение"`; документация —
[data-and-config.md §1.4](data-and-config.md#14-тестовые-и-диагностические-ключи).

## 8. Как добавить шейдер

1. Файл `shaders\<имя>.hlsl`; общие части — `*.hlsli`. Модель шейдеров — 5.0 (`vs_5_0`, `ps_5_0`, `cs_5_0`; у тесселяции
   жидкости — свои цели), флаг `D3DCOMPILE_OPTIMIZATION_LEVEL3`.
2. `install.bat` копирует все `*.hlsl` и `*.hlsli` в `Modules\D3D11Client\`; клиент компилирует оттуда.
3. Кеш (`CachedCompileFromFile`, `ShaderCache.cpp:37-57`): ключ — FNV-1a 64 от байтов файла и, рекурсивно (до 16
   уровней), файлов из `#include "…"` (путь относительно папки файла), точки входа, цели, флагов и макросов; байткод —
   `Modules\D3D11Client\shadercache\<ключ>.cso`. Изменение исходника или подключаемого файла даёт новый ключ и новую
   компиляцию. Подключения в угловых скобках в ключ не входят — их изменение кеш не заметит. Если основной файл не
   найден, кеш не используется. Устаревшие `.cso` не удаляются.
4. Ошибки и предупреждения компилятора — строка `shader <путь> <вход>: <текст>` (`Client.cpp:483`, `Fx.cpp:12`) или
   строка модуля (`ar.hlsl VS: …`, `sketch.hlsl PS: …`, `wavefield.hlsl …`).
5. Компилировать через `CompileShaderFile` (§5 п. 2).
6. Описание — в [shaders.md](shaders.md#файлы).
