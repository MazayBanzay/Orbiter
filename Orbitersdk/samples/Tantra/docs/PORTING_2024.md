# Перенос на Orbiter 2024 — заметки «Тантры»

Установка: `C:\Games\Orbiter-2024` (сборка x86: Orbiter.exe, Orbiter_ng.exe — i386). В поставке:
D3D9Client, D3D7Client, GDIClient (`Modules\Plugin`), XRSound (`Modules\Plugin\XRSound.dll`, SDK `Orbitersdk\XRSound`).
**2026-10-04: полный переезд.** Исходники, ShipView, Tantra_Design, Tantra_Tools — в `C:\Games\Orbiter-2024`;
сборка `build.bat` (SDK 2024, /MD), полный цикл `install_all.bat`. Копии в Orbiter 2016 заморожены (MOVED_TO_2024.txt).

## SDK 2016 → 2024 (сверено по заголовкам)

- OrbiterAPI.h, VesselAPI.h, DrawAPI.h, MFDAPI.h, ModuleAPI.h: ни одна функция не удалена. VESSEL3 есть, добавлен VESSEL4.
- `gcAPI.h` / `gcAPI.lib` — нет. Замена: `gcCoreAPI.h`, интерфейс `gcCore2` через `gcGetCoreInterface()`
  (без библиотеки, привязка к D3D9Client.dll по GetProcAddress). Камеры: `SetupCustomCamera`, `CustomCameraOnOff`,
  `DeleteCustomCamera` — методы интерфейса, параметры те же.
- `Sketchpad2.h`, `Sketchpad3` — нет. Всё слито в `oapi::Sketchpad` (QuickPen, QuickBrush, StretchRect, SetBrightness,
  SetRenderParam...). Поддержку проверять `skp->GetVersion() >= 2` вместо `dynamic_cast<Sketchpad2*>`: у базового
  класса эти методы — `assert(false)`.
- Константы: `SKP3_PRM_GAMMA` → `oapi::Sketchpad::PRM_GAMMA`. Типы `IVECTOR2`, `FVECTOR2`, `FVECTOR4` — в `namespace oapi`.
- Рантайм C++: Orbitersdk.lib и XRSound.lib 2024 собраны с **/MD** (динамический CRT). Наши модули — тоже /MD
  (было /MT). msvcp140/vcruntime лежат в корне Orbiter 2024.
- XRSound.lib 2024 требует **ATL** (`atls.lib`, символ `ATL::_AtlBaseModule`). В Build Tools нужен компонент
  «C++ ATL for latest build tools» (Microsoft.VisualStudio.Component.VC.ATL).
- XRSound API (LoadWav/PlayWav/PlaybackType/SetDefaultSoundEnabled) не изменился.

## Пробная сборка Tantra.dll (2026-10-03)

Скрипт в черновике сессии, репозиторий не менялся; несовместимые заголовки закрыты временными заглушками.
- Компиляция всех 22 файлов (core, orbiter2016, ShipView) — **прошла**.
- Правки в коде: TantraDisplays.cpp (Sketchpad2/3 → Sketchpad), TantraGlyphs.h (то же), ShipView/ViewScreen.cpp
  (gcAPI → gcCore), build.bat (SDK 2024, /MD, без gcAPI.lib).
- Компоновка: после установки ATL (2026-10-03, компонент VC.ATL в Build Tools) — **Tantra.dll и TantraTrap.dll собраны**.
  В игре 2024 ещё не запускались.

## Код под оба SDK (2026-10-03)

Исходники «Тантры» собираются и под 2016, и под 2024 — переход без поломки чужих сборок:
- `orbiter2016/SkpCompat.h` — Sketchpad2/3: на 2016 dynamic_cast, на 2024 базовый Sketchpad при загруженном
  D3D9Client.dll (GetVersion() в 2024 всегда 1, по нему не различить). Подключён в TantraDisplays.cpp и TantraGlyphs.h.
- `ShipView/ViewScreen.cpp` — на 2024 функции gcAPI заменены вызовами интерфейса gcCore (выбор по `__has_include`).
- Переходные `build_2024.bat` / `install_2024.bat` удалены после переезда; `build.bat` собирает под 2024.
- Проверено: обе сборки проходят.

## Касается других модулей

- CrewMember.dll («Экипаж»): в CrewMember.vcxproj подключены XRSound.lib и gcAPI.lib — те же три вопроса (gcAPI, /MD, ATL).
