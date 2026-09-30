# «Тантра» и OrbiterCrew — Orbiter 2016

Аддон звездолёта «Тантра» из романа И. А. Ефремова «Туманность Андромеды» и собственная система
живого экипажа OrbiterCrew для Orbiter 2016 (D3D9Client R4.26, XRSound 2.0).

Репозиторий лежит в корне установки Orbiter 2016 и отслеживает только файлы проекта
(см. `.gitignore` — белый список); сама игра и сторонние аддоны в него не входят.

## Состав

| Путь | Что это |
|---|---|
| `Orbitersdk/samples/Tantra/` | Звездолёт: ядро (`core/`), адаптер Orbiter 2016 (`orbiter2016/`), инструменты, тесты, документация |
| `Meshes/Tantra/`, `Textures/Tantra*`, `XRSound/Tantra/` | Меши, текстуры и звуки корабля и экипажа |
| `Config/Vessels/Tantra*.cfg`, `Scenarios/Tantra/` | Конфиги и сценарии корабля |
| `Orbitersdk/OrbiterCrew/` | OrbiterCrew: члены экипажа со скиннингом и анимацией по захвату движения, жизнеобеспечение (см. его README) |
| `Config/OrbiterCrew/`, `Config/Vessels/OrbiterCrew/`, `Scenarios/OrbiterCrew/` | Конфиги и сценарии экипажа |
| `Orbitersdk/resources/Orbiter.props` | Лист свойств MSBuild для сборки под SDK 2016 в VS2022 |
| `Tantra_Design/blender/` | Скрипты Blender (запуск без окна через `run.ps1`): астронавигатор на базе MPFB2/MakeHuman, одежда, экспорт в Orbiter |
| `Tantra_Design/mocap/` | Клипы захвата движения CMU Graphics Lab (BVH, MotionBuilder-friendly конверсия cgspeed); данные бесплатны, запрещена лишь их перепродажа |
| `Tantra_Design/audio/` | Генерация звуков экипажа |
| `Tantra_Design/*.html`, `DESIGN_LOCAL.md` | Макеты, эскизы, проектные заметки |

## Не в репозитории (воспроизводится)

- `Modules/` — собранные модули (`Tantra.dll`, `OrbiterCrew\CrewMember.dll`).
- `Tantra_Design/assets/` — MPFB2 2.0.17 (extensions.blender.org) и пакеты ресурсов MakeHuman (CC0).
- `Tantra_Design/bx/`, `Tantra_Design/mpfbu/` — установленное расширение MPFB2 и его данные
  (короткий путь из-за ограничения MAX_PATH у Blender из Microsoft Store).
- `*.blend`, рендеры, промежуточные текстуры, журналы, артефакты сборки.
