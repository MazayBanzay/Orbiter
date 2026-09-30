# «Тантра» — Orbiter 2016

Аддон звездолёта «Тантра» из романа И. А. Ефремова «Туманность Андромеды» для Orbiter 2016
(D3D9Client R4.26, XRSound 2.0, UACS 1.0).

Репозиторий лежит в корне установки Orbiter 2016 и отслеживает только файлы проекта
(см. `.gitignore` — белый список); сама игра и сторонние аддоны в него не входят.

## Состав

| Путь | Что это |
|---|---|
| `Tantra_Design/blender/` | Скрипты Blender (запуск без окна через `run.ps1`): астронавигатор на базе MPFB2/MakeHuman, комбинезон, превью |
| `Tantra_Design/*.html` | Эскизы скафандров и 3D-прототип |
| `Orbitersdk/resources/Orbiter.props` | Лист свойств MSBuild для сборки под SDK 2016 в VS2022 |
| `Scenarios/UACS/Tantra*.scn` | Тестовые сценарии |
| `Tantra_Design/mocap/` | Клипы захвата движения CMU Graphics Lab (BVH, MotionBuilder-friendly конверсия cgspeed); данные бесплатны, запрещена лишь их перепродажа |
| `Config/Vessels/UACS/Astronauts/Z2.cfg` | Конфиг астронавта UACS с описанием суставов для анимации ходьбы |

Форк UACS с анимацией ходьбы — отдельный репозиторий (`Orbitersdk/UACS`, ветка `tantra`).

## Не в репозитории (воспроизводится)

- `Tantra_Design/assets/` — MPFB2 2.0.17 (extensions.blender.org) и пакеты ресурсов MakeHuman (CC0).
- `Tantra_Design/bx/`, `Tantra_Design/mpfbu/` — установленное расширение MPFB2 и его данные
  (короткий путь из-за ограничения MAX_PATH у Blender из Microsoft Store).
- `*.blend`, рендеры, текстуры, журналы — результаты скриптов сборки.
