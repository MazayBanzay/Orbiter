# «Тантра»: перенос в Orbiter 2016

Решение пользователя 2026-09-30: основная платформа — `C:\Games\Orbiter 2016`. Там экипаж OrbiterCrew
(наш, соседняя ветка, `Orbitersdk/OrbiterCrew`), D3D9Client R4.26 и XRSound 2.0. UMmu и OrbiterSound в 2016
нет. Каталог 2010 остаётся как есть (заморожен, справочно).

## Порядок

1. **Копия исходников**, не перенос: `orbiter100830/Orbitersdk/samples/Tantra` →
   `Orbiter 2016/Orbitersdk/samples/Tantra` (core/, tools/, tests/, docs/, адаптер `orbiter2016/` из
   `orbiter2010/`). Меши, конфиги, сценарии, панель, звуки — в соответствующие папки 2016.
   Параллельные сессии (выхлоп/радиация) переключаются на копию по команде пользователя, не раньше.
2. **Сборка** VS2022 x86 против SDK 2016 (`Orbitersdk/include`, `Orbitersdk/lib`); общий
   `Orbitersdk/resources/Orbiter.props` уже есть (его писали для UACS). Модуль — `Modules/Tantra.dll`,
   `Modules/TantraTrap.dll`.
3. **Звук: XRSound** вместо OrbiterSound (`Orbitersdk/XRSound/XRSound.h`, `XRSound.lib`):
   - `XRSound::CreateInstance(this)` в `clbkPostCreation`, удалить в деструкторе;
   - `LoadWav(id, "XRSound\\Tantra\\x.wav", PlaybackType)` — путь от корня Orbiter; свои id < 10000;
   - типы: InternalOnly / BothViewFar / BothViewMedium / BothViewClose / Radio / Wind / Global
     (прежние INTERNAL_ONLY → InternalOnly, BOTHVIEW_FADED_FAR → BothViewFar, _MEDIUM → BothViewMedium);
   - `PlayWav(id, loop, volume)` — **нет управления высотой тона**. Гул двигателей: 2–3 слоя
     (низкий/средний/высокий) с перекрёстным затуханием по тяге;
   - штатные звуки: `SetDefaultSoundEnabled(MainEngines/HoverEngines/RetroEngines, false)`; РСУ — заменить
     через `LoadWav(RCSSustain / RCSAttack*…)`; пригодятся Touchdown, MetalCrunch, LandedWind, FlightWind,
     ReentryPlasma, SonicBoom, CabinAmbienceGroup;
   - конфиг класса — `XRSound/XRSound-Tantra.cfg`.
4. **Опоры: TOUCHDOWNVTX** (VESSEL4 / 2016): произвольное число точек с жёсткостью, демпфированием и
   трением. `core/Carriage` отдаёт не 3 точки, а по точке на тарелку (6 ног) + точки корпуса; жёсткость из
   площади тарелки и грунта. Это даёт честное выравнивание и осадку одной ноги (пределы и износ — см.
   DESIGN.md «Ноги лафета — финал»). Переходы между наборами точек проверить заново (tests/).
5. **Новые ноги в меше**: `legs_massive.html` → gen_mesh (бедро-лопасть, голень-лента, тарелки,
   ниши заподлицо, ось 14 м), риг и MeshLayout.h, TantraGear.
6. **Экипаж: OrbiterCrew** вместо UMmu. Со стороны корабля нужен интерфейс шлюза: список экипажа в
   сценарии корабля, выход (создать аппарат члена экипажа у шлюза или на подъёмнике), вход (по близости
   к шлюзу), скафандр/воздух. Заголовок интерфейса — согласовать с веткой OrbiterCrew; её файлы
   этой сессией не правятся.
7. **Git**: репозиторий в корне 2016 с белым списком `.gitignore`. Добавить (с согласия пользователя):
   `Orbitersdk/samples/Tantra`, `Meshes/Tantra`, `Config/Vessels/Tantra*.cfg`, `Scenarios/Tantra`,
   `Textures/Tantra`, `XRSound/Tantra`, `XRSound/XRSound-Tantra.cfg`. Модули (dll) — не коммитить.

## Что проверить в 2016
- Panel2D (SetPanelBackground, RegisterPanelMFDGeometry) и заливки в D3D9Client R4.26 (в 2010 не работала
  заливка ниже строки 1024 текстуры — проверить заново).
- Кириллица в HUD/Sketchpad.
- Анимации с родителями и масштабом (в 2010 масштаб под повёрнутым родителем искажался).
- ShiftCG и точки касания (в 2016 точки задаются массивом — проще сдвигать вместе с ЦМ).

## Статус (2026-09-30)
- Скопировано всё (исходники, меши, текстуры, конфиги, сценарии, звуки → `XRSound/Tantra`, макеты).
  Адаптер — `orbiter2016/` (копия `orbiter2010/`). Каталог 2010 не тронут.
- Собирается `build.bat` (VS2022 x86, /MT — XRSound.lib статический), ставит `Modules/Tantra.dll`,
  `Modules/TantraTrap.dll`.
- Звук: XRSound (громкость по тяге, штатные гулы двигателей выключены, РСУ заменена своими).
- Экипаж: `TantraCrew` — список в сценарии (`CREW`, `AIRLOCK`), выход создаёт аппарат
  `OrbiterCrew\Astronavigator` у подъёмника шлюза (пока одна фигура на всех), возвращение — постоять
  в 6 м от подъёмника при открытом шлюзе.
- Опоры: TOUCHDOWNVTX — три точки текущей позы упругие (осадка 0,25 м при 1,7 g, демпфирование 0,3
  критического → амортизация и небольшая раскачка), 14 жёстких точек корпуса на случай удара.
- `.gitignore`: добавлены пути корабля; коммитов этой сессией не делалось.
- Дальше: проверка в симуляторе; ноги по `legs_massive.html` в меш (с исправлением ошибок макета:
  шарнир ноги лафета связать с кареткой, нижние кормовые ноги над грунтом, колодец кормы закрыть);
  точки касания — по тарелкам (6 ног), износ и пределы.
