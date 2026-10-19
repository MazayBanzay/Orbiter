# D3D11Client — справочник разработчика

Собственный графический клиент Orbiter 2024 на Direct3D 11. Реализует `oapi::GraphicsClient` из
`Orbitersdk\include\GraphicsAPI.h`; написан по SDK, код D3D9Client не используется (у него GPL/LGPL).
Orbiter 2024 — 32-битный, поэтому клиент собирается под **x86**.

> **Срез кода.** Документ описывает исходники `_src\d3d11client` по состоянию на **19.10.2026, 15:21**
> (номера строк сверены с файлами на это время). Пока он писался, две другие сессии меняли код:
> сглаживание (AA.cpp, Client.cpp/Client.h, VideoTab.cpp, `.rc`, aa.hlsl; добавляли TAA с масштабом рендера) и эффекты
> (Fx*.cpp, FxAtmo.cpp, Liquid.cpp, fx_atmo.hlsl). Их части описаны по состоянию на момент чтения и помечены
> «⚠ в работе». После следующих правок номера строк могут сдвинуться — ищите по имени функции.
> Документ написан по чтению кода; сам клиент при этом не собирался и не запускался.

## Содержание

| Файл | О чём |
|---|---|
| [architecture.md](architecture.md) | Модули и файлы, кадр (порядок проходов), системы координат, потоки, владение ресурсами |
| [orbiter-api.md](orbiter-api.md) | Все `clbk*`: что ждёт Orbiter, что делаем мы, что осталось по умолчанию; найденные ловушки |
| [extensions.md](extensions.md) | gcCore (что реализовано), экспорты AtmoFX и `ExtState`, прокладка `D3D9Client.dll` |
| [data-and-config.md](data-and-config.md) | `Config\D3D11Client\*.cfg`, ключи `Config\GC\*.atm.cfg`, форматы тайлов (`.elv`, `.tree`), каталог звёзд, DDS |
| [shaders.md](shaders.md) | Каждый `.hlsl`: константные буферы, регистры b/t/s, общие функции `common.hlsli` |
| [build-and-test.md](build-and-test.md) | Сборка, установка, тестовый прогон, самопроверка в `Orbiter.log`, как добавить проход или `clbk` |
| [roadmap.md](roadmap.md) | Что ещё не реализовано (по коду и по SDK) |

## Коротко

- **Файлы:** `src\Client.cpp` (модуль, устройство, окно, поверхности, кадр), `Mesh.cpp` (меши кораблей, анимации),
  `Planet.cpp` (тайлы, `.tree`, высоты, атмосфера/жидкость из cfg), `Draw.cpp` (2D: Sketchpad на DIB, Blt, слой экрана,
  2D-панели), `Ext.cpp` (gcCore, AtmoFX), `AA.cpp` (MSAA/FXAA/TAA), `Liquid.cpp`, `Particles.cpp`, `Stars.cpp`,
  `Dds.cpp`, `VideoTab.cpp`, `Fx.cpp/Fx.h/FxInternal.h/FxAtmo.cpp` (крючки эффектов и атмосфера снаружи). Шейдеры — `shaders\*.hlsl`, компилируются при запуске из `Modules\D3D11Client\`.
- **Кадр:** тени → небо → звёзды → планеты → корабли (непрозрачные, затем прозрачные) → жидкость →
  `FxRenderTransparent` (атмосфера снаружи) → частицы → резолв MSAA / TAA → тон-маппинг (+ FXAA) в back buffer →
  2D-панели (`Render2DOverlay`) → [после `clbkRenderScene`] HUD-процедуры модулей → слой экрана (GDI) → `Present`.
- **Глубина:** обратный Z, бесконечная дальняя плоскость, ближняя 0.1 м; один буфер D32F на всё от кабины до планет.
- **Координаты:** глобальная система Orbiter со сдвигом начала в камеру (всё в float относительно камеры);
  матрицы — строчные векторы (DirectXMath), `RotToXM` транспонирует матрицы Orbiter.
- **Потоки:** один поток-загрузчик тайлов (создаёт текстуры/буферы через free-threaded `ID3D11Device`);
  контекст устройства — только в главном потоке.
- **Самопроверка:** раз в 10 с строка `D3D11Client: fps … | GPU ms …` в `Orbiter.log`, через 15 с снимок
  `Modules\D3D11Client\shots\start.png`; тестовый прогон — `tools\run_test.ps1` (скрытое окно, без звука,
  cfg и состояние восстанавливаются).

## Глоссарий

| Термин | Значение |
|---|---|
| `Surf` | наша реализация `SURFHANDLE` (текстура D3D11 + при необходимости DIB для GDI) — `Client.h:71` |
| `DevMesh` / `DGroup` | наша реализация `DEVMESHHANDLE` и её группы — `Client.h:107-131` |
| `VesselVis` | наша реализация `VISHANDLE` для корабля — `Client.h:134` |
| `Planet` / `Tile` | планета и узел дерева тайлов — `Client.h:170-236` |
| `PStream` | наш `oapi::ParticleStream` — `Client.h:93` |
| `ExtState` (`g_ext`) | данные, переданные модулями через AtmoFX/gcCore — `Client.h:55` |
| `ClientCfg` (`g_cfg`) | настройки клиента (Advanced) — `Client.h:42` |
| слой экрана | DIB размером с окно, куда Orbiter и модули рисуют HUD/инфо-блоки через Sketchpad `NULL` — `Draw.cpp:316-382` |
| прокладка (shim) | `Modules\D3D11Client\D3D9Client.dll`, переадресующая экспорты в `D3D11Client.dll` — `shim\` |
