// OrbiterCrew - the suit computer's helmet display (see SuitHud.h and Tantra_Design/suit_computer_mfd.html).
// Everything is laid out on 1280x720 display units: left parts hang on the left edge, right parts on the right edge,
// the rest on the centre, so any aspect ratio works. The MFD pages use their own 340x300 units, as in the mockup.
#include "SuitHud.h"
#include "../../samples/AtmoFX/AtmoFXHud.h"   // the conformal helmet HUD of the AtmoFX D3D9 client (no-op without it)
#include "LifeSupport.h"
#include "Frame.h"
#include <gcCoreAPI.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <map>
#include <unordered_map>
#include <cctype>
#include <windows.h>

namespace ocrew
{
	namespace
	{
		// ---- the display's language: Russian or English (the user, 2026-10-08: English for the tests; one setting with the
		// crew's menu - Config\OrbiterCrew\OrbiterCrew.cfg, LANGUAGE ru | en; not set - English). Every Russian text of the
		// display goes through TR: a fragment, as it is joined with the numbers in the code ----
		bool g_en = true;
		// a trace atmosphere inside "vacuum" (Io's SO2 ~1 nbar): its pressure in bar units that read, empty below 1e-12 bar
		std::string Trace(double kPa)
		{
			const double bar = kPa / 100.0;
			if (!(bar > 1e-12)) return std::string();
			char b[64];
			if (bar < 1e-6) std::snprintf(b, sizeof b, " · %.2g %s", bar * 1e9, g_en ? "nbar" : "нбар");
			else if (bar < 1e-3) std::snprintf(b, sizeof b, " · %.2g %s", bar * 1e6, g_en ? "ubar" : "мкбар");
			else std::snprintf(b, sizeof b, " · %.2g %s", bar * 1e3, g_en ? "mbar" : "мбар");
			return b;
		}

		const char* TR(const char* ru)
		{
			if (!g_en || !ru) return ru;
			static const std::unordered_map<std::string, const char*> M = {
			{ "   Shift+Пробел — полная   Shift+Ctrl — выкл   W/S — вектор   Q/E — вбок   A/D — поворот", "   Shift+Space — full   Shift+Ctrl — off   W/S — vector   Q/E — sideways   A/D — turn" },
			{ "WASD — шаг   Shift — серво   B — ранец   Shift+V — щиток   L — фонари   K — скафандр", "WASD — walk   Shift — servo   B — jet pack   Shift+V — sun shade   L — lamps   K — suit" },
			{ "W/S ход  A/D поворот  Q/E шаг  Shift бег  Пробел прыжок  K скафандр", "W/S walk  A/D turn  Q/E side step  Shift run  Space jump  K suit" },
			{ "режим ещё раз — снова автоматически   ·   заголовок МФД — свернуть", "mode again — back to automatic   ·   MFD title — fold" },
			{ "РСУ: цифровой блок   ·   цель и автопилот — мышью на правом МФД", "RCS: numpad   ·   target and autopilot — mouse on the right MFD" },
			{ "ПНВ недоступен: включите CustomCamMode в D3D9Client.cfg", "NIGHT VISION unavailable: enable CustomCamMode in D3D9Client.cfg" },
			{ "ПЕРЕЗАПУСК ПОСЛЕ ПЕРЕГРЕВА · сначала жизнеобеспечение", "RESTART AFTER OVERHEAT · life support first" },
			{ "W/S ход · A/D руль · Shift полный · Пробел тормоз", "W/S drive · A/D steer · Shift full · Space brake" },
			{ "касание ≤ 1,5 м/с · травма > 7 · гибель > 14", "touchdown ≤ 1.5 m/s · injury > 7 · death > 14" },
			{ "круиз выключен · кнопка включит с текущими", "cruise off · the button engages at the present values" },
			{ "Shift - полный вектор вручную, круиз ждёт", "Shift - full vector by hand, cruise waits" },
			{ "КРУИЗ · ВЫСОТУ И СКОРОСТЬ ДЕРЖИТ ПОМОЩНИК", "CRUISE · THE ASSISTANT HOLDS HEIGHT AND SPEED" },
			{ "тепло уходит насосом на радиатор (КПД 3)", "heat pumped to the radiator (COP 3)" },
			{ "ЗОНА 30 м · СБЛИЖЕНИЕ БЫСТРЕЕ 0,25 м/с", "30 m ZONE · CLOSING FASTER THAN 0.25 m/s" },
			{ "A / D - курс · пробел / Ctrl - высота", "A / D - heading · Space / Ctrl - height" },
			{ "Caps Lock платформа · V вид на машину", "Caps Lock platform · V view of the machine" },
			{ "круиз включён · снос вбок гасится сам", "cruise on · sideways drift cancelled" },
			{ "Пробел / Ctrl — тяга больше / меньше", "Space / Ctrl — thrust up / down" },
			{ " °C · поле и сервоприводы отключены", " °C · field and servos off" },
			{ "основной белый, автопилот оранжевый", "white main, orange autopilot" },
			{ "W / S - скорость, 3 м/с за секунду", "W / S - speed, 3 m/s per second" },
			{ "основной оранжевый, автопилот циан", "orange main, cyan autopilot" },
			{ "основной циан, автопилот оранжевый", "cyan main, orange autopilot" },
			{ "ОБОЛОЧКА РАЗРУШЕНА · СРЕДА ВНУТРИ", "SHELL BREACHED · OUTSIDE GAS INSIDE" },
			{ "ПНВ недоступен: нужен клиент D3D9", "NIGHT VISION unavailable: needs the D3D9 client" },
			{ "ПРОФИЛЬ ПЕРЕЛЁТА · РЕЛЬЕФ ПО ПУТИ", "TRANSFER PROFILE · TERRAIN ALONG THE PATH" },
			{ "ПНВ недоступен: нет поверхности", "NIGHT VISION unavailable: no surface" },
			{ "на странице ЦЕЛИ или на карте", "on the TARGETS page or on the map" },
			{ "Пробел / Ctrl — выше / ниже", "Space / Ctrl — up / down" },
			{ "ПУТЬ %03.0f° · СНОС %+.0f°", "TRACK %03.0f° · DRIFT %+.0f°" },
			{ "ОТНОСИТЕЛЬНОЕ ДВИЖЕНИЕ · ", "RELATIVE MOTION · " },
			{ "ОХЛАЖДЕНИЕ НЕ СПРАВЛЯЕТСЯ", "COOLING CANNOT KEEP UP" },
			{ "ИЗОЛЯЦИЯ ПРОГОРАЕТ ", "INSULATION BURNING THROUGH " },
			{ "ТЕМА ЭКРАНА", "DISPLAY THEME" }, { "ЛАЗЕР", "LASER" }, { "ПРОЕКТОР", "PROJECTOR" }, { "ДОП. РЕАЛЬНОСТЬ", "AUGMENTED" }, { "ВЫСОКОКОНТРАСТ", "HIGH CONTRAST" },
			{ "зелёный лазер, тонкая линия", "green laser, a fine line" }, { "голубой свет с мягким ореолом", "cyan light, a soft glow" }, { "белый и синий, плоско", "white and blue, flat" }, { "оранжевый, плотный", "orange, dense" },
			{ "ЗАПАСЫ И РАСХОД", "RESERVES AND USE" }, { "ЗАПАСЫ", "RESERVES" }, { "Топливо ранца", "Jet pack fuel" }, { "Вода (питьё)", "Water (drink)" }, { "ЗАПАС", "LEFT" }, { "РАСХОД", "USE" }, { "предел: ", "limit: " }, { "готов", "ready" }, { "на грунте", "on the ground" }, { "«БУРАН»", "«BURAN»" }, { "ИНДУСТРИАЛЬНАЯ", "INDUSTRIAL" },
			{ "«Буран» — зелёный лазер", "«Buran» — green laser" }, { "индустриальная — белый и оранжевый", "industrial — white and orange" },
			{ "ЖУРНАЛ", "LOG" }, { " · активно ", " · active " }, { "СКАФАНДР", "SUIT" }, { " · ДЫХ ", " · BR " }, { "ЗАПАСЫ · РАСХОД", "RESERVES · USE" },
			{ "воздух", "air" }, { " г/мин", " g/min" }, { "висение ", "hover " }, { "МЕХАНИЗАЦИЯ", "MECHANISMS" }, { "МОДУЛЬ ПОЛЁТА", "FLIGHT MODULE" },
			{ "ЛУЧ ", "BEAM " }, { "шир", "wide" }, { "раб", "work" }, { "дал", "far" }, { "ЩИТОК", "SHADE" }, { "ШТОРКА", "LOUVRES" }, { "ШЛЕМ ОТКР", "HELMET OPEN" }, { "ШЛЕМ", "HELMET" },
			{ "H-АЛЬФА", "H-ALPHA" }, { "ГОРИЗОНТ", "HORIZON" }, { "ГОРИЗОНТ ▸", "HORIZON ▸" }, { "ХАД", "HUD" }, { "ХАД ▸", "HUD ▸" }, { "МФД НА СТЕКЛЕ", "MFD ON THE GLASS" }, { "ПО СТОРОНАМ", "AT THE SIDES" }, { "ВПЕРЕДИ", "IN FRONT" }, { "МФД · ПО СТОРОНАМ", "MFD · AT THE SIDES" }, { "МФД · В ЦЕНТРЕ", "MFD · IN FRONT" }, { "КАМЕРА · ", "CAMERA · " }, { "БОК Л", "SIDE L" }, { "БОК П", "SIDE R" }, { "СТЕРЕО", "STEREO" }, { "широта 70°", "field 70°" }, { "широта 120°", "field 120°" },
			{ "ШТОРКА ОТКРЫТА · вид через окно · Shift+V — камеры", "SHUTTER OPEN · through the window · Shift+V — cameras" }, { "ШТОРКА ЗАКРЫТА", "SHUTTER CLOSED" }, { " · АВТО: ", " · AUTO: " }, { " · Shift+V — окно", " · Shift+V — window" }, { "ЗАТЕНЕНИЕ СТЕКЛА", "VISOR SHADING" }, { "яркость линий — ниже, «ЯРКОСТЬ ЭКРАНА»", "line brightness — below, DISPLAY BRIGHTNESS" }, { "СЕРВО", "SERVO" }, { "ЭКОНОМ", "ECONOMY" }, { "ПОЛЕ", "FIELD" }, { "СВЕТ", "LAMPS" }, { "ПНВ", "NIGHT VIS" },
			{ "щелчок — вкл / выкл", "click — on / off" }, { "ВЕРТ ", "VERT " }, { "ЗАПАС Δv ", "Δv LEFT " }, { "ПУЛЬС", "PULSE" }, { "ТОПЛ", "FUEL" }, { "БАТ", "BAT" },
			{ "ПАНЦИРЬ", "PANTSIR" },
			{ "управление", "control" },
			{ "ДАННЫЕ", "DATA" },
			{ "РАДИАТОР", "RADIATOR" },
			{ "ПРИВОДЫ", "DRIVES" },
			{ "ПОВРЕЖД.", "DAMAGE" },
			{ "Корпус", "Shell" },
			{ " °C · порог ", " °C · onset " },
			{ "Предел по жаре", "Heat limit" },
			{ " · без поля ", " · without field " },
			{ "Допуск снаружи", "Outside rating" },
			{ "Насос", "Pump" },
			{ "Компрессор", "Compressor" },
			{ "перегрузка x", "overload x" },
			{ "норма", "normal" },
			{ "Теплообмен", "Heat exchange" },
			{ "Испаритель", "Evaporator" },
			{ "Аппаратура", "Equipment" },
			{ "Контур", "Loop" },
			{ "ОТКАЗ", "FAILED" },
			{ "исправен", "working" },
			{ "Сервоприводы", "Drives" },
			{ "выкл", "off" },
			{ "сброшены", "shed" },
			{ "работают", "working" },
			{ "Мощность", "Power" },
			{ "Ход к норме", "Gait vs normal" },
			{ "Тормоза суставов", "Joint brakes" },
			{ "держат стоя", "hold upright" },
			{ "нет", "none" },
			{ "Поле", "Field" },
			{ "ОТКАЗ КАТУШЕК", "COILS FAILED" },
			{ "сброшено", "shed" },
			{ "Ослабление", "Attenuation" },
			{ "Доза сейчас", "Dose rate" },
			{ "За выход", "This outing" },
			{ " мЗв из 250", " mSv of 250" },
			{ "Подбой (тантал)", "Liner (tantalum)" },
			{ "тормозное x", "bremsstrahlung x" },
			{ "Изоляция", "Insulation" },
			{ "прогорела ", "burnt " },
			{ "цела", "intact" },
			{ "Уплотнения", "Seals" },
			{ " · утечка", " · leaking" },
			{ "целы", "intact" },
			{ "Катушки поля", "Field coils" },
			{ "исправны", "working" },
			{ "Радиатор", "Radiator" },
			{ "Оболочка", "Shell" },
			{ "ВСКРЫТА", "BREACHED" },
			{ "герметична", "sealed" },
			{ "Ноги", "Legs" },
			{ " · обувь ", " · boots " },
			{ " · в расплаве", " · in the melt" },
			{ " · поле ", " · field " },
			{ "Батарея", "Battery" },
			{ "Кислород", "Oxygen" },
			{ "Поглотитель CO2", "CO2 sorbent" },
			{ "Вода испарителя", "Evaporator water" },
			{ "РАД", "RAD" },
			{ "ТЕЛО", "BODY" },
			{ "СРЕДА", "SURROUNDINGS" },
			{ "КОРПУС", "SHELL" },
			{ " мЗв/ч", " mSv/h" },
			{ "ЗАПАС мин", "RESERVE min" },
			{ "по жаре · вода", "heat · water" },
			{ "перегрев тела", "body overheat" },
			{ "батарея", "battery" },
			{ "кислород", "oxygen" },
			{ "доза 250 мЗв", "dose 250 mSv" },
			{ "СКОРОСТЬ", "SPEED" },
			{ "КУРС", "HEADING" },
			{ "ЦЕЛЬ", "TARGET" },
			{ "ГРУНТ", "GROUND" },
			{ "НОГИ В РАСПЛАВЕ · ", "LEGS IN THE MELT · " },
			{ "ОБУВЬ ПРОГОРАЕТ ", "BOOTS BURNING THROUGH " },
			{ "В скафандре", "In the suit" },
			{ "Снаружи", "Outside" },
			{ "Среда (излучение)", "Surroundings (radiant)" },
			{ "Грунт", "Ground" },
			{ "держится", "holds" },
			{ "растёт", "rising" },
			{ "падает", "falling" },
			{ " · солнце", " · sun" },
			{ " · тень", " · shade" },
			{ " · светится", " · glowing" },
			{ " · иней", " · frost" },
			{ "ОХЛАЖДЕНИЕ ", "COOLING " },
			{ "ОБОГРЕВ ", "HEATING " },
			{ " Вт", " W" },
			{ " кг", " kg" },
			{ " мин", " min" },
			{ " мЗв", " mSv" },
			{ " мкЗв/ч", " µSv/h" },
			{ "вакуум", "vacuum" },
			{ " кПа", " kPa" },
			{ "УПЛОТНЕНИЯ: УТЕЧКА ", "SEALS: LEAKING " },
			{ "УПЛОТНЕНИЯ ПЕРЕГРЕТЫ ", "SEALS OVERHEATED " },
			{ "УПЛОТНЕНИЯ ПЕРЕОХЛАЖДЕНЫ ", "SEALS FROZEN " },
			{ "АППАРАТУРА ПЕРЕОХЛАЖДЕНА ", "EQUIPMENT TOO COLD " },
			{ " °C · отключена", " °C · shut down" },
			{ " °C · отключение при −40", " °C · shutdown at −40" },
			{ "УПЛОТНЕНИЯ ", "SEALS " },
			{ " °C · ХОЛОД", " °C · COLD" },
			{ "ПОЛЕ: ОТКАЗ КАТУШЕК", "FIELD: COILS FAILED" },
			{ "СЕРВОПРИВОДЫ: ОТКАЗ", "DRIVES: FAILED" },
			{ "РАДИАТОР: ОТКАЗ КОНТУРА", "RADIATOR: LOOP FAILED" },
			{ "  ·  подтвердите щелчком", "  ·  click to acknowledge" },
			{ " °C · отключение при +90", " °C · shutdown at +90" },
			{ "У ЦЕЛИ НЕТ УЗЛОВ · ЦЕНТР", "TARGET HAS NO PORTS · CENTRE" },
			{ " м/с · 2-я косм. здесь ", " m/s · escape here " },
			{ "ПОД НОГАМИ · ВИД СВЕРХУ", "UNDERFOOT · FROM ABOVE" },
			{ "АП · УДЕРЖАНИЕ ВЫСОТЫ ", "AP · HEIGHT HOLD " },
			{ "В РАДИУСЕ 50 км НИКОГО", "NOBODY WITHIN 50 km" },
			{ "НЕЗАМКНУТАЯ ТРАЕКТОРИЯ", "OPEN TRAJECTORY" },
			{ "ОБОГРЕВ НЕ СПРАВЛЯЕТСЯ", "HEATING CANNOT KEEP UP" },
			{ "ЦВЕТ — только для голо", "COLOUR — holo only" },
			{ "к базе только ПЕРЕЛЁТ", "to a base TRANSFER only" },
			{ "посадка рядом с целью", "landing next to the target" },
			{ "   ▸   ПОЛНЫЙ ВЕКТОР", "   ▸   FULL VECTOR" },
			{ " · урезан теплообмен", " · thermal control cut back" },
			{ "ПЕРЕГРЕВ АППАРАТУРЫ ", "EQUIPMENT OVERHEAT " },
			{ "заголовок — свернуть", "title — fold" },
			{ "из воздуха · баллон ", "from the air · tank " },
			{ "ОПТИКА: ПОТЕМНЕНИЕ ", "OPTICS: DARKENED " },
			{ "аппарат · на грунте", "vessel · landed" },
			{ "круто, ищите ровнее", "too steep, find flatter ground" },
			{ "посадка на площадку", "landing on a pad" },
			{ " °C · ВЫШЕ ДОПУСКА", " °C · ABOVE RATING" },
			{ "ВЫСОТА НАД ГРУНТОМ", "HEIGHT ABOVE GROUND" },
			{ "Охлаждение (насос)", "Cooling (pump)" },
			{ "ПЕРЕГРУЗКА ПИТАНИЯ", "POWER OVERLOAD" },
			{ "СРЕДА ВНЕ ДОПУСКА ", "ENVIRONMENT OUT OF RATING " },
			{ "обогрев от батареи", "heating from the battery" },
			{ " · НА ПОВЕРХНОСТИ", " · ON THE SURFACE" },
			{ " кПа · у предела ", " kPa · near the limit " },
			{ "БЫСТРОЕ СНИЖЕНИЕ ", "FAST DESCENT " },
			{ "ДАВЛЕНИЕ СНАРУЖИ ", "OUTSIDE PRESSURE " },
			{ "СНИЖЕНИЕ ↓ БЫСТРО", "DESCENT ↓ FAST" },
			{ "Среда (излучение)", "Environment (radiant)" },
			{ "вода в скафандре ", "water in the suit " },
			{ "уклон под ногами ", "slope underfoot " },
			{ " · ВКЛЮЧИТЕ ПОЛЕ", " · SWITCH THE FIELD ON" },
			{ "АП · АВТОПОСАДКА", "AP · AUTO LANDING" },
			{ "Жизнеобеспечение", "Life support" },
			{ "КОРИДОР СКОРОСТИ", "SPEED CORRIDOR" },
			{ "ПЛАН · ПО ХОДУ →", "PLAN · AHEAD →" },
			{ "ТЕМПЕРАТУРА ТЕЛА", "BODY TEMPERATURE" },
			{ "база · площадок ", "base · pads " },
			{ "— моя   - - цель", "— mine   - - target" },
			{ " °C · у предела", " °C · near the limit" },
			{ "ПИТАНИЕ И ТЕПЛО", "POWER AND HEAT" },
			{ "Поглотитель CO2", "CO2 scrubber" },
			{ "СКОРОСТЬ ВПЕРЁД", "FORWARD SPEED" },
			{ "ТЕПЛО · ДОПУСК ", "HEAT · RATING " },
			{ "воздух пригоден", "air breathable" },
			{ " · отключено: ", " · off: " },
			{ " кПа · ПРЕДЕЛ ", " kPa · LIMIT " },
			{ "2× СГЛАЖИВАНИЕ", "2× SMOOTHED" },
			{ "ПУЛЬС · 10 мин", "PULSE · 10 min" },
			{ "РАНЕЦ НЕ НАДЕТ", "NO JET PACK" },
			{ "РЕЛЬЕФ ВПЕРЕДИ", "TERRAIN AHEAD" },
			{ "СУТКИ (ЗВЁЗД.)", "DAY (SIDEREAL)" },
			{ "ТОПЛИВО РАНЦА ", "JET PACK FUEL " },
			{ "ТЯЖЁЛЫЕ ТРАВМЫ", "SEVERE INJURIES" },
			{ "ЯРКОСТЬ ЭКРАНА", "DISPLAY BRIGHTNESS" },
			{ "допуск снаружи", "outside rating" },
			{ "обезвоживание ", "dehydration " },
			{ "садиться можно", "landing possible" },
			{ "%d ч %02d мин", "%d h %02d min" },
			{ "БЕЛЫЙ + ОРАНЖ", "WHITE + ORANGE" },
			{ "ВЫБЕРИТЕ ЦЕЛЬ", "SELECT A TARGET" },
			{ "ДО ПЕРИЦЕНТРА", "TO PERIAPSIS" },
			{ "ДОЗА ЗА ВЫХОД", "DOSE THIS EVA" },
			{ "ИК · засветка", "IR · washed out" },
			{ "ОТН. СКОРОСТЬ", "REL. SPEED" },
			{ "ПОЛНЫЙ ВЕКТОР", "FULL VECTOR" },
			{ "РАНЕЦ СОТЫ: ", "JET PACK SOTY: " }, { "РАНЕЦ ТРИЛИСТНИК: ", "JET PACK TREFOIL: " }, { "МОДУЛЬ ПОЛЁТА: ", "FLIGHT MODULE: " }, { " · ФОРСАЖ", " · AFTERBURN" }, { " · КАМЕРА ", " · CHAMBER " }, { "П", "R" }, { "Л", "L" }, { " ОТКАЗ", " FAILED" }, { " ПЕРЕГРЕВ", " OVERHEAT" }, { "РАНЕЦ НАПЛЕЧНЫЙ: ", "JET PACK SHOULDER: " }, { "СОТЫ ", "CELLS " },
			{ " · ДОЖИГ ", " · BOOST " }, { " · ЭНЕРГОБЛОК ", " · POWER UNIT " }, { " % ЖАРА", " % HOT AIR" }, { "ЗАПУСК", "STARTING" },
			{ "ЭНЕРГОБЛОК СТОП", "POWER UNIT STOPPED" }, { "НЕТ АТМОСФЕРЫ · ДОЖИГ ", "NO AIR · BOOST " }, { "ТОПЛИВА НЕТ", "NO FUEL" },
			{ "ТЯГА ", "THRUST " }, { " · РУЧН", " · MANUAL" }, { " · ПОМОЩН", " · ASSIST" }, { " · УДЕРЖ", " · ALT HOLD" },
			{ " · ПОСАДКА", " · LANDING" }, { " · ВЕКТОР", " · VECTOR" }, { " · СОТЫ МАКС ", " · CELLS MAX " }, { "ГОТОВ", "READY" },
			{ "ТЕРМОКОНТРОЛЬ", "THERMAL CONTROL" },
			{ "дышать нельзя", "not breathable" },
			{ "у цели склон ", "slope at the target " },
			{ "БЕЗ СОЗНАНИЯ", "UNCONSCIOUS" },
			{ "В СКАФАНДРЕ ", "IN THE SUIT " },
			{ "ДАВЛЕНИЕ O2 ", "O2 PRESSURE " },
			{ "ДО АПОЦЕНТРА", "TO APOAPSIS" },
			{ "ЗЕМЛЯ БЛИЗКО", "GROUND CLOSE" },
			{ "КОМБИНЕЗОН ·", "COVERALL ·" },
			{ "Мет. водород", "Met. hydrogen" },
			{ "ОБЛИК ЭКРАНА", "DISPLAY LOOK" },
			{ "ОРАНЖ + ЦИАН", "ORANGE + CYAN" },
			{ "ПОГЛОТИТЕЛЬ ", "SCRUBBER " },
			{ "Сервоприводы", "Servos" },
			{ "УЗЕЛ ЦЕЛИ · ", "TARGET PORT · " },
			{ "ЦИАН + ОРАНЖ", "CYAN + ORANGE" },
			{ "до предела: ", "to the limit: " },
			{ "свет вокруг ", "light around " },
			{ "сервоприводы", "servos" },
			{ " · «Тантра»", " · «Tantra»" },
			{ " · висение ", " · hover " },
			{ "АП: ПОСАДКА", "AP: LANDING" },
			{ "АППАРАТУРА ", "EQUIPMENT " },
			{ "В скафандре", "In the suit" },
			{ "ЗАПАС ПОСЛЕ", "LEFT AFTER" },
			{ "НЕТ ПИТАНИЯ", "NO POWER" },
			{ "ОТ СРЕДН. R", "FROM MEAN R" },
			{ "ОТН. НАКЛОН", "REL. PITCH" },
			{ "ОХЛАЖДЕНИЕ ", "COOLING " },
			{ "ПЛАТФОРМА ▲", "PLATFORM UP" },
			{ "ПЛАТФОРМА ▼", "PLATFORM DOWN" },
			{ "ЦЕЛЬ — БАЗА", "TARGET — BASE" },
			{ "зазор мин. ", "min. clearance " },
			{ "питания нет", "no power" },
			{ "поглотитель", "scrubber" },
			{ "прыжком до ", "hop to " },
			{ "склон > 15°", "slope > 15°" },
			{ "  ·  снято", "  ·  cleared" },
			{ "+V по ходу", "+V ahead" },
			{ "Аппаратура", "Equipment" },
			{ "ЗАПАС ХОДА", "RANGE" },
			{ "КОМБИНЕЗОН", "COVERALL" },
			{ "ПОЛНЫЙ ХОД", "FULL SPEED" },
			{ "СНИЖЕНИЕ ↓", "DESCENT ↓" },
			{ "СТОЯНОЧНЫЙ", "PARKING" },
			{ "СТЫК. УЗЛЫ", "DOCKING PORTS" },
			{ "Теплообмен", "Heat exchange" },
			{ "УГОЛ МЕСТА", "ELEVATION" },
			{ "ранец снят", "jet pack off" },
			{ "стояночный", "parking" },
			{ "хватит на ", "lasts " },
			{ " (тяжело)", " (hard)" },
			{ " · вакуум", " · vacuum" },
			{ " · орбита", " · orbit" },
			{ " · солнце", " · sun" },
			{ " · эконом", " · economy" },
			{ " г/мин · ", " g/min · " },
			{ " кВт·ч · ", " kWh · " },
			{ " м/с · с ", " m/s · from " },
			{ "1-Я КОСМ.", "ORBITAL V" },
			{ "2-Я КОСМ.", "ESCAPE V" },
			{ "M · КАРТА", "M · MAP" },
			{ "АП: УДЕРЖ", "AP: HOLD" },
			{ "ВЫБРАНО: ", "SELECTED: " },
			{ "ГОРИЗ м/с", "HORIZ m/s" },
			{ "ДАЛЬНОСТЬ", "RANGE" },
			{ "КИСЛОРОД ", "OXYGEN " },
			{ "НЕ ДЕРЖИТ", "NOT HOLDING" },
			{ "ОПАСНОСТЬ", "WARNING" },
			{ "ОРБИТА · ", "ORBIT · " },
			{ "ОСЛАБЛЕНА", "WEAKENED" },
			{ "ПЛАТФОРМА", "PLATFORM" },
			{ "ПОЛНЫЙ · ", "FULL · " },
			{ "РАДИАЦИЯ ", "RADIATION " },
			{ "СБЛИЖ м/с", "CLOSING m/s" },
			{ "СБЛИЖЕНИЕ", "RENDEZVOUS" },
			{ "СОСТОЯНИЕ", "CONDITION" },
			{ "УДЕРЖ ВЫС", "HOLD HGT" },
			{ "атмосфера", "atmosphere" },
			{ "до грунта", "to the ground" },
			{ "нагрузка ", "load " },
			{ "плывёт · ", "drifts · " },
			{ " мЗв из ", " mSv of " },
			{ "° · день", "° · day" },
			{ "ΔV СИНХР", "ΔV MATCH" },
			{ "Азот РСУ", "RCS nitrogen" },
			{ "БАТАРЕЯ ", "BATTERY " },
			{ "ВЕРТ м/с", "VERT m/s" },
			{ "ВНИМАНИЕ", "CAUTION" },
			{ "ВРАЩЕНИЕ", "ROTATION" },
			{ "ВЫСОТА м", "HEIGHT m" },
			{ "ГОРИЗОНТ", "HORIZON" },
			{ "ЗАПАС Δv", "Δv LEFT" },
			{ "ИСТОЩЕНА", "EXHAUSTED" },
			{ "Кислород", "Oxygen" },
			{ "МАКСИМУМ", "MAXIMUM" },
			{ "МАСШТАБ ", "SCALE " },
			{ "МОНОХРОМ", "MONOCHROME" },
			{ "МОЩНОСТЬ", "POWER" },
			{ "Меркурий", "Mercury" },
			{ "ОБОГРЕВ ", "HEATING " },
			{ "ОГРАНИЧ.", "LIMITER" },
			{ "ОРГАНИЗМ", "BODY" },
			{ "ПЛОЩАДКИ", "PADS" },
			{ "ПОМОЩНИК", "ASSISTANT" },
			{ "РАДИАЦИЯ", "RADIATION" },
			{ "Радиация", "Radiation" },
			{ "СКОРОСТЬ", "SPEED" },
			{ "СТЫКОВКА", "DOCKING" },
			{ "ФОТОНИКА", "PHOTONICS" },
			{ "ЦЕЛИ НЕТ", "NO TARGET" },
			{ "ЧЁТКОСТЬ", "SHARPNESS" },
			{ "без еды ", "no food " },
			{ "держится", "holding" },
			{ "кислород", "oxygen" },
			{ "крейсер ", "cruise " },
			{ "лежит · ", "lying · " },
			{ "на месте", "still" },
			{ "топливо ", "fuel " },
			{ " · день", " · day" },
			{ " · ночь", " · night" },
			{ " · поле", " · field" },
			{ " · тень", " · shade" },
			{ " г/с · ", " g/s · " },
			{ " кПа · ", " kPa · " },
			{ " м/с · ", " m/s · " },
			{ " мкЗв/ч", " uSv/h" },
			{ " уд/мин", " bpm" },
			{ "888 кПа", "888 kPa" },
			{ "8888 Вт", "8888 W" },
			{ "V ГОРИЗ", "V HORIZ" },
			{ "V СБЛИЖ", "V CLOSE" },
			{ "БАТАРЕЯ", "BATTERY" },
			{ "Батарея", "Battery" },
			{ "В НОРМЕ", "NORMAL" },
			{ "ВИСЕНИЕ", "HOVER" },
			{ "ВРЕМЯ ≈", "TIME ≈" },
			{ "ДАЛЬН м", "RANGE m" },
			{ "ДО ЦЕЛИ", "TO TARGET" },
			{ "ДОЛГОТА", "LONGITUDE" },
			{ "ДЫХАНИЕ", "BREATHING" },
			{ "КАСАНИЕ", "TOUCHDOWN" },
			{ "НАБОР ↑", "CLIMB ↑" },
			{ "Обогрев", "Heating" },
			{ "ПЕРЕЛЁТ", "TRANSFER" },
			{ "ПИТАНИЕ", "POWER" },
			{ "ПОСАДКА", "LANDING" },
			{ "ПОХОДКА", "GAIT" },
			{ "Питание", "Power" },
			{ "СИСТЕМЫ", "SYSTEMS" },
			{ "Снаружи", "Outside" },
			{ "аппарат", "vessel" },
			{ "батарея", "battery" },
			{ "вправо ", "right " },
			{ "отдача ", "regen " },
			{ "сейчас ", "now " },
			{ "— круто", "— steep" },
			{ " в мин", " per min" },
			{ " кВт·ч", " kWh" },
			{ " мЗв/ч", " mSv/h" },
			{ "> 99 ч", "> 99 h" },
			{ "V ВЕРТ", "V VERT" },
			{ "ВЕТЕР ", "WIND " },
			{ "ВСЕГДА", "ALWAYS" },
			{ "ВЫСОТА", "HEIGHT" },
			{ "Венера", "Venus" },
			{ "Воздух", "Air" },
			{ "ГИБЕЛЬ", "DEAD" },
			{ "ПУЛЬС 0", "PULSE 0" },
			{ "ГРУНТ ", "GROUND " },
			{ "МАШИНА", "MACHINE" },
			{ "СОЕДИНЕНИЯ", "CONNECTIONS" },
			{ "СОЕД", "CONN" },
			{ "РАМА «УСИЛИТЕЛЬ»", "FRAME «USILITEL»" },
			{ "КАССЕТА", "CASSETTE" },
			{ "от кассеты", "from the cassette" },
			{ "от скафандра", "from the suit" },
			{ "нет", "none" },
			{ "БАТАРЕЯ СКАФАНДРА → РАМА", "SUIT BATTERY → FRAME" },
			{ "ВКЛ", "ON" },
			{ " кг", " kg" },
			{ "кассета пуста: рама стоит", "cassette flat: the frame is dead" },
			{ "кассета пуста: тянет скафандр", "cassette flat: the suit feeds it" },
			{ "ПОДЪЁМ", "LIFT" },
			{ "В РУКАХ", "HELD" },
			{ "без скафандра связи нет", "no link without the suit" },
			{ "НАКЛОН", "PITCH" },
			{ "ОБЪЕКТ", "OBJECT" },
			{ "ОРБИТА", "ORBIT" },
			{ "ПЕЛЕНГ", "BEARING" },
			{ "ПЕРИОД", "PERIOD" },
			{ "ПРЕДЕЛ", "LIMIT" },
			{ "РАДИАЦ", "RADIAT" },
			{ "РАНЕЦ ", "JET PACK " },
			{ "РАСХОД", "CONSUMPTION" },
			{ "РЕЗЕРВ", "RESERVE" },
			{ "СОЛНЦЕ", "SUN" },
			{ "ТАНГАЖ", "PITCH" },
			{ "ТОРМОЗ", "BRAKE" },
			{ "ТРАВМЫ", "INJURIES" },
			{ "ФОНАРИ", "LAMPS" },
			{ "Фонари", "Lamps" },
			{ "ХВАТИТ", "LASTS" },
			{ "ЦЕЛЬ: ", "TARGET: " },
			{ "ШИРОТА", "LATITUDE" },
			{ "ЭКОНОМ", "ECONOMY" },
			{ "вакуум", "vacuum" },
			{ "ветер ", "wind " },
			{ "влево ", "left " },
			{ "вниз ↓", "down ↓" },
			{ "воздух", "air" },
			{ "гибель", "death" },
			{ "голова", "head" },
			{ "грунт ", "ground " },
			{ "корпус", "torso" },
			{ "курс ↑", "heading ↑" },
			{ "назад ", "back " },
			{ "опущен", "down" },
			{ "падает", "falling" },
			{ "поднят", "up" },
			{ "растёт", "rising" },
			{ "среда ", "env " },
			{ "травма", "injury" },
			{ "фонари", "lamps" },
			{ "экипаж", "crew" },
			{ " км/ч", " km/h" },
			{ " м/с²", " m/s²" },
			{ "V СБЛ", "V CLS" },
			{ "АП · ", "AP · " },
			{ "ГОРИЗ", "HORIZ" },
			{ "ГРУНТ", "GROUND" },
			{ "Грунт", "Ground" },
			{ "ДАЛЬН", "RANGE" },
			{ "ДОЗА ", "DOSE " },
			{ "ЗАРЯД", "CHARGE" },
			{ "ЗЕНИТ", "ZENITH" },
			{ "Земля", "Earth" },
			{ "ИТОГО", "TOTAL" },
			{ "КАРТА", "MAP" },
			{ "КУРС ", "HEADING " },
			{ "МЕСТН", "LOCAL" },
			{ "НАДИР", "NADIR" },
			{ "НАЗАД", "BACK" },
			{ "НОРМА", "NORMAL" },
			{ "ОПЦИИ", "OPTIONS" },
			{ "ПОЛЁТ", "FLIGHT" },
			{ "ПУЛЬС", "PULSE" },
			{ "РАНЕЦ", "JET PACK" },
			{ "СБЛИЖ", "CLOSE" },
			{ "СВЯЗЬ", "COMMS" },
			{ "СЕРВО", "SERVO" },
			{ "СИНХР", "MATCH" },
			{ "СРЕДА", "ENV" },
			{ "ТЕЛО ", "BODY " },
			{ "ТЕПЛО", "HEAT" },
			{ "Титан", "Titan" },
			{ "УДЕРЖ", "HOLD" },
			{ "ЩИТОК", "SHADE" },
			{ "прямо", "straight" },
			{ "серво", "servo" },
			{ "снос ", "drift " },
			{ "стоит", "standing" },
			{ " кВт", " kW" },
			{ " кПа", " kPa" },
			{ " м/с", " m/s" },
			{ " мЗв", " mSv" },
			{ " мин", " min" },
			{ " сут", " d" },
			{ "1 км", "1 km" },
			{ "30 м", "30 m" },
			{ "° в.", "°E" },
			{ "° з.", "°W" },
			{ "° с.", "°N" },
			{ "° ю.", "°S" },
			{ "АВТО", "AUTO" },
			{ "АП: ", "AP: " },
			{ "ВЕРТ", "VERT" },
			{ "ВЫКЛ", "OFF" },
			{ "ГОЛО", "HOLO" },
			{ "Д, м", "D, m" },
			{ "ДАВЛ", "PRESS" },
			{ "ДУГА", "ARC" },
			{ "Луна", "Moon" },
			{ "Марс", "Mars" },
			{ "ПОЛЕ", "FIELD" },
			{ "ПРИВ", "DRIVE" },
			{ "РУЛЬ", "STEER" },
			{ "РУЧН", "MAN" },
			{ "РЫСК", "YAW" },
			{ "СВЕТ", "LAMPS" },
			{ "ЛУЧ ШИР", "FLOOD" },
			{ "ЛУЧ РАБ", "WORK" },
			{ "ЛУЧ ДАЛ", "SPOT" },
			{ "шир", "flood" },
			{ "раб", "work" },
			{ "дал", "spot" },
			{ "СВЯЗ", "COMM" },
			{ "СТОП", "STOP" },
			{ "СТЫК", "DOCK" },
			{ "ТЕЛО", "BODY" },
			{ "ТЕПЛ", "HEAT" },
			{ "ТОПЛ", "FUEL" },
			{ "ТЯГА", "THRUST" },
			{ "Тело", "Body" },
			{ "ЦВЕТ", "COLOUR" },
			{ "ЦЕЛИ", "TARGETS" },
			{ "ЦЕЛЬ", "TARGET" },
			{ "база", "base" },
			{ "выкл", "off" },
			{ "ноги", "legs" },
			{ "ночь", "night" },
			{ "поле", "field" },
			{ "руки", "arms" },
			{ "снят", "off" },
			{ " Вт", " W" },
			{ " км", " km" },
			{ "Ап ", "Ap " },
			{ "БАТ", "BAT" },
			{ "ВКД", "EVA" },
			{ "ДЫХ", "BR" },
			{ "ПНВ", "NV" },
			{ "Пе ", "Pe " },
			{ "РАД", "RAD" },
			{ "РЕЗ", "RES" },
			{ "ХОД", "SPEED" },
			{ "бег", "run" },
			{ "вкл", "on" },
			{ "шаг", "walk" },
			{ " л", " L" },
			{ " м", " m" },
			{ " с", " s" },
			{ " ч", " h" },
			{ "АП", "AP" },
			{ "ЖО", "LS" },
			{ "ИК", "IR" },
			{ "Л ", "L " },
			{ "П ", "R " },
			{ "ЭЛ", "EL" },
			{ "В", "E" },
			{ "З", "W" },
			{ "С", "N" },
			{ "Ю", "S" },
			{ "Я", "ME" }
,
			{ "нет цели · выберите на странице ЦЕЛИ", "no target · pick one on the TARGETS page" },
			{ "цель — база · к ней только ПЕРЕЛЁТ", "target is a base · TRANSFER only" },
			{ "СИНХР · скорость уравнена → УДЕРЖ", "MATCH · speeds matched → HOLD" },
			{ "ПЕРЕЛЁТ · над точкой · посадка", "TRANSFER · over the point · landing" },
			{ "только у поверхности с ранцем", "only near a surface, with the jet pack" },
			{ "СБЛИЖ · на месте → УДЕРЖ", "CLOSE · arrived → HOLD" },
			{ "она за плоскостью узла", "she is behind the port's plane" },
			{ "СТЫК · у узла · держу", "DOCK · at the port · holding" },
			{ "ранец в ручном режиме", "jet pack in manual" },
			{ "топлива мало: нужно ", "fuel low: needs " },
			{ "нет питания или сил", "no power, or she cannot act" },
			{ "СБЛИЖ · до точки ", "CLOSE · to the point " },
			{ "у цели нет узлов", "target has no ports" },
			{ "только в космосе", "in space only" },
			{ "нет поверхности", "no surface" },
			{ "СТЫК · по оси ", "DOCK · on the axis " },
			{ " на площадку ", " on pad " },
			{ "УДЕРЖ · уход ", "HOLD · drift " },
			{ "ЗАВИС · снос ", "HOVER · drift " },
			{ "СИНХР · Vотн ", "MATCH · Vrel " },
			{ "снят вручную", "taken over by hand" },
			{ "цель исчезла", "target lost" },
			{ "дальше 15 км", "beyond 15 km" },
			{ " · выключен", " · off" },
			{ " м/с, есть ", " m/s, have " },
			{ "ПЕРЕЛЁТ · ", "TRANSFER · " },
			{ " м · вбок ", " m · sideways " },
			{ "торможение", "braking" },
			{ " · включён", " · on" },
			{ "выключен", "off" },
			{ "посадка", "landing" },
			{ "подъём", "climb" },
			{ "разгон", "boost" },
			{ "полёт", "coast" },
			{ "ЗАВИС", "HOVER" },
			{ " см", " cm" }
			};
			const auto it = M.find(ru);
			return it != M.end() ? it->second : ru;
		}
		// ---- colours: three palettes over seven atlas colours ----
		enum Col { CP, CA, CW, CD, CR, CK };   // primary, accent (autopilot, target, caution), white, dim, red, black
		struct Palette { DWORD col[6]; int atlas[6]; };   // 0xAABBGGRR
		const Palette PALS[3] = {
			{ { 0xFFFFE246, 0xFF2894FF, 0xFFFFF8F2, 0xFFA58E5E, 0xFF4646FF, 0xFF000000 }, { 0, 1, 2, 4, 3, 4 } },   // cyan + orange
			{ { 0xFF2894FF, 0xFFFFE246, 0xFFFFF8F2, 0xFF527BA3, 0xFF4646FF, 0xFF000000 }, { 1, 0, 2, 5, 3, 5 } },   // orange + cyan
			{ { 0xFFFFF8F2, 0xFF2894FF, 0xFFFFFFFF, 0xFF9C8D7D, 0xFF4646FF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } } }; // white + orange
		// light-built: cold blue-white light; electroluminescent: flat blue-green. Accent and red stay as they are.
		const Palette LOOK_PAL[3] = {
			{},
			{ { 0xFFFFECB8, 0xFF6AC4FF, 0xFFFFF6E6, 0xFFB89F6F, 0xFF303BFF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } },   // photonics (the mockup)
			{ { 0xFFC8FF5C, 0xFF4AB8FF, 0xFFF0FFC9, 0xFF728A2B, 0xFF4A5AFF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } } };
		// the GRI ring: neon orange gas-discharge digits and lamps in dark helmet hardware (its own iron, not the light display)
		const Palette GRI_PAL = { { 0xFF3A8AFF, 0xFF4AC0FF, 0xFF70B0FF, 0xFF223A5A, 0xFF2A3BFF, 0xFF080A0B }, { 1, 1, 1, 5, 3, 5 } };
		// per role (primary, accent, white, dim, red, black): multiply the atlas glyph colour, or {0} = as baked
		const float LOOK_TINT[3][6][3] = {
			{},
			{ { 0.80f, 0.95f, 1.0f }, {}, { 0.95f, 1.0f, 1.0f }, { 0.75f, 0.95f, 1.1f }, {}, {} },
			{ { 0.36f, 1.0f, 0.78f }, {}, { 0.80f, 1.0f, 0.94f }, { 0.35f, 0.90f, 0.75f }, {}, {} } };
		// the light suit's «Буран» layout: its own two themes, laser light without the photonics' tint. 0 «Буран» - a green
		// laser, amber for the autopilot and cautions; 1 industrial - white light, orange for what is on and for cautions
		// the themes (the user, 2026-10-20): laser - a green coherent line; projector - cyan light with a soft glow;
		// augmented reality - white and blue, flat; high contrast - orange (not white)
		const Palette BURAN_PAL[4] = {
			{ { 0xFF93FF6D, 0xFF2EB0FF, 0xFFF0FFE9, 0xFF77A85F, 0xFF3D4AFF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } },
			{ { 0xFFFFE357, 0xFF2EB0FF, 0xFFFFF8F2, 0xFFA58E5E, 0xFF4646FF, 0xFF000000 }, { 0, 1, 2, 4, 3, 4 } },
			{ { 0xFFFFE6CF, 0xFF3AD2FF, 0xFFFFFFFF, 0xFFAD937F, 0xFF4A55FF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } },
			{ { 0xFF1A8AFF, 0xFF4AE6FF, 0xFFE0F2FF, 0xFF1A5FB8, 0xFF2A35FF, 0xFF000000 }, { 1, 1, 2, 5, 3, 5 } } };
		const int BURAN_LOOK[4] = { 1, 0, 2, 1 };
		// the glass canvas: an angular picture of the helmet from the eye - x the yaw (±GLASS_YW), y the pitch (GLASS_PT at
		// the top .. GLASS_PB at the bottom). The same window as Tantra_Design\blender\make_visor_glass_hud.py
		const double GLASS_YW = 105 * RAD, GLASS_PT = 50 * RAD, GLASS_PB = -72 * RAD, GLASS_FREF_HALF = 31 * RAD;   // the layout's reference view on the glass (smaller: more of the view open)
		const int GLASS_CW = 2560, GLASS_CH = 1280;   // the whole glass: 12 px a degree
		// the helmet's window seen from the eye (0, 0.700, 0.150): the opening of its inner shell (InHelm) measured from
		// the mesh (2026-10-20), its top and bottom pitch every 10 deg of yaw, 0..100 (the same both sides)
		const double WIN_TOP[12] = { 58.9, 58.9, 58.7, 58.3, 57.4, 55.9, 53.8, 49.2, 45.3, 36.2, 26.2, -5.0 };   // 110: the side, closed
		const double WIN_BOT[12] = { -70.0, -69.8, -69.5, -68.4, -67.6, -65.5, -62.7, -59.3, -55.3, -46.2, -36.1, -5.0 };
		// a smooth curve through a table's points (Catmull-Rom; mirror: the curve is symmetric about the table's start)
		double Spline(const double* t, int n, double a, bool mirror)
		{
			const int i = std::clamp(static_cast<int>(a), 0, n - 2); const double f = a - i;
			const double p0 = i > 0 ? t[i - 1] : mirror ? t[1] : t[0], p1 = t[i], p2 = t[i + 1], p3 = t[(std::min)(i + 2, n - 1)];
			return 0.5 * (2 * p1 + (p2 - p0) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f + (3 * p1 - p0 - 3 * p2 + p3) * f * f * f);
		}
		double WinEdge(const double* t, double yawDeg) { return Spline(t, 12, (std::min)(std::abs(yawDeg), 109.999) / 10, true); }
		// its side edge: the yaw every 10 deg of pitch, -60..50 (rays from the eye against InHelm and VisorB, 2026-10-20)
		const double WIN_SIDE[12] = { 65, 81, 91, 98, 102, 104, 104, 102, 98, 91, 82, 66 };
		double WinSide(double pitchDeg) { return Spline(WIN_SIDE, 12, (std::clamp(pitchDeg, -60.0, 49.999) + 60) / 10, false); }
		// the MFDs at the sides (the user's frames, 2026-10-20, in the helmet's angles by the camera fitted to the panels on
		// the screenshots; then: «не растягивай» - every button the same): one flat panel seen from the eye, its edges
		// straight lines of the view - the inner edge at yaw 54 from pitch 20 to -18, the top and the bottom straight, the
		// outer corners at 88 deg, inside the window's side edge less 9. Fitted (smfd_fit.py): the centre yaw 71.09, pitch
		// 0.89, roll 0.27, the distance 470 layout units for the panel's 289 width. Its frame's outer side runs along the
		// glass, 9 deg inside the window's side edge, from corner to corner
		const double SMFD_YAW = 71.09, SMFD_PITCH = 0.89, SMFD_ROLL = 0.27, SMFD_F = 470.0 / 289, SMFD_OUT = 9, SMFD_BULGE = 0.55;   // (the frame's outer side: 0.55 of the glass's bulge over the straight corner-to-corner line - the user: less convex)
		// the reserves' rings along the window's bottom edge, the outer first (the user's oval: yaw -64..-13), their centres
		// 11 deg over the edge (the times and the rates under them clear of it); each ring's own scale so all four are seen
		// the same size (radius 4.3 deg: the glass is nearer the eye low and in front)
		// (the user, 2026-10-20: «не сплющивать, не деформировать» - the top, the counters and the mechanization are flat
		// panels seen from the eye, as the side MFDs: no squeeze, no shear, straight lines straight). Fitted by plates_fit.py:
		// the counters (260 x 150, the rings 64 apart): the first ring at yaw -54, 21 deg over the window's bottom edge, the
		// last at -10, 19 over it (raised and drawn in: the user - the rings were cut at the bottom) - the centre yaw -34.32,
		// pitch -50.99, roll 15.33, distance 353.9; the CO2 line at its top right clear of the low lines (-38) by 2 deg
		const double CNT_YAW = -34.32, CNT_PITCH = -50.99, CNT_ROLL = 15.33, CNT_F = 353.9, CNT_W = 260, CNT_H = 150;
		// the mechanization: one column of the twelve down the right of the chin (the user's squares: yaw 44..46, pitch
		// -35..-52; the area yaw 37..66, pitch -23..-58), clear of the right MFD's bottom
		// fitted to the user's frame on a screenshot (fit2.py: the camera from the panels on it, rms 9 px), then 3 deg lower:
		// its top right corner 2.9 deg under the right MFD, its bottom 6 deg over the window's bottom
		const double MECH_YAW = 54.4, MECH_PITCH = -41.4, MECH_ROLL = -19.54, MECH_F = 562.6, MECH_W = 200, MECH_H = 350;
		// the top band: one flat panel, the layout's (640, 68) at pitch 40, 700 layout units away - the name at yaw -50,
		// pitch 31, the modes at 43, 16 deg under the window's top
		const double TOP_PC = 40, TOP_F = 700;
		// the top band: its x laid along the arc of its own pitch (equal yaw up there is narrower: cos 44 = 0.72 - the
		// middle looked squeezed), 0.88 of the old width so the name and the clock stay inside the window
		const double TOP_ARC_Q = 0.88;
		// its middle (the modes, the compass, the lamps, the dangers) 1.3 times the layout round (640, 8): the size of the
		// brow's scales beside it (the user: the top not squeezed)
		const double TOP_MID = 1.3;
		// the layout on the glass, every number from that window and the view at FoV 90 looking level (±45 up and down,
		// ±60 to the sides on 16:9): all of it within ±42 / ±58
		const double LAY_DEG = 58.0 / 640;       // degrees per layout unit: the layout's half width (640) = 58 deg
		const double TOP_P0 = 40.0;              // the top band's centre line (layout y 68): 40 deg up, 19 under the window's top
		const double BOT_P0 = -38.0;             // the low lines (layout y 652): 38 deg down, 32 over the window's bottom
		const double MFD_YAW = 41, MFD_PITCH = 3, MFD_SIDE_YAW = 74, MFD_SIDE_PITCH = 2, LOW_YAW = 38, LOW_PITCH = -30;   // (side: a folded MFD's tab, the middle of its frame)
		// the visor's glass itself: the sphere fitted to VisorGlassHud.msh (2026-10-20), seen from the eye (x right, y up,
		// z forward, metres) - its centre 10.5 cm behind the eye, radius 15.2 cm. The panels lie on it as decals: their
		// edges are lines of the glass, as flat as the glass is (the eye is not at its centre: no fish-eye bulge)
		const double GLASS_SPH_C[3] = { 0.0, -0.0046, -0.1051 }, GLASS_SPH_R = 0.1522;
		// the panels' size on the glass: metres of glass a radian of the layout at the reference view. Measured sizes seen
		// from the eye: an MFD at the sides 26 x 44 deg (yaw 58..84, pitch -22..22), in front 25 x 34 deg (yaw 28..53,
		// pitch -14..20), the low plates 26 x 16 / 29 x 19 deg (yaw 22..52, pitch -39..-21) - nothing overlaps, all inside
		// the window and, in front, inside FoV 90 looking level
		const double MFD_SIDE_ARC = 0.095, MFD_ARC = 0.055, LOW_ARC = 0.05;
		const char* LOOK_NAME[3] = { "ГОЛО", "ФОТОНИКА", "ЭЛ" };
		const char* PAL_NAME[3] = { "ЦИАН + ОРАНЖ", "ОРАНЖ + ЦИАН", "БЕЛЫЙ + ОРАНЖ" };
		const char* PAL_NOTE[3] = { "основной циан, автопилот оранжевый", "основной оранжевый, автопилот циан", "основной белый, автопилот оранжевый" };
		const double SIZE_BASE[4] = { 9.5, 11, 13, 15 };
		const double ZOOM[10] = { 50, 100, 250, 500, 1000, 2500, 5000, 25000, 100000, 500000 };   // local map: outer ring, m

		using V2 = std::pair<double, double>;

		// The machine she drives from its post (МПУ, Modules\MPU.dll): its state through the module's own export, looked up
		// each frame (the module loads with the first platform and goes with the session) - no link to it, no header.
		bool MpuOf(OBJHANDLE person, MpuState& st)
		{
			HMODULE m = GetModuleHandleA("MPU.dll");
			if (!m || !person) return false;
			st = MpuState{};
			// the sized call first: MPU.dll writes no more than our copy holds, whatever its MpuState has grown to
			using SizedFn = int(*)(OBJHANDLE, void*, int);
			if (const SizedFn fs = reinterpret_cast<SizedFn>(GetProcAddress(m, "mpuDriverStateSized")))
				return fs(person, &st, static_cast<int>(sizeof st)) > 0;
			using Fn = bool(*)(OBJHANDLE, MpuState*);
			const Fn f = reinterpret_cast<Fn>(GetProcAddress(m, "mpuDriverState"));
			return f && f(person, &st);
		}
		// a command to that machine (1 the parking brake on/off, 2 the platform up/down), if MPU.dll takes commands
		using MpuCmdFn = bool(*)(OBJHANDLE, int);
		MpuCmdFn MpuCmd()
		{
			HMODULE m = GetModuleHandleA("MPU.dll");
			return m ? reinterpret_cast<MpuCmdFn>(GetProcAddress(m, "mpuDriverCommand")) : nullptr;
		}

		// the D3D9 client's core (Orbiter 2024: gcCore replaces gcAPI; the Sketchpad2/3 calls are on oapi::Sketchpad itself)
		gcCore2* Core()
		{
			static bool tried = false; static gcCore2* core = nullptr;
			if (!tried) { tried = true; core = gcGetCoreInterface(); }
			return core;
		}
		// the sketchpad if it draws the extended calls (alpha pens, StretchRect, brightness...), else null: the plain path.
		// Under the D3D9 client every sketchpad has them (in 2024 its SketchpadVersion() always says 1: D3D9Pad does not
		// override GetVersion) - so the test is the client itself
		oapi::Sketchpad* Ext(oapi::Sketchpad* skp)
		{
			return Core() && skp ? skp : nullptr;
		}

		std::vector<unsigned> Decode(const std::string& s)
		{
			std::vector<unsigned> out;
			for (size_t i = 0; i < s.size();)
			{
				const unsigned char c = s[i];
				unsigned cp; int n;
				if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; } else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; } else { cp = c & 0x07; n = 4; }
				for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
				out.push_back(cp); i += n;
			}
			return out;
		}
		std::vector<std::string> Chars(const std::string& s)
		{
			std::vector<std::string> out;
			for (size_t i = 0; i < s.size();)
			{
				const unsigned char c = s[i];
				const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
				out.push_back(s.substr(i, n)); i += n;
			}
			return out;
		}
		unsigned long long Key(int size, int colour, unsigned cp) { return (static_cast<unsigned long long>(size) << 40) | (static_cast<unsigned long long>(colour) << 32) | cp; }

		std::string Upper(const std::string& utf8)
		{
			const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
			if (wn <= 1) return utf8;
			std::wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wn);
			CharUpperBuffW(&w[0], wn - 1);
			const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
			std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
			if (!s.empty() && s.back() == '\0') s.pop_back();
			return s;
		}
		// Russian number: decimal comma, a real minus
		std::string Num(double v, int dec = 1, bool plus = false)
		{
			char b[48]; snprintf(b, sizeof b, "%.*f", dec, v);
			std::string s = b;
			if (!g_en) for (char& c : s) if (c == '.') c = ',';   // Russian: a decimal comma
			if (s == "-0" || s == "-0,0" || s == "-0,00") s = s.substr(1);
			if (!s.empty() && s[0] == '-') s = "−" + s.substr(1);
			else if (plus && v > 0) s = "+" + s;
			return s;
		}
		std::string Dist(double m)
		{
			if (m < 100) return Num(m, 1) + TR(" м");
			if (m < 1000) return Num(m, 0) + TR(" м");
			if (m < 100000) return Num(m / 1000, m < 10000 ? 2 : 1) + TR(" км");
			return Num(m / 1000, 0) + TR(" км");
		}
		std::string Clock(double s)
		{
			if (s < 0 || s > 359999) return "—";
			char b[32];
			if (s < 3600) snprintf(b, sizeof b, "%d:%02d", static_cast<int>(s) / 60, static_cast<int>(s) % 60);
			else snprintf(b, sizeof b, TR("%d ч %02d мин"), static_cast<int>(s) / 3600, (static_cast<int>(s) / 60) % 60);
			return b;
		}
		std::string Hours(double h) { return h > 99 ? TR("> 99 ч") : Num(h, h < 10 ? 1 : 0) + TR(" ч"); }
		const char* BodyRu(const std::string& n)
		{
			if (n == "Earth") return TR("Земля"); if (n == "Moon") return TR("Луна"); if (n == "Mars") return TR("Марс");
			if (n == "Venus") return TR("Венера"); if (n == "Titan") return TR("Титан"); if (n == "Mercury") return TR("Меркурий");
			return nullptr;
		}
		double Wrap(double a) { while (a > PI) a -= PI2; while (a < -PI) a += PI2; return a; }
		VECTOR3 Unit(const VECTOR3& v) { const double l = length(v); return l > 0 ? v / l : v; }
	}

	// the language's one state lives with the crew (CrewMember.cpp): read once from OrbiterCrew.cfg, set by the switch here
	bool UiEnglish();
	void SetUiLanguage(bool english);

	// the suit computer's other files (the autopilot) speak the display's language through the same table
	const char* SuitTr(const char* ru) { return TR(ru); }
	bool SuitEnglish() { return g_en; }

	std::string Ru(const std::string& utf8)
	{
		if (utf8.empty()) return utf8;
		const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wn);
		const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0'); WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
		if (!s.empty() && s.back() == '\0') s.pop_back();
		return s;
	}

	// ================= glyph atlas =================
	HudText::~HudText() { Release(); }
	void HudText::Release()
	{
		if (tex) oapiReleaseTexture(tex);
		tex = nullptr; glyphs.clear(); index.clear();
	}

	bool HudText::Load()
	{
		FILE* f = fopen("Config\\Tantra\\HudFont.txt", "r");
		if (!f) { oapiWriteLog(const_cast<char*>("OrbiterCrew: Config\\Tantra\\HudFont.txt missing - helmet display falls back to system text")); return false; }
		char line[256];
		while (fgets(line, sizeof line, f))
		{
			if (line[0] == '#') continue;
			int aw, ah, u;
			if (sscanf(line, "ATLAS %d %d %d", &aw, &ah, &u) == 3) { unit = u; continue; }
			int si, ci, x_, y_, w_, h_, ox, oy; unsigned cp; float adv;
			if (sscanf(line, "%d %d %u %d %d %d %d %d %d %f", &si, &ci, &cp, &x_, &y_, &w_, &h_, &ox, &oy, &adv) == 10)
			{
				index.emplace_back(Key(si, ci, cp), static_cast<int>(glyphs.size()));
				glyphs.push_back({ x_, y_, w_, h_, ox, oy, adv });
			}
		}
		fclose(f);
		std::sort(index.begin(), index.end());
		tex = oapiLoadTexture("Tantra\\HudFont.dds");
		if (!tex) oapiWriteLog(const_cast<char*>("OrbiterCrew: Textures\\Tantra\\HudFont.dds missing - helmet display falls back to system text"));
		return tex != nullptr;
	}

	const HudText::Glyph* HudText::Find(int size, int colour, unsigned cp) const
	{
		const auto k = Key(size, colour, cp);
		auto it = std::lower_bound(index.begin(), index.end(), std::make_pair(k, -1));
		if (it == index.end() || it->first != k) return nullptr;   // not in the atlas: nothing, never a '?'
		return &glyphs[it->second];
	}

	double HudText::Width(const std::string& utf8, int size, double scale) const
	{
		double w = 0;
		for (unsigned cp : Decode(utf8)) if (const Glyph* g = Find(size, 0, cp)) w += g->adv;
		return w * scale / unit;
	}

	bool HudText::Draw(oapi::Sketchpad* skp, double x, double y, const std::string& utf8, int size, int colour, int align, double scale, double kx, double ky) const
	{
		auto* s2 = Ext(skp);
		if (!tex || !s2) return false;
		const double f = scale / unit, fx = f * kx, fy = f * ky;
		double px = x - (align == 1 ? 0.5 : align == 2 ? 1.0 : 0.0) * Width(utf8, size, scale) * kx;
		for (unsigned cp : Decode(utf8))
		{
			const Glyph* g = Find(size, colour, cp);
			if (!g) continue;
			if (g->w > 0)
			{
				RECT src = { g->x, g->y, g->x + g->w, g->y + g->h };
				const double dx = px + g->ox * fx, dy = y + g->oy * fy;
				RECT dst = { static_cast<LONG>(std::lround(dx)), static_cast<LONG>(std::lround(dy)), static_cast<LONG>(std::lround(dx + g->w * fx)), static_cast<LONG>(std::lround(dy + g->h * fy)) };
				s2->StretchRect(tex, &src, &dst);
			}
			px += g->adv * fx;
		}
		return true;
	}

	// ================= drawing =================
	class Gfx
	{
	public:
		Gfx(oapi::Sketchpad* skp, SuitHud& o, const HudText& t, const Palette& p)   // a fixed palette (the GRI ring)
			: skp(skp), s2(Ext(skp)), o(o), txt(t), P(p), look(0) { if (o.drawCv && o.gx) { gx = o.gx; cv = o.drawCv; } }
		Gfx(oapi::Sketchpad* skp, SuitHud& o, const HudText& t, const Palette& p, int look, bool tinted)   // «Буран»: its palette, the photonics' lines
			: skp(skp), s2(Ext(skp)), o(o), txt(t), P(p), look(look), tint(tinted)
		{
			if (o.drawCv && o.gx) { gx = o.gx; cv = o.drawCv; }
			if (look && !o.gcTried) { o.gcTried = true; o.gcOk = Core() != nullptr; }
			if (look && o.gcOk && s2) s3 = s2;
		}
		Gfx(oapi::Sketchpad* skp, SuitHud& o, const HudText& t, int pal, int look = 0)
			: skp(skp), s2(Ext(skp)), o(o), txt(t), P(look ? LOOK_PAL[look] : PALS[pal]), look(look)
		{
			if (o.drawCv && o.gx) { gx = o.gx; cv = o.drawCv; }   // the native canvas: every shape below goes there
			if (look && !o.gcTried) { o.gcTried = true; o.gcOk = Core() != nullptr; }
			if (look && o.gcOk && s2) s3 = s2;
		}
		oapi::Sketchpad* skp; oapi::Sketchpad* s2; SuitHud& o; const HudText& txt; const Palette& P;
		const D3D11Hud::Api* gx{}; D3D11Hud::Canvas cv{};   // the D3D11 client's canvas (null: the Sketchpad)
		// ---- the native side: the same widths, colours and alphas as the Sketchpad's, in float surface pixels
		float PW(double w) const { return static_cast<float>((std::max)(1.0, w * u * 0.8 * (1 + 0.6 * boost) * 0.5 * (kx + ky))); }
		float FX(double x) const { return static_cast<float>(X(x) * kx); }
		float FY(double y) const { return static_cast<float>(Y(y) * ky); }
		bool tint{ true };   // the photonics' glyph tint (off for «Буран»'s own palettes)
		// ---- a plate («Буран»): a flat panel set round the eye and turned to face it - its normal points at the eye, all
		// plates share one perspective centred on the view (the focal length of the approved mockup, 0.85 of the view's
		// width: the real eye's would turn them too far to read). Every point drawn while a plate is set goes through its
		// projection; text gets the plate's local slant, so the glyphs stay straight and sharp
		bool pj{}; double pjCx{}, pjCy{}, pjF{}, pjW2{}, pjH2{}, pjC[3]{}, pjEx[3]{}, pjEy[3]{}, pjPad{};   // (kept together: SetBase copies them as one block)
		void Plate(double x0, double y0, double w, double h, double W, double H)   // view pixels
		{
			pjW2 = W / 2; pjH2 = H / 2; pjF = 0.85 * W; pjCx = x0 + w / 2; pjCy = y0 + h / 2;
			const double C[3] = { pjCx - pjW2, pjCy - pjH2, pjF }, l = std::sqrt(C[0] * C[0] + C[1] * C[1] + C[2] * C[2]);
			const double n[3] = { C[0] / l, C[1] / l, C[2] / l };
			double ex[3] = { n[2], 0, -n[0] }; const double le = std::sqrt(ex[0] * ex[0] + ex[2] * ex[2]); ex[0] /= le; ex[2] /= le;   // (0,1,0) x n
			const double ey[3] = { n[1] * ex[2] - n[2] * ex[1], n[2] * ex[0] - n[0] * ex[2], n[0] * ex[1] - n[1] * ex[0] };          // n x ex
			for (int i = 0; i < 3; ++i) { pjC[i] = C[i]; pjEx[i] = ex[i]; pjEy[i] = ey[i]; }
			pj = true;
		}
		// a plate fixed on the helmet's glass: q = HelmDir's centre, right, down (camera space, y down). Its flat layout is
		// the rectangle (x0, y0, w, h) the plate has looking straight ahead; F the view's focal length in pixels. False: the
		// panel is behind or across the eye - not drawn
		bool PlateAt(double x0, double y0, double w, double h, double W, double H, double F, const double q[9], double dist = 0)
		{
			if (dist <= 0) dist = F;
			pjW2 = W / 2; pjH2 = H / 2; pjF = F; pjCx = x0 + w / 2; pjCy = y0 + h / 2;
			for (int i = 0; i < 3; ++i) { pjC[i] = q[i] * dist; pjEx[i] = q[3 + i]; pjEy[i] = q[6 + i]; }
			for (int c = 0; c < 4; ++c)
			{
				const double u = (c & 1 ? 0.5 : -0.5) * w, vv = (c & 2 ? 0.5 : -0.5) * h;
				if (pjC[2] + pjEx[2] * u + pjEy[2] * vv < 0.25 * dist) { pj = false; return false; }
			}
			pj = true; return true;
		}
		void NoPlate() { pj = false; gl = false; noHit = false; if (baseOn) { std::copy(bGl, bGl + 25, &glC[0]); gl = true; pj = bPj; std::copy(bPjv, bPjv + 15, &pjCx); noHit = bNoHit; } }
		// the layout's own place on the glass (the glass pass): everything not on a panel goes through it
		bool baseOn{}, bPj{}, bNoHit{}; double bGl[25]{}, bPjv[15]{};
		void SetBase() { baseOn = true; std::copy(&glC[0], &glC[0] + 25, bGl); bPj = pj; std::copy(&pjCx, &pjCx + 15, bPjv); bNoHit = noHit; }
		// ---- the helmet's glass: the panel drawn into the glass canvas (an angular picture of the helmet, seen from the eye:
		// x = yaw, y = pitch), which the glass mesh - the visor's own shape - carries. glC/glR/glD: the panel's centre
		// direction, right and down in the helmet (y up); glF its distance in view pixels (the reference focal length)
		bool gl{}, noHit{}; double glC[3]{}, glR[3]{}, glD[3]{}, glF{}, glCx{}, glCy{}, glCW{}, glCH{}, glYW{}, glPT{}, glPB{}, glLin{}, glY0{}, glP0{}, glC0{}, glK{}, glS{}, glW{}, glH{};   // (kept together: SetBase copies them as one block)
		VESSEL* hv{}; double hitW{}, hitH{}, hitF{};   // the clicks of an equal-angle part: through the helmet to the view
		void Glass(double x0, double y0, double w, double h, double F, double yaw, double pitch, double CW, double CH, double YW, double PT, double PB)
		{
			const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
			glC[0] = cp * sy; glC[1] = sp; glC[2] = cp * cy;
			glR[0] = cy; glR[1] = 0; glR[2] = -sy;
			glD[0] = sp * sy; glD[1] = -cp; glD[2] = sp * cy;
			glF = F; glCx = x0 + w / 2; glCy = y0 + h / 2; glCW = CW; glCH = CH; glYW = YW; glPT = PT; glPB = PB;
			glLin = 0; glY0 = yaw; glP0 = pitch;
			gl = true;
		}
		void Gl(double sx, double sy, double& ox_, double& oy_) const   // view pixels of the flat panel -> glass canvas pixels
		{
			const double u = sx - glCx, v = sy - glCy;
			if (glLin > 0) { double yaw, pitch; LinAng(sx, sy, yaw, pitch); ox_ = glCW * (0.5 + yaw / (2 * glYW)); oy_ = glCH * (glPT - pitch) / (glPT - glPB); return; }   // equal angles
			const double P0 = glC[0] * glF + glR[0] * u + glD[0] * v, P1 = glC[1] * glF + glR[1] * u + glD[1] * v, P2 = glC[2] * glF + glR[2] * u + glD[2] * v;
			const double yaw = std::atan2(P0, P2), pitch = std::atan2(P1, std::hypot(P0, P2));
			ox_ = glCW * (0.5 + yaw / (2 * glYW)); oy_ = glCH * (glPT - pitch) / (glPT - glPB);
		}
		// a point of the layout (view pixels) -> the surface's pixels: through the glass, the plate, or as it is
		void Map(double sx, double sy, double& ox_, double& oy_) const
		{
			if (gl) { Gl(sx, sy, ox_, oy_); return; }
			if (pj) Pj(sx, sy, sx, sy);
			ox_ = sx * kx; oy_ = sy * ky;
		}
		bool On() const { return pj || gl; }
		// equal angles: x by LAY_DEG from the centre; y from the mode's line (glCy) - flat, or along the window's edge
		void LinAng(double sx, double sy, double& yaw, double& pitch) const
		{
			if (glLin == 4)   // a decal on the glass: from the point the panel's centre is seen at, along the sphere
			{
				const double* Cs = GLASS_SPH_C; const double Rs = GLASS_SPH_R;
				const double d0[3] = { std::cos(glP0) * std::sin(glY0), std::sin(glP0), std::cos(glP0) * std::cos(glY0) };
				const double dc = d0[0] * Cs[0] + d0[1] * Cs[1] + d0[2] * Cs[2], cc = Cs[0] * Cs[0] + Cs[1] * Cs[1] + Cs[2] * Cs[2];
				const double t = dc + std::sqrt(dc * dc - cc + Rs * Rs);
				double n[3]; for (int i = 0; i < 3; ++i) n[i] = (t * d0[i] - Cs[i]) / Rs;
				const double le = std::hypot(n[0], n[2]), ex[3] = { n[2] / le, 0, -n[0] / le };
				const double ey[3] = { n[1] * ex[2] - n[2] * ex[1], n[2] * ex[0] - n[0] * ex[2], n[0] * ex[1] - n[1] * ex[0] };
				const double u = (sx - glCx) / glF * glS, w = -(sy - glCy) / glF * glS, r = std::hypot(u, w), th = r / Rs;
				const double cs = std::cos(th), sn = r > 1e-9 ? std::sin(th) / r : 1 / Rs;
				double q[3]; for (int i = 0; i < 3; ++i) q[i] = Cs[i] + Rs * (n[i] * cs + (u * ex[i] + w * ey[i]) * sn);
				yaw = std::atan2(q[0], q[2]); pitch = std::atan2(q[1], std::hypot(q[0], q[2]));
				return;
			}
			if (glLin == 6)   // a flat panel seen from the eye (glC its centre, glR / glD its right and down, rolled; glF its distance)
			{
				const double u = sx - glCx, v = sy - glCy;
				const double P0 = glC[0] * glF + glR[0] * u + glD[0] * v, P1 = glC[1] * glF + glR[1] * u + glD[1] * v, P2 = glC[2] * glF + glR[2] * u + glD[2] * v;
				yaw = std::atan2(P0, P2); pitch = std::atan2(P1, std::hypot(P0, P2));
				return;
			}
			if (glLin == 7) { yaw = sx / 100 * RAD; pitch = sy / 100 * RAD; return; }   // the points in hundredths of a degree
			// x: degrees of arc along the band's line, so the yaw is the arc over the cosine of the pitch (no squeeze up high)
			const double xd = (sx - glCx) / glK * LAY_DEG * TOP_ARC_Q;
			double line = glP0 * DEG, dy = sy - glCy;
			if (glLin == 2)   // the brow's scales: the layout's rim arc (centre 640, 1560, r 1492) laid on the window's top edge
			{
				const double lx = (sx - glC0) / glK - 640, yr = 1560 - std::sqrt((std::max)(0.0, 1492.0 * 1492.0 - lx * lx));
				dy = sy - yr * glK;
			}
			double yd = xd, p = line;
			for (int it = 0; it < 3; ++it)
			{
				const double ln = line + (glLin == 2 ? WinEdge(WIN_TOP, yd) - WIN_TOP[0] : glLin == 3 ? WinEdge(WIN_BOT, yd) - WIN_BOT[0] : 0.0);
				p = ln - dy / glK * LAY_DEG;
				yd = xd / std::cos(p * RAD);
			}
			yaw = yd * RAD; pitch = p * RAD;
		}
		void Pj(double sx, double sy, double& ox_, double& oy_) const   // view pixels -> view pixels
		{
			const double u = sx - pjCx, v = sy - pjCy;
			const double P0 = pjC[0] + pjEx[0] * u + pjEy[0] * v, P1 = pjC[1] + pjEx[1] * u + pjEy[1] * v, P2 = pjC[2] + pjEx[2] * u + pjEy[2] * v;
			ox_ = pjW2 + pjF * P0 / P2; oy_ = pjH2 + pjF * P1 / P2;
		}
		void FP(double x, double y, float& fx, float& fy) const   // a point in surface pixels, through the plate when set
		{
			double ox_, oy_; Map(X(x), Y(y), ox_, oy_);
			fx = static_cast<float>(ox_); fy = static_cast<float>(oy_);
		}
		// the plate's local affine at a point (surface pixels): maps the flat surface point to its projected place
		void PjAffine(double x, double y, float m[6]) const
		{
			const double sx = X(x), sy = Y(y), e = 2.0; double ax, ay, bx, by, cx_, cy_;
			Map(sx, sy, ax, ay); Map(sx + e, sy, bx, by); Map(sx, sy + e, cx_, cy_);   // surface pixels
			const double a = (bx - ax) / e / kx, b = (cx_ - ax) / e / ky, c = (by - ay) / e / kx, d = (cy_ - ay) / e / ky;
			const double px = sx * kx, py = sy * ky, qx = ax, qy = ay;
			m[0] = static_cast<float>(a); m[1] = static_cast<float>(b); m[2] = static_cast<float>(qx - a * px - b * py);
			m[3] = static_cast<float>(c); m[4] = static_cast<float>(d); m[5] = static_cast<float>(qy - c * px - d * py);
		}
		static void Mul(const float A[6], const float B[6], float out[6])   // A after B
		{
			out[0] = A[0] * B[0] + A[1] * B[3]; out[1] = A[0] * B[1] + A[1] * B[4]; out[2] = A[0] * B[2] + A[1] * B[5] + A[2];
			out[3] = A[3] * B[0] + A[4] * B[3]; out[4] = A[3] * B[1] + A[4] * B[4]; out[5] = A[3] * B[2] + A[4] * B[5] + A[5];
		}
		void Quad(double x0, double y0, double x1, double y1, float q[8]) const { FP(x0, y0, q[0], q[1]); FP(x1, y0, q[2], q[3]); FP(x1, y1, q[4], q[5]); FP(x0, y1, q[6], q[7]); }
		// a mapping that bends straight lines (the glass's perspective middle, the bands along its edges): segments are cut
		// into pieces of ~6 layout units, each mapped - smooth curves on the glass, no breaks
		bool Bends() const { return gl && glLin != 1; }
		void PathXY(const std::vector<V2>& pts, bool closed, std::vector<float>& xy) const
		{
			xy.clear();
			const size_t n = pts.size(), m = closed ? n : n - 1;
			for (size_t i = 0; i < m; ++i)
			{
				const V2 a = pts[i], b = pts[(i + 1) % n];
				const int k = Bends() ? (std::max)(1, static_cast<int>(std::hypot(b.first - a.first, b.second - a.second) / 6)) : 1;
				for (int j = 0; j < k; ++j) { float fx, fy; FP(a.first + (b.first - a.first) * j / k, a.second + (b.second - a.second) * j / k, fx, fy); xy.push_back(fx); xy.push_back(fy); }
			}
			if (!closed && n) { float fx, fy; FP(pts[n - 1].first, pts[n - 1].second, fx, fy); xy.push_back(fx); xy.push_back(fy); }
		}
		void QuadPath(double x0, double y0, double x1, double y1, std::vector<float>& xy) const { PathXY({ { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }, true, xy); }
		// a stroke with the look's halo: holo a soft glow, light-built a double halo (as its two wider passes), the
		// electroluminescent none
		void NStroke(const float* xy, int n, int c, double w, double a, bool dash, bool glow, bool closed)
		{
			double cw = w, gw = 0, gs = 0;
			if (look == 1) { a *= shimmer; cw = w * 0.85; if (glow && !dash) { gw = (PW(w + 5) - PW(cw)) * 0.5; gs = 0.35; } }
			else if (look == 0 && glow && !dash) { gw = (PW(w + 2.4) - PW(w)) * 0.5 + 1.0; gs = 0.3; }
			gx->SetGlow(cv, static_cast<float>(gw), static_cast<float>(gs));
			const float pw = PW(cw);
			gx->Polyline(cv, xy, n, pw, C(c, a), (dash ? D3D11Hud::DASHED : 0u) | (closed ? D3D11Hud::CLOSED : 0u), dash ? 3 * pw + 2 : 0);
			gx->SetGlow(cv, 0, 0);
		}
		DWORD TextCol(int c) const   // the text's colour: the palette's, with the look's tint, the shimmer and the dark optics
		{
			const float* tn = look && tint ? LOOK_TINT[look][c] : nullptr;
			const bool hasTn = tn && (tn[0] > 0 || tn[1] > 0);
			const double k = look == 1 ? shimmer : 1.0;
			const DWORD col = P.col[c];
			const double r = (col & 0xFF) * (hasTn ? tn[0] : 1.0) * k * dimR, gg = ((col >> 8) & 0xFF) * (hasTn ? tn[1] : 1.0) * k * dimG, b = ((col >> 16) & 0xFF) * (hasTn ? tn[2] : 1.0) * k * dimB;
			auto q = [](double v) { return static_cast<DWORD>(std::clamp(v, 0.0, 255.0)); };
			return q(r) | (q(gg) << 8) | (q(b) << 16) | 0xFF000000u;
		}
		// a 1 px line, not scaled up (the sparks)
		void Raw(double x0, double y0, double x1, double y1, int c)
		{
			if (cv) { float xy[4]; FP(x0, y0, xy[0], xy[1]); FP(x1, y1, xy[2], xy[3]); gx->Polyline(cv, xy, 2, 1.0f, C(c, 1), 0, 0); return; }
			NoBrush(); if (s2) s2->QuickPen(C(c, 1), 1.0f, 1); else Pen(c, 1, 1, false);
			skp->Line(IX(x0), IY(y0), IX(x1), IY(y1));
		}
		// text turned about a point (radians, clockwise on the screen), as the rim's arcs read it
		void TurnedText(double px, double py, const std::string& s, int c, double sz, int align, double ang, double cx, double cy)
		{
			if (cv)
			{
				const float X0 = FX(cx), Y0 = FY(cy), cs = static_cast<float>(std::cos(ang)), sn = static_cast<float>(std::sin(ang));
				const float m[6] = { cs, -sn, X0 - cs * X0 + sn * Y0, sn, cs, Y0 - sn * X0 - cs * Y0 };
				if (On()) { float a[6], mm[6]; PjAffine(cx, cy, a); Mul(a, m, mm); const bool was = pj, wasG = gl; pj = gl = false; gx->SetTransform(cv, mm); T(px, py, s, c, sz, align); pj = was; gl = wasG; }
				else { gx->SetTransform(cv, m); T(px, py, s, c, sz, align); }
				gx->SetTransform(cv, nullptr);
				return;
			}
			if (s2)
			{
				oapi::IVECTOR2 ctr{ IX(cx), IY(cy) };
				s2->SetWorldTransform2D(1.0f, static_cast<float>(ang), &ctr, nullptr);
				T(px, py, s, c, sz, align);
				s2->SetWorldTransform2D();
			}
			else T(px, py, s, c, sz, align);
		}
		int look{}; oapi::Sketchpad* s3{}; double shimmer{ 1 };   // light-built: a fine flicker of coherent light
		double ox{}, oy{}, u{ 1 }, boost{};   // boost 0..1: brighter surroundings, bolder lines
		// a shift of the whole display (pixels; 0 - it rides the view). world = true: signs projected through the camera
		double hx{}, hy{}; bool world{};
		double clipL{ -1e9 }, clipR{ 1e9 };   // the panel's inner edges in frame units: text is fitted inside
		double kx{ 1 }, ky{ 1 };              // surface pixels per screen pixel (1 on the screen; the helmet display's square texture)
		void Frame(double x0, double y0, double scale) { ox = x0; oy = y0; u = scale; }
		double X(double x) const { return ox + x * u + (world ? 0.0 : hx); }
		double Y(double y) const { return oy + y * u + (world ? 0.0 : hy); }
		int IX(double x) const { return static_cast<int>(std::lround(X(x) * kx)); }
		int IY(double y) const { return static_cast<int>(std::lround(Y(y) * ky)); }
		double dimR{ 1 }, dimG{ 1 }, dimB{ 1 };   // the darkened optics (1 = clear)
		DWORD C(int c, double a) const
		{
			const DWORD col = P.col[c];
			const DWORD r = static_cast<DWORD>((col & 0xFF) * dimR), gg = static_cast<DWORD>(((col >> 8) & 0xFF) * dimG), b = static_cast<DWORD>(((col >> 16) & 0xFF) * dimB);
			return r | (gg << 8) | (b << 16) | (static_cast<DWORD>(std::clamp(a, 0.0, 1.0) * 255) << 24);
		}

		void Pen(int c, double w, double a, bool dash)
		{
			const double px = (std::max)(1.0, w * u * 0.8 * (1 + 0.6 * boost) * 0.5 * (kx + ky));
			if (s2) { s2->QuickPen(C(c, a), static_cast<float>(px), dash ? 2 : 1); return; }
			const unsigned long long key = (static_cast<unsigned long long>(P.col[c] & 0xFFFFFF) << 16) | (static_cast<int>(px) << 1) | (dash ? 1 : 0);
			oapi::Pen*& p = o.pens[key];
			if (!p) p = oapiCreatePen(dash ? 2 : 1, static_cast<int>(px), P.col[c] & 0xFFFFFF);
			skp->SetPen(p);
		}
		void NoPen() { if (s2) s2->QuickPen(0); else skp->SetPen(nullptr); }
		bool Brush(int c, double a)
		{
			if (s2) { s2->QuickBrush(C(c, a)); return true; }
			if (a < 0.5) { skp->SetBrush(nullptr); return false; }
			oapi::Brush*& b = o.brushes[P.col[c] & 0xFFFFFF];
			if (!b) b = oapiCreateBrush(P.col[c] & 0xFFFFFF);
			skp->SetBrush(b); return true;
		}
		void NoBrush() { if (s2) s2->QuickBrush(0); else skp->SetBrush(nullptr); }

		void Line(double x1, double y1, double x2, double y2, int c = CP, double w = 1.2, double a = 1, bool dash = false, bool glow = true)
		{
			if (cv && Bends()) { std::vector<float> xy; PathXY({ { x1, y1 }, { x2, y2 } }, false, xy); NStroke(xy.data(), static_cast<int>(xy.size() / 2), c, w, a, dash, glow, false); return; }
			if (cv) { float xy[4]; FP(x1, y1, xy[0], xy[1]); FP(x2, y2, xy[2], xy[3]); NStroke(xy, 2, c, w, a, dash, glow, false); return; }
			NoBrush();
			if (look == 1)
			{
				a *= shimmer;
				if (glow && s2 && !dash) { Pen(c, w + 5, a * 0.10, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); Pen(c, w + 1.6, a * 0.35, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); }
				Pen(c, w * 0.85, a, dash); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2));
				return;
			}
			if (glow && s2 && !dash && look == 0) { Pen(c, w + 2.4, a * 0.2, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); }
			Pen(c, w, a, dash); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2));
		}
		void Rect(double x, double y, double w, double h, int sc, double sw = 1, double sa = 1, int fc = -1, double fa = 0)
		{
			if (cv && On())
			{
				float q[8];
				if (look == 1 && fc >= 0 && fc != CK && fa >= 0.99)
				{
					std::vector<float> p; QuadPath(x, y, x + w, y + h, p); gx->Polyline(cv, p.data(), static_cast<int>(p.size() / 2), PW(1.8), C(fc, shimmer), D3D11Hud::CLOSED, 0);
					const double in = 2.5 / u; QuadPath(x + in, y + in, x + w - in, y + h - in, p); gx->Polyline(cv, p.data(), static_cast<int>(p.size() / 2), PW(1.0), C(fc, 0.8 * shimmer), D3D11Hud::CLOSED, 0);
					return;
				}
				if (look == 1 && fc >= 0 && fc != CK && fa > 0)
				{
					if (fa >= 0.15) { const double step = 3.2 / u; for (double xx = x + step / 2; xx < x + w; xx += step) Line(xx, y + 0.5, xx, y + h - 0.5, fc, 0.9, (std::min)(1.0, fa * 1.2), false, false); }
				}
				else if (fc >= 0 && fa > 0) { std::vector<float> p; QuadPath(x, y, x + w, y + h, p); gx->Polygon(cv, p.data(), static_cast<int>(p.size() / 2), C(fc, fa)); }
				if (sc >= 0) { std::vector<float> p; QuadPath(x, y, x + w, y + h, p); gx->Polyline(cv, p.data(), static_cast<int>(p.size() / 2), PW(sw), C(sc, sa * (look == 1 ? shimmer : 1)), D3D11Hud::CLOSED, 0); }
				return;
			}
			if (cv)
			{
				if (look == 1 && fc >= 0 && fc != CK && fa >= 0.99)
				{
					gx->Rect(cv, FX(x), FY(y), FX(x + w), FY(y + h), 0, C(fc, shimmer), PW(1.8));
					const double in = 2.5 / u; gx->Rect(cv, FX(x + in), FY(y + in), FX(x + w - in), FY(y + h - in), 0, C(fc, 0.8 * shimmer), PW(1.0));
					return;
				}
				if (look == 1 && fc >= 0 && fc != CK && fa > 0)
				{
					if (fa >= 0.15) { const double step = 3.2 / u; for (double xx = x + step / 2; xx < x + w; xx += step) Line(xx, y + 0.5, xx, y + h - 0.5, fc, 0.9, (std::min)(1.0, fa * 1.2), false, false); }
				}
				else if (fc >= 0 && fa > 0) gx->Rect(cv, FX(x), FY(y), FX(x + w), FY(y + h), C(fc, fa), 0, 0);
				if (sc >= 0) gx->Rect(cv, FX(x), FY(y), FX(x + w), FY(y + h), 0, C(sc, sa * (look == 1 ? shimmer : 1)), PW(sw));
				return;
			}
			if (look == 1 && fc >= 0 && fc != CK && fa >= 0.99)
			{
				// light-built, a solid fill (a button that is on): a double line of light round it, the inside left clear -
				// hatching it hid the label
				NoBrush(); Pen(fc, 1.8, shimmer, false); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h));
				const double in = 2.5 / u; Pen(fc, 1.0, 0.8 * shimmer, false); skp->Rectangle(IX(x + in), IY(y + in), IX(x + w - in), IY(y + h - in));
				NoBrush(); return;
			}
			if (look == 1 && fc >= 0 && fc != CK && fa > 0)
			{
				if (fa >= 0.15)
				{
					const double step = 3.2 / u;
					for (double xx = x + step / 2; xx < x + w; xx += step) Line(xx, y + 0.5, xx, y + h - 0.5, fc, 0.9, (std::min)(1.0, fa * 1.2), false, false);
				}
			}
			else if (fc >= 0 && fa > 0 && Brush(fc, fa)) { NoPen(); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h)); }
			if (sc >= 0) { NoBrush(); Pen(sc, sw, sa * (look == 1 ? shimmer : 1), false); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h)); }
			NoBrush();
		}
		void Circle(double cx, double cy, double r, int sc, double sw = 1.2, double sa = 1, int fc = -1, double fa = 0, bool dash = false)
		{
			if (cv && On())
			{
				std::vector<float> xy; const int n = 40; xy.resize(2 * n);
				for (int i = 0; i < n; ++i) FP(cx + r * std::cos(PI2 * i / n), cy + r * std::sin(PI2 * i / n), xy[2 * i], xy[2 * i + 1]);
				if (fc >= 0 && fa > 0 && look == 1) { if (sc < 0) { sc = fc; sa = fa; } }
				else if (fc >= 0 && fa > 0) gx->Polygon(cv, xy.data(), n, C(fc, fa));
				if (sc >= 0) gx->Polyline(cv, xy.data(), n, PW(sw), C(sc, sa * (look == 1 ? shimmer : 1)), D3D11Hud::CLOSED | (dash ? D3D11Hud::DASHED : 0u), dash ? 3 * PW(sw) + 2 : 0);
				return;
			}
			if (cv)
			{
				if (fc >= 0 && fa > 0 && look == 1) { if (sc < 0) { sc = fc; sa = fa; } }
				else if (fc >= 0 && fa > 0) gx->Ellipse(cv, FX(cx), FY(cy), static_cast<float>(r * u * kx), static_cast<float>(r * u * ky), C(fc, fa), 0, 0);
				if (sc >= 0)
				{
					if (dash) gx->Arc(cv, FX(cx), FY(cy), static_cast<float>(r * u * 0.5 * (kx + ky)), 0.0f, 6.2831853f, PW(sw), C(sc, sa), D3D11Hud::DASHED, 3 * PW(sw) + 2);
					else gx->Ellipse(cv, FX(cx), FY(cy), static_cast<float>(r * u * kx), static_cast<float>(r * u * ky), 0, C(sc, sa * (look == 1 ? shimmer : 1)), PW(sw));
				}
				return;
			}
			if (fc >= 0 && fa > 0 && look == 1) { if (sc < 0) { sc = fc; sa = fa; } }   // light-built: the outline, not a disc
			else if (fc >= 0 && fa > 0 && Brush(fc, fa)) { NoPen(); skp->Ellipse(IX(cx - r), IY(cy - r), IX(cx + r), IY(cy + r)); }
			if (sc >= 0) { NoBrush(); Pen(sc, sw, sa, dash); skp->Ellipse(IX(cx - r), IY(cy - r), IX(cx + r), IY(cy + r)); }
			NoBrush();
		}
		void Poly(const std::vector<V2>& pts, int c, double w = 1.2, double a = 1, bool closed = false, bool dash = false)
		{
			if (pts.size() < 2) return;
			if (cv)
			{
				std::vector<float> xy; PathXY(pts, closed, xy);
				NStroke(xy.data(), static_cast<int>(xy.size() / 2), c, w, a, dash, false, closed);
				return;
			}
			std::vector<oapi::IVECTOR2> v;
			for (const V2& p : pts) { oapi::IVECTOR2 q; q.x = IX(p.first); q.y = IY(p.second); v.push_back(q); }
			if (closed) v.push_back(v.front());
			NoBrush(); Pen(c, w, a, dash);
			skp->Polyline(v.data(), static_cast<int>(v.size()));
		}
		void Fill(const std::vector<V2>& pts, int c, double a)
		{
			if (look == 1 && pts.size() >= 3) { Poly(pts, c, 1.2, a, true); return; }   // light-built: drawn, not filled
			if (cv)
			{
				if (pts.size() < 3 || a <= 0) return;
				std::vector<float> xy; PathXY(pts, true, xy);
				gx->Polygon(cv, xy.data(), static_cast<int>(xy.size() / 2), C(c, a));
				return;
			}
			if (pts.size() < 3 || !Brush(c, a)) return;
			std::vector<oapi::IVECTOR2> v;
			for (const V2& p : pts) { oapi::IVECTOR2 q; q.x = IX(p.first); q.y = IY(p.second); v.push_back(q); }
			NoPen(); skp->Polygon(v.data(), static_cast<int>(v.size())); NoBrush();
		}
		static int SizeIdx(double sz) { return sz <= 10 ? 0 : sz <= 11.9 ? 1 : sz <= 13.9 ? 2 : 3; }
		void T(double x, double y, const std::string& s0, int c = CP, double sz = 12, int align = 0)
		{
			if (s0.empty()) return;
			std::string s = s0;
			if (clipR < 1e8)
			{
				const double avail = align == 0 ? clipR - x : align == 2 ? x - clipL : 2 * (std::min)(x - clipL, clipR - x);
				double w = TW(s, sz);
				if (w > avail && avail > 4)
				{
					if (w * 0.75 <= avail) sz *= avail / w;
					else
					{
						sz *= 0.75;
						while (s.size() > 1 && TW(s + "…", sz) > avail) { s.pop_back(); while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back(); if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0xC0) s.pop_back(); }
						s += "…";
					}
				}
			}
			const int si = SizeIdx(sz * (std::max)(1.0, ky));   // at 2x the bigger glyphs
			if (look == 1 && c == CK) c = CA;
			if (boost > 1.05 && c == CD) c = CP;   // the maximum brightness: the dim labels are lit as the rest
			if (cv && o.sdf[0])
			{
				// signed-distance glyphs: the em is the size in display units, the glow (the atlas's baked one) from the distance
				const float em = static_cast<float>(sz * u * 0.5 * (kx + ky));
				const float gw = look == 0 ? em * 0.16f : look == 1 ? em * 0.12f : em * 0.07f, gs = look == 0 ? 0.55f : look == 1 ? 0.4f : 0.25f;
				gx->SetGlow(cv, gw, gs);
				if (On()) { float m[6]; PjAffine(x, y, m); gx->SetTransform(cv, m); }
				gx->Text(cv, o.sdf[si == 3 && o.sdf[1] ? 1 : 0], FX(x), FY(y), em, s.c_str(), TextCol(c), (align == 1 ? D3D11Hud::CENTER : align == 2 ? D3D11Hud::RIGHT : D3D11Hud::LEFT) | D3D11Hud::BASELINE);
				if (On()) gx->SetTransform(cv, nullptr);
				gx->SetGlow(cv, 0, 0);
				return;
			}
			const float* tn = look && tint ? LOOK_TINT[look][c] : nullptr;
			const bool hasTn = tn && (tn[0] > 0 || tn[1] > 0), dimmed = dimR < 0.999 || dimG < 0.999 || dimB < 0.999;
			const bool tint = s3 && (hasTn || dimmed);
			if (tint)
			{
				const float k = look == 1 ? static_cast<float>(shimmer) : 1.0f;
				const float r0 = hasTn ? tn[0] : 1.0f, g0 = hasTn ? tn[1] : 1.0f, b0 = hasTn ? tn[2] : 1.0f;
				const oapi::FVECTOR4 b(r0 * k * static_cast<float>(dimR), g0 * k * static_cast<float>(dimG), b0 * k * static_cast<float>(dimB), 1.0f);
				s3->SetBrightness(&b);
			}
			const bool ok = txt.Draw(skp, X(x) * kx, Y(y) * ky, s, si, P.atlas[c], align, u * sz / SIZE_BASE[si], kx, ky);
			if (tint) s3->SetBrightness(nullptr);
			if (ok) return;
			const int ph = (std::max)(8, static_cast<int>(sz * u * 1.3));
			oapi::Font*& f = o.fonts[ph];
			if (!f) f = oapiCreateFont(ph, true, const_cast<char*>("Arial"));
			skp->SetFont(f); skp->SetTextColor(P.col[c] & 0xFFFFFF);
			skp->SetTextAlign(align == 0 ? oapi::Sketchpad::LEFT : align == 1 ? oapi::Sketchpad::CENTER : oapi::Sketchpad::RIGHT);
			const std::string a = Ru(s); skp->Text(IX(x), IY(y - sz * 0.95), a.c_str(), static_cast<int>(a.size()));
		}
		double TW(const std::string& s, double sz) const
		{
			const int si = SizeIdx(sz);
			if (cv && o.sdf[0]) return gx->TextWidth(o.sdf[si == 3 && o.sdf[1] ? 1 : 0], static_cast<float>(sz), s.c_str());
			if (txt.Ok()) return txt.Width(s, si, sz / SIZE_BASE[si]);
			return Decode(s).size() * sz * 0.55;
		}
		// a label above a value
		void Cell(double x, double y, const std::string& lab, const std::string& val, int c = CW, int align = 0)
		{
			T(x, y, lab, CD, 9.5, align); T(x, y + 17, val, c, 15, align);
		}
		void Bar(double x, double y, double w, double h, double f, int c)
		{
			Rect(x, y, w, h, CD, 1, 0.6);
			f = std::clamp(f, 0.0, 1.0);
			if (f > 0.002) Rect(x + 1, y + 1, (w - 2) * f, h - 2, -1, 0, 0, c, 0.9);
		}
		void Hit(double x, double y, double w, double h, int kind, int arg = 0)
		{
			if (gl && glLin > 0 && hv)   // an equal-angle part: each corner through the helmet into the view
			{
				double xs[4], ys[4];
				for (int i = 0; i < 4; ++i)
				{
					double yw, pt, d[9]; LinAng(X(i & 1 ? x + w : x), Y(i & 2 ? y + h : y), yw, pt);
					if (!o.HelmDir(hv, yw, pt, d) || d[2] < 0.2) return;
					xs[i] = hitW / 2 + hitF * d[0] / d[2]; ys[i] = hitH / 2 + hitF * d[1] / d[2];
				}
				o.hits.push_back({ (std::min)({ xs[0], xs[1], xs[2], xs[3] }), (std::min)({ ys[0], ys[1], ys[2], ys[3] }), (std::max)({ xs[0], xs[1], xs[2], xs[3] }), (std::max)({ ys[0], ys[1], ys[2], ys[3] }), kind, arg });
				return;
			}
			if (noHit) return;
			if (!pj) { o.hits.push_back({ X(x), Y(y), X(x + w), Y(y + h), kind, arg }); return; }
			double xs[4], ys[4]; Pj(X(x), Y(y), xs[0], ys[0]); Pj(X(x + w), Y(y), xs[1], ys[1]); Pj(X(x + w), Y(y + h), xs[2], ys[2]); Pj(X(x), Y(y + h), xs[3], ys[3]);
			o.hits.push_back({ (std::min)({ xs[0], xs[1], xs[2], xs[3] }), (std::min)({ ys[0], ys[1], ys[2], ys[3] }), (std::max)({ xs[0], xs[1], xs[2], xs[3] }), (std::max)({ ys[0], ys[1], ys[2], ys[3] }), kind, arg });
		}
		void Arrow(double x0, double y0, double x1, double y1, int c, double w = 1.6, double a = 1, bool dash = false)
		{
			const double dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
			if (l < 2) return;
			Line(x0, y0, x1, y1, c, w, a, dash);
			const double ux = dx / l, uy = dy / l;
			Tri(x1, y1, -8 * ux - 4 * uy, -8 * uy + 4 * ux, -8 * ux + 4 * uy, -8 * uy - 4 * ux, c, a);
		}
		void Tri(double x, double y, double dx1, double dy1, double dx2, double dy2, int c, double a = 1)
		{
			Fill({ { x, y }, { x + dx1, y + dy1 }, { x + dx2, y + dy2 } }, c, a);
		}
	};

	// everything a page needs
	struct Ctx
	{
		Gfx& g; const HudData& d; SuitHud& h; VESSEL* v; double W, H, k; bool blink;
	};
	// a «Буран» plate: on the helmet's glass when the head turns inside the helmet (fixed there, the look slides over
	// it), else turned to the eye on the view. False: on the glass but out of sight now - skip it
	// a panel on the helmet's glass (the glass pass): centred at yaw/pitch (degrees), arc metres of glass a radian of the
	// layout (mode 4, a decal); mode 5 an MFD shaped by the window, arc its side (-1 left, 1 right)
	bool SetDecal(Ctx& c, double x0, double y0, double w, double h, double yawDeg, double pitchDeg, double arc, int lin = 4, double fUnits = 0)
	{
		if (!c.g.cv) return true;
		const double F = (c.H / 2) / std::tan((std::max)(0.1, oapiCameraAperture())), Fr = (c.H / 2) / std::tan(GLASS_FREF_HALF);
		double q[9];
		const double yaw = yawDeg * RAD, pitch = pitchDeg * RAD;
		const bool seen = c.h.HelmDir(c.v, yaw, pitch, q) && c.g.PlateAt(x0, y0, w, h, c.W, c.H, F, q, Fr);
		c.g.pjF = F;
		c.g.Glass(x0, y0, w, h, Fr, yaw, pitch, GLASS_CW, GLASS_CH, GLASS_YW, GLASS_PT, GLASS_PB);
		c.g.glLin = lin; c.g.glS = arc; c.g.glW = w; c.g.glH = h;
		if (lin == 6)   // a flat panel: arc its roll (degrees), its distance from its width (SMFD_F)
		{
			const double r = arc * RAD, cr = std::cos(r), sr = std::sin(r);
			for (int i = 0; i < 3; ++i) { const double R0 = c.g.glR[i], D0 = c.g.glD[i]; c.g.glR[i] = R0 * cr + D0 * sr; c.g.glD[i] = -R0 * sr + D0 * cr; }
			c.g.glF = fUnits > 0 ? fUnits * c.k : SMFD_F * w;   // (the side MFDs: from their width)
		}
		c.g.hv = c.v; c.g.hitW = c.W; c.g.hitH = c.H; c.g.hitF = F;   // its clicks through the helmet into the view
		c.g.noHit = !seen;
		return true;
	}
	bool SetPlate(Ctx& c, double x0, double y0, double w, double h, int kind = 0)
	{
		if (!c.g.cv) return true;
		const double F = (c.H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
		double q[9];
		if (c.h.GlassOn() && !c.h.glassPass) return false;   // the glass layer carries the panels: none on the view
		(void)q;
		if (c.h.glassPass)
		{
			// on the glass, at the measured places: 1 an MFD, 2 a low plate, 3 a folded MFD's tab (where the MFD was)
			const double Fr = (c.H / 2) / std::tan(GLASS_FREF_HALF);
			const double side = x0 + w / 2 < c.W / 2 ? -1 : 1;
			if (kind == 1 && c.h.mfdSide)
			{
				// the frame: the panel's inner edge, top and bottom (straight), its outer side along the glass from corner to corner
				SetDecal(c, x0, y0, w, h, side * SMFD_YAW, SMFD_PITCH, side * SMFD_ROLL, 6);
				double cy[4], cp[4];   // the corners: inner top, outer top, outer bottom, inner bottom
				const double ix = side < 0 ? x0 + w : x0, ox_ = side < 0 ? x0 : x0 + w;
				c.g.LinAng(ix, y0, cy[0], cp[0]); c.g.LinAng(ox_, y0, cy[1], cp[1]); c.g.LinAng(ox_, y0 + h, cy[2], cp[2]); c.g.LinAng(ix, y0 + h, cy[3], cp[3]);
				for (int i = 0; i < 4; ++i) { cy[i] *= DEG; cp[i] *= DEG; }
				const double rT = std::abs(cy[1]) - (WinSide(cp[1]) - SMFD_OUT), rB = std::abs(cy[2]) - (WinSide(cp[2]) - SMFD_OUT);
				std::vector<V2> pts;   // the outer side: along the glass, through both outer corners (degrees x 100)
				for (int i = 0; i <= 40; ++i)
				{
					const double t = i / 40.0, p = cp[1] + (cp[2] - cp[1]) * t, chord = std::abs(cy[1]) + (std::abs(cy[2]) - std::abs(cy[1])) * t;
					const double ya = chord + SMFD_BULGE * (WinSide(p) - SMFD_OUT + rT + (rB - rT) * t - chord);
					pts.push_back({ side * ya * 100, p * 100 });
				}
				c.g.glLin = 7; c.g.Frame(0, 0, 1);
				c.g.Poly(pts, CP, 1.2 * c.k, 0.55);
				SetDecal(c, x0, y0, w, h, side * SMFD_YAW, SMFD_PITCH, side * SMFD_ROLL, 6);   // the straight edges: on the panel
				c.g.Frame(0, 0, 1);
				c.g.Poly({ { ox_, y0 }, { ix, y0 }, { ix, y0 + h }, { ox_, y0 + h } }, CP, 1.2 * c.k, 0.55);
				return true;
			}
			double yaw = std::atan((x0 + w / 2 - c.W / 2) / Fr) * DEG, pitch = std::atan(-(y0 + h / 2 - c.H / 2) / Fr) * DEG;
			if (kind == 1 || kind == 3) { yaw = side * (c.h.mfdSide ? MFD_SIDE_YAW : MFD_YAW); pitch = c.h.mfdSide ? MFD_SIDE_PITCH : MFD_PITCH; }
			else if (kind == 2) { yaw = side * LOW_YAW; pitch = LOW_PITCH; }
			return SetDecal(c, x0, y0, w, h, yaw, pitch, kind == 2 ? LOW_ARC : c.h.mfdSide ? MFD_SIDE_ARC : MFD_ARC);
		}
		if (c.h.HelmDir(c.v, std::atan((x0 + w / 2 - c.W / 2) / F), std::atan(-(y0 + h / 2 - c.H / 2) / F), q)) return c.g.PlateAt(x0, y0, w, h, c.W, c.H, F, q);
		c.g.Plate(x0, y0, w, h, c.W, c.H);
		return true;
	}

	// ================= state =================
	SuitHud::~SuitHud()
	{
		for (auto& p : pens) if (p.second) oapiReleasePen(p.second);
		for (auto& b : brushes) if (b.second) oapiReleaseBrush(b.second);
		for (auto& f : fonts) if (f.second) oapiReleaseFont(f.second);
		if (nvCam) Core()->DeleteCustomCamera(nvCam);   // cameras first, then their surface
		if (nvSrf) oapiDestroySurface(nvSrf);
		if (modSrf) oapiDestroySurface(modSrf);
		if (ipi && Core()) Core()->ReleaseIPInterface(static_cast<gcIPInterface*>(ipi));
		if (lightSrc) oapiDestroySurface(lightSrc);
		if (lightOut) oapiDestroySurface(lightOut);
		if (gx) { for (auto* c : { lightCv, modCv, sunCv, glassCv }) if (c) gx->CanvasRelease(c); for (auto* f : sdf) if (f) gx->FontRelease(f); }
	}

	void SuitHud::Detach()
	{
		// the body leaves the world (the session ends, or the person goes aboard): everything bound to it and to the graphics
		// goes back now, while the client still runs - the person lives on, and a next session builds it all again
		if (nvCam && Core()) Core()->DeleteCustomCamera(nvCam);
		nvCam = nullptr;
		if (nvSrf) { oapiDestroySurface(nvSrf); nvSrf = nullptr; }
		if (modSrf) { oapiDestroySurface(modSrf); modSrf = nullptr; }
		if (sunSrf) { oapiDestroySurface(sunSrf); sunSrf = nullptr; }
		sunDm = nullptr; sunAmp = -1; modKeyed = false; glassDm = nullptr; glassSet = -1;
		if (ipi && Core()) Core()->ReleaseIPInterface(static_cast<gcIPInterface*>(ipi));
		ipi = nullptr; ipiTried = false;
		if (lightSrc) { oapiDestroySurface(lightSrc); lightSrc = nullptr; }
		if (lightOut) { oapiDestroySurface(lightOut); lightOut = nullptr; }
		if (gx) { for (auto* c : { lightCv, modCv, sunCv, glassCv }) if (c) gx->CanvasRelease(c); for (auto* f : sdf) if (f) gx->FontRelease(f); }
		glassCv = nullptr; glassMeshDm = nullptr; glassLive = false; glassLogged = false;
		lightCv = modCv = sunCv = drawCv = nullptr; sdf[0] = sdf[1] = nullptr; gx = nullptr; gxTried = false;   // the client may change next session
		lightW = lightH = modW = modH = nvW = nvH = 0; lightLive = false; lightLogged = modLogged = false;
		glyphs.Release(); glyphsTried = false;
		for (auto& p : pens) if (p.second) oapiReleasePen(p.second);
		for (auto& b : brushes) if (b.second) oapiReleaseBrush(b.second);
		for (auto& f : fonts) if (f.second) oapiReleaseFont(f.second);
		pens.clear(); brushes.clear(); fonts.clear();
		// the plate was a mesh of the body that leaves: the next body adds its own (HelmetMesh). The index is kept: a new body
		// may have been made before the old one went (standing up from a seat) and already holds its plate under it
		plateOwner = nullptr; plateVis = nullptr; modDm = nullptr; lightDm = nullptr; onVisor = false;
	}

	void SuitHud::NightVision(oapi::Sketchpad* skp, VESSEL* v, double W, double H, double SW, double SH)
	{
		if (!gcTried) { gcTried = true; gcOk = Core() != nullptr; }
		if (!gcOk || !Ext(skp)) { nvNote = TR("ПНВ недоступен: нужен клиент D3D9"); return; }
		const int w = (std::max)(64, static_cast<int>(W) / 2), h = (std::max)(64, static_cast<int>(H) / 2);   // half resolution: grain hides it
		if (!nvSrf || w != nvW || h != nvH)
		{
			if (nvCam) { Core()->DeleteCustomCamera(nvCam); nvCam = nullptr; }
			if (nvSrf) oapiDestroySurface(nvSrf);
			nvSrf = oapiCreateSurfaceEx(w, h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
			nvW = w; nvH = h;
			if (nvSrf) oapiClearSurface(nvSrf);
		}
		if (!nvSrf) { nvNote = TR("ПНВ недоступен: нет поверхности"); return; }
		// the same eye as the main camera, in her own frame
		VECTOR3 cg, cd; oapiCameraGlobalPos(&cg); oapiCameraGlobalDir(&cd);
		MATRIX3 cr; oapiCameraRotationMatrix(&cr);
		MATRIX3 R; v->GetRotationMatrix(R);
		VECTOR3 pos, dir = tmul(R, cd), up = tmul(R, _V(cr.m12, cr.m22, cr.m32));
		v->Global2Local(cg, pos);
		up = up - dir * dotp(up, dir); { const double l = length(up); if (l > 1e-6) up = up / l; }
		nvCam = Core()->SetupCustomCamera(nvCam, v->GetHandle(), pos, dir, up, oapiCameraAperture(), nvSrf, 0xFF);
		if (!nvCam) { nvNote = TR("ПНВ недоступен: включите CustomCamMode в D3D9Client.cfg"); return; }
		// the second camera renders the whole scene again (~half the frame rate on a heavy scene - Titan with the AtmoFX
		// passes went to ~15 fps, 2026-10-19): it renders one frame in three; the grainy picture hides the lag
		nvTick = (nvTick + 1) % 3;
		Core()->CustomCameraOnOff(nvCam, nvTick == 0);
		// the gain suits the night side; in daylight the tube is washed out and throttles itself
		const double gain = sunElev > 0.05 && surface ? 1.2 : 4.0;
		nvNote = gain > 2 ? TR("ИК") : TR("ИК · засветка");
		oapi::Sketchpad* s3 = skp;
		// short-wave infrared, as cameras see it today: a neutral grey picture, mild gamma, fine grain - not the green tube of the last century
		const oapi::FVECTOR4 bright(static_cast<float>(gain), static_cast<float>(gain), static_cast<float>(gain * 1.04), 1.0f);
		const oapi::FVECTOR4 gamma(0.55f, 0.55f, 0.55f, 1.0f), noise(0.10f, 0.10f, 0.10f, 0.0f);
		s3->SetBrightness(&bright); s3->SetRenderParam(oapi::Sketchpad::PRM_GAMMA, &gamma); s3->SetRenderParam(oapi::Sketchpad::PRM_NOISE, &noise);
		RECT src = { 0, 0, nvW, nvH }, dst = { 0, 0, static_cast<LONG>(SW), static_cast<LONG>(SH) };
		s3->StretchRect(nvSrf, &src, &dst);
		s3->SetBrightness(nullptr); s3->SetRenderParam(oapi::Sketchpad::PRM_GAMMA, nullptr); s3->SetRenderParam(oapi::Sketchpad::PRM_NOISE, nullptr);
	}

	// the D3D11 client's table and the display's fonts, looked up once (null: another client - the Sketchpad path)
	const D3D11Hud::Api* SuitHud::Native()
	{
		if (gxTried) return gx;
		gxTried = true;
		gx = D3D11Hud::Get();
		if (!gx) return nullptr;
		sdf[0] = gx->FontLoad("Tantra\\HudSDF600.dds", "Config\\Tantra\\HudSDF600.txt");
		sdf[1] = gx->FontLoad("Tantra\\HudSDF700.dds", "Config\\Tantra\\HudSDF700.txt");
		char b[160]; snprintf(b, sizeof b, "OrbiterCrew: helmet display - the D3D11 client's own drawing (API %d), fonts %s", gx->version, sdf[0] ? "loaded" : "MISSING (glyph text off)");
		oapiWriteLog(b);
		return gx;
	}

	// the night vision on the D3D11 canvas: the same camera (gcCore), its picture drawn with the gain, gamma and grain
	void SuitHud::NightVisionNative(VESSEL* v, double W, double H, double SW, double SH)
	{
		if (!gcTried) { gcTried = true; gcOk = Core() != nullptr; }
		if (!gcOk || !gx || !drawCv) { nvNote = TR("ПНВ недоступен: нет камер клиента"); return; }
		const int w = (std::max)(64, static_cast<int>(W) / 2), h = (std::max)(64, static_cast<int>(H) / 2);
		if (!nvSrf || w != nvW || h != nvH)
		{
			if (nvCam) { Core()->DeleteCustomCamera(nvCam); nvCam = nullptr; }
			if (nvSrf) oapiDestroySurface(nvSrf);
			nvSrf = oapiCreateSurfaceEx(w, h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
			nvW = w; nvH = h;
		}
		if (!nvSrf) { nvNote = TR("ПНВ недоступен: нет поверхности"); return; }
		VECTOR3 cg, cd; oapiCameraGlobalPos(&cg); oapiCameraGlobalDir(&cd);
		MATRIX3 cr; oapiCameraRotationMatrix(&cr);
		MATRIX3 R; v->GetRotationMatrix(R);
		VECTOR3 pos, dir = tmul(R, cd), up = tmul(R, _V(cr.m12, cr.m22, cr.m32));
		v->Global2Local(cg, pos);
		up = up - dir * dotp(up, dir); { const double l = length(up); if (l > 1e-6) up = up / l; }
		nvCam = Core()->SetupCustomCamera(nvCam, v->GetHandle(), pos, dir, up, oapiCameraAperture(), nvSrf, 0xFF);
		if (!nvCam) { nvNote = TR("ПНВ недоступен: нет камер клиента"); return; }
		nvTick = (nvTick + 1) % 3;
		Core()->CustomCameraOnOff(nvCam, nvTick == 0);
		const double gain = sunElev > 0.05 && surface ? 1.2 : 4.0;
		nvNote = gain > 2 ? TR("ИК") : TR("ИК · засветка");
		const D3D11Hud::ImageFx fx = { { static_cast<float>(gain), static_cast<float>(gain), static_cast<float>(gain * 1.04) }, 0.55f, 0.10f };
		const float dst[4] = { 0, 0, static_cast<float>(SW), static_cast<float>(SH) };
		gx->Image(drawCv, nvSrf, nullptr, dst, 0xFFFFFFFF, &fx);
	}

	std::string SuitHud::Save() const
	{
		char b[96];
		snprintf(b, sizeof b, "%d %d %d %d %d %d %d %d %d %.2f %d %d 3 0", pal, mode, autoMode ? 1 : 0, lpage, rpage, openL ? 1 : 0, openR ? 1 : 0, zoom, brightAuto ? 1 : 0, brightManual, look, mapMono ? 1 : 0);
		return b;
	}

	void SuitHud::Load(const std::string& line)
	{
		std::istringstream ss(line);
		int a[8] = { pal, mode, autoMode, lpage, rpage, openL, openR, zoom };
		for (int& x : a) ss >> x;
		pal = std::clamp(a[0], 0, 2); mode = std::clamp(a[1], 0, 3); autoMode = a[2] != 0; lpage = std::clamp(a[3], 0, L_COUNT - 1);
		rpage = std::clamp(a[4], 0, static_cast<int>(R_LINKS)); openL = a[5] != 0; openR = a[6] != 0; zoom = std::clamp(a[7], 0, 9);
		int ba = 1; double bm = brightManual;
		if (ss >> ba >> bm) { brightAuto = ba != 0; brightManual = std::clamp(bm, 0.0, 1.5); }
		int lk = 0; if (ss >> lk) look = std::clamp(lk, 0, 2);
		int mm = 0; if (ss >> mm) mapMono = mm != 0;
		int ver = 0; ss >> ver;
		if (ver < 2) look = 1;   // lines saved before photonics became the default: photonics

	}

	void SuitHud::SetMode(int m, bool manual)
	{
		mode = m;
		if (manual) autoMode = false;
		static const int LP[4] = { L_LOCAL, L_LOCAL, L_ORBIT, L_POWER };
		static const int RPs[4] = { R_TARGETS, R_TRANSFER, R_APPROACH, R_BODY };
		lpage = LP[m]; rpage = RPs[m];
		if (m == RDV && surface) rpage = R_TARGETS;
		if (m == FLIGHT && space) rpage = R_APPROACH;
	}

	std::vector<SuitHud::RPage> SuitHud::RPages() const
	{
		std::vector<RPage> v;
		if (surface && onMpu) v = { R_MACHINE, R_TARGETS, R_FLIGHT, R_TRANSFER, R_LANDING, R_BODY };
		else if (surface) v = { R_TARGETS, R_FLIGHT, R_TRANSFER, R_LANDING, R_BODY };
		else v = { R_TARGETS, R_APPROACH, R_DOCK, R_BODY };
		if (frame) v.push_back(R_LINKS);
		return v;
	}

	const SuitHud::Target* SuitHud::Selected() const
	{
		for (const Target& t : targets) if (t.h == sel) return &t;
		return nullptr;
	}

	void SuitHud::Click(double x, double y)
	{
		++clickSeq;   // the display redraws at once, not at its next turn
		struct SaveAfter { const SuitHud& h; const std::string before; ~SaveAfter() { if (h.prefsLoaded) { std::string now; char b[96]; snprintf(b, sizeof b, "%d %d %d %.2f %d %d %d %d", h.look, h.pal, h.brightAuto ? 1 : 0, h.brightManual, h.mapMono ? 1 : 0, h.hudScale, h.horizonMode, h.synth ? 1 : 0); now = b; if (now != before) h.SavePrefs(); } } };
		char b0[96]; snprintf(b0, sizeof b0, "%d %d %d %.2f %d %d %d %d", look, pal, brightAuto ? 1 : 0, brightManual, mapMono ? 1 : 0, hudScale, horizonMode, synth ? 1 : 0);
		SaveAfter saveAfter{ *this, b0 };
		for (auto it = hits.rbegin(); it != hits.rend(); ++it)
		{
			if (x < it->x0 || x > it->x1 || y < it->y0 || y > it->y1) continue;
			switch (it->kind)
			{
			case H_LTAB: lpage = it->arg; openL = true; break;
			case H_RTAB: rpage = it->arg; openR = true; break;
			case H_LFOLD: openL = !openL; break;
			case H_RFOLD: openR = !openR; break;
			case H_TGT: if (it->arg >= 0 && it->arg < static_cast<int>(targets.size())) { sel = targets[it->arg].h == sel ? nullptr : targets[it->arg].h; rrHist.clear(); prof.t = -1; } break;   // again: let go
			case H_AP: request = it->arg; break;
			case H_PAL: pal = std::clamp(it->arg, 0, 2); break;
			case H_ZOOM: zoom = std::clamp(zoom + it->arg, 0, 9); grid.t = -1; break;
			case H_MODE: if (!autoMode && it->arg == mode) autoMode = true; else SetMode(it->arg, true); break;
			case H_MPU: if (const MpuCmdFn f = MpuCmd()) if (mpuPerson) f(mpuPerson, it->arg); break;
			case H_LANG: g_en = it->arg == 1; SaveLanguage(g_en); SetUiLanguage(g_en); break;   // the file, and the crew's state at once
			case H_RES: hudScale = std::clamp(it->arg, 1, 2); break;
			case H_HORIZON: horizonMode = std::clamp(it->arg, 0, 2); break;
			case H_SYNTH: synth = it->arg != 0; break;
			case H_LINK: if (frame) frame->link = !frame->link; break;
			case H_PZTAB: pzTab = std::clamp(it->arg, 0, 4); break;
			case H_SUIT: if (life) { if (it->arg == 0) life->drivesOn = !life->drivesOn; else life->econ = !life->econ; } break;
			case H_NVG: nvg = !nvg; if (!nvg && nvCam) Core()->CustomCameraOnOff(nvCam, false); break;
			case H_LOG: logOpen = !logOpen; if (!logOpen) logShowT = -1; break;
			case H_LOGPG: logPage = (std::max)(0, logPage + it->arg); logOpen = true; break;
			case H_SUNF: sunHAlpha = !sunHAlpha; SavePrefs(); break;
			case H_THEME: hudTheme = std::clamp(it->arg, 0, 3); SavePrefs(); break;
			case H_MFDSIDE: mfdSide = !mfdSide; SavePrefs(); break;
			case H_LADDER: hideLadder = !hideLadder; SavePrefs(); break;
			case H_HUDMIN: hudMin = !hudMin; SavePrefs(); break;
			case H_SHADEK: shadeK = std::clamp(shadeK + 0.1 * it->arg, 0.0, 2.0); modKeyed = false; SavePrefs(); break;
			case H_ACK:
				if (it->arg < 0) { for (auto& a : alerts) if (a.active) a.ack = true; }
				else if (it->arg < static_cast<int>(alerts.size())) alerts[it->arg].ack = true;
				break;
			case H_LOOK: if (it->arg == 10) { mapMono = !mapMono; grid.t = -1; } else look = std::clamp(it->arg, 0, 2); break;
			case H_BRIGHT:
				if (it->arg == 0) brightAuto = !brightAuto;
				else { if (brightAuto) brightManual = ambient; brightAuto = false; brightManual = std::clamp(brightManual + 0.1 * it->arg, 0.0, 1.5); }   // above 1: the maximum (labels at full light)
				break;
			case H_NEXT:
				if (targets.empty()) break;
				{ size_t k = 0; for (; k < targets.size(); ++k) if (targets[k].h == sel) break; sel = k >= targets.size() ? targets[0].h : k + 1 < targets.size() ? targets[k + 1].h : nullptr; }
				rrHist.clear(); prof.t = -1; break;
			}
			return;
		}
	}

	// ================= caution & warning =================
	int SuitHud::Trend(int i, double thr) const
	{
		const auto& q = hist.v[i];
		if (q.size() < 7) return 0;
		const double dv = q.back() - q[q.size() - 7];   // over the last 30 s
		return dv > thr ? 1 : dv < -thr ? -1 : 0;
	}

	void SuitHud::Alerts(const HudData& d)
	{
		const double t = oapiGetSimTime();
		// radiation where she is: under an atmosphere almost nothing; low Earth orbit inside the field; the Moon and open space more
		char bn[64] = ""; if (hBody) oapiGetObjectName(hBody, bn, 64);
		const std::string b = bn;
		if (d.radValid) { doseRate = d.radRate * 1e6; dose = d.radDose * 1e6; }   // the radiation model (Sv -> uSv)
		else
		{
			// until the model is in: a rough rate by place
			if (!d.vacuum && d.airKPa > 20) doseRate = 0.15;
			else if (b == "Earth") doseRate = alt < 2.0e6 ? 20 : 80;
			else if (b == "Mars") doseRate = 25;
			else if (b == "Moon") doseRate = 55;
			else doseRate = 75;
			if (lastDoseT >= 0 && t > lastDoseT && t - lastDoseT < 60 && d.suit) dose += doseRate * (t - lastDoseT) / 3600.0;
		}
		lastDoseT = t;
		if (hist.t < 0 || t - hist.t >= 5 || t < hist.t)
		{
			hist.t = t;
			const double vals[5] = { d.o2, d.batt, d.sorbent, d.ppCO2, d.tInC };
			for (int i = 0; i < 5; ++i) { hist.v[i].push_back(vals[i]); if (hist.v[i].size() > 13) hist.v[i].erase(hist.v[i].begin()); }
		}

		struct C { const char* key; const char* tile; int level; std::string text; };
		std::vector<C> now;
		if (d.suit)
		{
			if (d.o2Vent) {}   // breathing the air around: the tank is a reserve
			else if (d.o2 < 0.10) now.push_back({ "o2", "O2", 2, TR("КИСЛОРОД ") + Num(100 * d.o2, 0) + " % · " + Hours(d.o2Hours) });
			else if (d.o2 < 0.25) now.push_back({ "o2", "O2", 1, TR("КИСЛОРОД ") + Num(100 * d.o2, 0) + " % · " + Hours(d.o2Hours) });
			const double pWarn = d.o2Vent ? 14 : 15, pCaut = d.o2Vent ? 17 : 25;   // in breathable air the helmet holds the air around (~21 kPa O2)
			if (d.ppO2 < pWarn) now.push_back({ "p", TR("ДАВЛ"), 2, TR("ДАВЛЕНИЕ O2 ") + Num(d.ppO2, 1) + TR(" кПа") });
			else if (d.ppO2 < pCaut) now.push_back({ "p", TR("ДАВЛ"), 1, TR("ДАВЛЕНИЕ O2 ") + Num(d.ppO2, 1) + TR(" кПа") });
			if (d.ppCO2 > 5) now.push_back({ "co2", "CO2", 2, "CO2 " + Num(d.ppCO2, 2) + TR(" кПа") });
			else if (d.ppCO2 > 1) now.push_back({ "co2", "CO2", 1, "CO2 " + Num(d.ppCO2, 2) + TR(" кПа") });
			if (d.sorbent < 0.10) now.push_back({ "sorb", "CO2", 2, TR("ПОГЛОТИТЕЛЬ ") + Num(100 * d.sorbent, 0) + " %" });
			else if (d.sorbent < 0.25) now.push_back({ "sorb", "CO2", 1, TR("ПОГЛОТИТЕЛЬ ") + Num(100 * d.sorbent, 0) + " %" });
			if (!d.powered || d.batt < 0.10) now.push_back({ "batt", TR("БАТ"), 2, TR("БАТАРЕЯ ") + Num(100 * d.batt, 0) + " % · " + Hours(d.battHours) });
			else if (d.batt < 0.25) now.push_back({ "batt", TR("БАТ"), 1, TR("БАТАРЕЯ ") + Num(100 * d.batt, 0) + " % · " + Hours(d.battHours) });
			if (!d.inSpec) now.push_back({ "env", TR("ТЕПЛО"), 2, TR("СРЕДА ВНЕ ДОПУСКА ") + Num(d.envC, 0, true) + " °C" });
			// the outside pressure against the shell's rating: amber from 75 %, red above; crushed - the shell is open
			if (d.suitBreached) now.push_back({ "shell", TR("ДАВЛ"), 2, TR("ОБОЛОЧКА РАЗРУШЕНА · СРЕДА ВНУТРИ") });
			else if (!d.vacuum && d.airKPa > d.suitPMax) now.push_back({ "pout", TR("ДАВЛ"), 2, TR("ДАВЛЕНИЕ СНАРУЖИ ") + Num(d.airKPa, 0) + TR(" кПа · ПРЕДЕЛ ") + Num(d.suitPMax, 0) });
			else if (!d.vacuum && d.airKPa > 0.75 * d.suitPMax) now.push_back({ "pout", TR("ДАВЛ"), 1, TR("ДАВЛЕНИЕ СНАРУЖИ ") + Num(d.airKPa, 0) + TR(" кПа · у предела ") + Num(d.suitPMax, 0) });
			if (d.tInC > 30 || d.tInC < 12) now.push_back({ "tin", TR("ТЕПЛО"), 2, TR("В СКАФАНДРЕ ") + Num(d.tInC, 1) + " °C" });
			else if (std::abs(d.residualW) > 30) now.push_back({ "heat", TR("ТЕПЛО"), 1, d.residualW > 0 ? TR("ОХЛАЖДЕНИЕ НЕ СПРАВЛЯЕТСЯ") : TR("ОБОГРЕВ НЕ СПРАВЛЯЕТСЯ") });
			if (d.coreC > 39.5 || d.coreC < 35) now.push_back({ "core", TR("ТЕПЛО"), 2, TR("ТЕЛО ") + Num(d.coreC, 1) + " °C" });
			else if (d.coreC > 38.5 || d.coreC < 35.8) now.push_back({ "core", TR("ТЕПЛО"), 1, TR("ТЕЛО ") + Num(d.coreC, 1) + " °C" });
			// the ground: a lunar day of +100..+120 C is normal for the suit; warn only near and above its rating
			if (d.hasGround && d.landed && d.groundC > d.ratedMaxC) now.push_back({ "ground", TR("ТЕПЛО"), 2, TR("ГРУНТ ") + Num(d.groundC, 0, true) + TR(" °C · ВЫШЕ ДОПУСКА") });
			else if (d.hasGround && d.landed && d.groundC > d.ratedMaxC - 20) now.push_back({ "ground", TR("ТЕПЛО"), 1, TR("ГРУНТ ") + Num(d.groundC, 0, true) + TR(" °C · у предела") });
			// rate: caution above 1 mSv/h, warning above 10 mSv/h (belts, flares); the dose of the outing: 100 / 500 mSv
			const std::string hint = d.fieldOn ? "" : TR(" · ВКЛЮЧИТЕ ПОЛЕ");
			if (doseRate > 10000) now.push_back({ "rad", TR("РАДИАЦ"), 2, TR("РАДИАЦИЯ ") + Num(doseRate / 1000, 1) + TR(" мЗв/ч") + hint });
			else if (doseRate > 1000) now.push_back({ "rad", TR("РАДИАЦ"), 1, TR("РАДИАЦИЯ ") + Num(doseRate / 1000, 2) + TR(" мЗв/ч") + hint });
			if (dose > 2.5e5) now.push_back({ "dose", TR("РАДИАЦ"), 2, TR("ДОЗА ") + Num(dose / 1000, 0) + TR(" мЗв") });
			if (d.life && d.life->optDark > 0.4) now.push_back({ "optics", TR("РАДИАЦ"), 2, TR("ОПТИКА: ПОТЕМНЕНИЕ ") + Num(100 * d.life->optDark, 0) + " %" });
			else if (d.life && d.life->optDark > 0.05) now.push_back({ "optics", TR("РАДИАЦ"), 1, TR("ОПТИКА: ПОТЕМНЕНИЕ ") + Num(100 * d.life->optDark, 0) + " %" });
			else if (dose > 1e5) now.push_back({ "dose", TR("РАДИАЦ"), 1, TR("ДОЗА ") + Num(dose / 1000, 0) + TR(" мЗв") });
			if (const Suit* s = d.life)
			{
				// the supply over its limit: what was shed, by the loops (task first)
				if (s->overload)
				{
					std::string off;
					auto add = [&](bool b, const char* n) { if (b) off += (off.empty() ? "" : ", ") + std::string(n); };
					add(s->shedField, TR("поле")); add(s->shedLamps, TR("фонари")); add(s->shedDrives, TR("сервоприводы"));
					now.push_back({ "over", TR("БАТ"), 1, TR("ПЕРЕГРУЗКА ПИТАНИЯ") + (off.empty() ? std::string(TR(" · урезан теплообмен")) : TR(" · отключено: ") + off) });
				}
				const double te = s->tEq - 273.15;
				if (s->tEq > Suit::EQ_LIMIT) now.push_back({ "eq", TR("ТЕПЛО"), 2, TR("ПЕРЕГРЕВ АППАРАТУРЫ ") + Num(te, 0, true) + TR(" °C · поле и сервоприводы отключены") });
				else if (s->tEq > Suit::EQ_WARN) now.push_back({ "eq", TR("ТЕПЛО"), 1, TR("АППАРАТУРА ") + Num(te, 0, true) + TR(" °C · отключение при +90") });
				if (s->coldTrip) now.push_back({ "eqc", TR("ТЕПЛО"), 2, TR("АППАРАТУРА ПЕРЕОХЛАЖДЕНА ") + Num(te, 0, true) + TR(" °C · отключена") });
				else if (s->tEq < Suit::EQ_COLDWARN) now.push_back({ "eqc", TR("ТЕПЛО"), 1, TR("АППАРАТУРА ") + Num(te, 0, true) + TR(" °C · отключение при −40") });
				if (s->restartT > 0) now.push_back({ "restart", TR("БАТ"), 1, TR("ПЕРЕЗАПУСК ПОСЛЕ ПЕРЕГРЕВА · сначала жизнеобеспечение") });
				// heat and cold damage (both suits): what has burnt, what the cold has cracked and what has failed for good
				if (s->dmgIns > 0.02) now.push_back({ "ins", TR("ТЕПЛО"), s->dmgIns > 0.3 ? 2 : 1, TR("ИЗОЛЯЦИЯ ПРОГОРАЕТ ") + Num(100 * s->dmgIns, 0) + " %" });
				if (s->dmgSeal > 0.2) now.push_back({ "seal", TR("ДАВЛ"), 2, TR("УПЛОТНЕНИЯ: УТЕЧКА ") + Num(100 * s->dmgSeal, 0) + " %" });
				else if (s->dmgSeal > 0.02) now.push_back({ "seal", TR("ДАВЛ"), 1, (s->coldSeal ? TR("УПЛОТНЕНИЯ ПЕРЕОХЛАЖДЕНЫ ") : TR("УПЛОТНЕНИЯ ПЕРЕГРЕТЫ ")) + Num(100 * s->dmgSeal, 0) + " %" });
				if (s->coldSeal && s->dmgSeal <= 0.02) now.push_back({ "sealc", TR("ДАВЛ"), 1, TR("УПЛОТНЕНИЯ ") + Num(s->tSeal - 273.15, 0, true) + TR(" °C · ХОЛОД") });
				if (s->fieldFail) now.push_back({ "ffail", TR("РАДИАЦ"), 2, TR("ПОЛЕ: ОТКАЗ КАТУШЕК") });
				if (s->drivesFail) now.push_back({ "dfail", TR("БАТ"), 2, TR("СЕРВОПРИВОДЫ: ОТКАЗ") });
				if (s->immersion > 0.02) now.push_back({ "melt", TR("ТЕПЛО"), 2, TR("НОГИ В РАСПЛАВЕ · ") + Num(s->tLeg - 273.15, 0, true) + " °C" });
				if (s->dmgLeg > 0.02) now.push_back({ "leg", TR("ДАВЛ"), 2, TR("ОБУВЬ ПРОГОРАЕТ ") + Num(100 * s->dmgLeg, 0) + " %" });
				if (s->Heavy() && s->coolFail) now.push_back({ "cfail", TR("ТЕПЛО"), 2, TR("РАДИАТОР: ОТКАЗ КОНТУРА") });
			}
			if (d.jet && d.jetFuel < 0.05) now.push_back({ "fuel", TR("РЕЗЕРВ"), 2, TR("ТОПЛИВО РАНЦА ") + Num(100 * d.jetFuel, 0) + " % · " + Num(d.jetDv, 0) + TR(" м/с") });
			else if (d.jet && d.jetFuel < 0.15) now.push_back({ "fuel", TR("РЕЗЕРВ"), 1, TR("ТОПЛИВО РАНЦА ") + Num(100 * d.jetFuel, 0) + " % · " + Num(d.jetDv, 0) + TR(" м/с") });
			// coming down too fast for the height left (the pack's own guard line): a warning, not a hint
			if (d.jet && d.jetFlying && d.alt < 25 && d.vs < -(1.0 + 0.8 * d.alt) - 1.5) now.push_back({ "sink", "", 2, TR("БЫСТРОЕ СНИЖЕНИЕ ") + Num(-d.vs, 1) + TR(" м/с") });
			if (d.jet && d.jetProtect) now.push_back({ "near", "", 1, TR("ЗЕМЛЯ БЛИЗКО") });
			if (d.jet && d.jetTerrain) now.push_back({ "terr", "", 1, TR("РЕЛЬЕФ ВПЕРЕДИ") });
			if (const Target* s = Selected(); s && space && s->dist < 30 && -s->rate > 0.25) now.push_back({ "zone", "", 1, TR("ЗОНА 30 м · СБЛИЖЕНИЕ БЫСТРЕЕ 0,25 м/с") });
		}
		if (d.state == 2) now.push_back({ "dead", "", 2, d.life && d.life->Heavy() ? TR("ПУЛЬС 0") : TR("ГИБЕЛЬ") });
		else if (!d.warning.empty()) now.push_back({ "bio", "", 2, d.warning });

		const double mjd = oapiGetSimMJD(), day = (mjd - std::floor(mjd)) * 86400;
		for (auto& a : alerts) a.seen = false;
		for (const C& c : now)
		{
			Alert* f = nullptr;
			for (auto& a : alerts) if (a.active && a.key == c.key) { f = &a; break; }
			if (f) { if (c.level > f->level) f->ack = false; f->level = c.level; f->text = c.text; f->tile = c.tile; f->seen = true; }
			else alerts.insert(alerts.begin(), Alert{ c.key, c.tile, c.text, c.level, day, false, true, true });
		}
		for (auto& a : alerts) if (a.active && !a.seen) a.active = false;
		while (alerts.size() > 40) alerts.pop_back();
		// every change of what is raised goes to the log (the user, 2026-10-16: a constant alarm - which one)
		{
			std::string sig;
			for (const auto& a : alerts) if (a.active) sig += std::string(a.key) + ":" + std::to_string(a.level) + (a.ack ? "a " : " ");
			if (sig != alertSig)
			{
				alertSig = sig;
				std::string txt;
				for (const auto& a : alerts) if (a.active) txt += " [" + std::string(a.key) + " " + std::to_string(a.level) + (a.ack ? " ack" : "") + "] " + a.text + ";";
				oapiWriteLogV("OrbiterCrew: suit alerts:%s", txt.empty() ? " none" : txt.c_str());
			}
		}
	}

	// ================= navigation =================
	void SuitHud::Nav(VESSEL* v, const HudData& d)
	{
		const double t = oapiGetSimTime();
		hBody = v->GetSurfaceRef(); if (!hBody) hBody = v->GetGravityRef();
		if (!hBody) return;
		bodyR = oapiGetSize(hBody);
		VECTOR3 gp, bp; v->GetGlobalPos(gp); oapiGetGlobalPos(hBody, &bp);
		double rad; oapiGlobalToEqu(hBody, gp, &lng, &lat, &rad);
		alt = v->GetAltitude(ALTMODE_GROUND);
		oapiGetHeading(v->GetHandle(), &hdg); pitch = v->GetPitch(); bank = v->GetBank();
		v->GetGroundspeedVector(FRAME_HORIZON, gsH);
		surface = d.landed || alt < 5000; space = !surface;
		const VECTOR3 up = Unit(gp - bp);
		VECTOR3 sp; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sp);
		sunElev = std::asin(std::clamp(dotp(up, Unit(sp - gp)), -1.0, 1.0));

		// trail on the ground
		if (t - lastTrail > 0.5 || t < lastTrail)
		{
			lastTrail = t;
			if (trail.empty() || std::hypot((lat - trail.back().first) * bodyR, (lng - trail.back().second) * bodyR * std::cos(lat)) > 0.4)
			{
				trail.emplace_back(lat, lng);
				if (trail.size() > 16) trail.erase(trail.begin());
			}
		}

		// everything around: vessels, dropped packs, other crew
		targets.clear();
		int packs = 0;
		const DWORD n = oapiGetVesselCount();
		for (DWORD i = 0; i < n; ++i)
		{
			OBJHANDLE h = oapiGetVesselByIndex(i);
			if (!h || h == v->GetHandle()) continue;
			VESSEL* o = oapiGetVesselInterface(h);
			if (!o) continue;
			Target tg; tg.h = h;
			v->GetRelativePos(h, tg.rel); v->GetRelativeVel(h, tg.relV);
			tg.dist = length(tg.rel);
			if (tg.dist > 50e3) continue;
			tg.rate = tg.dist > 0 ? dotp(tg.rel, tg.relV) / tg.dist : 0;
			tg.landed = (o->GetFlightStatus() & 1) != 0;
			std::string cls = o->GetClassName() ? o->GetClassName() : "";
			std::transform(cls.begin(), cls.end(), cls.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			const std::string crew = CrewDisplayName(h);
			if (cls.find("jetpack") != std::string::npos)
			{
				tg.pack = true; ++packs;
				double fuel = 0;
				if (o->GetPropellantCount() > 0) { PROPELLANT_HANDLE ph = o->GetPropellantHandleByIndex(0); const double mx = o->GetPropellantMaxMass(ph); fuel = mx > 0 ? o->GetPropellantMass(ph) / mx : 0; }
				tg.name = TR("РАНЕЦ");
				tg.kind = std::string(tg.landed ? TR("лежит · ") : TR("плывёт · ")) + TR("топливо ") + Num(100 * fuel, 0) + " %";
			}
			else if (!crew.empty()) { tg.crew = true; tg.name = Upper(crew); tg.kind = TR("экипаж"); }
			else { tg.name = o->GetName(); tg.kind = tg.landed ? TR("аппарат · на грунте") : TR("аппарат"); }
			VECTOR3 og; oapiGetGlobalPos(h, &og);
			double olng, olat, orad; oapiGlobalToEqu(hBody, og, &olng, &olat, &orad);
			tg.north = (olat - lat) * bodyR; tg.east = Wrap(olng - lng) * bodyR * std::cos(lat); tg.up = orad - rad;
			targets.push_back(tg);
		}
		if (v->GetSurfaceRef() == hBody)
			for (DWORD b = 0; b < oapiGetBaseCount(hBody); ++b)
			{
				OBJHANDLE hb = oapiGetBaseByIndex(hBody, b);
				double blng, blat; oapiGetBaseEquPos(hb, &blng, &blat);
				Target tg; tg.h = hb; tg.base = tg.landed = true;
				tg.north = (blat - lat) * bodyR; tg.east = Wrap(blng - lng) * bodyR * std::cos(lat);
				if (std::hypot(tg.north, tg.east) > 15e3) continue;
				v->GetRelativePos(hb, tg.rel); v->GetRelativeVel(hb, tg.relV);
				tg.dist = length(tg.rel); tg.rate = tg.dist > 0 ? dotp(tg.rel, tg.relV) / tg.dist : 0;
				tg.up = -(rad - bodyR);
				char nm[256] = ""; oapiGetObjectName(hb, nm, 255);
				tg.name = Upper(nm); tg.pads = static_cast<int>(oapiGetBasePadCount(hb));
				tg.kind = tg.pads ? TR("база · площадок ") + std::to_string(tg.pads) : TR("база");
				targets.push_back(tg);
			}
		std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.dist < b.dist; });
		if (targets.size() > 8) targets.resize(8);
		if (packs > 1)   // several packs: number them by distance
		{
			int k = 0;
			for (Target& tg : targets) if (tg.pack) tg.name = TR("РАНЕЦ ") + std::to_string(++k);
		}
		if (sel && !Selected()) { sel = nullptr; rrHist.clear(); }   // out of reach: let go (none is ever chosen for her)

		// orbit
		orbitOk = tgtOrbitOk = false;
		if (space)
		{
			OBJHANDLE ref = v->GetGravityRef();
			orbitOk = v->GetElements(ref, el, &op, 0, FRAME_EQU) && el.e < 1;
			if (const Target* s = Selected())
			{
				VESSEL* o = s->base ? nullptr : oapiGetVesselInterface(s->h);
				tgtOrbitOk = o && o->GetElements(ref, tel, &top, 0, FRAME_EQU) && tel.e < 1;
				if (tgtOrbitOk)
				{
					VECTOR3 r1, v1, r2, v2; v->GetRelativePos(ref, r1); v->GetRelativeVel(ref, v1); o->GetRelativePos(ref, r2); o->GetRelativeVel(ref, v2);
					const VECTOR3 h1 = crossp(r1, v1), h2 = crossp(r2, v2);
					relInc = std::acos(std::clamp(dotp(Unit(h1), Unit(h2)), -1.0, 1.0));
				}
			}
		}

		// histories
		if (const Target* s = Selected())
			if (t - lastRR > 2 || t < lastRR) { lastRR = t; rrHist.emplace_back(s->dist, -s->rate); if (rrHist.size() > 200) rrHist.erase(rrHist.begin()); }
		if (t - trend.t > 10 || t < trend.t)
		{
			trend.t = t;
			auto push = [](std::vector<double>& q, double x) { q.push_back(x); if (q.size() > 61) q.erase(q.begin()); };
			push(trend.pulse, d.pulse); push(trend.breath, d.breath); push(trend.core, d.coreC);
		}

		if (surface)
		{
			const double half = ZOOM[zoom] * 1.2;
			const double moved = std::hypot((lat - grid.lat) * bodyR, (lng - grid.lng) * bodyR * std::cos(lat));
			if (grid.t < 0 || t - grid.t > 3 || t < grid.t || grid.half != half || moved > half / 6) Terrain(v);
			TerrainRows(*this, hBody, bodyR);
			if (sel && (prof.tgt != sel || t - prof.t > 1 || t < prof.t)) Profile(v);
		}
		lastNav = t;
	}

	void SuitHud::Terrain(VESSEL* v)
	{
		// start a new grid; the rows are filled over the next frames and it replaces the shown one when complete
		if (nextRow >= 0) return;
		next.n = 25; next.half = ZOOM[zoom] * 1.2; next.lat = lat; next.lng = lng; next.t = oapiGetSimTime();
		next.h.assign(next.n * next.n, 0);
		nextRow = 0;
		grid.t = next.t;   // no new request until this one is done
	}

	void TerrainRows(SuitHud& h, OBJHANDLE body, double bodyR)
	{
		auto& n = h.next;
		if (h.nextRow < 0) return;
		const double step = 2 * n.half / (n.n - 1), cl = (std::max)(0.05, std::cos(n.lat));
		for (int k = 0; k < 3 && h.nextRow < n.n; ++k, ++h.nextRow)   // 3 rows (75 queries) per frame
			for (int i = 0; i < n.n; ++i)
			{
				const double north = -n.half + h.nextRow * step, east = -n.half + i * step;
				n.h[h.nextRow * n.n + i] = oapiSurfaceElevation(body, n.lng + east / (bodyR * cl), n.lat + north / bodyR);
			}
		if (h.nextRow >= n.n) { h.grid = n; h.nextRow = -1; }
	}

	void SuitHud::Profile(VESSEL* v)
	{
		const Target* s = Selected();
		prof.tgt = sel; prof.t = oapiGetSimTime(); prof.h.clear();
		if (!s) return;
		prof.dist = std::hypot(s->north, s->east);
		prof.brg = std::atan2(s->east, s->north);
		const double cl = (std::max)(0.05, std::cos(lat));
		for (int i = 0; i <= 40; ++i)
		{
			const double f = i / 40.0;
			prof.h.push_back(oapiSurfaceElevation(hBody, lng + f * s->east / (bodyR * cl), lat + f * s->north / bodyR));
		}
	}

	// ================= MFD pages =================
	namespace
	{
		// the page's width in page units: 340 (portrait); the heavy suit's landscape MFD widens the local map's window
		double gMapW = 340;
		inline double MCX() { return gMapW / 2; }
		void Grid(Gfx& g)
		{
			for (int x = 20; x < gMapW; x += 20) g.Line(x, 0, x, 300, CP, 0.6, 0.07, false, false);
			for (int y = 20; y < 300; y += 20) g.Line(0, y, gMapW, y, CP, 0.6, 0.07, false, false);
		}
		void Msg(Gfx& g, const std::string& a, const std::string& b = "")
		{
			g.T(gMapW / 2, 140, a, CD, 12, 1); g.T(gMapW / 2, 160, b, CD, 9.5, 1);
		}
	}

	namespace pages
	{
		// heading-up map coordinates: centre (170,170)

		// ---- a base's layout for the map (Orbiter's API gives only the pads' centres): read once from its config ----
		// footprints (BLOCK, HANGAR*, TANK, LPAD*) and lines (RUNWAY, TRAIN*) in the base's own frame, metres;
		// the frame's axes are matched to east/north on the pads the API does give
		struct BaseLayout
		{
			struct Box { double x, z, w, d, rot; bool round, pad; int padNo; };
			struct Seg { double x1, z1, x2, z2, w; };
			std::vector<Box> boxes; std::vector<Seg> segs;
			double ex[2]{ 1, 0 }, nz[2]{ 0, 1 };   // east = ex[0]*x + ex[1]*z, north = nz[0]*x + nz[1]*z
			bool ok{};
		};
		const BaseLayout& LayoutOf(OBJHANDLE base, OBJHANDLE planet)
		{
			static std::map<OBJHANDLE, BaseLayout> cache;
			if (auto it = cache.find(base); it != cache.end()) return it->second;
			BaseLayout& L = cache[base];
			char bname[256] = "", pname[256] = ""; oapiGetObjectName(base, bname, 255); oapiGetObjectName(planet, pname, 255);
			const std::string dir = std::string("Config\\") + pname + "\\Base\\";
			WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA((dir + "*.cfg").c_str(), &fd);
			if (fh == INVALID_HANDLE_VALUE) return L;
			std::vector<std::pair<double, double>> padPos;
			do
			{
				std::ifstream f(dir + fd.cFileName); std::string line, type; bool ours = false, inList = false;
				double pos[3]{}, scl[3]{ 1, 1, 1 }, rot = 0, e1[3]{}, e2[3]{}, width = 0; bool hasScl = false;
				BaseLayout tmp; std::vector<std::pair<double, double>> pads;
				auto flush = [&]()
				{
					if (type.empty()) return;
					const bool pad = type.rfind("LPAD", 0) == 0;
					if (pad) { const double half = 40 * (hasScl ? scl[0] : 1); tmp.boxes.push_back({ pos[0], pos[2], 2 * half, 2 * half, rot, type == "LPAD1", true, static_cast<int>(pads.size()) + 1 }); pads.push_back({ pos[0], pos[2] }); }
					else if (type == "BLOCK" || type.rfind("HANGAR", 0) == 0) tmp.boxes.push_back({ pos[0], pos[2], scl[0], scl[2], rot, false, false, 0 });
					else if (type == "TANK") tmp.boxes.push_back({ pos[0], pos[2], scl[0], scl[2], 0, true, false, 0 });
					else if (type == "RUNWAY" || type.rfind("TRAIN", 0) == 0) tmp.segs.push_back({ e1[0], e1[2], e2[0], e2[2], type == "RUNWAY" ? width : 0 });
					type.clear();
				};
				while (std::getline(f, line))
				{
					std::istringstream ss(line); std::string w; ss >> w;
					if (w.empty() || w[0] == ';') continue;
					if (w == "Name") { std::string eq, rest; ss >> eq; std::getline(ss >> std::ws, rest); while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back(); ours = rest == bname; }
					else if (w == "BEGIN_OBJECTLIST") inList = true;
					else if (w == "END_OBJECTLIST") { flush(); inList = false; }
					else if (!inList) continue;
					else if (w == "END") flush();
					else if (w == "POS") ss >> pos[0] >> pos[1] >> pos[2];
					else if (w == "SCALE") { ss >> scl[0]; if (!(ss >> scl[1] >> scl[2])) scl[1] = scl[2] = scl[0]; hasScl = true; }
					else if (w == "ROT") ss >> rot;
					else if (w == "END1") ss >> e1[0] >> e1[1] >> e1[2];
					else if (w == "END2") ss >> e2[0] >> e2[1] >> e2[2];
					else if (w == "WIDTH") ss >> width;
					else if (std::isupper(static_cast<unsigned char>(w[0])) && w.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos)
					{ flush(); type = w; pos[0] = pos[1] = pos[2] = 0; scl[0] = scl[1] = scl[2] = 1; hasScl = false; rot = 0; width = 0; }
				}
				if (ours) { L.boxes = tmp.boxes; L.segs = tmp.segs; padPos = pads; L.ok = true; break; }
			} while (FindNextFileA(fh, &fd));
			FindClose(fh);
			// the frame's axes: of the four ways x/z can lie on east/north, the one that puts the pads where Orbiter has them
			const double R = oapiGetSize(planet); double blng, blat; oapiGetBaseEquPos(base, &blng, &blat);
			const double cand[8][4] = { { 1, 0, 0, 1 }, { 1, 0, 0, -1 }, { -1, 0, 0, 1 }, { -1, 0, 0, -1 }, { 0, 1, 1, 0 }, { 0, 1, -1, 0 }, { 0, -1, 1, 0 }, { 0, -1, -1, 0 } };
			double best = 1e18; int bi = 0;
			for (int ci = 0; ci < 8; ++ci)
			{
				double err = 0; int n = 0;
				for (DWORD k = 0; k < oapiGetBasePadCount(base) && k < padPos.size(); ++k)
				{
					double plng, plat, prad; if (!oapiGetBasePadEquPos(base, k, &plng, &plat, &prad)) continue;
					const double e = (plng - blng) * R * std::cos(blat), nn = (plat - blat) * R;
					const double x = padPos[k].first, z = padPos[k].second;
					err += std::hypot(cand[ci][0] * x + cand[ci][1] * z - e, cand[ci][2] * x + cand[ci][3] * z - nn); ++n;
				}
				if (n && err < best) { best = err; bi = ci; }
			}
			L.ex[0] = cand[bi][0]; L.ex[1] = cand[bi][1]; L.nz[0] = cand[bi][2]; L.nz[1] = cand[bi][3];
			return L;
		}

		V2 MapXY(double north, double east, double hdg, double s)
		{
			const double c = std::cos(hdg), sn = std::sin(hdg);
			return { MCX() + (east * c - north * sn) * s, 170 - (east * sn + north * c) * s };
		}
		bool In(const V2& p) { return p.first > 2 && p.first < gMapW - 2 && p.second > 2 && p.second < 298; }
		// the map's own window (the same box as In): a line cut to it (Liang-Barsky); false - nothing of it inside
		bool ClipSeg(V2& a, V2& b)
		{
			const double x0 = 2, x1 = gMapW - 2, y0 = 2, y1 = 298, dx = b.first - a.first, dy = b.second - a.second;
			double t0 = 0, t1 = 1;
			const double p[4] = { -dx, dx, -dy, dy }, q[4] = { a.first - x0, x1 - a.first, a.second - y0, y1 - a.second };
			for (int i = 0; i < 4; ++i)
			{
				if (p[i] == 0) { if (q[i] < 0) return false; continue; }
				const double r = q[i] / p[i];
				if (p[i] < 0) t0 = (std::max)(t0, r); else t1 = (std::min)(t1, r);
				if (t0 > t1) return false;
			}
			const V2 a0 = a;
			a = { a0.first + t0 * dx, a0.second + t0 * dy }; b = { a0.first + t1 * dx, a0.second + t1 * dy };
			return true;
		}
		// a polygon cut to the map's window (Sutherland-Hodgman), for footprints that reach over its edge
		std::vector<V2> ClipPoly(const std::vector<V2>& in)
		{
			std::vector<V2> poly = in;
			for (int e = 0; e < 4 && !poly.empty(); ++e)
			{
				auto inside = [&](const V2& p) { return e == 0 ? p.first >= 2 : e == 1 ? p.first <= gMapW - 2 : e == 2 ? p.second >= 2 : p.second <= 298; };
				auto cross = [&](const V2& a, const V2& b)
				{
					const double edge = e == 0 ? 2 : e == 1 ? gMapW - 2 : e == 2 ? 2 : 298;
					const double t = e < 2 ? (edge - a.first) / (b.first - a.first) : (edge - a.second) / (b.second - a.second);
					return V2{ a.first + t * (b.first - a.first), a.second + t * (b.second - a.second) };
				};
				std::vector<V2> out;
				for (size_t i = 0; i < poly.size(); ++i)
				{
					const V2 &cur = poly[i], &prev = poly[(i + poly.size() - 1) % poly.size()];
					if (inside(cur)) { if (!inside(prev)) out.push_back(cross(prev, cur)); out.push_back(cur); }
					else if (inside(prev)) out.push_back(cross(prev, cur));
				}
				poly = out;
			}
			return poly;
		}

		void Local(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.space)
			{
				// plan of the neighbourhood in her orbital frame: along the velocity to the right, the orbit normal up
				OBJHANDLE ref = c.v->GetGravityRef();
				VECTOR3 r, vv; c.v->GetRelativePos(ref, r); c.v->GetRelativeVel(ref, vv);
				const VECTOR3 vb = Unit(vv), nb = Unit(crossp(r, vv));
				double far_ = 20;
				for (const auto& t : h.targets) far_ = (std::max)(far_, (std::min)(t.dist, 2000.0));
				const double s = 120 / (far_ * 1.1);
				g.T(14, 18, TR("ПЛАН · ПО ХОДУ →"), CD, 9.5); g.T(gMapW - 10, 18, TR("МАСШТАБ ") + Dist(far_ * 1.1), CD, 9.5, 2);
				for (double f : { 0.5, 1.0 }) g.Circle(MCX(), 160, 120 * f, CP, 1, 0.3, -1, 0, true);
				for (const auto& t : h.targets)
				{
					const VECTOR3 rel = -t.rel;   // target minus her
					double x = MCX() + dotp(rel, vb) * s, y = 160 - dotp(rel, nb) * s;
					const bool on = t.h == h.sel;
					if (!In({ x, y })) continue;
					g.Hit(x - 10, y - 10, 20, 20, SuitHud::H_TGT, static_cast<int>(&t - &h.targets[0]));
					if (t.pack) g.Rect(x - 4, y - 4, 8, 8, on ? CA : CP, 1.4);
					else g.Circle(x, y, 5, on ? CA : CP, 1.4);
					g.T(x + 8, y + 4, t.name + " " + Dist(t.dist), on ? CA : CP, 10);
				}
				g.Tri(MCX(), 152, 6, 14, -6, 14, CW);
				return;
			}
			const double range = ZOOM[h.zoom], s = 128 / range;
			// navigation display (default, user's choice: variant B, white): one colour of light - white - for the whole
			// map, the meaning in brightness, weight and dash; track/heading up with a heading arc on top. The colour map
			// keeps the accents as before
			const int cA = h.mapMono ? CW : CA, cW = CW, cP = h.mapMono ? CW : CP;
			const auto& G = h.grid;
			// contours (marching squares) and steep ground
			if (G.n > 1 && !G.h.empty())
			{
				double lo = 1e9, hi = -1e9; for (double z : G.h) { lo = (std::min)(lo, z); hi = (std::max)(hi, z); }
				const double step = 2 * G.half / (G.n - 1);
				const double nice[] = { 0.25, 0.5, 1, 2, 5, 10, 20, 50, 100, 200 };
				double ci = nice[9]; for (double q : nice) if ((hi - lo) / q <= 7) { ci = q; break; }
				const double dN = (G.lat - h.lat) * h.bodyR, dE = Wrap(G.lng - h.lng) * h.bodyR * std::cos(h.lat);
				auto P = [&](double i, double j) { return MapXY(-G.half + j * step + dN, -G.half + i * step + dE, h.hdg, s); };
				if (h.mapMono && hi - lo > 0.3)   // brightness bands: four levels of the ground, higher = brighter
					for (int j = 0; j + 1 < G.n; ++j)
						for (int i = 0; i + 1 < G.n; ++i)
						{
							const double zc = 0.25 * (G.h[j * G.n + i] + G.h[j * G.n + i + 1] + G.h[(j + 1) * G.n + i] + G.h[(j + 1) * G.n + i + 1]);
							const double band = std::floor(std::clamp((zc - lo) / (hi - lo), 0.0, 0.999) * 4) / 3;
							const V2 q[4] = { P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1) };
							if (In(q[0]) || In(q[2])) g.Fill({ q[0], q[1], q[2], q[3] }, CW, 0.02 + 0.07 * band);
						}
				if (!h.mapMono && hi - lo > 0.3)
					for (double lv = std::ceil(lo / ci) * ci; lv < hi; lv += ci)
						for (int j = 0; j + 1 < G.n; ++j)
							for (int i = 0; i + 1 < G.n; ++i)
							{
								const double z[4] = { G.h[j * G.n + i], G.h[j * G.n + i + 1], G.h[(j + 1) * G.n + i + 1], G.h[(j + 1) * G.n + i] };
								const double ex[4][4] = { { 0, 0, 1, 0 }, { 1, 0, 1, 1 }, { 1, 1, 0, 1 }, { 0, 1, 0, 0 } };   // edges: (i0,j0)-(i1,j1)
								std::vector<V2> cut;
								for (int e = 0; e < 4; ++e)
								{
									const double a = z[e], b = z[(e + 1) % 4];
									if ((a < lv) != (b < lv))
									{
										const double f = (lv - a) / (b - a);
										cut.push_back(P(i + ex[e][0] + f * (ex[e][2] - ex[e][0]), j + ex[e][1] + f * (ex[e][3] - ex[e][1])));
									}
								}
								for (size_t k = 0; k + 1 < cut.size(); k += 2)
									if (In(cut[k]) && In(cut[k + 1])) g.Line(cut[k].first, cut[k].second, cut[k + 1].first, cut[k + 1].second, CD, 1, 0.6, false, false);
							}
				for (int j = 0; j + 1 < G.n; ++j)
					for (int i = 0; i + 1 < G.n; ++i)
					{
						const double gz = std::hypot(G.h[j * G.n + i + 1] - G.h[j * G.n + i], G.h[(j + 1) * G.n + i] - G.h[j * G.n + i]) / step;
						if (gz < std::tan(15 * RAD)) continue;
						const V2 a = P(i, j), b = P(i + 1, j + 1), a2 = P(i + 0.5, j), b2 = P(i + 1, j + 0.5), a3 = P(i, j + 0.5), b3 = P(i + 0.5, j + 1);
						if (h.mapMono) { const V2 m = P(i + 0.5, j + 0.5); if (In(m)) g.Circle(m.first, m.second, 0.9, -1, 0, 0, CW, 0.55); }
						else if (In(a) && In(b)) { g.Line(a.first, a.second, b.first, b.second, cA, 1, 0.55, false, false); g.Line(a2.first, a2.second, b2.first, b2.second, cA, 1, 0.55, false, false); g.Line(a3.first, a3.second, b3.first, b3.second, cA, 1, 0.55, false, false); }
					}
			}
			// bases of the body: within 5 km of scale - their layout (footprints, pads with numbers, runways and tracks);
			// beyond - a symbol; every base within 500 km is on the map (on the ring, with its distance, if beyond it)
			if (h.hBody)
				for (DWORD bi = 0; bi < oapiGetBaseCount(h.hBody); ++bi)
				{
					OBJHANDLE hb = oapiGetBaseByIndex(h.hBody, bi);
					double blng, blat; oapiGetBaseEquPos(hb, &blng, &blat);
					const double bn = (blat - h.lat) * h.bodyR, be = Wrap(blng - h.lng) * h.bodyR * std::cos(h.lat), bd = std::hypot(bn, be);
					if (bd > 500e3) continue;
					const BaseLayout& Lb = range <= 5000 && bd < range * 2 + 3000 ? LayoutOf(hb, h.hBody) : BaseLayout{};
					if (Lb.ok)
					{
						auto at = [&](double x, double z) { return MapXY(bn + Lb.nz[0] * x + Lb.nz[1] * z, be + Lb.ex[0] * x + Lb.ex[1] * z, h.hdg, s); };
						for (const auto& sg : Lb.segs)
						{
							V2 p1 = at(sg.x1, sg.z1), p2 = at(sg.x2, sg.z2);
							if (!ClipSeg(p1, p2)) continue;   // runways and tracks are long: only the part in the map's window
							g.Line(p1.first, p1.second, p2.first, p2.second, CW, sg.w > 0 ? (std::max)(1.0, sg.w * s) : 1.0, sg.w > 0 ? 0.5 : 0.6, sg.w <= 0, false);
						}
						for (const auto& bx : Lb.boxes)
						{
							const double rr = bx.rot * RAD, cr = std::cos(rr), sr = std::sin(rr);
							auto corner = [&](double u, double v) { return at(bx.x + u * cr - v * sr, bx.z + u * sr + v * cr); };
							if (bx.round && !bx.pad) { const V2 cc = at(bx.x, bx.z); if (In(cc)) g.Circle(cc.first, cc.second, (std::max)(1.0, bx.w / 2 * s), CW, 1.2, 0.9, CW, 0.18); continue; }
							std::vector<V2> poly;
							if (bx.pad && bx.round) for (int kk = 0; kk < 8; ++kk) { const double t = kk * PI / 4 + PI / 8, q = bx.w / 2 / std::cos(PI / 8); poly.push_back(corner(q * std::cos(t), q * std::sin(t))); }
							else poly = { corner(-bx.w / 2, -bx.d / 2), corner(bx.w / 2, -bx.d / 2), corner(bx.w / 2, bx.d / 2), corner(-bx.w / 2, bx.d / 2) };
							poly = ClipPoly(poly);   // cut to the map's window: nothing drawn over its edge
							if (poly.size() < 3) continue;
							if (!bx.pad) g.Fill(poly, CW, 0.18);
							g.Poly(poly, CW, bx.pad ? 1.4 : 1.1, 0.9, true);
							if (bx.pad) { const V2 cc = at(bx.x, bx.z); if (In(cc) && bx.w * s > 10) g.T(cc.first, cc.second + 4, std::to_string(bx.padNo), CW, bx.w * s > 24 ? 12.5 : 9.5, 1); }
						}
						continue;
					}
					bool listed = false; for (const auto& t : h.targets) listed = listed || t.h == hb;
					if (listed) continue;   // a target already: drawn with the targets below
					V2 p = MapXY(bn, be, h.hdg, s);
					const double r = std::hypot(p.first - MCX(), p.second - 170); const bool edge = r > 134 || !In(p);
					if (edge) { const double f = 128 / (std::max)(1.0, r); p = { MCX() + (p.first - MCX()) * f, 170 + (p.second - 170) * f }; }
					g.Rect(p.first - 5, p.second - 5, 10, 10, CW, 1.3, edge ? 0.6 : 1); g.Line(p.first - 5, p.second, p.first + 5, p.second, CW, 1, edge ? 0.6 : 1); g.Line(p.first, p.second - 5, p.first, p.second + 5, CW, 1, edge ? 0.6 : 1);
					char bnm[256] = ""; oapiGetObjectName(hb, bnm, 255);
					const bool left = p.first > 250;
					g.T(p.first + (left ? -9 : 9), p.second + 4, Upper(bnm) + " " + Dist(bd), edge ? CD : CW, 9.5, left ? 2 : 0);
				}
			// range rings
			for (double f : { 0.25, 0.5, 1.0 })
			{
				g.Circle(MCX(), 170, 128 * f, cP, 1, 0.35, -1, 0, true);
				g.T(MCX() + 128 * f * 0.707 + 3, 170 + 128 * f * 0.707 + 10, Dist(range * f), CD, 9.5);
			}
			// trail and the next 10 s
			for (size_t i = 0; i < h.trail.size(); ++i)
			{
				const V2 p = MapXY((h.trail[i].first - h.lat) * h.bodyR, Wrap(h.trail[i].second - h.lng) * h.bodyR * std::cos(h.lat), h.hdg, s);
				if (In(p)) g.Circle(p.first, p.second, 1.3, -1, 0, 0, cP, 0.25 + 0.7 * i / h.trail.size());
			}
			{
				// vectors from her: velocity over the ground, the wind, the autopilot's push (1 m/s = 14 units)
				auto vec = [&](double north, double east, double k_) { const V2 p = MapXY(north, east, h.hdg, 1.0); return V2{ MCX() + (p.first - MCX()) * k_, 170 + (p.second - 170) * k_ }; };
				const double gv = std::hypot(h.gsH.x, h.gsH.z);
				if (gv > 0.1) { const double kk = (std::min)(14.0, 110 / gv); const V2 p = vec(h.gsH.z, h.gsH.x, kk); g.Arrow(MCX(), 170, p.first, p.second, cW, 1.6); g.T(p.first + 6, p.second - 4, "V " + Num(gv, 1), cW, 9.5); }
				const double wv = std::hypot(c.d.wind.x, c.d.wind.z);
				if (!c.d.vacuum && wv > 0.1) { const double kk = (std::min)(14.0, 110 / wv); const V2 p = vec(c.d.wind.z, c.d.wind.x, kk); g.Arrow(MCX(), 170, p.first, p.second, cP, 1.2, 0.8, true); g.T(p.first + 6, p.second + 10, TR("ветер ") + Num(wv, 1), cP, 9.5); }
				const double av = std::hypot(c.d.apCmd.x, c.d.apCmd.z);
				if (c.d.apMode && av > 0.05) { const double kk = (std::min)(30.0, 90 / av); const V2 p = vec(c.d.apCmd.z, c.d.apCmd.x, kk); g.Arrow(MCX(), 170, p.first, p.second, cA, 1.8); g.T(p.first + 6, p.second + 10, TR("АП"), cA, 9.5); }
			}
			// what is around
			for (const auto& t : h.targets)
			{
				const bool on = t.h == h.sel;
				const int idx = static_cast<int>(&t - &h.targets[0]);
				V2 p = MapXY(t.north, t.east, h.hdg, s);
				const double r = std::hypot(p.first - MCX(), p.second - 170);
				const bool edge = r > 134 || !In(p);
				if (edge) { const double f = 128 / (std::max)(1.0, r); p = { MCX() + (p.first - MCX()) * f, 170 + (p.second - 170) * f }; }
				const int col = on ? cA : cP;
				if (on) g.Line(MCX(), 170, p.first, p.second, cA, 1, 0.7, true);
				if (t.pack) { g.Rect(p.first - 5, p.second - 4, 10, 8, col, 1.5); g.Line(p.first - 5, p.second - 1, p.first + 5, p.second - 1, col, 1, 1, false, false); }
				else if (t.crew) g.Circle(p.first, p.second, 5, col, 1.4);
				else if (t.base) { g.Rect(p.first - 6, p.second - 6, 12, 12, col, 1.5); g.Line(p.first - 6, p.second, p.first + 6, p.second, col, 1); g.Line(p.first, p.second - 6, p.first, p.second + 6, col, 1); }
				else g.Poly({ { p.first, p.second - 7 }, { p.first + 7, p.second }, { p.first, p.second + 7 }, { p.first - 7, p.second } }, col, 1.6, 1, true);
				g.Hit(p.first - 10, p.second - 10, 20, 20, SuitHud::H_TGT, idx);
				const std::string lab = (edge ? "◂ " : "") + t.name + " " + Dist(t.dist);
				const bool left = p.first > 250;
				g.T(p.first + (left ? -9 : 9), p.second + 4, lab, col, 10, left ? 2 : 0);
			}
			// her
			g.Fill({ { MCX(), 161 }, { MCX() + 7, 177 }, { MCX(), 173 }, { MCX() - 7, 177 } }, cW, 1);
			// north arrow, scale
			const double nx = -std::sin(h.hdg), ny = -std::cos(h.hdg);
			g.Line(24, 34, 24 + nx * 12, 34 + ny * 12, cP, 1.6); g.T(24 + nx * 20, 34 + ny * 20 + 4, TR("С"), cP, 10, 1);
			char hd[16]; snprintf(hd, sizeof hd, "%03.0f°", std::fmod(h.hdg * DEG + 360, 360));
			g.T(gMapW - 10, 18, std::string(TR("КУРС ")) + hd, CD, 9.5, 2);
			// heading arc: +-60 deg of the course, ticks every 10, numbers every 30, the course under the pointer
			for (int dd = -60; dd <= 60; dd += 10)
			{
				const double a = -PI05 + dd * RAD, r1 = 134, r2 = dd % 30 ? 138 : 142;
				const double x1 = MCX() + r1 * std::cos(a), y1 = 170 + r1 * std::sin(a), x2 = MCX() + r2 * std::cos(a), y2 = 170 + r2 * std::sin(a);
				if (y2 < 24) continue;
				g.Line(x1, y1, x2, y2, CW, 1, 0.8, false, false);
				const int deg = static_cast<int>(std::lround(std::fmod(h.hdg * DEG + dd + 720, 360)));
				if (dd % 30 == 0 && dd != 0) { char nb[8]; snprintf(nb, sizeof nb, "%02d", deg / 10); g.T(MCX() + 150 * std::cos(a), 170 + 150 * std::sin(a) + 4, nb, CD, 9.5, 1); }
			}
			g.Tri(MCX(), 28, -5, -8, 5, -8, CW);
			g.Line(14, 288, 78, 288, cP, 1.4); g.Line(14, 284, 14, 292, cP, 1); g.Line(78, 284, 78, 292, cP, 1);
			g.T(46, 282, Dist(range / 2), CD, 9.5, 1);
			g.Line(238, 285, 250, 285, cA, 1, 0.7); g.T(254, 289, TR("склон > 15°"), cA, 9.5);
			for (int zi = 0; zi < 2; ++zi)
			{
				const double bx = 296 + zi * 22, by = 244;
				g.Rect(bx, by, 18, 18, cP, 1, 0.7, cP, 0.1); g.T(bx + 9, by + 14, zi ? "−" : "+", cP, 13, 1);
				g.Hit(bx, by, 18, 18, SuitHud::H_ZOOM, zi ? 1 : -1);
			}
		}

		void Orbit(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.surface)
			{
				const double cx = 170, cy = 112, r = 82;
				g.Circle(cx, cy, r, CP, 1.3, 1, CP, 0.05);
				for (double l : { 30.0, 60.0 })
				{
					const double w = r * std::cos(l * RAD);
					g.Line(cx - w, cy - r * std::sin(l * RAD), cx + w, cy - r * std::sin(l * RAD), CP, 0.8, 0.35, false, false);
					g.Line(cx - w, cy + r * std::sin(l * RAD), cx + w, cy + r * std::sin(l * RAD), CP, 0.8, 0.35, false, false);
				}
				g.Line(cx - r, cy, cx + r, cy, CP, 0.8, 0.35, false, false); g.Line(cx, cy - r, cx, cy + r, CP, 0.8, 0.35, false, false);
				// her position, seen from above her meridian
				const double x = cx, y = cy - r * std::sin(h.lat);
				g.Circle(x, y, 4, CW, 1.5, 1, CW, 1); g.Circle(x, y, 9, CA, 1.2);
				char bn[64] = ""; if (h.hBody) oapiGetObjectName(h.hBody, bn, 64);
				const char* ru = BodyRu(bn);
				g.T(14, 18, Upper(std::string(ru ? ru : bn)) + TR(" · НА ПОВЕРХНОСТИ"), CD, 9.5);
				g.Cell(14, 214, TR("ШИРОТА"), Num(std::abs(h.lat * DEG), 1) + (h.lat >= 0 ? TR("° с.") : TR("° ю.")));
				g.Cell(120, 214, TR("ДОЛГОТА"), Num(std::abs(h.lng * DEG), 1) + (h.lng >= 0 ? TR("° в.") : TR("° з.")));
				g.Cell(226, 214, TR("СОЛНЦЕ"), h.sunElev > 0 ? Num(h.sunElev * DEG, 0) + TR("° · день") : TR("ночь"), h.sunElev > 0 ? CW : CA);
				// what the body asks of anyone who would leave it, and what it gives by turning
				if (h.hBody)
				{
					const double R = h.bodyR, GM = GGRAV * oapiGetMass(h.hBody), gs = GM / (R * R);
					const double vc = std::sqrt(GM / R), ve = vc * std::sqrt(2.0);
					const double Tsid = std::abs(oapiGetPlanetPeriod(h.hBody)), vrot = Tsid > 0 ? PI2 * R * std::cos(h.lat) / Tsid : 0;
					double lng_, lat_, rad_; c.v->GetEquPos(lng_, lat_, rad_);
					g.Cell(14, 250, "g", Num(gs, 2) + TR(" м/с²"));
					g.Cell(120, 250, TR("1-Я КОСМ."), Num(vc, 0) + TR(" м/с"));
					g.Cell(226, 250, TR("2-Я КОСМ."), Num(ve, 0) + TR(" м/с"));
					g.Cell(14, 286, TR("ВРАЩЕНИЕ"), Num(vrot, 1) + TR(" м/с"));
					g.Cell(120, 286, TR("СУТКИ (ЗВЁЗД.)"), Tsid > 2 * 86400 ? Num(Tsid / 86400, 1) + TR(" сут") : Num(Tsid / 3600, 1) + TR(" ч"));
					g.Cell(226, 286, TR("ОТ СРЕДН. R"), Num(oapiSurfaceElevation(h.hBody, lng_, lat_), 0, true) + TR(" м"));
				}
				return;
			}
			if (!h.orbitOk)
			{
				// open path (escape) or no elements: what there is
				OBJHANDLE ref = c.v->GetGravityRef(); VECTOR3 rr, vv; c.v->GetRelativePos(ref, rr); c.v->GetRelativeVel(ref, vv);
				const double GM = GGRAV * oapiGetMass(ref), r0 = length(rr), v0 = length(vv), ve = std::sqrt(2 * GM / r0);
				Msg(g, TR("НЕЗАМКНУТАЯ ТРАЕКТОРИЯ"), (Num(v0, 0) + TR(" м/с · 2-я косм. здесь ") + Num(ve, 0) + TR(" м/с")).c_str());
				return;
			}
			char bn[64] = ""; oapiGetObjectName(c.v->GetGravityRef(), bn, 64);
			const char* ru = BodyRu(bn);
			const double R = oapiGetSize(c.v->GetGravityRef());
			const double apd = h.op.ApD, s = 118 / (std::max)(apd, R * 1.05);
			const double cx = 170, cy = 140;
			g.Circle(cx, cy, R * s, CP, 1.3, 1, CP, 0.07);
			g.T(cx, cy + 4, Upper(std::string(ru ? ru : bn)), CD, 9.5, 1);
			auto ell = [&](const ELEMENTS& e, double rot, int col, bool dash)
			{
				std::vector<V2> pts;
				const double p = e.a * (1 - e.e * e.e);
				for (int i = 0; i <= 72; ++i)
				{
					const double th = i * PI2 / 72, r = p / (1 + e.e * std::cos(th));
					pts.push_back({ cx + r * std::cos(th + rot) * s, cy - r * std::sin(th + rot) * s });
				}
				g.Poly(pts, col, dash ? 1.2 : 1.5, 1, false, dash);
			};
			auto at = [&](const ELEMENTS& e, double th, double rot) { const double r = e.a * (1 - e.e * e.e) / (1 + e.e * std::cos(th)); return V2{ cx + r * std::cos(th + rot) * s, cy - r * std::sin(th + rot) * s }; };
			ell(h.el, 0, CP, false);
			double drot = 0;
			if (h.tgtOrbitOk) { drot = Wrap(h.tel.omegab - h.el.omegab); ell(h.tel, drot, CA, true); const V2 q = at(h.tel, h.top.TrA, drot); g.Circle(q.first, q.second, 3.5, CA, 1.2, 1, CA, 1); }
			const V2 me = at(h.el, h.op.TrA, 0); g.Circle(me.first, me.second, 4.5, CW, 1.5, 1, CW, 1); g.T(me.first + 8, me.second + 14, TR("Я"), CW, 10);
			const V2 ap = at(h.el, PI, 0), pe = at(h.el, 0, 0);
			g.Circle(ap.first, ap.second, 2.5, CP, 1, 1, CP, 1); g.T(ap.first - 6, ap.second - 6, TR("Ап ") + Num((h.op.ApD - R) / 1000, 0), CP, 10, 2);
			g.Circle(pe.first, pe.second, 2.5, CP, 1, 1, CP, 1); g.T(pe.first + 6, pe.second - 6, TR("Пе ") + Num((h.op.PeD - R) / 1000, 0), CP, 10);
			g.T(14, 18, TR("ОРБИТА · ") + Upper(std::string(ru ? ru : bn)), CD, 9.5); g.T(330, 18, TR("— моя   - - цель"), CD, 9.5, 2);
			const bool hitsGround = h.op.PeD < R;   // a ballistic arc: it meets the surface before the periapsis
			g.Cell(14, 250, TR("ПЕРИОД"), Num(h.op.T / 60, 1) + TR(" мин"));
			g.Cell(120, 250, TR("НАКЛОН"), Num(h.el.i * DEG, 2) + "°");
			g.Cell(226, 250, TR("ОТН. НАКЛОН"), h.tgtOrbitOk ? Num(h.relInc * DEG, 2) + "°" : "—", CA);
			{
				VECTOR3 vv; c.v->GetRelativeVel(c.v->GetGravityRef(), vv);
				g.Cell(14, 286, TR("СКОРОСТЬ"), Num(length(vv), 0) + TR(" м/с"));
				g.Cell(120, 286, TR("ДО АПОЦЕНТРА"), Clock(h.op.ApT));
				g.Cell(226, 286, hitsGround ? TR("ДУГА") : TR("ДО ПЕРИЦЕНТРА"), hitsGround ? std::string(TR("до грунта")) : Clock(h.op.PeT), hitsGround ? CA : CW);
			}
		}

		// ЗАПАСЫ: every reserve the suit carries, what is left, how long it lasts and how fast it goes (the user,
		// 2026-10-20: one page of all the parameters - the pack's fuel and the RCS nitrogen too)
		void Reserves(Ctx& c)
		{
			Gfx& g = c.g; const HudData& d = c.d; Grid(g);
			if (!d.suit) { Msg(g, TR("КОМБИНЕЗОН"), TR("питания нет")); return; }
			struct Row { std::string n, left, rate; double f; bool has; };
			const bool fly = d.jet && d.jetFlying;
			std::vector<Row> rows = {
				{ TR("Батарея"), Num(100 * d.batt, 0) + " % · " + Hours(d.battHours), Num(d.powerW, 0) + TR(" Вт"), d.batt, true },
				{ TR("Кислород"), d.o2Vent ? TR("из воздуха · баллон ") + Num(100 * d.o2, 0) + " %" : Num(100 * d.o2, 0) + " % · " + Hours(d.o2Hours), d.o2Vent ? std::string("—") : Num(d.o2Flow * 60000, 2) + TR(" г/мин"), d.o2, true },
				{ TR("Поглотитель CO2"), Num(100 * d.sorbent, 0) + " % · " + Hours(d.sorbHours), "CO2 " + Num(d.ppCO2, 2) + TR(" кПа"), d.sorbent, true },
				{ TR("Топливо ранца"), d.jet ? Num(100 * d.jetFuel, 0) + " % · " + Num(d.jetDv, 0) + TR(" м/с") : std::string(TR("ранец снят")), d.jet ? (fly ? Num(d.jetFlow * 1000, 0) + TR(" г/с") : d.jetHover > 0 ? TR("висение ") + Clock(d.jetHover) : std::string("—")) : std::string("—"), d.jet ? d.jetFuel : 0.0, d.jet },
				{ TR("Азот РСУ"), Num(100 * d.n2, 0) + " % · " + Num(d.n2Dv, 1) + TR(" м/с"), d.rcs == 1 ? std::string(TR("готов")) : d.rcs == 2 ? std::string(TR("нет питания")) : std::string(TR("на грунте")), d.n2, true } };
			if (d.life) rows.push_back({ TR("Вода (питьё)"), Num(d.life->water, 2) + TR(" кг"), "", d.life->waterCap > 0 ? d.life->water / d.life->waterCap : 0.0, true });
			if (d.life && d.life->evapCap > 0) rows.push_back({ TR("Вода испарителя"), Num(d.life->evap, 1) + TR(" кг"), d.life->evapW > 1 ? Num(d.life->evapW, 0) + TR(" Вт") : std::string("—"), d.life->evap / d.life->evapCap, true });
			g.T(14, 18, TR("ЗАПАС"), CD, 9.5); g.T(326, 18, TR("РАСХОД"), CD, 9.5, 2);
			for (size_t i = 0; i < rows.size(); ++i)
			{
				const Row& r = rows[i]; const double y = 40 + i * 38, f = std::clamp(r.f, 0.0, 1.0);
				const int col = !r.has ? CD : f < 0.1 ? CR : f < 0.25 ? CA : CP;
				g.T(14, y, r.n, r.has ? CW : CD, 11);
				g.T(326, y, r.rate, CD, 10.5, 2);
				g.T(14, y + 15, r.left, r.has ? col == CP ? CW : col : CD, 10.5);
				g.Bar(170, y + 7, 156, 7, f, col);
			}
			const double lim = (std::min)({ d.battHours, d.o2Hours > 0 && !d.o2Vent ? d.o2Hours : 1e9, d.sorbHours > 0 ? d.sorbHours : 1e9 });
			const char* limName = lim == d.battHours ? TR("батарея") : lim == d.o2Hours ? TR("кислород") : TR("поглотитель");
			const double yl = 40 + rows.size() * 38 + 6;
			g.Line(14, yl - 14, 326, yl - 14, CD, 1, 0.5, false, false);
			g.T(14, yl, TR("хватит на ") + Hours(lim), lim < 1 ? CR : CW, 12.5); g.T(326, yl, std::string(TR("предел: ")) + limName, CD, 10.5, 2);
		}
		void Power(Ctx& c)
		{
			Gfx& g = c.g; const HudData& d = c.d; Grid(g);
			if (!d.suit) { Msg(g, TR("КОМБИНЕЗОН"), TR("питания нет")); return; }
			g.T(14, 22, TR("БАТАРЕЯ"), CD, 9.5);
			g.T(326, 22, Num(d.battKWh, 1) + TR(" кВт·ч · ") + Hours(d.battHours), CW, 11, 2);
			g.Bar(14, 30, 312, 16, d.batt, d.batt < 0.1 ? CR : d.batt < 0.25 ? CA : CP);
			const bool cooling = d.heatW >= 0;
			struct Row { const char* n; double w; } rows[] = {
				{ TR("Жизнеобеспечение"), d.lifeW }, { cooling ? TR("Охлаждение (насос)") : TR("Обогрев"), d.thermalW }, { TR("Сервоприводы"), d.driveW }, { TR("Фонари"), d.lampW } };
			for (int i = 0; i < 4; ++i)
			{
				const double y = 72 + i * 25, w = rows[i].w;
				g.T(14, y + 9, rows[i].n, w > 0.5 ? CP : CD, 11);
				g.Bar(150, y, 120, 11, w / 200, CP);
				g.T(326, y + 9, w > 0.5 ? Num(w, 0) + TR(" Вт") : TR("выкл"), w > 0.5 ? CW : CD, 11, 2);
			}
			g.Line(14, 180, 326, 180, CD, 1, 0.5, false, false);
			g.T(14, 196, TR("ИТОГО"), CD, 9.5); g.T(326, 196, Num(d.powerW, 0) + TR(" Вт"), CW, 13, 2);
			const double x0 = 20, x1 = 320;
			auto X = [&](double t) { return x0 + std::clamp((t - d.ratedMinC) / (d.ratedMaxC - d.ratedMinC), 0.0, 1.0) * (x1 - x0); };
			g.T(14, 226, TR("ТЕПЛО · ДОПУСК ") + Num(d.ratedMinC, 0) + "…" + Num(d.ratedMaxC, 0, true) + " °C", CD, 9.5);
			g.Line(x0, 252, x1, 252, CP, 3, 0.5); g.Line(x0, 244, x0, 260, CR, 2); g.Line(x1, 244, x1, 260, CR, 2);
			g.Tri(X(d.envC), 248, -5, -8, 5, -8, d.inSpec ? CP : CR); g.T(X(d.envC), 238, TR("среда ") + Num(d.envC, 0, true), d.inSpec ? CP : CR, 9.5, 1);
			if (d.hasGround) { g.Tri(X(d.groundC), 256, -5, 8, 5, 8, CA); g.T(X(d.groundC), 278, TR("грунт ") + Num(d.groundC, 0, true), CA, 9.5, 1); }
			g.T(14, 296, cooling ? TR("тепло уходит насосом на радиатор (КПД 3)") : TR("обогрев от батареи"), CD, 9.5);
		}

		void Opts(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (c.d.suit && c.d.life && !c.d.life->Heavy())   // the light suit's «Буран»: its two themes instead of the looks and colours
			{
				g.T(14, 16, TR("ТЕМА ЭКРАНА"), CD, 9.5);
				const char* TN[4] = { TR("ЛАЗЕР"), TR("ПРОЕКТОР"), TR("ДОП. РЕАЛЬНОСТЬ"), TR("ВЫСОКОКОНТРАСТ") };
				for (int i = 0; i < 4; ++i)
				{
					const double x = 10 + (i % 2) * 162, y = 22 + (i / 2) * 26, w = 158; const bool on = i == h.hudTheme;
					if (on) { g.Rect(x, y, w, 22, -1, 0, 0, CA, 1); g.T(x + w / 2, y + 15, TN[i], CK, 11, 1); }
					else { g.Rect(x, y, w, 22, CP, 1, 0.7, CP, 0.08); g.T(x + w / 2, y + 15, TN[i], CP, 11, 1); }
					g.Hit(x, y, w, 22, SuitHud::H_THEME, i);
				}
				static const char* const TNOTE[4] = { "зелёный лазер, тонкая линия", "голубой свет с мягким ореолом", "белый и синий, плоско", "оранжевый, плотный" };
				g.T(14, 88, TR(TNOTE[std::clamp(h.hudTheme, 0, 3)]), CD, 9.5);
				// the shading of the glass behind the display (the modulator): how dark under the plates and along the edges
				g.T(14, 112, TR("ЗАТЕНЕНИЕ СТЕКЛА"), CD, 9.5); g.T(326, 112, Num(100 * h.shadeK, 0) + " %", CP, 9.5, 2);
				const double y = 118;
				g.Rect(14, y, 20, 20, CP, 1, 0.7, CP, 0.08); g.T(24, y + 15, "−", CP, 13, 1); g.Hit(14, y, 20, 20, SuitHud::H_SHADEK, -1);
				g.Bar(40, y + 6, 250, 8, h.shadeK / 2, CP);
				g.Rect(296, y, 20, 20, CP, 1, 0.7, CP, 0.08); g.T(306, y + 15, "+", CP, 13, 1); g.Hit(296, y, 20, 20, SuitHud::H_SHADEK, 1);
				g.T(14, 160, TR("яркость линий — ниже, «ЯРКОСТЬ ЭКРАНА»"), CD, 9);
				g.T(14, 182, TR("МФД НА СТЕКЛЕ"), CD, 9.5);
				for (int i = 0; i < 2; ++i)
				{
					const double x = 120 + i * 104, w = 98; const bool on = (i == 1) == h.mfdSide;
					if (on) { g.Rect(x, 170, w, 18, -1, 0, 0, CA, 1); g.T(x + w / 2, 183, i ? TR("ПО СТОРОНАМ") : TR("ВПЕРЕДИ"), CK, 9.5, 1); }
					else { g.Rect(x, 170, w, 18, CP, 1, 0.7, CP, 0.08); g.T(x + w / 2, 183, i ? TR("ПО СТОРОНАМ") : TR("ВПЕРЕДИ"), CP, 9.5, 1); g.Hit(x, 170, w, 18, SuitHud::H_MFDSIDE); }
				}
			}
			else {
			g.T(14, 16, TR("ОБЛИК ЭКРАНА"), CD, 9.5);
			for (int i = 0; i < 3; ++i)
			{
				const double x = 10 + i * 108, w = 104; const bool on = i == h.look;
				if (on) { g.Rect(x, 22, w, 22, -1, 0, 0, CA, 1); g.T(x + w / 2, 37, TR(LOOK_NAME[i]), CK, 11, 1); }
				else { g.Rect(x, 22, w, 22, CP, 1, 0.7, CP, 0.08); g.T(x + w / 2, 37, TR(LOOK_NAME[i]), CP, 11, 1); }
				g.Hit(x, 22, w, 22, SuitHud::H_LOOK, i);
			}
			g.T(14, 62, h.look ? TR("ЦВЕТ — только для голо") : TR("ЦВЕТ"), CD, 9.5);
			for (int i = 0; i < 3; ++i)
			{
				const double y = 68 + i * 40; const bool on = i == h.pal;
				g.Rect(10, y, 320, 36, on ? CA : CD, on ? 1.6 : 1, 1, on ? CA : -1, 0.1);
				g.Hit(10, y, 320, 36, SuitHud::H_PAL, i);
				// swatches: the palette's own colours
				Gfx sw(g.skp, g.o, g.txt, i); sw.Frame(g.ox, g.oy, g.u); sw.kx = g.kx; sw.ky = g.ky;
				sw.Rect(22, y + 7, 22, 22, -1, 0, 0, CP, 1); sw.Rect(48, y + 7, 22, 22, -1, 0, 0, CA, 1);
				g.T(88, y + 16, std::string(on ? "▶ " : "") + TR(PAL_NAME[i]), on ? CA : CW, 12.5);
				g.T(88, y + 30, TR(PAL_NOTE[i]), CD, 9.5);
			}
			}
			g.Line(14, 192, 326, 192, CD, 1, 0.5, false, false);
			g.T(14, 207, TR("ЯРКОСТЬ ЭКРАНА"), CD, 9.5);
			g.T(326, 207, TR("свет вокруг ") + Num(h.ambient, 2), CD, 9.5, 2);
			{
				const double y = 213; const bool au = h.brightAuto;
				if (au) { g.Rect(14, y, 62, 20, -1, 0, 0, CA, 1); g.T(45, y + 14, TR("АВТО"), CK, 11, 1); }
				else { g.Rect(14, y, 62, 20, CP, 1, 0.7, CP, 0.08); g.T(45, y + 14, TR("АВТО"), CP, 11, 1); }
				g.Hit(14, y, 62, 20, SuitHud::H_BRIGHT, 0);
				g.Rect(84, y, 20, 20, CP, 1, 0.7, CP, 0.08); g.T(94, y + 15, "−", CP, 13, 1); g.Hit(84, y, 20, 20, SuitHud::H_BRIGHT, -1);
				g.Bar(110, y + 6, 180, 8, au ? h.ambient : h.brightManual / 1.5, au ? CD : h.brightManual > 1.05 ? CA : CP);
				g.Line(110 + 180 / 1.5, y + 3, 110 + 180 / 1.5, y + 17, CD, 1, 0.8, false, false);   // 1.0; beyond it - the maximum
				if (!au && h.brightManual > 1.05) g.T(200, y - 2, TR("МАКСИМУМ"), CA, 8.5, 1);
				g.Rect(296, y, 20, 20, CP, 1, 0.7, CP, 0.08); g.T(306, y + 15, "+", CP, 13, 1); g.Hit(296, y, 20, 20, SuitHud::H_BRIGHT, 1);
			}
			// the helmet display's resolution: 1:1 with the screen, or half (lighter)
			g.T(14, 252, TR("ЧЁТКОСТЬ"), CD, 9.5);
			for (int i = 0; i < 2; ++i)
			{
				const bool on = h.hudScale == i + 1; const double x = 70 + i * 128;
				if (on) g.Rect(x, 239, 120, 18, -1, 0, 0, CA, 1); else g.Rect(x, 239, 120, 18, CP, 1, 0.7, CP, 0.08);
				g.T(x + 60, 252, i == 0 ? "1:1" : TR("2× СГЛАЖИВАНИЕ"), on ? CK : CP, 10, 1);
				if (!on) g.Hit(x, 239, 120, 18, SuitHud::H_RES, i + 1);
			}
			// the pitch ladder: by itself (in flight and in space), always, or never
			g.T(14, 276, TR("ГОРИЗОНТ"), CD, 9.5);
			for (int i = 0; i < 3; ++i)
			{
				const char* HN[3] = { TR("АВТО"), TR("ВСЕГДА"), TR("ВЫКЛ") };
				const bool on = h.horizonMode == i; const double x = 70 + i * 86;
				if (on) g.Rect(x, 263, 80, 18, -1, 0, 0, CA, 1); else g.Rect(x, 263, 80, 18, CP, 1, 0.7, CP, 0.08);
				g.T(x + 40, 276, HN[i], on ? CK : CP, 10, 1);
				if (!on) g.Hit(x, 263, 80, 18, SuitHud::H_HORIZON, i);
			}
			// the local map: technical monochrome or with colour accents
			g.T(14, 300, TR("КАРТА"), CD, 9.5);
			for (int i = 0; i < 2; ++i)
			{
				const bool on = (i == 0) == h.mapMono; const double x = 70 + i * 128;
				if (on) g.Rect(x, 288, 120, 18, -1, 0, 0, CA, 1); else g.Rect(x, 288, 120, 18, CP, 1, 0.7, CP, 0.08);
				g.T(x + 60, 301, i == 0 ? TR("МОНОХРОМ") : TR("ЦВЕТ"), on ? CK : CP, 10, 1);
				if (!on) g.Hit(x, 288, 120, 18, SuitHud::H_LOOK, 10);
			}
			// «СИНТЕТИКА»: the terrain's contours on the ground itself (the AtmoFX client; with another client nothing shows)
			g.T(14, 324, TR("СИНТЕТИКА"), CD, 9.5);
			for (int i = 0; i < 2; ++i)
			{
				const bool on = (i == 1) == h.synth; const double x = 70 + i * 128;
				if (on) g.Rect(x, 312, 120, 18, -1, 0, 0, CA, 1); else g.Rect(x, 312, 120, 18, CP, 1, 0.7, CP, 0.08);
				g.T(x + 60, 325, i == 0 ? TR("ВЫКЛ") : TR("РЕЛЬЕФ"), on ? CK : CP, 10, 1);
				if (!on) g.Hit(x, 312, 120, 18, SuitHud::H_SYNTH, i);
			}
		}

		void Targets(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.targets.empty()) { Msg(g, TR("В РАДИУСЕ 50 км НИКОГО")); return; }
			g.T(14, 20, TR("ОБЪЕКТ"), CD, 9.5); g.T(250, 20, TR("ДАЛЬН"), CD, 9.5, 2); g.T(326, 20, TR("V СБЛ"), CD, 9.5, 2);
			const size_t rows = (std::min)(h.targets.size(), static_cast<size_t>(5));
			for (size_t i = 0; i < rows; ++i)
			{
				const auto& t = h.targets[i]; const double y = 28 + i * 32; const bool on = t.h == h.sel;
				if (on) g.Rect(8, y, 324, 29, CA, 1.2, 1, CA, 0.12);
				g.Hit(8, y, 324, 29, SuitHud::H_TGT, static_cast<int>(i));
				g.T(16, y + 13, (on ? "▶ " : "") + t.name, on ? CA : CW, 12.5);
				g.T(16, y + 25, t.kind, CD, 9.5);
				g.T(250, y + 18, Dist(t.dist), on ? CA : CW, 12.5, 2);
				g.T(326, y + 18, Num(t.rate, 2, true), CP, 11, 2);
			}
			const SuitHud::Target* s = h.Selected();
			if (!s) return;
			g.Line(14, 196, 326, 196, CD, 1, 0.5, false, false);
			g.T(14, 212, TR("ВЫБРАНО: ") + s->name, CA, 12);
			double brg = std::atan2(s->east, s->north) * DEG; if (brg < 0) brg += 360;
			const double elev = std::atan2(s->up, std::hypot(s->north, s->east)) * DEG;
			char b[16]; snprintf(b, sizeof b, "%03.0f°", brg);
			g.Cell(14, 228, TR("ПЕЛЕНГ"), b); g.Cell(100, 228, TR("УГОЛ МЕСТА"), Num(elev, 0, true) + "°");
			g.Cell(14, 264, TR("ОТН. СКОРОСТЬ"), Num(length(s->relV), 2) + TR(" м/с"));
			if (s->base) g.Cell(130, 264, TR("ПЛОЩАДКИ"), std::to_string(s->pads));
			else g.Cell(130, 264, TR("СТЫК. УЗЛЫ"), [&] { VESSEL* o = oapiGetVesselInterface(s->h); return o ? std::to_string(o->DockCount()) : std::string("—"); }());
			// the target's bearing relative to her heading
			const double rb = std::atan2(s->east, s->north) - h.hdg;
			g.Circle(288, 250, 30, CP, 1, 0.6);
			g.Line(288 + 22 * std::sin(rb), 250 - 22 * std::cos(rb), 288, 250, CA, 2);
			g.Circle(288 + 22 * std::sin(rb), 250 - 22 * std::cos(rb), 3, CA, 1, 1, CA, 1);
			g.T(288, 216, TR("курс ↑"), CD, 9.5, 1);
		}

		void Transfer(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; const HudData& d = c.d; Grid(g);
			const SuitHud::Target* s = h.Selected();
			if (!s || h.prof.h.size() < 2) { Msg(g, TR("ВЫБЕРИТЕ ЦЕЛЬ"), TR("на странице ЦЕЛИ или на карте")); return; }
			g.T(14, 18, TR("ПРОФИЛЬ ПЕРЕЛЁТА · РЕЛЬЕФ ПО ПУТИ"), CD, 9.5);
			const double hereGround = h.prof.h.front();
			const double cruise = d.apMode == 2 && d.jetMode == 1 ? d.jetAltHold : std::clamp(h.prof.dist / 40, 3.0, 60.0);
			double top = (std::max)(cruise, d.alt) + 2, bot = 0;
			for (double z : h.prof.h) { top = (std::max)(top, z - hereGround + 2); bot = (std::min)(bot, z - hereGround); }
			const double D = (std::max)(1.0, h.prof.dist);
			auto X = [&](double f) { return 22 + 296 * f; };
			auto Y = [&](double z) { return 150 - 110 * (z - bot) / (std::max)(1.0, top - bot); };
			std::vector<V2> ground;
			double minClear = 1e9;
			for (size_t i = 0; i < h.prof.h.size(); ++i)
			{
				const double f = static_cast<double>(i) / (h.prof.h.size() - 1), z = h.prof.h[i] - hereGround;
				ground.push_back({ X(f), Y(z) });
				minClear = (std::min)(minClear, cruise - z);
			}
			std::vector<V2> area = ground; area.push_back({ X(1), 150 }); area.push_back({ X(0), 150 });
			g.Fill(area, CD, 0.25); g.Poly(ground, CD, 1, 0.8);
			const double zEnd = h.prof.h.back() - hereGround;
			g.Poly({ { X(0), Y(d.alt) }, { X(0.08), Y(cruise) }, { X(0.85), Y(cruise) }, { X(1), Y(zEnd) } }, CA, 1.6, 1, false, true);
			g.Circle(X(0), Y(d.alt), 4, CW, 1.5, 1, CW, 1); g.T(X(0) + 6, Y(d.alt) - 6, TR("Я"), CW, 10);
			g.Line(X(1), Y(zEnd), X(1), Y(zEnd) - 22, CA, 1.4); g.Tri(X(1), Y(zEnd) - 22, 12, 4, 0, 8, CA);
			g.T(X(1) - 4, Y(zEnd) - 26, s->name, CA, 9.5, 2);
			g.T(160, 164, TR("зазор мин. ") + Num(minClear, 1) + TR(" м"), minClear < 1.5 ? CR : CP, 9.5, 1);
			double brg = h.prof.brg * DEG; if (brg < 0) brg += 360; char b[16]; snprintf(b, sizeof b, "%03.0f°", brg);
			const double g0 = h.hBody ? GGRAV * oapiGetMass(h.hBody) / (h.bodyR * h.bodyR) : 1.62;
			const bool hop = h.hBody && !oapiPlanetHasAtmosphere(h.hBody) && D > 400;
			const double vCruise = hop ? std::sqrt(D * g0 / std::sin(50 * RAD)) : std::clamp(std::sqrt(g0 * D / 2), 3.0, 40.0);
			const double time = hop ? 2 * vCruise * std::sin(25 * RAD) / g0 + 20 : D / vCruise;
			const double dv = hop ? 2.6 * std::sqrt(D * g0) + 80 : g0 * time + 2 * vCruise;
			g.Cell(14, 180, TR("ДАЛЬНОСТЬ"), Dist(D)); g.Cell(120, 180, TR("ПЕЛЕНГ"), b); g.Cell(226, 180, TR("ВРЕМЯ ≈"), Clock(time));
			g.Cell(14, 216, TR("ВЫСОТА"), Num(cruise, 1) + TR(" м"), CA); g.Cell(120, 216, "Δv ≈", Num(dv, 0) + TR(" м/с"));
			g.Cell(226, 216, TR("ЗАПАС ПОСЛЕ"), Num(d.jetDv - dv, 0) + TR(" м/с"), d.jetDv - dv < 20 ? CR : CW);
			const size_t n = h.prof.h.size();
			const double slope = std::atan(std::abs(h.prof.h[n - 1] - h.prof.h[n - 2]) / (D / (n - 1))) * DEG;
			g.Line(14, 254, 326, 254, CD, 1, 0.5, false, false);
			g.T(14, 272, TR("у цели склон ") + Num(slope, 0) + "° · " + (slope < 10 ? TR("садиться можно") : TR("круто, ищите ровнее")), slope < 10 ? CP : CA, 10.5);
			g.T(14, 290, (hop ? TR("прыжком до ") : TR("крейсер ")) + Num(vCruise, 0) + TR(" м/с · ") + (s->base ? TR("посадка на площадку") : TR("посадка рядом с целью")), CD, 9.5);
		}

		// МАШИНА: the platform she drives from its post - speed and its limit, the wheel, the platform's height, the brake,
		// the energy (charge, what is drawn or given back, what is left, how far it goes); the brake and the platform by the mouse
		// «СОЕДИНЕНИЯ»: what is plugged into the suit - the amplifier frame: its cassette, its draw, the suit's battery behind it
		void Links(Ctx& c)
		{
			Gfx& g = c.g; const HudData& d = c.d; const Frame* f = c.h.frame;
			if (!f) return;
			g.T(14, 22, TR("РАМА «УСИЛИТЕЛЬ»"), CW, 12);
			const double cap = (std::max)(1.0, f->battCap), q = f->Batt() / cap;
			const int cc = q < 0.15 ? CR : q < 0.3 ? CA : CP;
			g.T(14, 50, TR("КАССЕТА"), CD, 9.5);
			g.T(326, 50, Num(100 * q, 0) + " % · " + Num(f->Batt() / 3.6e6, 2) + " / " + Num(cap / 3.6e6, 1) + TR(" кВт·ч"), cc == CP ? CW : cc, 11, 2);
			g.Bar(14, 56, 312, 8, q, cc);
			g.T(14, 84, TR("РАСХОД"), CD, 9.5);
			g.T(326, 84, Num(f->DrawW(), 0) + " / " + Num(f->supplyW, 0) + TR(" Вт"), CW, 11, 2);
			g.Bar(14, 90, 312, 8, f->supplyW > 0 ? f->DrawW() / f->supplyW : 0, CP);
			g.T(14, 118, TR("ПИТАНИЕ"), CD, 9.5);
			const bool flat = f->Batt() <= 0;
			g.T(326, 118, !f->Live() ? std::string(TR("нет")) : f->fromSuit && flat ? TR("от скафандра") : TR("от кассеты"), f->Live() ? (flat ? CA : CW) : CR, 11, 2);
			if (flat) g.T(14, 136, f->Live() ? TR("кассета пуста: тянет скафандр") : TR("кассета пуста: рама стоит"), f->Live() ? CA : CR, 9.5);
			g.Line(14, 150, 326, 150, CD, 1, 0.5, false, false);
			// the load against what she lifts now
			g.T(14, 170, TR("ПОДЪЁМ"), CD, 9.5); g.T(326, 170, Num(d.liftKg, 0) + TR(" кг"), CW, 11, 2);
			g.T(14, 192, TR("В РУКАХ"), CD, 9.5);
			const double r = d.liftKg > 0 ? d.heldKg / d.liftKg : 0;
			g.T(326, 192, Num(d.heldKg, 0) + TR(" кг"), r > 0.85 ? CR : r > 0.5 ? CA : CW, 11, 2);
			g.Bar(14, 198, 312, 8, (std::min)(1.0, r), r > 0.85 ? CR : r > 0.5 ? CA : CP);
			g.Line(14, 220, 326, 220, CD, 1, 0.5, false, false);
			// the switch: the suit's battery behind the cassette
			g.T(14, 244, TR("БАТАРЕЯ СКАФАНДРА → РАМА"), CD, 9.5);
			const double x = 256, w = 70;
			if (f->link) { g.Rect(x, 230, w, 22, -1, 0, 0, CA, 1); g.T(x + w / 2, 245, TR("ВКЛ"), CK, 10.5, 1); }
			else { g.Rect(x, 230, w, 22, CP, 1, 0.7, CP, 0.08); g.T(x + w / 2, 245, TR("ВЫКЛ"), CP, 10.5, 1); }
			g.Hit(x, 230, w, 22, SuitHud::H_LINK, 0);
			if (!d.suit) g.T(14, 268, TR("без скафандра связи нет"), CA, 9.5);
		}

		void Machine(Ctx& c)
		{
			Gfx& g = c.g; const SuitHud& h = c.h; const MpuState& m = h.mpu;
			const double kmh = std::abs(m.speed) * 3.6, lim = m.speedMax * 3.6;
			g.T(14, 22, TR("СКОРОСТЬ"), CD, 9.5); g.T(326, 22, TR("ПРЕДЕЛ"), CD, 9.5, 2);
			g.T(14, 50, Num(kmh, kmh < 10 ? 1 : 0) + TR(" км/ч"), CW, 15);
			if (m.speed < -0.1) g.T(150, 50, TR("НАЗАД"), CA, 11);
			g.T(326, 50, Num(lim, 0) + TR(" км/ч"), m.full ? CA : CW, 13, 2);
			if (m.full) g.T(326, 66, TR("ПОЛНЫЙ ХОД"), CA, 9.5, 2);
			g.Bar(14, 74, 312, 8, lim > 0 ? kmh / lim : 0, m.full ? CA : CP);
			// the wheel: the marker off the centre line
			g.T(14, 104, TR("РУЛЬ"), CD, 9.5);
			g.Rect(70, 95, 256, 12, CD, 1, 0.6);
			g.Line(198, 92, 198, 110, CD, 1, 0.7, false, false);
			const double sx = 198 + std::clamp(m.steer, -1.0, 1.0) * 124;
			g.Rect(sx - 3, 96, 6, 10, -1, 0, 0, CP, 1);
			g.T(326, 124, std::abs(m.steer) < 0.05 ? std::string(TR("прямо")) : std::string(m.steer > 0 ? TR("вправо ") : TR("влево ")) + Num(100 * std::abs(m.steer), 0) + " %", CW, 10.5, 2);
			// the platform between its lowest and highest
			g.T(14, 146, TR("ПЛАТФОРМА"), CD, 9.5); g.T(326, 146, Num(m.deck, 2) + TR(" м"), CW, 11, 2);
			const double span = m.deckMax - m.deckMin;
			g.Bar(14, 152, 312, 8, span > 1e-3 ? (m.deck - m.deckMin) / span : 0, CP);
			g.Line(14, 174, 326, 174, CD, 1, 0.5, false, false);
			// the energy
			const int cc = m.charge < 0.15 ? CR : m.charge < 0.3 ? CA : CP;
			g.T(14, 192, TR("ЗАРЯД"), CD, 9.5);
			g.T(326, 192, Num(100 * m.charge, 0) + " % · " + Num(m.energyKWh, 1) + TR(" кВт·ч"), cc == CP ? CW : cc, 11, 2);
			g.Bar(14, 198, 312, 8, m.charge, cc);
			g.T(14, 226, TR("МОЩНОСТЬ"), CD, 9.5);
			g.T(326, 226, m.powerW < -50 ? TR("отдача ") + Num(-m.powerW / 1000, 1) + TR(" кВт") : Num((std::max)(0.0, m.powerW) / 1000, 1) + TR(" кВт"), m.powerW < -50 ? CA : CW, 11, 2);
			g.T(14, 244, TR("ЗАПАС ХОДА"), CD, 9.5);
			g.T(326, 244, m.rangeKm < 0 ? std::string(TR("стоит")) : Num(m.rangeKm, m.rangeKm < 10 ? 1 : 0) + TR(" км"), m.rangeKm >= 0 && m.rangeKm < 2 ? CA : CW, 11, 2);
			g.T(14, 262, TR("ТОРМОЗ"), CD, 9.5);
			g.T(326, 262, m.brake ? TR("стояночный") : TR("снят"), m.brake ? CA : CW, 11, 2);
			// the mouse: the parking brake and the platform (when the machine takes commands)
			if (MpuCmd())
			{
				auto button = [&](double x, double w, const char* n, bool on, int cmd)
				{
					if (on) { g.Rect(x, 278, w, 22, -1, 0, 0, CA, 1); g.T(x + w / 2, 293, n, CK, 10.5, 1); }
					else { g.Rect(x, 278, w, 22, CP, 1, 0.7, CP, 0.08); g.T(x + w / 2, 293, n, CP, 10.5, 1); }
					g.Hit(x, 278, w, 22, SuitHud::H_MPU, cmd);
				};
				button(14, 150, TR("СТОЯНОЧНЫЙ"), m.brake != 0, 1);
				button(176, 150, m.deck > 0.5 * (m.deckMin + m.deckMax) ? TR("ПЛАТФОРМА ▼") : TR("ПЛАТФОРМА ▲"), false, 2);
			}
			g.T(14, 322, TR("W/S ход · A/D руль · Shift полный · Пробел тормоз"), CD, 9.5);
			g.T(14, 338, TR("Caps Lock платформа · V вид на машину"), CD, 9.5);
		}

		void Flight(Ctx& c)
		{
			Gfx& g = c.g; const HudData& d = c.d; Grid(g);
			if (!d.jet) { Msg(g, TR("РАНЕЦ НЕ НАДЕТ")); return; }
			const bool hold = d.jetMode == 1 && !d.apMode;
			g.T(14, 18, TR("КРУИЗ · ВЫСОТУ И СКОРОСТЬ ДЕРЖИТ ПОМОЩНИК"), CD, 9.5);
			auto block = [&](double y, const char* name, const std::string& set, const std::string& now, int req0, const std::vector<const char*>& lab)
			{
				g.T(14, y, name, CD, 9.5);
				g.T(14, y + 28, set, hold ? CA : CD, 22);
				g.T(326, y + 28, TR("сейчас ") + now, CW, 12, 2);
				const double bw = 312.0 / lab.size() - 4;
				for (size_t i = 0; i < lab.size(); ++i)
				{
					const double bx = 14 + i * (bw + 4), by = y + 40;
					g.Hit(bx, by, bw, 22, SuitHud::H_AP, req0 + static_cast<int>(i));
					g.Rect(bx, by, bw, 22, CA, 1, 0.8, CA, 0.08);
					g.T(bx + bw / 2, by + 15, lab[i], CA, 10.5, 1);
				}
			};
			block(42, TR("ВЫСОТА НАД ГРУНТОМ"), Num(d.jetAltHold, 1) + TR(" м"), Num(d.alt, 1) + TR(" м"), SuitHud::AP_CRUISE, { "-10", "-1", "+1", "+10" });
			block(130, TR("СКОРОСТЬ ВПЕРЁД"), Num(hold ? d.jetSpd : d.jetVf, 1) + TR(" м/с"), Num(d.jetVf, 1) + TR(" м/с"), SuitHud::AP_CRUISE + 10, { "-5", "-1", TR("СТОП"), "+1", "+5" });
			g.Line(14, 214, 326, 214, CD, 1, 0.5, false, false);
			g.T(14, 232, hold ? TR("круиз включён · снос вбок гасится сам") : TR("круиз выключен · кнопка включит с текущими"), hold ? CP : CD, 10.5);
			g.T(14, 252, TR("W / S - скорость, 3 м/с за секунду"), CD, 9.5);
			g.T(14, 268, TR("A / D - курс · пробел / Ctrl - высота"), CD, 9.5);
			g.T(14, 284, TR("Shift - полный вектор вручную, круиз ждёт"), CD, 9.5);
		}

		void Landing(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; const HudData& d = c.d; Grid(g);
			const double cx = 110, cy = 128;
			g.T(14, 18, TR("ПОД НОГАМИ · ВИД СВЕРХУ"), CD, 9.5);
			for (double r : { 20.0, 45.0, 70.0 }) g.Circle(cx, cy, r, CP, 1, 0.4, -1, 0, true);
			g.Line(cx - 10, cy, cx + 10, cy, CA, 1.6); g.Line(cx, cy - 10, cx, cy + 10, CA, 1.6); g.Circle(cx, cy, 16, CA, 1.4);
			// drift, heading-up: 1 m/s = 40 units
			const V2 dv = MapXY(h.gsH.z, h.gsH.x, h.hdg, 40);
			const double ex = cx + (dv.first - 170), ey = cy + (dv.second - 170);
			g.Line(cx, cy, ex, ey, CW, 1.6); g.Circle(ex, ey, 3, CW, 1, 1, CW, 1);
			g.T(cx, cy + 88, TR("снос ") + Num(d.gs, 1) + TR(" м/с"), CW, 10, 1);
			// vertical speed with the injury limits
			auto Yv = [](double v) { return 30 + (2 - v) * 11.1; };
			g.Rect(236, Yv(0), 18, Yv(-1.5) - Yv(0), -1, 0, 0, CP, 0.3);
			g.Rect(236, Yv(-1.5), 18, Yv(-7) - Yv(-1.5), -1, 0, 0, CA, 0.3);
			g.Rect(236, Yv(-7), 18, Yv(-14) - Yv(-7), -1, 0, 0, CR, 0.3);
			g.Rect(236, Yv(-14), 18, Yv(-16) - Yv(-14), -1, 0, 0, CR, 0.6);
			g.Rect(236, Yv(2), 18, Yv(-16) - Yv(2), CD, 1, 0.8);
			for (double v : { 2.0, 0.0, -1.5, -7.0, -14.0 }) { g.Line(254, Yv(v), 262, Yv(v), CP, 1); g.T(266, Yv(v) + 4, Num(v, v == -1.5 ? 1 : 0, true), CD, 9.5); }
			g.T(300, Yv(-10.5), TR("травма"), CA, 9.5); g.T(300, Yv(-15) + 3, TR("гибель"), CR, 9.5);
			const double vs = std::clamp(d.vs, -16.0, 2.0);
			g.Tri(232, Yv(vs), -10, -7, -10, 7, CW); g.T(245, 22, TR("ВЕРТ м/с"), CD, 9.5, 1);
			g.Cell(14, 214, TR("ВЫСОТА"), Num(d.alt, 1) + TR(" м")); g.Cell(84, 214, TR("ВЕРТ"), Num(d.vs, 1, true) + TR(" м/с"), d.vs < -1.5 ? CA : CW);
			g.Cell(160, 214, TR("КАСАНИЕ"), d.vs < -0.05 ? "≈ " + Num(d.alt / -d.vs, 0) + TR(" с") : "—");
			// slope under her from the map grid
			double slope = 0;
			const auto& G = h.grid;
			if (G.n > 2 && !G.h.empty())
			{
				const int m = G.n / 2; const double step = 2 * G.half / (G.n - 1);
				slope = std::atan(std::hypot(G.h[m * G.n + m + 1] - G.h[m * G.n + m - 1], G.h[(m + 1) * G.n + m] - G.h[(m - 1) * G.n + m]) / (2 * step)) * DEG;
			}
			g.Line(14, 252, 222, 252, CD, 1, 0.5, false, false);
			g.T(14, 270, TR("уклон под ногами ") + Num(slope, 0) + "° " + (slope < 10 ? "✓" : TR("— круто")), slope < 10 ? CP : CA, 10.5);
			g.T(14, 288, TR("касание ≤ 1,5 м/с · травма > 7 · гибель > 14"), CD, 9.5);
		}

		void Approach(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			const SuitHud::Target* s = h.Selected();
			if (!s) { Msg(g, TR("ВЫБЕРИТЕ ЦЕЛЬ"), TR("на странице ЦЕЛИ или на карте")); return; }
			if (s->base) { Msg(g, TR("ЦЕЛЬ — БАЗА"), TR("к базе только ПЕРЕЛЁТ")); return; }
			VESSEL* o = oapiGetVesselInterface(s->h);
			OBJHANDLE ref = c.v->GetGravityRef();
			VECTOR3 rt, vt; o->GetRelativePos(ref, rt); o->GetRelativeVel(ref, vt);
			const VECTOR3 rb = -Unit(rt), hb = Unit(crossp(rt, vt)), vb = Unit(crossp(hb, -rb));   // R-bar down, V-bar along the motion
			const double x = dotp(s->rel, vb), z = dotp(s->rel, rb), vx = dotp(s->relV, vb), vz = dotp(s->relV, rb);
			const double tx = 200, ty = 86, sc = 100 / (std::max)(10.0, s->dist * 1.3);
			g.T(14, 18, TR("ОТНОСИТЕЛЬНОЕ ДВИЖЕНИЕ · ") + s->name, CD, 9.5);
			g.Line(10, ty, 330, ty, CD, 1, 0.6, true); g.Line(tx, 26, tx, 150, CD, 1, 0.6, true);
			g.T(326, ty - 5, TR("+V по ходу"), CD, 9, 2); g.T(tx + 4, 148, TR("вниз ↓"), CD, 9);
			const double side = x < 0 ? -1 : 1;
			g.Line(tx, ty, tx + side * 180, ty - 48, CA, 1, 0.45, true); g.Line(tx, ty, tx + side * 180, ty + 48, CA, 1, 0.45, true);
			if (30 * sc < 150) { g.Circle(tx, ty, 30 * sc, CR, 1, 0.7, -1, 0, true); g.T(tx + 30 * sc * 0.7 + 2, ty - 30 * sc * 0.7 - 2, TR("30 м"), CR, 9); }
			g.Rect(tx - 10, ty - 3, 20, 6, CW, 1.2, 1, CW, 0.2);
			const double mx = tx + x * sc, my = ty + z * sc;
			g.Line(mx, my, mx + vx * 60 * sc, my + vz * 60 * sc, CA, 1.6, 1, true);
			g.Fill({ { mx, my - 8 }, { mx + 6, my + 6 }, { mx, my + 2 }, { mx - 6, my + 6 } }, CW, 1);
			const double closing = -s->rate, tgo = closing > 0.01 ? s->dist / closing : -1;
			g.Cell(14, 162, TR("ДАЛЬН"), Dist(s->dist)); g.Cell(96, 162, TR("V СБЛИЖ"), Num(s->rate, 2, true), CA);
			g.Cell(186, 162, TR("ДО ЦЕЛИ"), Clock(tgo)); g.Cell(262, 162, TR("ΔV СИНХР"), Num(length(s->relV), 2));
			// range - closing rate corridor (log range)
			auto X = [](double r) { return 36 + 96 * std::log10(std::clamp(r, 1.0, 1000.0)); };
			auto Y = [](double v) { return 288 - 88 * std::clamp(v, 0.0, 1.0); };
			g.Line(36, 288, 326, 288, CD, 1, 0.7, false, false); g.Line(36, 196, 36, 288, CD, 1, 0.7, false, false);
			std::vector<V2> up, lo;
			for (double e = 0; e <= 3.0001; e += 0.1) { const double r = std::pow(10, e); up.push_back({ X(r), Y(r / 60) }); lo.push_back({ X(r), Y(r / 200) }); }
			std::vector<V2> band = up; band.insert(band.end(), lo.rbegin(), lo.rend());
			g.Fill(band, CP, 0.16); g.Poly(up, CP, 0.8, 0.5);
			std::vector<V2> hist; for (const auto& p : h.rrHist) hist.push_back({ X(p.first), Y(p.second) });
			g.Poly(hist, CW, 1.2, 0.7);
			g.Circle(X(s->dist), Y(closing), 4, CA, 1.6, 1, CA, 1);
			for (double r : { 1.0, 10.0, 100.0, 1000.0 }) { g.Line(X(r), 288, X(r), 292, CD, 1); g.T(X(r), 300, r >= 1000 ? TR("1 км") : Num(r, 0), CD, 8.5, 1); }
			for (double v : { 0.0, 0.5, 1.0 }) g.T(32, Y(v) + 3, Num(v, v > 0 ? 1 : 0), CD, 8.5, 2);
			g.T(326, 206, TR("КОРИДОР СКОРОСТИ"), CP, 9.5, 2);
		}

		void Dock(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			const SuitHud::Target* s = h.Selected();
			if (!s) { Msg(g, TR("ВЫБЕРИТЕ ЦЕЛЬ"), TR("на странице ЦЕЛИ или на карте")); return; }
			if (s->base) { Msg(g, TR("ЦЕЛЬ — БАЗА"), TR("к базе только ПЕРЕЛЁТ")); return; }
			VESSEL* o = oapiGetVesselInterface(s->h);
			VECTOR3 me; c.v->GetGlobalPos(me);
			VECTOR3 lp; o->Global2Local(me, lp);
			// the nearest port; without ports, the target's centre along the line to her
			VECTOR3 pos = _V(0, 0, 0), dir = Unit(lp), rot = _V(0, 1, 0);
			bool port = false; double best = 1e18;
			for (UINT i = 0; i < o->DockCount(); ++i)
			{
				VECTOR3 p, dd, r; o->GetDockParams(o->GetDockHandle(i), p, dd, r);
				const double dist = length(lp - p);
				if (dist < best) { best = dist; pos = p; dir = dd; rot = r; port = true; }
			}
			if (std::abs(dotp(dir, rot)) > 0.99) rot = std::abs(dir.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
			const VECTOR3 side = Unit(crossp(rot, dir)), upv = Unit(crossp(dir, side));
			const VECTOR3 rel = lp - pos;
			const double dx = dotp(rel, side), dy = dotp(rel, upv), dz = dotp(rel, dir);
			// her facing in the target's frame: she should look down the port axis (-dir)
			VECTOR3 fg; c.v->GlobalRot(_V(0, 0, 1), fg);
			MATRIX3 R; o->GetRotationMatrix(R);
			const VECTOR3 fl = tmul(R, fg);
			const double yaw = std::atan2(dotp(fl, side), -dotp(fl, dir)) * DEG, pit = std::atan2(dotp(fl, upv), -dotp(fl, dir)) * DEG;
			g.T(14, 18, port ? TR("УЗЕЛ ЦЕЛИ · ") + s->name : TR("У ЦЕЛИ НЕТ УЗЛОВ · ЦЕНТР"), port ? CA : CD, 9.5);
			const double cx = 140, cy = 132, sc = 60 / (std::max)(1.0, (std::min)(10.0, std::hypot(dx, dy) * 1.5 + 0.5));
			g.Circle(cx, cy, 100, CD, 1, 0.5); g.Circle(cx, cy, 60, CD, 1, 0.5); g.Circle(cx, cy, 20, CP, 1, 0.6, -1, 0, true);
			g.Line(cx - 100, cy, cx - 24, cy, CP, 1); g.Line(cx + 24, cy, cx + 100, cy, CP, 1); g.Line(cx, cy - 100, cx, cy - 24, CP, 1); g.Line(cx, cy + 24, cx, cy + 100, CP, 1);
			const double px = cx + std::clamp(-dx * sc, -95.0, 95.0), py = cy + std::clamp(dy * sc, -95.0, 95.0);
			g.Circle(px, py, 24, CA, 1.8); g.Line(px - 8, py, px + 8, py, CA, 1.4); g.Line(px, py - 8, px, py + 8, CA, 1.4);
			g.Line(cx, cy, px, py, CW, 1, 0.8, true);
			// range along the axis (log)
			auto Yr = [](double r) { return 246 - (std::log10(std::clamp(r, 0.1, 100.0)) + 1) * 54; };
			g.Line(290, Yr(100), 290, Yr(0.1), CD, 1, 0.8);
			for (double r : { 100.0, 10.0, 1.0, 0.1 }) { g.Line(290, Yr(r), 298, Yr(r), CP, 1); g.T(302, Yr(r) + 4, Num(r, r < 1 ? 1 : 0), CD, 9); }
			const double ax = std::abs(dz);
			g.Tri(288, Yr(ax), -10, -7, -10, 7, CA); g.T(274, Yr(ax) + 4, Num(ax, ax < 10 ? 2 : 1), CW, 11, 2);
			g.T(274, Yr(ax) + 20, Num(s->rate, 2, true) + TR(" м/с"), std::abs(s->rate) > 0.3 && ax < 5 ? CR : CA, 10, 2);
			g.T(290, 26, TR("Д, м"), CD, 9.5, 1);
			g.Cell(14, 250, "ΔX", Num(dx, 2, true) + TR(" м")); g.Cell(84, 250, "ΔY", Num(dy, 2, true) + TR(" м"));
			g.Cell(154, 250, TR("ТАНГАЖ"), Num(pit, 1, true) + "°"); g.Cell(224, 250, TR("РЫСК"), Num(yaw, 1, true) + "°");
		}

		void Body(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; const HudData& d = c.d; Grid(g);
			auto graph = [&](double y, const std::string& name, const std::string& val, const std::vector<double>& q, double lo, double hi, double nlo, double nhi, int col)
			{
				g.T(14, y, name, CD, 9.5); g.T(326, y, val, col, 12, 2);
				g.Rect(14, y + 6, 312, 34, CD, 1, 0.4);
				auto Y = [&](double v) { return y + 40 - 34 * std::clamp((v - lo) / (hi - lo), 0.0, 1.0); };
				g.Rect(15, Y(nhi), 310, Y(nlo) - Y(nhi), -1, 0, 0, CP, 0.1);
				std::vector<V2> pts;
				for (size_t i = 0; i < q.size(); ++i) pts.push_back({ 326 - (q.size() - 1 - i) * 5.2, Y(q[i]) });
				g.Poly(pts, col, 1.4);
			};
			graph(18, TR("ПУЛЬС · 10 мин"), Num(d.pulse, 0) + TR(" уд/мин"), h.trend.pulse, 40, 200, 55, 120, d.pulse > 170 ? CA : CP);
			graph(64, TR("ДЫХАНИЕ"), Num(d.breath, 0) + TR(" в мин"), h.trend.breath, 5, 50, 10, 25, CP);
			graph(110, TR("ТЕМПЕРАТУРА ТЕЛА"), Num(d.coreC, 1) + " °C", h.trend.core, 34, 41, 36.2, 37.8, d.coreC > 38.5 || d.coreC < 35.8 ? CA : CP);
			// below the graphs: radiation, the state, what is wrong
			g.Line(14, 160, 326, 160, CD, 1, 0.5, false, false);
			{
				// radiation: the rate now and the dose of this outing against the 30-day limit (250 mSv); time left to it.
				// Colours by the same marks as the alerts: rate amber from 1 mSv/h, red from 10; dose amber from 100 mSv, red from 250
				const double rate = d.radRate * 1000, dose = d.radDose * 1000, L30 = 250;   // mSv/h, mSv
				const int rc = rate >= 10 ? CR : rate >= 1 ? CA : CW, dc = dose >= L30 ? CR : dose >= 100 ? CA : CW;
				g.T(14, 176, TR("РАДИАЦИЯ"), CD, 9.5);
				g.T(326, 176, (rate < 1 ? Num(rate * 1000, 0) + TR(" мкЗв/ч") : Num(rate, 2) + TR(" мЗв/ч")) + (d.fieldOn ? TR(" · поле") : ""), rc, 11, 2);
				g.T(14, 194, TR("ДОЗА ЗА ВЫХОД"), CD, 9.5);
				g.T(326, 194, Num(dose, dose < 10 ? 2 : 0) + TR(" мЗв из ") + Num(L30, 0), dc, 10.5, 2);
				g.Bar(14, 200, 312, 7, dose / L30, dc == CW ? CP : dc);
				const double left = rate > 1e-4 ? (L30 - dose) / rate : 1e9;
				if (left < 1e5) g.T(14, 220, TR("до предела: ") + (left > 48 ? Num(left / 24, 0) + TR(" сут") : Num(left, 1) + TR(" ч")), left < 2 ? CR : left < 24 ? CA : CD, 9.5);
			}
			{
				// the state in words; injuries only if there are any; water and food only near their marks
				const char* PART[4] = { TR("голова"), TR("корпус"), TR("руки"), TR("ноги") };
				std::string hurts; bool bad = false;
				for (int i = 0; i < 4; ++i) if (d.hurt[i] > 0.05) { hurts += std::string(hurts.empty() ? "" : ", ") + PART[i] + (d.hurt[i] > 0.5 ? TR(" (тяжело)") : ""); bad = bad || d.hurt[i] > 0.5; }
				const double dehyd = d.bodyMass > 0 ? d.waterDef / d.bodyMass : 0;
				std::string st = d.state == 2 ? (d.life && d.life->Heavy() ? TR("ПУЛЬС 0") : TR("ГИБЕЛЬ")) : d.state == 1 ? TR("БЕЗ СОЗНАНИЯ") : !hurts.empty() ? (bad ? TR("ТЯЖЁЛЫЕ ТРАВМЫ") : TR("ТРАВМЫ")) : d.stamina < 0.15 ? TR("ИСТОЩЕНА") : dehyd > 0.04 || d.fastDays > 3 ? TR("ОСЛАБЛЕНА") : TR("В НОРМЕ");
				const int sc = d.state ? CR : bad ? CR : !hurts.empty() || st != TR("В НОРМЕ") ? CA : CP;
				const double keepR = g.clipR;
				g.Line(14, 228, 326, 228, CD, 1, 0.4, false, false);
				g.T(14, 240, TR("СОСТОЯНИЕ"), CD, 9.5); g.T(326, 240, st, sc, 12, 2);
				double y = 258;
				if (!hurts.empty()) { g.T(14, y, hurts, bad ? CR : CA, 10); y += 16; }
				g.T(14, y, TR("нагрузка ") + Num(100 * d.effort, 0) + " % · CO2 " + Num(d.ppCO2, 2) + TR(" кПа"), d.ppCO2 > 1 ? CA : CD, 9.5); y += 16;
				std::string need;
				if (d.suit && d.water < 0.3) need += TR("вода в скафандре ") + Num(d.water, 1) + TR(" л");
				if (dehyd > 0.02) need += std::string(need.empty() ? "" : " · ") + TR("обезвоживание ") + Num(100 * dehyd, 1) + " %";
				if (d.fastDays > 2) need += std::string(need.empty() ? "" : " · ") + TR("без еды ") + Num(d.fastDays, 1) + TR(" сут");
				if (!need.empty()) g.T(14, y, need, dehyd > 0.04 || d.fastDays > 3 ? CA : CD, 9.5);
				g.clipR = keepR;
			}
		}
	}

	// ================= the display =================
	void SuitHud::Mouse(double, double)
	{
		DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
		const double W = vw, H = vh;
		// the left button, caught on its press; only inside Orbiter's own window and only if that window is the view
		// (its proportions match the display), mapped to the display's pixels
		const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (down && !mouseWasDown && W > 0 && H > 0)
		{
			POINT p; GetCursorPos(&p);
			HWND w = WindowFromPoint(p); DWORD pid = 0;
			if (w) GetWindowThreadProcessId(w, &pid);
			RECT rc{};
			if (w && pid == GetCurrentProcessId() && ScreenToClient(w, &p) && GetClientRect(w, &rc) && rc.right > 0 && rc.bottom > 0 &&
				std::abs(static_cast<double>(rc.right) / rc.bottom - W / H) < 0.03)
				Click(p.x * W / rc.right, p.y * H / rc.bottom);
		}
		mouseWasDown = down;
	}

	// ================= «Буран»: the light suit's lower plates =================
	// the four plates in view pixels: 0 the autopilot's MFD (upper left), 1 the map's MFD (upper right), both at eye level
	// below the brow's scales; 2 the reserves' counters (lower left), 3 the mechanization (lower right)
	void SuitHud::BuranRects(double W, double H, double r[4][4]) const
	{
		const double k = H / 720.0, R0 = W - 1280 * k, mw = 340 * 0.85, mh = 400 * 0.85;
		const double q[4][4] = { { 16, 176, mw, mh }, { 1264 - mw, 176, mw, mh }, { 30, 532, 300, 150 }, { 1256 - 344, 510, 344, 182 } };
		for (int i = 0; i < 4; ++i)
		{
			const double ox = (i == 1 || i == 3) ? R0 : 0.0;
			r[i][0] = ox + q[i][0] * k; r[i][1] = q[i][1] * k; r[i][2] = r[i][0] + q[i][2] * k; r[i][3] = r[i][1] + q[i][3] * k;
		}
	}

	namespace
	{
		// the reserves (lower left): three rings - the battery, the oxygen, the pack's fuel (without the pack: the CO2
		// sorbent) - each with its share, how long it lasts and how fast it goes; the rest in one line
		void BuranCounters(Ctx& c, bool glass = false)   // glass: the flat panel on the glass (CNT_W wide, the rings 64 apart)
		{
			Gfx& g = c.g; const HudData& d = c.d;
			g.T(8, 14, TR("ЗАПАСЫ · РАСХОД"), CD, 9.5);
			g.T(glass ? CNT_W - 8 : 292, 14, d.jet ? "CO2 " + Num(100 * d.sorbent, 0) + " % · " + Hours(d.sorbHours) : std::string(), CD, 9, 2);
			struct Ring { const char* name; double f; std::string time, rate; };
			const double fly = d.jet && d.jetFlying;
			const Ring rings[4] = {
				{ TR("БАТ"), d.batt, Hours(d.battHours), "−" + Num(d.powerW, 0) + TR(" Вт") },
				{ "O2", d.o2, d.o2Vent ? std::string(TR("воздух")) : Hours(d.o2Hours), d.o2Vent ? std::string("—") : "−" + Num(d.o2Flow * 60000, 2) + TR(" г/мин") },
				d.jet ? Ring{ TR("ТОПЛ"), d.jetFuel, Num(d.jetDv, 0) + TR(" м/с"), fly ? "−" + Num(d.jetFlow * 1000, 0) + TR(" г/с") : (d.jetHover > 0 ? TR("висение ") + Clock(d.jetHover) : std::string("—")) }
				      : Ring{ "CO2", d.sorbent, Hours(d.sorbHours), std::string(TR("поглотитель")) },
				Ring{ "N2", d.n2, Num(d.n2Dv, 1) + TR(" м/с"), std::string(TR("РСУ")) } };
			for (int i = 0; i < 4; ++i)
			{
				const Ring& r = rings[i];
				const double cx = glass ? 34 + i * 64 : 38 + i * 75, cy = 64;
				const double R = 26, f = std::clamp(r.f, 0.0, 1.0);
				const int col = f < 0.1 ? CR : f < 0.25 ? CA : CP;
				std::vector<V2> track, fill;
				for (int j = 0; j <= 36; ++j) { const double a = PI * 0.75 + PI * 1.5 * j / 36; track.push_back({ cx + R * std::cos(a), cy + R * std::sin(a) }); }
				const int nf = static_cast<int>(std::lround(36 * f));
				for (int j = 0; j <= nf; ++j) { const double a = PI * 0.75 + PI * 1.5 * j / 36; fill.push_back({ cx + R * std::cos(a), cy + R * std::sin(a) }); }
				// the empty part a hairline, the reserve a thick arc ending in a tick across the ring: where it ends is seen at
				// a glance; low (a quarter, a tenth) - the whole counter turns amber or red, not just the arc
				const bool low = col != CP;
				g.Poly(track, low ? col : CD, 0.8, low ? 0.6 : 0.4);
				if (fill.size() > 1) g.Poly(fill, col, 4.2, 1);
				{
					const double a = PI * 0.75 + PI * 1.5 * f, ca = std::cos(a), sa = std::sin(a);
					g.Line(cx + (R - 6) * ca, cy + (R - 6) * sa, cx + (R + 6) * ca, cy + (R + 6) * sa, low ? col : CW, 2.0);
				}
				g.T(cx, cy + 5, Num(100 * f, 0), low ? col : CW, 15, 1);
				g.T(cx, cy + 18, r.name, low ? col : CD, 8.5, 1);
				g.T(cx, cy + 44, r.time, low ? col : CW, 10.5, 1);
				g.T(cx, cy + 58, r.rate, low ? col : CD, 9, 1);
			}
		}

		// the mechanization (lower right): all twelve of the suit's switches in two columns - lit = on - each a click to
		// switch it; the key beside it where it has one; what this suit has not got (the pack's controls without the pack)
		// stays dim with a dash
		void BuranMech(Ctx& c, bool big = false)   // big: on the glass, one column (MECH_W x MECH_H)
		{
			Gfx& g = c.g; const HudData& d = c.d; SuitHud& h = c.h;
			struct M { std::string name; bool on, has; int kind, arg; const char* key; int col; };
			static const char* const BEAM[3] = { "шир", "раб", "дал" };
			const bool helmOpen = d.life && d.life->helmetOpen;
			const M m[12] = {
				{ TR("МОДУЛЬ ПОЛЁТА"), d.jet, true, -1, 0, "B", -1 },
				{ TR("СВЕТ"), d.lampsOn, true, SuitHud::H_AP, SuitHud::AP_LAMP, "L", -1 },
				{ std::string(TR("ЛУЧ ")) + TR(BEAM[std::clamp(d.lampMode, 0, 2)]), d.lampsOn, true, SuitHud::H_AP, SuitHud::AP_LAMPMODE, "⇧L", -1 },
				{ TR("ПОЛЕ"), d.fieldOn, true, SuitHud::H_AP, SuitHud::AP_FIELD, "", -1 },
				{ d.life && d.life->Heavy() ? TR("ШТОРКА") : TR("ЩИТОК"), d.life && d.life->Heavy() ? !d.pzWindow : d.shadeDown, true, SuitHud::H_AP, SuitHud::AP_SHADE, "⇧V", -1 },   // the heavy suit: its louvres (on = closed, the cameras)
				{ helmOpen ? TR("ШЛЕМ ОТКР") : TR("ШЛЕМ"), d.life && !helmOpen, d.life != nullptr, SuitHud::H_AP, SuitHud::AP_HELMET, "", helmOpen ? CA : -1 },
				{ TR("ПНВ"), h.nvg, true, SuitHud::H_NVG, 0, "", -1 },
				{ TR("H-АЛЬФА"), h.sunHAlpha, true, SuitHud::H_SUNF, 0, "", -1 },
				{ TR("СЕРВО"), d.life && d.life->drivesOn, d.life != nullptr, SuitHud::H_SUIT, 0, "", -1 },
				{ TR("ЭКОНОМ"), d.life && d.life->econ, d.life != nullptr, SuitHud::H_SUIT, 1, "", -1 },
				{ TR("ПОМОЩНИК"), d.jet && d.jetAssist, d.jet, SuitHud::H_AP, SuitHud::AP_MANUAL, "", -1 },
				{ TR("ОГРАНИЧ."), d.jet && d.jetFine, d.jet, SuitHud::H_AP, SuitHud::AP_FINE, "Caps", -1 } };
			int on = 0; for (const M& x : m) if (x.on && x.has) ++on;
			g.T(8, 14, TR("МЕХАНИЗАЦИЯ"), CD, big ? 11 : 9.5);
			g.T(big ? MECH_W - 8 : 336, 14, std::to_string(on) + " / 12", CP, big ? 11 : 9.5, 2);
			for (int i = 0; i < 12 && big; ++i)
			{
				const M& x = m[i];
				const double cx = 8, cy = 46 + i * 26;
				const int col = !x.has ? CD : x.col >= 0 ? x.col : x.on ? CP : CD;
				if (*x.key) g.T(cx + 16, cy, x.key, CD, 11, 2);
				if (x.on && x.has) g.Rect(cx + 21, cy - 12, 12, 12, -1, 0, 0, col, 1); else g.Rect(cx + 21, cy - 12, 12, 12, CD, 1.2, x.has ? 0.8 : 0.35);
				g.T(cx + 40, cy, x.has ? x.name : x.name + " —", col, 15);
				if (x.has && x.kind >= 0) g.Hit(cx - 2, cy - 19, MECH_W - 12, 25, x.kind, x.arg);
			}
			if (big) { g.T(8, MECH_H - 4, TR("щелчок — вкл / выкл"), CD, 9.5); return; }
			for (int i = 0; i < 12; ++i)
			{
				const M& x = m[i];
				const double cx = 8 + (i / 6) * 170, cy = 42 + (i % 6) * 23;
				const int col = !x.has ? CD : x.col >= 0 ? x.col : x.on ? CP : CD;
				if (*x.key) g.T(cx + 16, cy, x.key, CD, 10, 2);   // the key, then the lamp, then the name
				if (x.on && x.has) g.Rect(cx + 21, cy - 10, 9, 9, -1, 0, 0, col, 1); else g.Rect(cx + 21, cy - 10, 9, 9, CD, 1, x.has ? 0.8 : 0.35);
				g.T(cx + 36, cy, x.has ? x.name : x.name + " —", col, 12.5);
				if (x.has && x.kind >= 0) g.Hit(cx - 2, cy - 15, 166, 21, x.kind, x.arg);
			}
			g.T(8, 178, TR("щелчок — вкл / выкл"), CD, 8.5);
		}
	}

	// the two lower plates of «Буран», each a plate turned to the eye
	void BuranLow(Ctx& c, double W, double H, double k, double L0, double R0)
	{
		(void)L0; (void)R0;
		double r[4][4]; c.h.BuranRects(W, H, r);
		for (int i = 2; i < 4; ++i)
		{
			if (c.h.glassPass)   // on the glass: the rings along the bottom edge, the mechanization a column down the right
			{
				if (i == 2) { SetDecal(c, r[i][0], r[i][1], CNT_W * k, CNT_H * k, CNT_YAW, CNT_PITCH, CNT_ROLL, 6, CNT_F); c.g.Frame(r[i][0], r[i][1], k); BuranCounters(c, true); }
				else { SetDecal(c, r[i][0], r[i][1], MECH_W * k, MECH_H * k, MECH_YAW, MECH_PITCH, MECH_ROLL, 6, MECH_F); c.g.Frame(r[i][0], r[i][1], k); BuranMech(c, true); }
				c.g.NoPlate();
				continue;
			}
			if (!SetPlate(c, r[i][0], r[i][1], r[i][2] - r[i][0], r[i][3] - r[i][1], 2)) continue;
			c.g.Frame(r[i][0], r[i][1], k);
			if (i == 2) BuranCounters(c); else BuranMech(c);
			c.g.NoPlate();
		}
	}

	void SuitHud::Draw(oapi::Sketchpad* skp, double SW, double SH, const HudData& d, VESSEL* v)
	{
		if (!glyphsTried) { glyphsTried = true; glyphs.Load(); }
		if (!prefsLoaded) LoadPrefs();
		// the display is the suit's: it goes dark only without power (and, when it is modelled, when it is damaged)
		if (!d.powered) { if (nvCam && nvg) Core()->CustomCameraOnOff(nvCam, false); return; }
		// the helmet's display is seen only from inside her helmet: with the camera on anything else (a machine's view from
		// its post, an outside view) nothing of it is drawn
		if (!v || ((!oapiCameraInternal() || oapiCameraTarget() != v->GetHandle()) && !glassExtPass)) { if (nvCam && nvg) Core()->CustomCameraOnOff(nvCam, false); return; }
		// the layout is the viewport's; the surface may be another size (the helmet display's square texture)
		DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
		const double W = vw > 0 ? vw : SW, H = vh > 0 ? vh : SH;
		const double k = H / 720.0;
		const double L0 = 0, C0 = W / 2 - 640 * k, R0 = W - 1280 * k;
		if (onVisor && v && d.firstPerson && !drawingLight) PlaceVisor(v);   // the camera is final now: the plate follows it exactly
		if (!drawingLight && lightLive && onVisor)
		{
			if (!oapiGetPause()) return;   // the light layer carries the HUD: the VC HUD texture stays empty, the buttons stay
			// paused: no steps, the light layer cannot be redrawn - the HUD goes through the VC HUD until the run resumes
			lightLive = false;
			if (lightOut) oapiClearSurface(lightOut, 0);
			if (lightCv && gx && gx->Begin(lightCv, true, 0)) gx->End(lightCv);   // the native light layer cleared too
		}
		if (!glassPass) { hits.clear(); ConformalHud(v, d); glassLines.clear(); }   // the helmet HUD's parts drawn in the scene by the AtmoFX client (contours, pad, gates)
		lastShade = d.shade; lastSunlit = d.sunlit; life = d.life; frame = d.frame;
		{
			const bool was = onMpu;
			mpuPerson = v ? v->GetHandle() : nullptr;
			onMpu = mpuPerson && MpuOf(mpuPerson, mpu);
			if (onMpu && !was) { rpageBeforeMpu = rpage; rpage = R_MACHINE; }
			if (!onMpu && was && rpage == R_MACHINE) rpage = rpageBeforeMpu >= 0 ? rpageBeforeMpu : R_TARGETS;
		}
		// the heavy suit «Панцирь» has its own computer on the same system (HEAVY_SUIT.md); the light suit (Каркас and its kinds)
		// draws the «Буран» layout; the layout it had before is kept below as legacy, under !bu, and not drawn for it
		const bool pz = d.suit && d.life && d.life->Heavy();
		const bool bu = d.suit && !pz;
		Gfx g = bu ? Gfx(skp, *this, glyphs, BURAN_PAL[std::clamp(hudTheme, 0, 3)], BURAN_LOOK[std::clamp(hudTheme, 0, 3)], false) : Gfx(skp, *this, glyphs, pal, look);
		g.kx = SW / W; g.ky = SH / H;
		if (glassPass)   // the glass canvas: its pixels per view pixel at the panels' reference distance
		{ const double Fref = (H / 2) / std::tan(GLASS_FREF_HALF); g.kx = GLASS_CW / (2 * GLASS_YW) / Fref; g.ky = GLASS_CH / (GLASS_PT - GLASS_PB) / Fref; }
		if (d.life && d.life->optDark > 0.002)
		{
			const double D = d.life->optDark;
			g.dimR = 1 - 0.35 * D; g.dimG = 1 - 0.55 * D; g.dimB = 1 - 0.9 * D;
			if (!g.s3) g.s3 = g.s2;   // the text is dimmed through the sketchpad's brightness
		}
		if (g.look == 1) { const double r = std::sin(oapiGetSimTime() * 1234.567 + oapiGetSysTime() * 789.1) * 43758.5453; g.shimmer = 0.94 + 0.06 * (r - std::floor(r)); }
		Ctx c{ g, d, *this, v, W, H, k, std::fmod(d.simt, 1.0) < 0.62 };
		// (pz: the Pantsir's analogue scales on the rim, its control panel and the consumption low beside two smaller MFDs,
		// a fixed shading top and bottom - every Pantsir branch below is under that flag)
		const bool fly = d.jet && d.jetFlying;
		// the light suit with the glass layer: everything but the scene's own signs (the horizon, the flight path, the
		// target, the tapes) is drawn on the helmet's glass, in the glass pass
		const bool glassItems = bu && GlassOn() ? glassPass : !glassPass;
		// the layout's place on the glass (the glass pass): the top arc on equal angles, raised into the helmet's crown;
		// the middle (the horizon, the tapes, the flight path) in perspective on the helmet's axis - conformal from the eye
		auto glassBase = [&](int mode)   // 0 the middle (perspective, conformal), 1 the top band, 2 the brow's scales, 3 the low lines
		{
			const double Fv = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture())), Fr = (H / 2) / std::tan(GLASS_FREF_HALF);
			g.hv = v; g.hitW = W; g.hitH = H; g.hitF = Fv;
			g.baseOn = false; g.NoPlate();
			double q[9];
			const bool seen = mode == 0 && HelmDir(v, 0, 0, q) && g.PlateAt(0, 0, W, H, W, H, Fv, q, Fr);
			g.Glass(0, 0, W, H, Fr, 0, 0, GLASS_CW, GLASS_CH, GLASS_YW, GLASS_PT, GLASS_PB);
			if (mode == 1 || mode == 2)   // the top band, the brow's scales with it: one flat panel (TOP_PC, TOP_F)
			{
				g.Glass(0, 0, W, H, Fr, 0, TOP_PC * RAD, GLASS_CW, GLASS_CH, GLASS_YW, GLASS_PT, GLASS_PB);
				g.glLin = 6; g.glF = TOP_F * k; g.glCx = W / 2; g.glCy = 68 * k;
			}
			else if (mode)
			{
				g.glLin = mode; g.glC0 = C0; g.glK = k; g.glCx = W / 2;
				g.glCy = 652 * k; g.glP0 = BOT_P0 * RAD;
			}
			g.noHit = mode == 0 ? !seen : false; g.SetBase();
		};
		// the helmet's frame as the "camera" of the glass pass (the middle's conformal signs), else the camera's own
		auto viewRot = [&](MATRIX3& M)
		{
			oapiCameraRotationMatrix(&M);
			if (!glassPass || !helmOk) return;
			MATRIX3 Rv; v->GetRotationMatrix(Rv);
			const VECTOR3 r = mul(Rv, helmX), u = mul(Rv, helmY), f = mul(Rv, helmZ);
			M = _M(r.x, u.x, f.x, r.y, u.y, f.y, r.z, u.z, f.z);
		};
		const double viewF = glassPass ? (H / 2) / std::tan(GLASS_FREF_HALF) : (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
		if (!glassPass) {   // (the view's pass: the state, the shading)
		Nav(v, d);
		Alerts(d);

		// automatic mode: what she is doing decides the pages until a mode key is pressed
		if (autoMode)
		{
			int m = EVA;
			const Target* s = Selected();
			if (d.jetFlying && surface) m = FLIGHT;
			else if (space && s && s->dist < 5000) m = RDV;
			if (m != mode) SetMode(m, false);
		}

		// how bright it is around her: in the sun, over a lit ground, looking towards the sun
		{
			const double t = oapiGetSimTime();
			double amb = 0.1;
			if (d.sunlit)
			{
				VECTOR3 cd, cp, sp; oapiCameraGlobalDir(&cd); oapiCameraGlobalPos(&cp); oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sp);
				const double toSun = (std::max)(0.0, dotp(cd, Unit(sp - cp)));
				amb = surface ? 0.45 + 0.4 * std::clamp(std::sin(sunElev) * 2, 0.0, 1.0) + 0.15 * toSun : 0.35 + 0.65 * toSun * toSun;
			}
			const double dt = lastAmbT < 0 || t < lastAmbT ? 1.0 : t - lastAmbT; lastAmbT = t;
			ambient += (std::clamp(amb, 0.0, 1.0) - ambient) * (std::min)(1.0, dt / 1.5);   // eyes and sensor adapt in a second or two
			g.boost = brightAuto ? ambient : brightManual;
		}
		if (d.suit && d.firstPerson)
		{
			if (nvg) { if (g.cv) NightVisionNative(v, W, H, SW, SH); else NightVision(skp, v, W, H, SW, SH); }
			if (nvg && surface && hBody)
			{
				// the ranger's relief: rings and spokes of the ground round her, drawn into the view through the camera;
				// brighter near, fading out by ~300 m. The image amplifier above needs some light; this needs none
				Relief& rl = relief;
				const double t = oapiGetSimTime(), R = bodyR;
				if (rl.body != hBody || rl.t < 0 || t - rl.t > 2 || t < rl.t || std::hypot((lat - rl.lat) * R, (lng - rl.lng) * R * std::cos(lat)) > 3)
				{
					rl.body = hBody; rl.lat = lat; rl.lng = lng; rl.t = t; rl.nr = 22; rl.na = 40;
					rl.lat_.assign(rl.nr * rl.na, 0); rl.lng_.assign(rl.nr * rl.na, 0); rl.rad_.assign(rl.nr * rl.na, 0);
					for (int k = 0; k < rl.nr; ++k)
						for (int a = 0; a < rl.na; ++a)
						{
							const double r = 3 * std::pow(1.25, k), az = a * PI2 / rl.na;
							const double pl = lat + r * std::cos(az) / R, pg = lng + r * std::sin(az) / (R * (std::max)(0.05, std::cos(lat)));
							const int i = k * rl.na + a;
							rl.lat_[i] = pl; rl.lng_[i] = pg; rl.rad_[i] = R + oapiSurfaceElevation(hBody, pg, pl) + 0.05;
						}
				}
				VECTOR3 cp; oapiCameraGlobalPos(&cp); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
				const double fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
				std::vector<V2> sp(rl.lat_.size()); std::vector<char> ok(rl.lat_.size());
				for (size_t i = 0; i < sp.size(); ++i)
				{
					VECTOR3 gp; oapiEquToGlobal(hBody, rl.lng_[i], rl.lat_[i], rl.rad_[i], &gp);
					const VECTOR3 q = tmul(Rc, gp - cp);
					ok[i] = q.z > 0.3; if (ok[i]) sp[i] = { W / 2 + q.x / q.z * fpx, H / 2 - q.y / q.z * fpx };
				}
				g.Frame(0, 0, 1);
				for (int k = 0; k < rl.nr; ++k)
				{
					const double al = 0.85 * std::pow(1.0 - static_cast<double>(k) / rl.nr, 0.8);
					for (int a = 0; a < rl.na; ++a)
					{
						const int i = k * rl.na + a, j = k * rl.na + (a + 1) % rl.na, o = (k + 1) * rl.na + a;
						if (ok[i] && ok[j]) g.Line(sp[i].first, sp[i].second, sp[j].first, sp[j].second, CP, 1, al, false, false);
						if (k + 1 < rl.nr && ok[i] && ok[o]) g.Line(sp[i].first, sp[i].second, sp[o].first, sp[o].second, CP, 1, al * 0.7, false, false);
					}
				}
			}
			// the sun shade: a gold filter against bright sources; the brighter it is around, the more it takes. On the helmet
			// display (light added to the view) a filter cannot be drawn: there the shade is its own glass
			if (d.shade > 0.02 && g.s2 && !onVisor)
			{
				const DWORD a = static_cast<DWORD>((0x40 + 0x50 * g.boost) * std::clamp(d.shade, 0.0, 1.0));
				g.s2->QuickPen(0); g.s2->QuickBrush((a << 24) | 0x00081830);   // 0xAABBGGRR: dark amber
				skp->Rectangle(0, 0, static_cast<int>(SW), static_cast<int>(SH)); g.s2->QuickBrush(0);
			}
		}
		else if (nvCam && nvg) Core()->CustomCameraOnOff(nvCam, false);

		// a dark tint behind the display: soft bands along the visor's upper and lower edge, and feathered backings under
		// the two side panels - no hard boxes. The brighter around, the denser (lines stay readable on a sunlit ground)
		if (pz && !onVisor)   // the Pantsir: a fixed vignette - solid above the rim's scales and below the panels' tops, soft edges
		{
			g.Frame(0, 0, 1); g.world = true;
			const int N = 32;
			g.Rect(0, 0, W, 47 * k, -1, 0, 0, CK, 0.88);
			g.Rect(0, 490 * k, W, H - 490 * k, -1, 0, 0, CK, 0.88);
			for (int i = 0; i < N; ++i)
			{
				const double f = 0.5 + 0.5 * std::cos(PI * (i + 0.5) / N);   // 1 at the solid band .. 0 inside
				g.Rect(0, (47 + i * 60.0 / N) * k, W, std::ceil(60.0 / N * k) + 1, -1, 0, 0, CK, 0.88 * f);
				g.Rect(0, (490 - (i + 1) * 60.0 / N) * k, W, std::ceil(60.0 / N * k) + 1, -1, 0, 0, CK, 0.88 * f);
			}
			g.world = false;
		}
		else if (bu && g.boost > 0.05 && !onVisor)   // «Буран» on the screen: the bands and soft backings under its four plates
		{
			const double a = 0.10 + 0.40 * (std::min)(1.0, g.boost);
			g.Frame(0, 0, 1); g.world = true;
			const int N = 48; const double topH = 140 * k, botH = 115 * k;
			for (int i = 0; i < N; ++i)
			{
				const double x = (i + 0.5) / N, f = 0.5 + 0.5 * std::cos(PI * x);
				g.Rect(0, std::floor(i * topH / N), W, std::ceil(topH / N) + 1, -1, 0, 0, CK, 0.85 * a * f);
				g.Rect(0, H - std::floor((i + 1) * botH / N), W, std::ceil(botH / N) + 1, -1, 0, 0, CK, 0.7 * a * f);
			}
			double r[4][4]; BuranRects(W, H, r);
			for (auto& q : r) for (int i = 7; i >= 0; --i) { const double e = i * 3 * k; g.Rect(q[0] - e, q[1] - e, q[2] - q[0] + 2 * e, q[3] - q[1] + 2 * e, -1, 0, 0, CK, a * 0.13); }
			g.world = false;
		}
		else if (d.suit && g.boost > 0.05 && !onVisor)   // projected light cannot darken: no backing on the helmet display
		{
			const double a = 0.10 + 0.40 * (std::min)(1.0, g.boost);
			g.Frame(0, 0, 1); g.world = true;
			// thin strips (a few pixels each), cosine fall-off: no visible steps
			const int N = 48;
			const double topH = 140 * k, botH = 115 * k;
			for (int i = 0; i < N; ++i)
			{
				const double x = (i + 0.5) / N, f = 0.5 + 0.5 * std::cos(PI * x);           // 1 at the edge .. 0 inside
				g.Rect(0, std::floor(i * topH / N), W, std::ceil(topH / N) + 1, -1, 0, 0, CK, 0.85 * a * f);
				g.Rect(0, H - std::floor((i + 1) * botH / N), W, std::ceil(botH / N) + 1, -1, 0, 0, CK, 0.7 * a * f);
			}
			auto soft = [&](double x, double y, double w, double h)
			{
				for (int i = 7; i >= 0; --i) g.Rect(x - i * 2, y - i * 2, w + i * 4, h + i * 4, -1, 0, 0, CK, a * 0.13);
			};
			g.world = false;
			g.Frame(L0, 0, k); soft(18, 166, 242, 170);
			g.Frame(R0, 0, k); soft(1020, 166, 242, 192);
		}
		}   // (the view's pass)
		if (glassItems) {
		if (glassPass) glassBase(2);   // the brow's scales first: on the window's top edge
		g.Frame(C0, 0, k);
		// the person's state is data on the display (the body page, the ribbon), never a reason for it to go dark

		// ---- the rim: body and suit in one line each, on the visor's edge ----
		{
			auto arcLine = [&](double a0, double a1)
			{
				std::vector<V2> pts;
				for (int i = 0; i <= 24; ++i) { const double a = a0 + (a1 - a0) * i / 24; pts.push_back({ 640 + 1492 * std::cos(a), 1560 + 1492 * std::sin(a) }); }
				g.Poly(pts, CP, 1.2, 0.55);
			};
			// the rim's readouts: fixed slots - each label stays where it is, each value is right-aligned in room kept for its
			// widest form, so nothing slides along the arc as the numbers change
			struct Slot { std::string label, value, room; int col; };
			auto charsOn = [&](double s0, const std::string& txt, int col, double amid)
			{
				double sArc = s0;
				for (const std::string& ch : Chars(txt))
				{
					const double w = g.TW(ch, 13), a = amid + (sArc + w / 2) / 1500;
					const double px = 640 + 1500 * std::cos(a), py = 1560 + 1500 * std::sin(a);
					g.TurnedText(px, py, ch, col, 13, 1, a + PI05, px, py - 4.5);   // turned to the arc: the line reads as one slope, not a staircase
					sArc += w;
				}
			};
			auto arcSlots = [&](double amid, const std::vector<Slot>& slots)
			{
				const double sep = g.TW(" · ", 13);
				double total = 0;
				for (size_t i = 0; i < slots.size(); ++i) total += g.TW(slots[i].label + " ", 13) + g.TW(slots[i].room, 13) + (i + 1 < slots.size() ? sep : 0);
				double sArc = -total / 2;
				for (size_t i = 0; i < slots.size(); ++i)
				{
					const Slot& sl = slots[i];
					charsOn(sArc, sl.label + " ", CD, amid); sArc += g.TW(sl.label + " ", 13);
					const double room = g.TW(sl.room, 13);
					charsOn(sArc + room - g.TW(sl.value, 13), sl.value, sl.col, amid); sArc += room;
					if (i + 1 < slots.size()) { charsOn(sArc, " · ", CD, amid); sArc += sep; }
				}
			};
			if (pz || bu)
			{
				// the Pantsir (and «Буран»): four analogue scales on the rim - a scale line, ticks, the red zone from the limit, a pointer outside,
				// the name and the value along the arc. Radiation on a log scale; body, surroundings and the shell linear
				auto scale = [&](double a0, double a1, double f, double red0, const std::string& name, const std::string& val, int col)
				{
					const double R = 1492;
					std::vector<V2> pts, red;
					for (int i = 0; i <= 24; ++i) { const double a = a0 + (a1 - a0) * i / 24; pts.push_back({ 640 + R * std::cos(a), 1560 + R * std::sin(a) }); }
					g.Poly(pts, CP, 1.2, 0.6);
					for (int i = 0; i <= 12; ++i) { const double a = a0 + (a1 - a0) * (red0 + (1 - red0) * i / 12); red.push_back({ 640 + (R - 3) * std::cos(a), 1560 + (R - 3) * std::sin(a) }); }
					g.Poly(red, CR, 3, 0.9);
					for (int i = 0; i <= 12; ++i)
					{
						const double a = a0 + (a1 - a0) * i / 12, l = i % 4 == 0 ? 9 : 4;
						g.Line(640 + (R - l) * std::cos(a), 1560 + (R - l) * std::sin(a), 640 + R * std::cos(a), 1560 + R * std::sin(a), CP, 1, 0.7, false, false);
					}
					f = std::clamp(f, 0.0, 1.0);
					const double a = a0 + (a1 - a0) * f, ca = std::cos(a), sa = std::sin(a);
					g.Tri(640 + (R + 3) * ca, 1560 + (R + 3) * sa, 14 * ca - 5 * sa, 14 * sa + 5 * ca, 14 * ca + 5 * sa, 14 * sa - 5 * ca, f >= red0 ? CR : col);
				// the name and the value along the arc, inside the scale, every glyph turned to the arc's tangent (upright
					// glyphs on a slope read as a staircase); without the extended sketchpad - unturned, as before
					auto tilted = [&](double s0, const std::string& txt, int c, double amid)
					{
						const double Rt = R - 22; double sArc = s0;
						for (const std::string& ch : Chars(txt))
						{
							const double cw = g.TW(ch, 13), a = amid + (sArc + cw / 2) / Rt;
							const double px = 640 + Rt * std::cos(a), py = 1560 + Rt * std::sin(a);
							g.TurnedText(px, py + 4.5, ch, c, 13, 1, a + PI05, px, py);
							sArc += cw;
						}
					};
					const double tw = g.TW(name + "  " + val, 13), amid = (a0 + a1) / 2;
					tilted(-tw / 2, name + "  ", CD, amid);
					tilted(-tw / 2 + g.TW(name + "  ", 13), val, f >= red0 ? CR : col, amid);
				};
				const double msv = doseRate / 1000;   // mSv/h
				const double tShellC = pz ? d.life->tShell - 273.15 : 0.0, onsetC = pz ? (d.life->damageK > 0 ? d.life->damageK : d.life->tMax + 70) - 273.15 : 0.0;
				auto lin = [](double v, double lo, double hi) { return (v - lo) / (hi - lo); };
				if (pz)
				{
				// the Pantsir sees through cameras (its visor is a screen, not a curved glass): the four scales in a straight row
				// under the switches - two left, two right, the middle left to the lamps and the warnings
				auto hscale = [&](double x0, double x1, double f, double red0, const std::string& name, const std::string& val, int col)
				{
					const double y = 128;
					g.Line(x0, y, x1, y, CP, 1.2, 0.6, false, false);
					g.Line(x0 + (x1 - x0) * red0, y - 1.5, x1, y - 1.5, CR, 3, 0.9, false, false);
					for (int i = 0; i <= 12; ++i) { const double x = x0 + (x1 - x0) * i / 12, l = i % 4 == 0 ? 9 : 4; g.Line(x, y, x, y + l, CP, 1, 0.7, false, false); }
					f = std::clamp(f, 0.0, 1.0);
					const double px = x0 + (x1 - x0) * f;
					g.Tri(px, y - 3, -5, -12, 5, -12, f >= red0 ? CR : col);
					const double tw = g.TW(name + "  " + val, 13);
					g.T((x0 + x1) / 2 - tw / 2, y + 26, name + "  ", CD, 13); g.T((x0 + x1) / 2 - tw / 2 + g.TW(name + "  ", 13), y + 26, val, f >= red0 ? CR : col, 13);
				};
				g.Frame(L0, 0, k);
				hscale(40, 220, (std::log10((std::max)(msv, 0.01)) + 2) / 5, 0.6, TR("РАД"), Num(msv, msv < 10 ? 1 : 0) + TR(" мЗв/ч") + (d.fieldOn ? TR(" · поле ") + Num(d.life->fieldLevel * 100, 0) + " %" : ""), d.fieldOn ? CW : CA);
				hscale(250, 410, lin(d.coreC, 35, 43), lin(39.5, 35, 43), TR("ТЕЛО"), Num(d.coreC, 1) + "° · " + Num(d.pulse, 0), CW);
				g.Frame(R0, 0, k);
				hscale(870, 1030, lin(d.envC, -200, 800), lin(d.ratedMaxC, -200, 800), TR("СРЕДА"), Num(d.envC, 0, true) + "°", CW);
				hscale(1060, 1240, lin(tShellC, -200, 800), lin(onsetC, -200, 800), TR("КОРПУС"), Num(tShellC, 0, true) + "°", CW);
				g.Frame(C0, 0, k);
				}
				else if (false)
				{
				scale(-1.925, -1.805, (std::log10((std::max)(msv, 0.01)) + 2) / 5, 0.6, TR("РАД"), Num(msv, msv < 10 ? 1 : 0) + TR(" мЗв/ч") + (d.fieldOn ? TR(" · поле ") + Num(d.life->fieldLevel * 100, 0) + " %" : ""), d.fieldOn ? CW : CA);
				scale(-1.790, -1.672, lin(d.coreC, 35, 43), lin(39.5, 35, 43), TR("ТЕЛО"), Num(d.coreC, 1) + "° · " + Num(d.pulse, 0), CW);
				scale(-1.470, -1.352, lin(d.envC, -200, 800), lin(d.ratedMaxC, -200, 800), TR("СРЕДА"), Num(d.envC, 0, true) + "°", CW);
				scale(-1.337, -1.217, lin(tShellC, -200, 800), lin(onsetC, -200, 800), TR("КОРПУС"), Num(tShellC, 0, true) + "°", CW);
				}
				else
				{
					// «Буран»: only what is shown nowhere else - the body and the air in the suit (the reserves are on the counters)
					scale(-1.950, -1.832, lin(d.pulse, 40, 200), lin(150, 40, 200), TR("ПУЛЬС"), Num(d.pulse, 0) + TR(" · ДЫХ ") + Num(d.breath, 0), d.pulse > 150 ? CR : CW);
					scale(-1.817, -1.705, lin(d.coreC, 34, 40), lin(38.5, 34, 40), TR("ТЕЛО"), Num(d.coreC, 1) + "°", d.coreC < 35.8 ? CA : CW);
					scale(-1.437, -1.325, lin(d.tInC, -10, 40), lin(30, -10, 40), TR("СКАФАНДР"), Num(d.tInC, 1) + "°" + (Trend(4, 0.3) > 0 ? " ↑" : Trend(4, 0.3) < 0 ? " ↓" : ""), d.tInC < 12 ? CA : CW);
					scale(-1.310, -1.192, (std::log10((std::max)(msv, 0.01)) + 2) / 5, 0.6, TR("РАД"), Num(msv, msv < 10 ? 2 : 0) + TR(" мЗв/ч") + (d.fieldOn ? TR(" · поле") : ""), d.fieldOn ? CW : msv > 1 ? CA : CW);
				}
			}
			else {
			arcLine(-1.925, -1.672); arcLine(-1.470, -1.217);
			arcSlots(-1.798, { { TR("ПУЛЬС"), Num(d.pulse, 0), "888", CW }, { TR("ДЫХ"), Num(d.breath, 0), "88", CW },
				{ "O2", Num(d.ppO2, 0) + TR(" кПа"), TR("888 кПа"), d.ppO2 < 14 ? CR : d.ppO2 < 17 ? CA : CW }, { TR("ТЕЛО"), Num(d.coreC, 1) + "°", "88,8°", d.coreC > 38.5 || d.coreC < 35.8 ? CA : CW } });
			if (d.suit)
				arcSlots(-1.344, { { "O2", Num(100 * d.o2, 0) + " %", "100 %", d.o2 < 0.25 ? CA : CW }, { TR("БАТ"), Num(100 * d.batt, 0) + " %", "100 %", d.batt < 0.25 ? CA : CW },
					{ "", Num(d.powerW, 0) + TR(" Вт"), TR("8888 Вт"), CW }, { TR("СРЕДА"), Num(d.envC, 0, true) + "°", "+888°", d.inSpec ? CW : CR } });
			else
				arcSlots(-1.344, { { TR("КОМБИНЕЗОН ·"), d.breathable ? TR("воздух пригоден") : TR("дышать нельзя"), TR("воздух пригоден"), d.breathable ? CW : CR } });
			}
		}

		if (glassPass) glassBase(1);   // the rest of the top band: flat, equal angles
		// ---- corners ----
		g.Frame(L0, 0, k);
		g.T(24, 32, Upper(d.name), CW, 15); g.T(24, 49, d.role + TR(" · «Тантра»"), CD, 9.5);
		if (pz)
		{
			// ЗАПАС: the minutes left by whatever limits first - the heat (the water, then her own core), the battery, the O2,
			// the outing's dose limit (250 mSv); a drum of three digits and what it is
			struct Lim { double min; const char* why; };
			std::vector<Lim> L;
			const Suit& s = *d.life;
			if (s.evapW > 1 && s.evap > 0) L.push_back({ s.evap * 2.26e6 / s.evapW / 60, TR("по жаре · вода") });
			else if (d.residualW > 30) L.push_back({ (std::max)(0.0, 41.0 - d.coreC) * 62 * 3500 / d.residualW / 60, TR("перегрев тела") });
			L.push_back({ d.battHours * 60, TR("батарея") });
			if (!d.o2Vent && d.o2Hours > 0) L.push_back({ d.o2Hours * 60, TR("кислород") });
			if (doseRate > 1) L.push_back({ (std::max)(0.0, 2.5e5 - dose) / doseRate * 60, TR("доза 250 мЗв") });
			const Lim* m = &L[0]; for (const Lim& l : L) if (l.min < m->min) m = &l;
			const int mins = static_cast<int>(std::clamp(m->min, 0.0, 999.0));
			char db[8]; snprintf(db, sizeof db, "%03d", mins);
			const int col = m->min < 15 ? CR : m->min < 60 ? CA : CW;
			for (int i = 0; i < 3; ++i) { g.Rect(24 + i * 19, 58, 17, 24, CP, 1, 0.6, CK, 0.5); g.T(32.5 + i * 19, 76, std::string(1, db[i]), col, 16, 1); }
			g.T(86, 67, TR("ЗАПАС мин"), col, 10); g.T(86, 80, m->why, col, 9.5);
		}
		g.Frame(R0, 0, k);
		{
			const double mjd = oapiGetSimMJD(), day = (mjd - std::floor(mjd)) * 86400;
			char b[32]; snprintf(b, sizeof b, "UTC %02d:%02d:%02d", static_cast<int>(day) / 3600, (static_cast<int>(day) / 60) % 60, static_cast<int>(day) % 60);
			g.T(1256, 32, b, CW, 15, 2);
			char bn[64] = ""; if (hBody) oapiGetObjectName(hBody, bn, 64);
			const char* ru = BodyRu(bn);
			std::string where = std::string(ru ? ru : bn) + (surface ? (sunElev > 0 ? TR(" · день") : TR(" · ночь")) : TR(" · орбита"));
			where += d.vacuum ? TR(" · вакуум") + (d.life && d.life->Heavy() ? Trace(d.airKPa) : std::string()) : " · " + Num(d.airKPa, 0) + TR(" кПа");
			g.T(1256, 49, where, CD, 9.5, 2);
			if (d.suit && !bu)   // legacy: the light suit's layout before «Буран» (kept, not drawn for the light suit)
			{
				// the suit's switches on the right; the pack's control block (assistant, limiter) apart on their left, framed
				struct Tg { const char* n; bool on; int kind, arg; };
				const Tg suitTg[4] = { { TR("ПОЛЕ"), d.fieldOn, H_AP, AP_FIELD }, { TR("СВЕТ"), d.lampsOn, H_AP, AP_LAMP }, { d.life && d.life->Heavy() ? TR("ШТОРКА") : TR("ЩИТОК"), d.life && d.life->Heavy() ? !d.pzWindow : d.shadeDown, H_AP, AP_SHADE }, { TR("ПНВ"), nvg, H_NVG, 0 } };
				const Tg jetTg[2] = { { TR("ПОМОЩНИК"), d.jetAssist, H_AP, AP_MANUAL }, { TR("ОГРАНИЧ."), d.jetFine, H_AP, AP_FINE } };
				double bx = 1256;
				auto button = [&](const Tg& t)
				{
					const double w = g.TW(t.n, 10) + 12; bx -= w;   // compact: the row stays right of the rim's arc
					if (t.on) { g.Rect(bx, 56, w, 18, -1, 0, 0, CA, 1); g.T(bx + w / 2, 69, t.n, CK, 10, 1); }
					else { g.Rect(bx, 56, w, 18, CP, 1, 0.6, CP, 0.08); g.T(bx + w / 2, 69, t.n, CP, 10, 1); }
					g.Hit(bx, 56, w, 18, t.kind, t.arg);
					bx -= 3;
				};
				for (int i = 3; i >= 0; --i) button(suitTg[i]);
				// the helmet's front (in a ship's or a station's air): lit while open
				if (d.life) { const Tg helm = { d.life->helmetPos >= 1 ? TR("ШЛЕМ ОТКР") : d.life->helmetPos > 0 || d.life->helmetEq > 0 ? TR("ШЛЕМ ...") : TR("ШЛЕМ"), d.life->helmetOpen, H_AP, AP_HELMET }; button(helm); }
				{   // the lamps' beam, left of the switches: a click goes flood -> work -> spot
					static const char* const BEAM[3] = { "ЛУЧ ШИР", "ЛУЧ РАБ", "ЛУЧ ДАЛ" };
					const Tg beam = { TR(BEAM[std::clamp(d.lampMode, 0, 2)]), d.lampsOn, H_AP, AP_LAMPMODE };
					button(beam);
				}
				{
					// the second row: the pack's block (when worn) right-aligned, then the suit's servo and economy on its left
					bx = 1256;
					auto button2 = [&](const Tg& t)
					{
						const double w = g.TW(t.n, 10) + 12; bx -= w;
						if (t.on) { g.Rect(bx, 77, w, 18, -1, 0, 0, CA, 1); g.T(bx + w / 2, 90, t.n, CK, 10, 1); }
						else { g.Rect(bx, 77, w, 18, CP, 1, 0.6, CP, 0.08); g.T(bx + w / 2, 90, t.n, CP, 10, 1); }
						g.Hit(bx, 77, w, 18, t.kind, t.arg);
						bx -= 3;
					};
					if (d.jet) { for (int i = 1; i >= 0; --i) button2(jetTg[i]); bx -= 8; }
					if (d.life)
					{
						const Tg suit2[2] = { { TR("СЕРВО"), d.life->drivesOn, H_SUIT, 0 }, { TR("ЭКОНОМ"), d.life->econ, H_SUIT, 1 } };
						for (int i = 1; i >= 0; --i) button2(suit2[i]);
					}
				}
				if (nvg && d.firstPerson && nvNote != TR("ПНВ")) g.T(1256, 108, nvNote, CA, 10, 2);
			}
		}

		// ---- modes and compass ----
		auto midFrame = [&]() { if (glassPass) g.Frame(C0 + 640 * k * (1 - TOP_MID), 8 * k * (1 - TOP_MID), k * TOP_MID); else g.Frame(C0, 0, k); };
		midFrame();
		{
			const char* NAMES[4] = { TR("ВКД"), TR("ПОЛЁТ"), TR("СБЛИЖЕНИЕ"), TR("СИСТЕМЫ") };
			const double xs[4] = { 490, 580, 690, 800 };
			for (int i = 0; i < 4; ++i)
			{
				const bool on = i == mode; const double w = g.TW(NAMES[i], 12.5) + 16;
				if (on) g.Rect(xs[i] - w / 2, 12, w, 22, CP, 1.2, 1, CP, 0.14);
				g.T(xs[i], 28, NAMES[i], on ? CP : CD, 12.5, 1);
				g.T(xs[i], 9, std::to_string(i + 1), CD, 8.5, 1);   // over its name
				g.Hit(xs[i] - w / 2, 8, w, 28, H_MODE, i);
			}
			g.T(868, 28, autoMode ? TR("АВТО") : TR("РУЧН"), autoMode ? CD : CA, 9.5);
			const double hd = std::fmod(hdg * DEG + 360, 360), y = 72;
			for (int dd = static_cast<int>(std::ceil((hd - 30) / 5)) * 5; dd <= hd + 30; dd += 5)
			{
				const double x = 640 + (dd - hd) * 4; const int a = ((dd % 360) + 360) % 360; const bool big = a % 10 == 0;
				g.Line(x, y, x, y - (big ? 8 : 4), CP, 1, 0.8, false, false);
				if (big)
				{
					char b[8]; snprintf(b, sizeof b, "%03d", a);
					g.T(x, y - 12, a == 0 ? TR("С") : a == 90 ? TR("В") : a == 180 ? TR("Ю") : a == 270 ? TR("З") : b, CD, 9.5, 1);
				}
			}
			g.Line(520, y, 760, y, CP, 1, 0.5, false, false);
			g.Rect(616, y + 4, 48, 18, CP, 1.2, 1, CK, 0.5);
			char b[8]; snprintf(b, sizeof b, "%03.0f°", hd); g.T(640, y + 18, b, CW, 13, 1);
			const double ws = std::hypot(d.wind.x, d.wind.z);
			if (!d.vacuum && ws > 0.1)
			{
				// where the wind blows to, relative to her heading
				const double wa = std::atan2(d.wind.x, d.wind.z) - hdg; double from = std::fmod((std::atan2(-d.wind.x, -d.wind.z)) * DEG + 360, 360);
				g.Arrow(700 - 9 * std::sin(wa), y + 13 + 9 * std::cos(wa), 700 + 9 * std::sin(wa), y + 13 - 9 * std::cos(wa), CP, 1.4);
				char wb[48]; snprintf(wb, sizeof wb, "%03.0f°", from);
				g.T(714, y + 18, TR("ВЕТЕР ") + Num(ws, 1) + TR(" м/с · с ") + wb, CD, 9.5);
			}
		}

		// ---- caution & warning: the master strips at the edges of view, the system lamps, the message ribbon ----
		{
			bool anyW = false, anyC = false, unacked = false;
			for (const auto& a : alerts) if (a.active) { (a.level == 2 ? anyW : anyC) = true; if (!a.ack) unacked = true; }
			const bool lit = anyW || anyC, on = lit && (!unacked || c.blink);
			const int mc = anyW ? CR : CA;
			// the side vision catches a blink at the very edge; a click on either strip acknowledges everything
			for (int sdx = 0; sdx < 2 && !bu; ++sdx)   // («Буран»: none - the user crossed them out, 2026-10-20)
			{
				g.Frame(sdx ? R0 : L0, 0, k);
				const double x = sdx ? 1268 : 6;
				if (on) g.Rect(x, 250, 6, 190, mc, 1, 1, mc, 1); else g.Rect(x, 250, 6, 190, lit ? mc : CD, 1, lit ? 0.6 : 0.3);
				g.Hit(x - 6, 240, 18, 210, H_ACK, -1);
			}
			midFrame();
			size_t shown = 0;   // lines of the ribbon on show (the message goes under them)
			const char* TILE[8] = { "O2", "CO2", TR("ДАВЛ"), TR("БАТ"), TR("ТЕПЛО"), TR("РАДИАЦ"), TR("СВЯЗЬ"), TR("РЕЗЕРВ") };
			const char* SHORT[8] = { "O2", "CO2", TR("ДАВЛ"), TR("БАТ"), TR("ТЕПЛ"), TR("РАД"), TR("СВЯЗ"), TR("РЕЗ") };
			if (d.suit)
			{
				for (int i = 0; i < 8; ++i)
				{
					int lvl = 0; bool un = false;
					for (const auto& a : alerts) if (a.active && a.tile == TILE[i]) { lvl = (std::max)(lvl, a.level); un = un || !a.ack; }
					const double x = bu ? 640 - 154 + i * 44 : 640 - 217 + i * 62;   // «Буран»: closer, clear of the brow's scales
					const int col = lvl == 2 ? CR : CA;
					if (lvl && (!un || c.blink)) g.Circle(x, 104, 4, col, 1.2, 1, col, 1); else g.Circle(x, 104, 4, lvl ? col : CD, 1, lvl ? 0.7 : 0.4);
					g.T(x + 8, 108, SHORT[i], lvl ? col : CD, 8.5);
				}
				g.Hit(640 - 230, 96, 500, 16, H_ACK, -1);
				if (bu)
				{
					// «Буран» (the user, 2026-10-20: the journal got in the way): over the view only the dangers that are on now -
					// at most two lines with the time, blinking until acknowledged; cautions stay on the lamps, the rest on the MFD
					std::vector<int> dz;
					for (int i = 0; i < static_cast<int>(alerts.size()); ++i) if (alerts[i].active && alerts[i].level == 2) dz.push_back(i);
					// the dangers as notices (the user: notices, not a journal) - the text alone, red, no time, no box; gone when
					// the condition is
					size_t nd = 0;
					for (size_t n = 0; n < dz.size() && n < 2; ++n) { g.T(640, 160 + n * 20, alerts[dz[n]].text, CR, 14, 1); g.Hit(400, 146 + n * 20, 480, 18, H_ACK, dz[n]); ++nd; }
					shown = nd;
					// the heat and the cold (cautions of the tile ТЕПЛО): a plain message under them, no time, no journal - as
					// long as it lasts
					int nm = 0;
					for (const auto& a : alerts)
						if (a.active && a.level == 1 && a.tile == TR("ТЕПЛО") && nm < 2) { g.T(640, 160 + (shown + nm) * 20, a.text, CA, 14, 1); ++nm; }
					shown += nm;
					alertLines = static_cast<int>(shown);
				}
				else
				{
				// the three newest of what is on now; a message whose condition is gone leaves the ribbon at once
				std::vector<int> order;
				for (int i = 0; i < static_cast<int>(alerts.size()); ++i) if (alerts[i].active) order.push_back(i);
				shown = (std::min)(order.size(), static_cast<size_t>(3));
				for (size_t n = 0; n < order.size() && n < 3; ++n)
				{
					const Alert& a = alerts[order[n]];
					const double y = 158 + n * 19;
					char tb[16]; snprintf(tb, sizeof tb, "%02d:%02d:%02d  ", static_cast<int>(a.t0) / 3600, (static_cast<int>(a.t0) / 60) % 60, static_cast<int>(a.t0) % 60);
					const std::string line = tb + std::string(a.level == 2 ? "! " : "▲ ") + a.text + (a.active ? (a.ack ? "" : TR("  ·  подтвердите щелчком")) : TR("  ·  снято"));
					const int col = !a.active ? CD : a.level == 2 ? CR : CA;
					if (a.active && !a.ack && !c.blink) continue;
					g.T(640, y, line, col, 13, 1);
					g.Hit(380, y - 14, 520, 18, H_ACK, order[n]);
				}
				}
			}
			if (d.suit && lit)
			{
				// the word itself, under the lamps: ОПАСНОСТЬ (red) / ВНИМАНИЕ (amber); blinks until acknowledged, a click acknowledges
				const std::string word = anyW ? TR("ОПАСНОСТЬ") : TR("ВНИМАНИЕ");
				if (!unacked || c.blink) { const double w = g.TW(word, 18) + 30; g.Rect(640 - w / 2, 112, w, 27, mc, 1.6, 1, mc, unacked ? 0.25 : 0.1); g.T(640, 132, word, mc, 18, 1); }
				g.Hit(640 - 90, 110, 180, 31, H_ACK, -1);
			}
			// these three lines low in the view: on the display itself, not on the glass - there they came out small and
			// off their place (the user, 2026-10-21: «МЕЛКО ТО ПОЧЕМУ ТАК», «ВЕРНИ РАЗМЕР КАК БЫЛ И ПОЛОЖЕНИЕ»)
			if (!d.message.empty()) { const int mc = d.messageLevel >= 2 ? CR : d.messageLevel == 1 ? CA : CW; g.T(640, 158 + shown * 19, d.message, mc, 12.5, 1); }   // up under the lamps, as it always was (the user, 2026-10-21)
		}

		}   // (glassItems)
		if (!glassPass) {
		// ---- the Pantsir: its control panel (low left) and the consumption (low right), the MFDs between them ----
		if (pz)
		{
			const Suit& s = *d.life;
			auto backing = [&](double x, double y, double w, double h)
			{
				g.Rect(x, y, w, h, -1, 0, 0, CK, 0.45); g.Rect(x, y, w, h, CP, 1, 0.32, CP, 0.06);
				for (int i = 0; i < 4; ++i)
				{
					const double bx = x + (i & 1 ? w : 0), by = y + (i & 2 ? h : 0), dx = i & 1 ? -10 : 10, dy = i & 2 ? -10 : 10;
					g.Line(bx, by, bx + dx, by, CP, 2); g.Line(bx, by, bx, by + dy, CP, 2);
				}
			};
			g.Frame(L0, 0, k);
			const double x = 20, y = 468, w = 270, h = 218;
			backing(x, y, w, h);
			g.T(x + 9, y + 17, TR("ПАНЦИРЬ"), CP, 13); g.T(x + w - 9, y + 17, TR("управление"), CD, 9.5, 2);
			const char* TABS[5] = { TR("ДАННЫЕ"), TR("РАДИАТОР"), TR("ПРИВОДЫ"), TR("ПОЛЕ"), TR("ПОВРЕЖД.") };
			const double tw = (w - 14) / 5.0;
			for (int i = 0; i < 5; ++i)
			{
				const double tx = x + 7 + i * tw; const bool on = i == pzTab;
				if (on) g.Rect(tx, y + 23, tw - 3, 16, CA, 1.2, 1, CA, 0.12); else g.Rect(tx, y + 23, tw - 3, 16, CP, 1, 0.4, CP, 0.05);
				g.T(tx + (tw - 3) / 2, y + 35, TABS[i], on ? CA : CD, 8.5, 1);
				g.Hit(tx, y + 21, tw - 3, 20, H_PZTAB, i);
			}
			struct R { std::string n, v; int c; };
			std::vector<R> rows;
			const double other = d.lifeW + d.driveW + d.lampW + d.fieldW;
			const double limC = s.HeatLimitK(120, other) - 273.15, noField = s.HeatLimitK(120, other - d.fieldW) - 273.15;
			const double onsetC = (s.damageK > 0 ? s.damageK : s.tMax + 70) - 273.15;
			const double shellC = s.tShell - 273.15;
			const bool holds = std::abs(d.residualW) < 30;
			switch (pzTab)
			{
			case 0:   // data
				rows = { { TR("В скафандре"), Num(d.tInC, 1) + " °C · " + (holds && d.powered ? TR("держится") : d.residualW > 0 ? TR("растёт") : TR("падает")), d.tInC > 30 || d.tInC < 12 ? CR : holds ? CW : CA },
				         { TR("Снаружи"), d.vacuum ? std::string(TR("вакуум")) + Trace(d.airKPa) : Num(d.airKPa, d.airKPa < 10 ? 2 : 1) + TR(" кПа"), d.suitBreached || d.airKPa > d.suitPMax ? CR : CW },
				         { TR("Среда (излучение)"), Num(d.envC, 0, true) + " °C" + (d.sunlit ? TR(" · солнце") : TR(" · тень")), d.inSpec ? CW : CR } };
				if (d.hasGround) rows.push_back({ TR("Грунт"), Num(d.groundC, 0, true) + " °C", d.groundC > d.ratedMaxC ? CR : CW });
				rows.push_back({ TR("Корпус"), Num(shellC, 0, true) + TR(" °C · порог ") + Num(onsetC, 0, true), shellC > onsetC ? CR : shellC > onsetC - 60 ? CA : CW });
				rows.push_back({ TR("Предел по жаре"), Num(limC, 0, true) + " °C" + (d.fieldW > 0 && noField > limC + 4 ? TR(" · без поля ") + Num(noField, 0, true) : ""), d.envC > limC - 30 ? CR : d.envC > limC - 80 ? CA : CW });
				rows.push_back({ TR("Допуск снаружи"), Num(d.ratedMinC, 0) + " .. " + Num(d.ratedMaxC, 0, true) + " °C", CW });
				break;
			case 1:   // radiator
				rows = { { TR("Радиатор"), Num(s.tRad - 273.15, 0, true) + " °C" + (s.tRad > 800 ? TR(" · светится") : s.tRad < 150 ? TR(" · иней") : ""), s.tRad > 800 ? CA : CW },
				         { TR("Насос"), Num(s.coolMaxW > 0 ? 100 * (std::max)(0.0, s.heatW - s.evapW) / s.coolMaxW : 0.0, 0) + " %", CW },
				         { TR("Компрессор"), s.compStrain > 1.5 ? TR("перегрузка x") + Num(s.compStrain, 1) : std::string(TR("норма")), s.compStrain > 2.5 ? CR : s.compStrain > 1.5 ? CA : CW },
				         { TR("Теплообмен"), std::string(d.heatW >= 0 ? TR("ОХЛАЖДЕНИЕ ") : TR("ОБОГРЕВ ")) + Num(std::abs(d.heatW), 0) + TR(" Вт"), CP },
				         { TR("Испаритель"), Num(s.evap, 1) + TR(" кг") + (s.evapW > 1 ? " · " + Num(s.evapW, 0) + TR(" Вт · ") + Num(s.evap * 2.26e6 / s.evapW / 60, 0) + TR(" мин") : ""), s.evapW > 1 ? CA : CW },
				         { TR("Аппаратура"), Num(s.tEq - 273.15, 0, true) + " °C", s.tEq > Suit::EQ_LIMIT ? CR : s.tEq > Suit::EQ_WARN ? CA : CW },
				         { TR("Контур"), s.coolFail ? std::string(TR("ОТКАЗ")) : std::string(TR("исправен")), s.coolFail ? CR : CW } };
				break;
			case 2:   // drives
				rows = { { TR("Сервоприводы"), std::string(s.drivesFail ? TR("ОТКАЗ") : !s.drivesOn ? TR("выкл") : s.shedDrives ? TR("сброшены") : TR("работают")), s.drivesFail ? CR : s.shedDrives ? CA : CW },
				         { TR("Мощность"), Num(d.driveW, 0) + TR(" Вт"), CW },
				         { TR("Ход к норме"), "x" + Num(s.walkFactor, 2), CW },
				         { TR("Тормоза суставов"), std::string(s.locksUpright ? TR("держат стоя") : TR("нет")), CW },
				         { TR("Аппаратура"), Num(s.tEq - 273.15, 0, true) + " °C", s.tEq > Suit::EQ_LIMIT ? CR : CW } };
				break;
			case 3:   // field
				rows = { { TR("Поле"), s.fieldFail ? std::string(TR("ОТКАЗ КАТУШЕК")) : !d.fieldOn ? std::string(TR("выкл")) : s.shedField ? std::string(TR("сброшено")) : Num(s.fieldLevel * 100, 0) + " % · " + Num(d.fieldW, 0) + TR(" Вт"), s.fieldFail ? CR : s.shedField ? CA : CW },
				         { TR("Ослабление"), "x" + Num(d.fieldOn && !s.shedField ? s.FieldFactorNow() : 1.0, 0), CW },
				         { TR("Доза сейчас"), doseRate >= 1000 ? Num(doseRate / 1000, 2) + TR(" мЗв/ч") : Num(doseRate, 0) + TR(" мкЗв/ч"), doseRate > 10000 ? CR : doseRate > 1000 ? CA : CW },
				         { TR("За выход"), Num(dose / 1000, 1) + TR(" мЗв из 250"), dose > 2.5e5 ? CR : dose > 1e5 ? CA : CW },
				         { TR("Подбой (тантал)"), TR("тормозное x") + Num(s.bremsFactor, 2), CW } };
				break;
			default:  // damage
				rows = { { TR("Изоляция"), s.dmgIns > 0.005 ? TR("прогорела ") + Num(100 * s.dmgIns, 0) + " %" : std::string(TR("цела")), s.dmgIns > 0.3 ? CR : s.dmgIns > 0.005 ? CA : CW },
				         { TR("Уплотнения"), s.dmgSeal > 0.005 ? Num(100 * s.dmgSeal, 0) + " %" + (s.dmgSeal > 0.2 ? TR(" · утечка") : "") : std::string(TR("целы")), s.dmgSeal > 0.2 ? CR : s.dmgSeal > 0.005 ? CA : CW },
				         { TR("Катушки поля"), std::string(s.fieldFail ? TR("ОТКАЗ") : TR("исправны")), s.fieldFail ? CR : CW },
				         { TR("Сервоприводы"), std::string(s.drivesFail ? TR("ОТКАЗ") : TR("исправны")), s.drivesFail ? CR : CW },
				         { TR("Радиатор"), std::string(s.coolFail ? TR("ОТКАЗ") : TR("исправен")), s.coolFail ? CR : CW },
				         { TR("Ноги"), Num(s.tLeg - 273.15, 0, true) + " °C" + (s.dmgLeg > 0.005 ? TR(" · обувь ") + Num(100 * s.dmgLeg, 0) + " %" : "") + (s.immersion > 0.02 ? TR(" · в расплаве") : ""), s.dmgLeg > 0.3 || s.immersion > 0.02 ? CR : s.dmgLeg > 0.005 ? CA : CW },
				         { TR("Оболочка"), std::string(d.suitBreached ? TR("ВСКРЫТА") : TR("герметична")), d.suitBreached ? CR : CW } };
				break;
			}
			for (size_t i = 0; i < rows.size(); ++i) { g.T(x + 9, y + 60 + i * 21, rows[i].n, CD, 10.5); g.T(x + w - 9, y + 60 + i * 21, rows[i].v, rows[i].c, 11, 2); }

			g.Frame(R0, 0, k);
			const double rx = 990;
			backing(rx, y, w, h);
			const double lim = (std::min)({ d.battHours, d.o2Hours > 0 ? d.o2Hours : 1e9, d.sorbHours > 0 ? d.sorbHours : 1e9 });
			const char* limName = lim == d.battHours ? TR("батарея") : lim == d.o2Hours ? TR("кислород") : TR("поглотитель");
			g.T(rx + 9, y + 17, TR("РАСХОД"), CP, 13); g.T(rx + w - 9, y + 17, TR("хватит на ") + Hours(lim) + " · " + limName, lim < 1 ? CR : CA, 9.5, 2);
			struct Row { std::string n, v; double f; } rr[] = {
				{ TR("Питание"), Num(d.powerW, 0) + TR(" Вт") + (d.fieldW > 0 ? TR(" · поле ") + Num(d.fieldW, 0) : ""), (std::min)(1.0, d.powerW / (std::max)(1.0, s.supplyW)) },
				{ TR("Батарея"), Num(100 * d.batt, 0) + " % · " + Hours(d.battHours), d.batt },
				{ TR("Кислород"), d.o2Vent ? TR("из воздуха · баллон ") + Num(100 * d.o2, 0) + " %" : Num(100 * d.o2, 0) + " % · " + Hours(d.o2Hours), d.o2 },
				{ TR("Поглотитель CO2"), Num(100 * d.sorbent, 0) + " % · " + Hours(d.sorbHours), d.sorbent },
				{ TR("Вода испарителя"), Num(s.evap, 1) + TR(" кг"), s.evapCap > 0 ? s.evap / s.evapCap : 0.0 } };
			for (int i = 0; i < 5; ++i)
			{
				const double yy = y + 44 + i * 34;
				g.T(rx + 9, yy, rr[i].n, CD, 10.5); g.T(rx + w - 9, yy, rr[i].v, i == 0 && rr[i].f > 0.9 ? CA : CW, 11, 2);
				g.Bar(rx + 9, yy + 6, w - 18, 6, rr[i].f, i == 0 ? (rr[i].f > 0.9 ? CA : CP) : rr[i].f < 0.1 ? CR : rr[i].f < 0.25 ? CA : CP);
			}
		}
		// ---- thermal control (left) and consumption (right) ----
		else if (d.suit && !bu)   // legacy: the light suit's layout before «Буран» (kept, not drawn for the light suit)
		{
			g.Frame(L0, 0, k);
			const double x = 24, y = 180, w = 230;
			const bool holds = std::abs(d.residualW) < 30;
			g.T(x, y, TR("ТЕРМОКОНТРОЛЬ"), CP, 12.5); g.T(x + w, y, !d.powered ? TR("НЕТ ПИТАНИЯ") : holds ? TR("НОРМА") : TR("НЕ ДЕРЖИТ"), holds && d.powered ? CP : CR, 11, 2);
			g.Line(x, y + 6, x + w, y + 6, CP, 1, 0.5, false, false);
			struct R { std::string n, v; int c; };
			std::vector<R> rows = {
				{ TR("В скафандре"), Num(d.tInC, 1) + " °C · " + (holds && d.powered ? TR("держится") : d.residualW > 0 ? TR("растёт") : TR("падает")) + (Trend(4, 0.3) > 0 ? " ↑" : Trend(4, 0.3) < 0 ? " ↓" : ""), d.tInC > 30 || d.tInC < 12 ? CR : holds && d.powered ? CW : CA },
				{ TR("Тело"), Num(d.coreC, 1) + " °C", d.coreC > 38.5 || d.coreC < 35.8 ? CA : CW },
				{ TR("Снаружи"), d.vacuum ? std::string(TR("вакуум")) + (d.life && d.life->Heavy() ? Trace(d.airKPa) : std::string()) : Num(d.airKPa, d.airKPa < 10 ? 2 : 1) + TR(" кПа · ") + (d.breathable ? TR("воздух") : TR("атмосфера")),
					d.suitBreached || d.airKPa > d.suitPMax ? CR : d.airKPa > 0.75 * d.suitPMax ? CA : CW },
				{ d.vacuum ? TR("Среда (излучение)") : TR("Воздух"), d.vacuum ? Num(d.envC, 0, true) + " °C" + (d.sunlit ? TR(" · солнце") : TR(" · тень")) : Num(d.airC, 0, true) + " °C" + (d.sunlit ? TR(" · солнце") : TR(" · тень")), d.inSpec ? CW : CR } };
			if (d.hasGround) rows.push_back({ TR("Грунт"), Num(d.groundC, 0, true) + " °C", d.groundC > 80 || d.groundC < -100 ? CA : CW });
			rows.push_back({ TR("Теплообмен"), std::string(d.heatW >= 0 ? TR("ОХЛАЖДЕНИЕ ") : TR("ОБОГРЕВ ")) + Num(std::abs(d.heatW), 0) + TR(" Вт") + (d.life && d.life->econ ? TR(" · эконом") : ""), CP });
			if (d.life) rows.push_back({ TR("Аппаратура"), Num(d.life->tEq - 273.15, 0, true) + " °C", d.life->tEq > Suit::EQ_LIMIT ? CR : d.life->tEq > Suit::EQ_WARN ? CA : CW });
			if (d.life && d.life->Heavy())   // the heavy suit: the limit floats with the loads, the equipment and the water
			{
				const double other = d.lifeW + d.driveW + d.lampW + d.fieldW;
				const double limC = d.life->HeatLimitK(120, other) - 273.15, noField = d.life->HeatLimitK(120, other - d.fieldW) - 273.15;
				rows.push_back({ TR("Предел по жаре"), Num(limC, 0, true) + " °C" + (d.fieldW > 0 && noField > limC + 4 ? TR(" · без поля ") + Num(noField, 0, true) : ""),
					d.envC > limC - 30 ? CR : d.envC > limC - 80 ? CA : CW });
			}
			if (d.life && d.life->Heavy())   // the radiator: dull red past ~800 K, frost below ~150 K
				rows.push_back({ TR("Радиатор"), Num(d.life->tRad - 273.15, 0, true) + " °C" + (d.life->tRad > 800 ? TR(" · светится") : d.life->tRad < 150 ? TR(" · иней") : "")
					+ (d.life->compStrain > 1.5 ? TR(" · компрессор ") + Num(d.life->compStrain * 100, 0) + " %" : ""),
					d.life->tRad > 800 || d.life->compStrain > 2.5 ? CA : CW });
			if (d.life && d.life->Heavy() && d.life->evapCap > 0)
				rows.push_back({ TR("Испаритель"), Num(d.life->evap, 1) + TR(" кг") + (d.life->evapW > 1 ? " · " + Num(d.life->evapW, 0) + TR(" Вт · ") + Num(d.life->evap * 2.26e6 / d.life->evapW / 60, 0) + TR(" мин") : ""),
					d.life->evapW > 1 ? CA : d.life->evap < 0.2 * d.life->evapCap ? CA : CW });
			rows.push_back({ std::string(TR("Радиация")) + (d.fieldOn ? (d.life && d.life->fieldSteps ? TR(" · поле ") + Num(d.life->fieldLevel * 100, 0) + " %" : std::string(TR(" · поле"))) : std::string()),
				(doseRate >= 1000 ? Num(doseRate / 1000, 2) + TR(" мЗв/ч") : Num(doseRate, doseRate < 1 ? 2 : 0) + TR(" мкЗв/ч")) + " · " + Num(dose / 1000, 2) + TR(" мЗв"),
				doseRate > 10000 ? CR : doseRate > 1000 ? CA : CW });
			for (size_t i = 0; i < rows.size(); ++i) { g.T(x, y + 24 + i * 17, rows[i].n, CD, 11); g.T(x + w, y + 24 + i * 17, rows[i].v, rows[i].c, 11.5, 2); }
			// the rated range with the environment and the ground on it
			const double yr = y + 24 + rows.size() * 17 + 8;
			auto X = [&](double t) { return x + std::clamp((t - d.ratedMinC) / (d.ratedMaxC - d.ratedMinC), 0.0, 1.0) * w; };
			g.Line(x, yr + 3, x + w, yr + 3, CP, 2, 0.5); g.Line(x, yr - 3, x, yr + 9, CR, 2); g.Line(x + w, yr - 3, x + w, yr + 9, CR, 2);
			g.Tri(X(d.envC), yr - 1, -4, -7, 4, -7, d.inSpec ? CP : CR);
			if (d.hasGround) g.Tri(X(d.groundC), yr + 7, -4, 7, 4, 7, CA);
			g.T(x, yr + 22, Num(d.ratedMinC, 0), CD, 9.5); g.T(x + w / 2, yr + 22, TR("допуск снаружи"), CD, 9.5, 1); g.T(x + w, yr + 22, Num(d.ratedMaxC, 0, true), CD, 9.5, 2);

			g.Frame(R0, 0, k);
			const double rx = 1256 - w;
			const double lim = (std::min)({ d.battHours, d.o2Hours > 0 ? d.o2Hours : 1e9, d.sorbHours > 0 ? d.sorbHours : 1e9 });
			const char* limName = lim == d.battHours ? TR("батарея") : lim == d.o2Hours ? TR("кислород") : TR("поглотитель");
			g.T(rx, y, TR("РАСХОД"), CP, 12.5); g.T(rx + w, y, TR("хватит на ") + Hours(lim) + " · " + limName, lim < 1 ? CR : CA, 11, 2);
			g.Line(rx, y + 6, rx + w, y + 6, CP, 1, 0.5, false, false);
			g.T(rx, y + 24, TR("Питание"), CD, 11); g.T(rx + w, y + 24, Num(d.powerW, 0) + TR(" Вт"), CW, 11.5, 2);
			struct Part { const char* n; double v; double a; } parts[] = { { TR("ЖО"), d.lifeW, 1 }, { TR("ТЕПЛО"), d.thermalW, 0.75 }, { TR("ПРИВ"), d.driveW, 0.5 }, { TR("ПОЛЕ"), d.fieldW, 0.4 }, { TR("СВЕТ"), d.lampW, 0.3 } };
			double px = rx;
			for (const Part& p : parts)
			{
				if (p.v < 0.5 || d.powerW < 1) continue;
				const double pw = w * p.v / d.powerW;
				g.Rect(px, y + 31, (std::max)(1.0, pw - 2), 7, -1, 0, 0, CP, p.a);
				if (pw > 34) g.T(px + (pw - 2) / 2, y + 50, std::string(p.n) + " " + Num(p.v, 0), CD, 8.5, 1);
				px += pw;
			}
			struct Row { std::string n, v; double f; bool warn; } rows2[] = {
				{ TR("Батарея"), Num(100 * d.batt, 0) + " % · " + Hours(d.battHours) + (Trend(1, 0.003) < 0 ? " ↓" : ""), d.batt, lim == d.battHours || Trend(1, 0.003) < 0 },
				{ TR("Кислород"), d.o2Vent ? TR("из воздуха · баллон ") + Num(100 * d.o2, 0) + " %" : Num(d.o2Flow * 60000, 2) + TR(" г/мин · ") + Hours(d.o2Hours) + (Trend(0, 0.003) < 0 ? " ↓" : ""), d.o2, lim == d.o2Hours || Trend(0, 0.003) < 0 },
				{ TR("Поглотитель CO2"), Num(100 * d.sorbent, 0) + " % · " + Hours(d.sorbHours) + (Trend(2, 0.003) < 0 ? " ↓" : ""), d.sorbent, lim == d.sorbHours || Trend(2, 0.003) < 0 },
				{ TR("Мет. водород"), d.jet ? (fly ? Num(d.jetFlow * 1000, 0) + TR(" г/с · ") : Num(100 * d.jetFuel, 0) + " % · ") + Num(d.jetDv, 0) + TR(" м/с") + (d.jetHover > 0 ? TR(" · висение ") + Clock(d.jetHover) : "") : TR("ранец снят"), d.jet ? d.jetFuel : 0, false },
				{ TR("Азот РСУ"), Num(100 * d.n2, 0) + " % · " + Num(d.n2Dv, 1) + TR(" м/с"), d.n2, false } };
			for (int i = 0; i < 5; ++i)
			{
				const double yy = y + 70 + i * 24; const Row& r = rows2[i];
				g.T(rx, yy, r.n, CD, 11); g.T(rx + w, yy, r.v, r.warn || (i == 3 && fly) ? CA : CW, 11.5, 2);
				g.Bar(rx, yy + 5, w, 6, r.f, r.f < 0.1 ? CR : r.f < 0.25 || r.warn ? CA : CP);
			}
		}

		// ---- side scales, low beside the MFDs ----
		auto vscale = [&](double x, double f, int col, const std::string& name, const std::string& val)
		{
			const double y0 = 476, hh = 196;
			g.Rect(x, y0, 10, hh, CD, 1, 0.8);
			f = std::clamp(f, 0.0, 1.0);
			if (f > 0.002) g.Rect(x + 1, y0 + 1 + (hh - 2) * (1 - f), 8, (hh - 2) * f, -1, 0, 0, col, 0.9);
			g.T(x + 5, y0 + hh + 16, name, CD, 9.5, 1); g.T(x + 5, y0 - 8, val, CW, 11, 1);
		};
		auto lvl = [](double f) { return f < 0.1 ? CR : f < 0.25 ? CA : CP; };
		if (d.suit && !pz && !bu)   // legacy: the light suit's layout before «Буран» (kept, not drawn for the light suit)
		{
			g.Frame(L0, 0, k);
			vscale(30, d.o2, lvl(d.o2), "O2", Num(100 * d.o2, 0)); vscale(58, d.sorbent, lvl(d.sorbent), "CO2", Num(100 * d.sorbent, 0)); vscale(86, d.batt, lvl(d.batt), TR("БАТ"), Num(100 * d.batt, 0));
			g.Frame(R0, 0, k);
			if (d.jet) { vscale(1212, d.jetFuel, lvl(d.jetFuel), TR("ТОПЛ"), Num(100 * d.jetFuel, 0)); vscale(1240, d.jetThrottle, CA, TR("ТЯГА"), Num(100 * d.jetThrottle, 0)); }
			else vscale(1240, d.n2, lvl(d.n2), "N2", Num(100 * d.n2, 0));
		}

		}   // (the view's block)
		if (glassItems && !(glassPass && hudMin)) {
		if (glassPass) glassBase(0);
		// ---- the middle: horizon, reticle, flight path, tapes ----
		g.Frame(C0, 0, k);
		{
			const double f = viewF / k;   // units per radian
			const double cx = 640, cy = 360;
			// the pitch ladder against the local horizon of the body she is at: in auto on the jet in flight and in space
			// (on foot she sees the horizon herself), or always, or never - the user's setting
			const OBJHANDLE ref = hBody ? hBody : v ? v->GetGravityRef() : nullptr;
			const bool flying = d.jet && d.jetFlying;
			const bool ladder = d.suit && mode != SYS && ref && !hideLadder && (horizonMode == 1 || (horizonMode == 0 && (flying || !surface)));
			if (ladder)
			{
				// the gaze's own pitch and roll (the camera, not the body: the head turns)
				VECTOR3 cpos, bpos; oapiCameraGlobalPos(&cpos); oapiGetGlobalPos(ref, &bpos);
				MATRIX3 Rc; viewRot(Rc);
				const VECTOR3 upL = Unit(cpos - bpos), cr = _V(Rc.m11, Rc.m21, Rc.m31), cu = _V(Rc.m12, Rc.m22, Rc.m32), cf = _V(Rc.m13, Rc.m23, Rc.m33);
				const double camPitch = std::asin(std::clamp(dotp(cf, upL), -1.0, 1.0)), camBank = std::atan2(dotp(cr, upL), dotp(cu, upL));
				const double ca = std::cos(camBank), sa = std::sin(camBank);
				// the window between the side panels and above the MFDs: lines are cut to it, not pinned at its edge
				const double WX0 = 300, WX1 = 980, WY0 = bu && alertLines ? 158 + alertLines * 20 + 14 : 105, WY1 = 565;   // «Буран»: clear of the messages
				auto clip = [&](double& x1, double& y1, double& x2, double& y2)
				{
					const double dx = x2 - x1, dy = y2 - y1; double t0 = 0, t1 = 1;
					const double p[4] = { -dx, dx, -dy, dy }, q[4] = { x1 - WX0, WX1 - x1, y1 - WY0, WY1 - y1 };
					for (int i = 0; i < 4; ++i)
					{
						if (p[i] == 0) { if (q[i] < 0) return false; continue; }
						const double r = q[i] / p[i];
						if (p[i] < 0) t0 = (std::max)(t0, r); else t1 = (std::min)(t1, r);
						if (t0 > t1) return false;
					}
					const double ax = x1, ay = y1; x1 = ax + t0 * dx; y1 = ay + t0 * dy; x2 = ax + t1 * dx; y2 = ay + t1 * dy;
					return true;
				};
				auto inWin = [&](double x, double y) { return x > WX0 && x < WX1 && y > WY0 && y < WY1; };
				// a rung at elevation e: its centre down the screen's "down" by f * tan(gaze pitch - e), turned with the roll
				for (int e = -90; e <= 90; e += 10)
				{
					const double de = camPitch - e * RAD;
					if (std::abs(de) > 75 * RAD) continue;
					const double off = std::tan(de) * f, px = cx - sa * off, py = cy + ca * off;
					if (e == 90 || e == -90)   // the zenith: a ring; the nadir: a ring with a cross
					{
						if (!inWin(px, py)) continue;
						g.Circle(px, py, 9, CP, 1.2, 1);
						if (e == -90) { g.Line(px - 6, py - 6, px + 6, py + 6, CP, 1, 1, false, false); g.Line(px - 6, py + 6, px + 6, py - 6, CP, 1, 1, false, false); }
						g.T(px + 14, py + 4, e > 0 ? TR("ЗЕНИТ") : TR("НАДИР"), CD, 9.5);
						continue;
					}
					const double a0 = e == 0 ? 28 : 24, a1 = e == 0 ? 130 : 56;   // the horizon long, the rungs short
					for (int side = -1; side <= 1; side += 2)
					{
						double x1 = px + side * a0 * ca, y1 = py + side * a0 * sa, x2 = px + side * a1 * ca, y2 = py + side * a1 * sa;
						if (clip(x1, y1, x2, y2)) g.Line(x1, y1, x2, y2, CP, e == 0 ? 1.3 : 1.1, e < 0 ? 0.6 : 1, e < 0, e >= 0);
					}
					if (e != 0)
					{
						const double lx = px - (a1 + 10) * ca, ly = py - (a1 + 10) * sa + 4;
						if (inWin(lx, ly)) g.T(lx, ly, e > 0 ? std::to_string(e) : "−" + std::to_string(-e), CD, 9.5, 2);
					}
				}
			}
			if (d.suit)
			{
				g.Circle(cx, cy, 3.5, CP, 1.3);
				g.Line(cx - 12, cy, cx - 6, cy, CP, 1); g.Line(cx + 6, cy, cx + 12, cy, CP, 1); g.Line(cx, cy - 12, cx, cy - 6, CP, 1);
			}
			// the velocity vector (flight path marker) in the view: where she is going, projected through the camera as it
			// really looks (any tilt, any bank). Near a surface: over the ground; in space: against the selected target or,
			// without one, the body she orbits. Behind her: the anti-velocity marker (a circle with a cross) where she comes from.
			if (d.suit && mode != SYS)
			{
				g.world = true;
				VECTOR3 vg{};
				if (surface) v->GetGroundspeedVector(FRAME_GLOBAL, vg);
				else if (const Target* s = Selected()) v->GetRelativeVel(s->h, vg);
				else v->GetRelativeVel(v->GetGravityRef(), vg);
				const double sp = length(vg);
				if (sp > 0.3)
				{
					MATRIX3 R; viewRot(R);
					const VECTOR3 c = tmul(R, vg / sp);
					auto put = [&](const VECTOR3& dir, double& x, double& y)   // false: outside the view
					{
						if (dir.z < 0.05) return false;
						x = cx + dir.x / dir.z * f; y = cy - dir.y / dir.z * f;
						return std::abs(x - cx) < 300 && std::abs(y - cy) < 220;
					};
					double x, y;
					if (put(c, x, y))
					{
						g.Circle(x, y, 7, CW, 1.6); g.Line(x - 20, y, x - 7, y, CW, 1.6); g.Line(x + 7, y, x + 20, y, CW, 1.6); g.Line(x, y - 7, x, y - 14, CW, 1.6);
						g.T(x + 24, y + 4, Num(sp, sp < 10 ? 1 : 0) + TR(" м/с"), CW, 9.5);
					}
					else if (put(-c, x, y))
					{
						g.Circle(x, y, 7, CA, 1.6); g.Line(x - 5, y - 5, x + 5, y + 5, CA, 1.4); g.Line(x - 5, y + 5, x + 5, y - 5, CA, 1.4);
					}
					else   // neither in the view: a mark on the ring towards the velocity
					{
						const double a = std::atan2(-c.y, c.x), r = 200;
						const double px = cx + r * std::cos(a), py = cy + r * std::sin(a) * 0.8;
						g.Circle(px, py, 5, CW, 1.4, 0.8); g.Line(px - 11, py, px - 5, py, CW, 1.4); g.Line(px + 5, py, px + 11, py, CW, 1.4);
					}
				}
			}
			g.world = false;
			auto tape = [&](double x, double val, double step, int maj, double px, int sgn, int dec, const std::string& label, bool hasTgt, double tgt)
			{
				const double hh = 150, top = cy - hh / 2, bot = cy + hh / 2;
				// «Буран»: the scale bows outward with the helmet's glass - the speed's to the left, the height's to the right
				auto tx = [&](double y) { const double t = (y - cy) / (hh / 2); return bu ? x + sgn * 9 * (1 - t * t) : x; };
				if (bu) { std::vector<V2> arc; for (int i = 0; i <= 24; ++i) { const double y = top + hh * i / 24; arc.push_back({ tx(y), y }); } g.Poly(arc, CD, 1, 0.7); }
				else g.Line(x, top, x, bot, CD, 1, 0.7, false, false);
				for (int kk = static_cast<int>(std::ceil((val - hh / 2 / px) / step)); kk <= static_cast<int>(std::floor((val + hh / 2 / px) / step)); ++kk)
				{
					const double vv = kk * step, y = cy - (vv - val) * px, xx = tx(y); const bool big = std::abs(kk) % maj == 0;
					g.Line(xx, y, xx + sgn * (big ? 8 : 4), y, CP, 1, 0.8, false, false);
					if (big && !(bu && std::abs(y - cy) < 13)) g.T(xx + sgn * 12, y + 4, Num(vv, dec), CD, 9.5, sgn > 0 ? 0 : 2);   // «Буран»: the labels give way to the pointer
				}
				if (bu)
				{
					// «Буран»: the reading outside the scale's labels - a pointer on the scale, a hairline out to a framed counter
					// (as on an instrument: nothing laid over the numbers of the scale)
					const double xc = tx(cy), bx0 = xc + sgn * 58, bx1 = xc + sgn * 116;
					g.Tri(xc + sgn * 1, cy, sgn * 9, -5, sgn * 9, 5, CW);
					g.Line(xc + sgn * 10, cy, bx0, cy, CP, 1, 0.7, false, false);
					g.Poly({ { bx0, cy }, { bx0 + sgn * 6, cy - 11 }, { bx1, cy - 11 }, { bx1, cy + 11 }, { bx0 + sgn * 6, cy + 11 } }, CP, 1.2, 1, true);
					g.T((bx0 + bx1) / 2 + sgn * 3, cy + 5, Num(val, dec), CW, 14, 1);
				}
				else
				{
				g.Poly({ { x, cy }, { x + sgn * 8, cy - 10 }, { x + sgn * 54, cy - 10 }, { x + sgn * 54, cy + 10 }, { x + sgn * 8, cy + 10 } }, CP, 1.2, 1, true);
				g.T(x + sgn * 31, cy + 5, Num(val, dec), CW, 13, 1);
				}
				if (hasTgt) { const double y = std::clamp(cy - (tgt - val) * px, top, bot); g.Tri(tx(y) - sgn, y, -sgn * 8, -6, -sgn * 8, 6, CA); }
				g.T(tx(top), top - 8, label, CD, 9.5, 1);
			};
			if (mode == FLIGHT && d.jet && surface)
			{
				tape(510, d.gs, 0.5, 2, 20, -1, 1, TR("ГОРИЗ м/с"), false, 0);
				tape(770, d.alt, d.alt > 30 ? 5 : 0.5, 2, d.alt > 30 ? 2 : 14, 1, 1, TR("ВЫСОТА м"), d.jetMode == 1, d.jetAltHold);
				// over the tapes: the two speeds that land her - horizontal (with the track and the drift) and vertical
				auto box = [&](double x0, const std::string& title, const std::string& val, int vc, const std::string& sub, int sc)
				{
					g.Rect(x0, 214, 124, 54, CP, 1.2, 0.8, CK, 0.35);
					g.T(x0 + 62, 227, title, CD, 9.5, 1);
					g.T(x0 + 62, 249, val, vc, 15, 1);
					g.T(x0 + 62, 263, sub, sc, 9, 1);
				};
				double track = std::atan2(gsH.x, gsH.z) * DEG; if (track < 0) track += 360;
				const double drift = Wrap(std::atan2(gsH.x, gsH.z) - hdg) * DEG;
				char tb[48]; snprintf(tb, sizeof tb, TR("ПУТЬ %03.0f° · СНОС %+.0f°"), track, d.gs > 0.3 ? drift : 0.0);
				const bool sinkHard = d.vs < -(1.0 + 0.8 * d.alt) && d.alt < 25;   // the same limit as the pack's ground guard
				if (bu)
				{
					g.T(483, 455, d.gs > 0.3 ? std::string(tb) : std::string(TR("на месте")), CD, 9.5, 1);
					g.T(797, 455, std::string(TR("ВЕРТ ")) + Num(d.vs, 1, true) + TR(" м/с"), sinkHard ? CR : CP, 10.5, 1);
					if (d.jet) g.T(483, 470, TR("ЗАПАС Δv ") + Num(d.jetDv, 0) + TR(" м/с"), d.jetDv < 30 ? CA : CD, 9.5, 1);
				}
				else box(424, TR("V ГОРИЗ"), Num(d.gs, 1) + TR(" м/с"), CW, d.gs > 0.3 ? std::string(tb) : std::string(TR("на месте")), CD);
				if (!bu) box(716, TR("V ВЕРТ"), Num(d.vs, 1, true) + TR(" м/с"), sinkHard ? CR : CW,
					d.vs > 0.2 ? TR("НАБОР ↑") : d.vs < -0.2 ? (sinkHard ? TR("СНИЖЕНИЕ ↓ БЫСТРО") : TR("СНИЖЕНИЕ ↓")) : TR("ВИСЕНИЕ"), sinkHard ? CR : CD);
			}
			if (mode == RDV)
				if (const Target* s = Selected())
				{
					tape(510, -s->rate, 0.05, 2, 220, -1, 2, TR("СБЛИЖ м/с"), true, (std::min)(1.0, s->dist / 100));
					tape(770, s->dist, s->dist > 200 ? 50 : 5, 2, s->dist > 200 ? 0.3 : 3, 1, 1, TR("ДАЛЬН м"), false, 0);
				}
		}

		}   // (the middle)
		if (glassItems)   // two keys over the horizon: the pitch ladder, the whole HUD (kept when the HUD is down to its arc)
		{
			if (glassPass) glassBase(0);
			g.Frame(C0, 0, k);
			if (bu)
			{
				const char* nm[2] = { hideLadder ? TR("ГОРИЗОНТ ▸") : TR("ГОРИЗОНТ"), hudMin ? TR("ХАД ▸") : TR("ХАД") };
				for (int i = 0; i < 2; ++i)
				{
					const double x = 574 + i * 70, y = 300, w = 62, on = i == 0 ? !hideLadder : !hudMin;
					g.Rect(x, y, w, 16, on ? CP : CD, 1, on ? 0.7 : 0.45);
					g.T(x + w / 2, y + 12, nm[i], on ? CP : CD, 8.5, 1);
					g.Hit(x, y, w, 16, i == 0 ? H_LADDER : H_HUDMIN);
				}
				// the MFDs' place (the user, 2026-10-20: by these two keys): centred over them - away from the horizon and the
				// flight path, where the look rests; a rare switch, so the higher, quieter place
				{
					const double x = 592, y = 280, w = 96;
					g.Rect(x, y, w, 16, CP, 1, 0.6);
					g.T(x + w / 2, y + 12, mfdSide ? TR("МФД · ПО СТОРОНАМ") : TR("МФД · В ЦЕНТРЕ"), CP, 8.5, 1);
					g.Hit(x, y, w, 16, H_MFDSIDE);
				}
			}
			g.NoPlate(); g.baseOn = false; g.NoPlate();
		}
		if (!glassPass) {   // (the view's block)
		// ---- the Pantsir's camera view («Обзорный прибор»): with the shutter closed she sees through the helmet's cameras -
		// the frame of a sight round the window, the camera in use and where it sits on the helmet, mil ticks on the frame's
		// sides, a short double flash when the picture moves to another camera; the shutter's line under it always
		if (pz && d.suit && d.firstPerson)
		{
			g.Frame(C0, 0, k);
			const double now = oapiGetSysTime();
			if (d.pzCam != lastPzCam) { if (lastPzCam != -9) camSwitchT = now; lastPzCam = d.pzCam; }
			const double fx0 = 300, fx1 = 980, fy0 = 178, fy1 = 470, arm = 46;
			if (!d.pzWindow)
			{
				const double fl = camSwitchT >= 0 && now - camSwitchT < 0.35 && now >= camSwitchT ? 1 - (now - camSwitchT) / 0.35 : 0.0;
				for (int i = 0; i < 4; ++i)
				{
					const double bx = i & 1 ? fx1 : fx0, by = i & 2 ? fy1 : fy0, dx = i & 1 ? -arm : arm, dy = i & 2 ? -arm : arm;
					g.Line(bx, by, bx + dx, by, CP, 2.2); g.Line(bx, by, bx, by + dy, CP, 2.2);
					if (fl > 0) { const double o = 6; g.Line(bx - (i & 1 ? -o : o), by - (i & 2 ? -o : o), bx + dx, by - (i & 2 ? -o : o), CW, 1.4, fl); g.Line(bx - (i & 1 ? -o : o), by - (i & 2 ? -o : o), bx - (i & 1 ? -o : o), by + dy, CW, 1.4, fl); }
				}
				// mil ticks on the sides: every 10 mrad of the camera's view, a long one each 50
				const double f = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture())) / k;
				for (int m = -150; m <= 150; m += 10)
				{
					const double y = 360 - std::tan(m * 0.001) * f; if (y < fy0 + arm + 8 || y > fy1 - arm - 8) continue;
					const double l = m % 50 == 0 ? 9 : 4;
					g.Line(fx0, y, fx0 + l, y, CP, 1, 0.7, false, false); g.Line(fx1, y, fx1 - l, y, CP, 1, 0.7, false, false);
				}
				// the camera in use and where it sits: the helmet seen from the front, three eyes, the active one lit
				static const char* const CAMN[3] = { "БОК Л", "СТЕРЕО", "БОК П" };
				const int ci = std::clamp(d.pzCam, -1, 1) + 1;
				g.T(fx0 + 10, fy0 + 18, std::string(TR("КАМЕРА · ")) + TR(CAMN[ci]), CW, 11.5);
				const double hx = fx0 + 30, hy = fy0 + 40;
				g.Circle(hx, hy, 14, CD, 1, 0.8);
				const double ex[3] = { hx - 12, hx, hx + 12 }, ey[3] = { hy + 2, hy - 7, hy + 2 };
				for (int i = 0; i < 3; ++i) g.Circle(ex[i], ey[i], 2.6, i == ci ? CW : CD, 1.2, 1, i == ci ? CW : -1, 1);
			}
			// the shutter, always
			std::string sh = d.pzWindow ? std::string(TR("ШТОРКА ОТКРЫТА · вид через окно · Shift+V — камеры"))
				: std::string(TR("ШТОРКА ЗАКРЫТА")) + (d.pzAutoShut ? std::string(TR(" · АВТО: ")) + d.pzShutReason : std::string(TR(" · Shift+V — окно")));
			const int shc = d.pzAutoShut ? CA : d.pzWindow ? CP : CD;
			g.T(fx0 + 54, fy0 + 40, sh, shc, 10, 0);   // inside the frame's corner, by the camera's name (the MFDs' titles are under the frame)
		}

		// ---- the selected target in the view: a box, or an arrow at the edge ----
		if (const Target* s = Selected(); s && d.suit && mode != SYS)
		{
			g.world = true;   // the box is where the target is in the view
			VECTOR3 cp, tp; oapiCameraGlobalPos(&cp); oapiGetGlobalPos(s->h, &tp);
			MATRIX3 R; oapiCameraRotationMatrix(&R);
			const VECTOR3 cam = tmul(R, tp - cp);
			const double fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
			g.Frame(0, 0, 1);
			const std::string lab = s->name + " · " + Dist(s->dist);
			const double sx = cam.z > 0 ? W / 2 + cam.x / cam.z * fpx : 0, sy = cam.z > 0 ? H / 2 - cam.y / cam.z * fpx : 0;
			if (cam.z > 0 && sx > 40 * k && sx < W - 40 * k && sy > 90 * k && sy < H - 40 * k)
			{
				// a thin ring of light round it and a hairline up to its name, dim: drawn into the scene, not over it
				const double rr = std::clamp((s->base ? 30.0 : oapiGetSize(s->h)) / cam.z * fpx, 9 * k, 70 * k);
				g.Circle(sx, sy, rr, CA, 1.0, 0.55);
				const double lx = sx + rr * 0.7 + 18 * k, ly = sy - rr * 0.7 - 18 * k;
				g.Line(sx + rr * 0.7, sy - rr * 0.7, lx, ly, CA, 0.8, 0.45, false, false);
				g.Frame(lx - 640 * k, ly - 360 * k, k);
				g.T(644, 357, lab, CA, 10, 0);
			}
			else
			{
				// off the view: an arrow on a ring around the centre
				double ax = cam.x, ay = cam.y; if (cam.z < 0 && std::hypot(ax, ay) < 1e-6) ax = 1;
				const double a = std::atan2(-ay, ax);
				g.Frame(C0, 0, k);
				const double r = 230, px = 640 + r * std::cos(a), py = 360 + r * std::sin(a) * 0.8;
				g.Tri(px + 12 * std::cos(a), py + 12 * std::sin(a), -12 * std::cos(a) - 7 * std::sin(a), -12 * std::sin(a) + 7 * std::cos(a), -12 * std::cos(a) + 7 * std::sin(a), -12 * std::sin(a) - 7 * std::cos(a), CA);
				g.T(px - 16 * std::cos(a), py - 16 * std::sin(a) + 4, lab, CA, 10, std::cos(a) > 0.3 ? 2 : std::cos(a) < -0.3 ? 0 : 1);
			}
		}

		g.world = false;
		// ---- particles through the optics: sparks and short streaks, ~400 a second per Sv/h (the field thins them).
		// Drawn in one batch: one pen, plain lines - no glow, no fills, no state change per spark ----
		if (d.suit && d.firstPerson)
		{
			const double t = oapiGetSimTime(), dt = sparkT < 0 || t < sparkT ? 0.0 : (std::min)(0.2, t - sparkT); sparkT = t;
			sparkAcc += 400 * (std::max)(0.0, d.radRate) * dt;
			while (sparkAcc >= 1 && sparks.size() < 600)
			{
				sparkAcc -= 1;
				const bool streak = Rand() < 0.2; const double ang = Rand() * PI2, len = streak ? 3 + 9 * Rand() : 1.0;   // mostly 1-2 px hits
				sparks.push_back({ Rand() * W, Rand() * H, std::cos(ang) * len, std::sin(ang) * len, 0.03 + 0.06 * Rand() });
			}
			if (sparkAcc > 1) sparkAcc = 0;
			if (!sparks.empty())
			{
				g.Frame(0, 0, 1); g.world = true;
				for (auto& sp : sparks) { g.Raw(sp.x, sp.y, sp.x + sp.dx, sp.y + sp.dy, CW); sp.life -= dt; }   // 1 px, not scaled up
				sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0; }), sparks.end());
				g.world = false;
			}
		}

		// ---- bottom: autopilot line and the numbers ----
		g.Frame(C0, 0, k);
		const double by = pz ? -244 : 0;   // the Pantsir: the line, the pack's state and the numbers above its MFDs
		if (d.suit)
		{
			std::vector<V2> pts;
			for (int i = 0; i <= 24; ++i) { const double a = PI * (0.385 + 0.23 * i / 24); pts.push_back({ 640 + 1000 * std::cos(a), -260 + 1000 * std::sin(a) }); }
			if (!pz && !bu) g.Poly(pts, CP, 1.2, 0.5);
			std::string ann;
			if (d.jet && d.jetMode == 1) ann = TR("АП · УДЕРЖАНИЕ ВЫСОТЫ ") + Num(d.jetAltHold, 1) + TR(" м");
			else if (d.jet && d.jetMode == 2) ann = TR("АП · АВТОПОСАДКА");
			if (d.apMode) ann = TR("АП · ") + d.apStatus + (ann.empty() ? "" : "   ▸   " + ann.substr(ann.find("·") + 3));
			if (d.jet && d.boost) ann += ann.empty() ? TR("ПОЛНЫЙ ВЕКТОР") : TR("   ▸   ПОЛНЫЙ ВЕКТОР");
			if (!ann.empty()) { if (bu) g.T(640, 672, ann, CA, 12, 1); else { const double w = g.TW(ann, 12.5) + 24; g.Rect(640 - w / 2, 618 + by, w, 22, CA, 1.2, 1, CA, 0.12); g.T(640, 634 + by, ann, CA, 12.5, 1); } }
			// the jet pack's working state, all the time it is worn (the user: its modes on the display permanently): which
			// pack, what drives her now (the fans' speed, the boost, the turbine's power in hot air, no air), the control mode
			if (d.jet)
			{
				std::string js = d.jetModel == 3 ? TR("МОДУЛЬ ПОЛЁТА: ") : d.jetModel == 2 ? TR("РАНЕЦ ТРИЛИСТНИК: ") : d.jetModel == 1 ? TR("РАНЕЦ СОТЫ: ") : TR("РАНЕЦ НАПЛЕЧНЫЙ: ");
				Col jc = CP;
				if (d.jetFuel <= 0) { js += TR("ТОПЛИВА НЕТ"); jc = CR; }
				else if (d.jetModel == 1 || d.jetModel == 2)   // the air packs: «Соты», «Трилистник»
				{
					if (d.jetFanAuth <= 0.01) { js += std::string(TR("НЕТ АТМОСФЕРЫ · ДОЖИГ ")) + Num(100 * d.jetRocket, 0) + " %"; jc = CA; }
					else if (d.jetDeploy < 0.98) js += TR("ЗАПУСК");
					else if (!d.jetTurbine) js += TR("ЭНЕРГОБЛОК СТОП");
					else
					{
						js += std::string(TR("СОТЫ ")) + Num(100 * d.jetFanLvl, 0) + " %";
						if (d.jetRocket > 0.005) { js += std::string(TR(" · ДОЖИГ ")) + Num(100 * d.jetRocket, 0) + " %"; jc = CA; }
						if (d.jetFanAuth < 0.6) js += std::string(TR(" · СОТЫ МАКС ")) + Num(100 * d.jetFanAuth, 0) + " %";
					}
					if (d.jetHeat < 0.999) { js += std::string(TR(" · ЭНЕРГОБЛОК ")) + Num(100 * d.jetHeat, 0) + TR(" % ЖАРА"); jc = d.jetHeat < 0.5 ? CR : CA; }
				}
				else { js += d.jetDeploy < 0.98 ? std::string(TR("ГОТОВ")) : std::string(TR("ТЯГА ")) + Num(100 * d.jetRocket, 0) + " %"; if (d.jetForsazh) { js += TR(" · ФОРСАЖ"); jc = CA; }
					for (int b = 0; b < 4; ++b)   // the chambers' faults: П/Л - the capsule, 1 outboard / 2 inboard
					{
						const int f = (d.jetChFault >> (2 * b)) & 3;
						if (!f) continue;
						js += std::string(TR(" · КАМЕРА ")) + (b < 2 ? TR("П") : TR("Л")) + std::to_string(b % 2 + 1) + ((f & 1) ? TR(" ОТКАЗ") : TR(" ПЕРЕГРЕВ"));
						jc = (f & 1) ? CR : CA;
					}
				}
				js += d.jetMode == 1 ? TR(" · УДЕРЖ") : d.jetMode == 2 ? TR(" · ПОСАДКА") : d.jetMode == 3 ? TR(" · ВЕКТОР") : d.jetAssist ? TR(" · ПОМОЩН") : TR(" · РУЧН");
				if (bu) g.T(640, 652, js, jc, 12.5, 1);
				else { const double w = g.TW(js, 11.5) + 20; g.Rect(640 - w / 2, 594 + by, w, 20, jc, 1.0, 1, jc, 0.08); g.T(640, 609 + by, js, jc, 11.5, 1); }
			}
			std::vector<std::tuple<std::string, std::string, int>> cells;
			const Target* s = Selected();
			const MpuState& ms = mpu;
			if (onMpu)   // at the post of a platform: the machine's numbers instead of her own
			{
				const double kmh = std::abs(ms.speed) * 3.6;
				cells = { { TR("СКОРОСТЬ"), (ms.speed < -0.1 ? TR("назад ") : "") + Num(kmh, 0) + TR(" км/ч"), CW },
				          { TR("ХОД"), (ms.full ? std::string(TR("ПОЛНЫЙ · ")) : std::string("")) + Num(ms.speedMax * 3.6, 0) + TR(" км/ч"), ms.full ? CA : CW },
				          { TR("РУЛЬ"), std::abs(ms.steer) < 0.05 ? std::string(TR("прямо")) : std::string(ms.steer > 0 ? TR("вправо ") : TR("влево ")) + Num(100 * std::abs(ms.steer), 0) + " %", CW },
				          { TR("ПЛАТФОРМА"), Num(ms.deck, 2) + TR(" м"), CW },
				          { TR("ТОРМОЗ"), ms.brake ? TR("стояночный") : TR("снят"), ms.brake ? CA : CW },
				          { TR("ЗАРЯД"), ms.charge < 0 ? std::string("—") : Num(100 * ms.charge, 0) + " %", ms.charge >= 0 && ms.charge < 0.15 ? CA : CW } };
			}
			else if (bu) {}   // «Буран»: the numbers are on the tapes and the counters
			else if (mode == FLIGHT && d.jet)
				cells = { { TR("ВЫСОТА"), Num(d.alt, 1) + TR(" м"), CW }, { TR("ВЕРТ"), Num(d.vs, 1, true) + TR(" м/с"), d.vs < -3 ? CA : CW }, { TR("ГОРИЗ"), Num(d.gs, 1) + TR(" м/с"), CW },
				          { TR("ТЯГА"), Num(100 * d.jetThrottle, 0) + " %", CW }, { TR("ЗАПАС Δv"), Num(d.jetDv, 0) + TR(" м/с"), d.jetDv < 30 ? CA : CW } };
			else if (mode == RDV && s)
				cells = { { TR("ДАЛЬН"), Dist(s->dist), CW }, { TR("СБЛИЖ"), Num(s->rate, 2, true), CA }, { TR("ΔV СИНХР"), Num(length(s->relV), 2), CW },
				          { TR("ЦЕЛЬ"), s->name, CA }, { TR("ЗАПАС Δv"), Num(d.jet ? d.jetDv : d.n2Dv, d.jet ? 0 : 1) + TR(" м/с"), CW } };
			else if (mode == SYS)
				cells = { { TR("БАТАРЕЯ"), Num(100 * d.batt, 0) + " %", CW }, { TR("РАСХОД"), Num(d.powerW, 0) + TR(" Вт"), CW }, { TR("ХВАТИТ"), Hours(d.battHours), CW },
				          { "O2", d.o2Vent ? std::string(TR("воздух")) : Hours(d.o2Hours), CW }, { TR("ТЕПЛО"), Num(-d.heatW, 0, true) + TR(" Вт"), CW } };
			else if (pz)   // the Pantsir's navigation line
			{
				char hb[16]; snprintf(hb, sizeof hb, "%03.0f°", std::fmod(hdg * DEG + 360, 360));
				cells = { { TR("СКОРОСТЬ"), Num(surface ? d.speed : std::hypot(gsH.x, gsH.z), 1) + TR(" м/с"), CW },   // (the heading is on the tape above)
				          { TR("ЦЕЛЬ"), s ? s->name + " · " + Dist(s->dist) : std::string(TR("нет")), s ? CA : CD },
				          { d.hasGround ? TR("ГРУНТ") : TR("СРЕДА"), Num(d.hasGround ? d.groundC : d.envC, 0, true) + " °C", d.hasGround && d.groundC > d.ratedMaxC ? CR : CW } };
			}
			else
				cells = { { TR("СКОРОСТЬ"), Num(surface ? d.speed : std::hypot(gsH.x, gsH.z), 1) + TR(" м/с"), CW }, { TR("ПОХОДКА"), d.servo ? TR("серво") : d.speed > 2.2 ? TR("бег") : d.speed > 0.1 ? TR("шаг") : TR("стоит"), d.servo ? CA : CW },
				          { TR("ЩИТОК"), d.shadeDown ? TR("опущен") : TR("поднят"), CW }, { TR("ФОНАРИ"), d.lampsOn ? std::string(TR(d.lampMode == 0 ? "шир" : d.lampMode == 2 ? "дал" : "раб")) + " · " + Num(d.lampW, 0) + TR(" Вт") : std::string(TR("выкл")), CW },
				          { d.hasGround ? TR("ГРУНТ") : TR("СРЕДА"), Num(d.hasGround ? d.groundC : d.envC, 0, true) + " °C", d.hasGround && d.groundC > 80 ? CA : CW } };
			const double cw = pz ? 150.0 : 85.0;
			const double x0 = 640 - (cells.size() - 1) * cw / 2;   // centred: five cells as before, six for the machine
			if (pz)   // the Pantsir: each number in a counter's frame, as its ЗАПАС drum, clear above the MFDs
				for (size_t i = 0; i < cells.size(); ++i)
				{
					const double cx = x0 + i * cw, yy = 398;
					const std::string& v = std::get<1>(cells[i]);
					const double bw = (std::max)(64.0, g.TW(v, 14) + 16);
					g.T(cx, yy, std::get<0>(cells[i]), CD, 9.5, 1);
					g.Rect(cx - bw / 2, yy + 4, bw, 22, CP, 1, 0.6, CK, 0.5);
					g.T(cx, yy + 20, v, std::get<2>(cells[i]), 14, 1);
				}
			else
				for (size_t i = 0; i < cells.size(); ++i) g.Cell(x0 + i * cw, 664 + by, std::get<0>(cells[i]), std::get<1>(cells[i]), std::get<2>(cells[i]), 1);
			// the pods, seen from her left side
			if (fly && surface && !bu)
				for (int i = 0; i < 2; ++i)
				{
					const double x = i == 0 ? 425 : 855, y = 664, t = d.jetTilt[i];
					g.Circle(x, y, 7, CP, 2);
					g.Line(x, y, x - 22 * std::sin(t), y + 22 * std::cos(t), d.jetThrottle > 0.01 ? CA : CP, 2.5);
					g.T(x, y + 40, std::string(i == 0 ? TR("П ") : TR("Л ")) + Num(t * DEG, 0, true) + "°", CW, 10, 1);
				}
		}

		// ---- the keys ----
		{
			std::string keys;
			if (!d.suit) keys = TR("W/S ход  A/D поворот  Q/E шаг  Shift бег  Пробел прыжок  K скафандр");
			else if (mode == FLIGHT && d.jet) keys = std::string(d.jetAssist ? TR("Пробел / Ctrl — выше / ниже") : TR("Пробел / Ctrl — тяга больше / меньше")) + TR("   Shift+Пробел — полная   Shift+Ctrl — выкл   W/S — вектор   Q/E — вбок   A/D — поворот");
			else if (mode == RDV) keys = TR("РСУ: цифровой блок   ·   цель и автопилот — мышью на правом МФД");
			else if (mode == SYS) keys = TR("режим ещё раз — снова автоматически   ·   заголовок МФД — свернуть");
			else keys = TR("WASD — шаг   Shift — серво   B — ранец   Shift+V — щиток   L — фонари   K — скафандр");
			if (!pz && !bu) g.T(640, 712, keys, CD, 10.5, 1);
		}
		}   // (!glassPass)
		if (!d.suit) return;

		// ---- the two MFDs, low in the corners, each folds on its own key ----
		const double ms = pz ? 0.62 : bu ? 0.85 : 0.78;   // both MFDs the same size, clear of the panels above (the Pantsir: smaller, between its panels)
		auto mfd = [&](bool left)
		{
			const auto rp = RPages();
			if (!left && std::find(rp.begin(), rp.end(), static_cast<RPage>(rpage)) == rp.end()) rpage = R_TARGETS;
			// the heavy suit: landscape MFDs (440 x 282 frame units, 340 x 218 on the layout) filling the room between its
			// panels; the pages inside at 0.767 of their size, the local map in the full width (a wider window)
			const double FW = pz ? 440 : 340, ph = pz ? 282 : 400, mf = pz ? 340.0 / 440 : ms, sub = pz ? 0.767 : 1.0;
			const bool wideMap = pz && left && lpage == L_LOCAL;
			g.clipL = 3; g.clipR = FW - 3;   // nothing written past the panel's edges
			double x0 = pz ? C0 + (left ? 640 - 6 - 340 : 640 + 6) * k : left ? L0 + 112 * k : R0 + (1168 - 340 * ms) * k, y0 = (686 - ph * mf) * k;
			if (bu) { double r[4][4]; BuranRects(W, H, r); x0 = r[left ? 1 : 0][0]; y0 = r[left ? 1 : 0][1]; if (!SetPlate(c, x0, y0, FW * k * mf, ph * k * mf, 1)) { g.clipL = -1e9; g.clipR = 1e9; return; } }
			g.Frame(x0, y0, k * mf);
			if (!bu) { g.Rect(0, 0, FW, ph, -1, 0, 0, CK, 0.38 + 0.4 * (std::min)(1.0, g.boost)); g.Rect(0, 0, FW, ph, CP, 1, 0.32, CP, 0.06); }
			for (int i = 0; i < 4 && (!bu || hudTheme == 3); ++i)
			{
				const double bx = i & 1 ? FW : 0, by = i & 2 ? ph : 0, dx = i & 1 ? -12 : 12, dy = i & 2 ? -12 : 12;
				g.Line(bx, by, bx + dx, by, CP, 2); g.Line(bx, by, bx, by + dy, CP, 2);
			}
			const char* LT[] = { TR("КАРТА"), TR("ОРБИТА"), TR("ПИТАНИЕ И ТЕПЛО"), TR("ЗАПАСЫ И РАСХОД"), TR("ОПЦИИ") };
			const char* LTAB[] = { TR("МЕСТН"), TR("ОРБИТА"), TR("ПИТАНИЕ"), TR("ЗАПАСЫ"), TR("ОПЦИИ") };
			const char* RT[] = { TR("ЦЕЛИ"), TR("ПЕРЕЛЁТ"), TR("ПОСАДКА"), TR("СБЛИЖЕНИЕ"), TR("СТЫКОВКА"), TR("ОРГАНИЗМ"), TR("ПОЛЁТ"), TR("МАШИНА"), TR("СОЕДИНЕНИЯ") };
			const char* RTAB[] = { TR("ЦЕЛИ"), TR("ПЕРЕЛЁТ"), TR("ПОСАДКА"), TR("СБЛИЖ"), TR("СТЫК"), TR("ОРГАНИЗМ"), TR("ПОЛЁТ"), TR("МАШИНА"), TR("СОЕД") };
			const Target* s = Selected();
			g.T(9, 17, left ? LT[lpage] : RT[rpage], CP, 13);
			g.Hit(0, 0, 200, 20, left ? H_LFOLD : H_RFOLD);
			g.Tri(200, 15, -4, -6, 4, -6, CD);   // fold mark
			if (left && lpage == L_LOCAL && surface) g.T(FW - 9, 16, TR("МАСШТАБ ") + Dist(ZOOM[zoom]), CA, 9.5, 2);
			if (left && lpage == L_OPTS)   // the language: one setting with the crew's menu
				for (int i = 0; i < 2; ++i)
				{
					const bool on = (i == 1) == g_en; const double x = FW - 78 + i * 38;
					if (on) g.Rect(x, 3, 34, 15, -1, 0, 0, CA, 1); else g.Rect(x, 3, 34, 15, CP, 1, 0.7, CP, 0.08);
					g.T(x + 17, 15, i == 0 ? "RU" : "EN", on ? CK : CP, 9.5, 1);
					if (!on) g.Hit(x, 1, 34, 19, H_LANG, i);
				}
			if (!left) { g.T(FW - 9, 16, s ? TR("ЦЕЛЬ: ") + s->name + "  ▸" : TR("ЦЕЛИ НЕТ"), CA, 9.5, 2); g.Hit(FW - 135, 0, 135, 20, H_NEXT); }
			double tx = 7;
			const int nt = left ? L_COUNT : static_cast<int>(rp.size());
			for (int i = 0; i < nt; ++i)
			{
				const int id = left ? i : rp[i]; const bool on = left ? id == lpage : id == rpage;
				const std::string nm = left ? LTAB[id] : RTAB[id];
				const double w = g.TW(nm, 9.5) + 14;
				if (on) g.Rect(tx, 22, w, 15, CP, 1, 0.55, CP, 0.13);
				g.Hit(tx, 20, w, 19, left ? H_LTAB : H_RTAB, id);
				g.T(tx + w / 2, 33, nm, on ? CP : CD, 9.5, 1);
				tx += w + 3;
			}
			g.Line(0, 40, FW, 40, CP, 1, 0.28, false, false);
			// the page: centred (the right MFD: kept left, its autopilot keys in a column on the right); the map: full width
			const double pageOff = !pz || wideMap ? 0.0 : left ? (FW - 340 * sub) / 2 : 8.0;
			gMapW = wideMap ? FW / sub : 340;
			g.clipR = wideMap ? gMapW - 3 : 337;
			g.Frame(x0 + pageOff * k * mf, y0 + 40 * k * mf, k * mf * sub);
			if (left) { switch (lpage) { case L_LOCAL: pages::Local(c); break; case L_ORBIT: pages::Orbit(c); break; case L_POWER: pages::Power(c); break; case L_RES: pages::Reserves(c); break; default: pages::Opts(c); } }
			else
			{
				switch (rpage)
				{
				case R_TARGETS: pages::Targets(c); break; case R_TRANSFER: pages::Transfer(c); break; case R_LANDING: pages::Landing(c); break; case R_FLIGHT: pages::Flight(c); break;
				case R_APPROACH: pages::Approach(c); break; case R_DOCK: pages::Dock(c); break; case R_MACHINE: pages::Machine(c); break; case R_LINKS: pages::Links(c); break; default: pages::Body(c);
				}
			}
			gMapW = 340; g.clipR = FW - 3;
			g.Frame(x0, y0, k * mf);
			double fy = pz ? 276 : 394;
			if (!left)
			{
				// the autopilot: filled = on, framed = armed, dim = not available here
				struct B { const char* n; int st; int req; };   // 0 off, 1 armed, 2 on, -1 not available
				std::vector<B> b;
				const bool hasSel = Selected() != nullptr, jetOk = d.jet && d.jetFuel > 0;
				if (surface) b = { { TR("УДЕРЖ ВЫС"), jetOk ? (d.jetMode == 1 && !d.apMode ? 2 : 0) : -1, AP_ALT },
				                   { TR("ПЕРЕЛЁТ"), jetOk && hasSel ? (d.apMode == 2 ? 2 : rpage == R_TRANSFER ? 1 : 0) : -1, 2 },
				                   { TR("ПОСАДКА"), jetOk ? (d.jetMode == 2 ? 2 : rpage == R_LANDING ? 1 : 0) : -1, AP_LAND },
				                   { TR("СТОП"), jetOk ? (d.jetBraking ? 2 : 0) : -1, AP_STOP } };
				else
				{
					const bool rcsOk = d.rcs == 1;
					b = { { TR("СБЛИЖ"), rcsOk && hasSel ? (d.apMode == 5 ? 2 : rpage == R_APPROACH ? 1 : 0) : -1, 5 },
					      { TR("СИНХР"), rcsOk && hasSel ? (d.apMode == 3 ? 2 : 0) : -1, 3 },
					      { TR("УДЕРЖ"), rcsOk && hasSel ? (d.apMode == 4 ? 2 : 0) : -1, 4 },
					      { TR("СТЫК"), rcsOk && hasSel ? (d.apMode == 6 ? 2 : rpage == R_DOCK ? 1 : 0) : -1, 6 } };
				}
				if (!d.apStatus.empty()) { if (pz) { g.clipL = FW - 96; g.clipR = FW - 4; g.T(FW - 92, 170, d.apStatus, d.apMode ? CA : CD, 9.5); g.clipL = 3; g.clipR = FW - 3; } else g.T(9, 353, d.apStatus, d.apMode ? CA : CD, 9.5); }
				const double bw = pz ? 86.0 : (340 - 14 - 12) / 4.0;
				for (int i = 0; i < 4; ++i)
				{
					const double bx = pz ? FW - 94 : 7 + i * (bw + 4), by = pz ? 52 + i * 28 : 360;   // the heavy suit: a column on the right
					if (b[i].st >= 0) g.Hit(bx, by, bw, 20, H_AP, b[i].req);
					if (b[i].st == 2) { g.Rect(bx, by, bw, 20, -1, 0, 0, CA, 1); g.T(bx + bw / 2, by + 14, b[i].n, CK, 9.5, 1); }
					else { g.Rect(bx, by, bw, 20, b[i].st == 1 ? CA : CD, 1, b[i].st < 0 ? 0.35 : 0.8); g.T(bx + bw / 2, by + 14, b[i].n, b[i].st == 1 ? CA : CD, 9.5, 1); }
				}
				fy = pz ? 276 : 394;
			}
			g.T(9, fy, TR("заголовок — свернуть"), CD, 9, 0);
			g.clipL = -1e9; g.clipR = 1e9;
			g.NoPlate();
		};
		auto tab = [&](bool left)
		{
			double x0 = pz ? C0 + (left ? 640 - 6 - 252 : 640 + 6) * k : left ? L0 + 112 * k : R0 + (1168 - 252) * k, ty = 662 * k;
			if (bu) { double r[4][4]; BuranRects(W, H, r); x0 = r[left ? 1 : 0][0]; ty = r[left ? 1 : 0][1]; if (!SetPlate(c, x0, ty, 252 * k, 24 * k, 3)) return; }
			g.Frame(x0, ty, k);
			g.Rect(0, 0, 252, 24, CP, 1, 0.4, CP, 0.08);
			g.Hit(0, 0, 252, 24, left ? H_LFOLD : H_RFOLD);
			const Target* s = Selected();
			std::string t = left ? std::string(TR("M · КАРТА")) : std::string("N · ") + (s ? TR("ЦЕЛЬ: ") + s->name : TR("ЦЕЛИ НЕТ"));
			g.T(10, 16, t, CP, 11);
			if (!left && (d.apMode || (d.jet && d.jetMode))) g.T(244, 16, d.apMode ? std::string(TR("АП: ")) + (d.apStatus.substr(0, d.apStatus.find(" "))) : d.jetMode == 1 ? TR("АП: УДЕРЖ") : TR("АП: ПОСАДКА"), CA, 10, 2);
		};
		g.baseOn = false; g.NoPlate();
		if (!(bu && hudMin)) { if (openL) mfd(true); else tab(true); if (openR) mfd(false); else tab(false); }
		if (bu && !hudMin) BuranLow(c, W, H, k, L0, R0);
		if (glassPass && !hudMin && !glassLines.empty())
		{
			// the lines low on the glass: glyph by glyph, each on its own place of the glass - the line bends with it
			glassBase(3);
			{
				g.Frame(C0, 0, k); g.noHit = true;
				for (const GlassLine& l : glassLines)
				{
					double x = 640 - g.TW(l.text, l.size) / 2;
					for (const std::string& ch : Chars(l.text)) { g.T(x, l.y, ch, l.col, l.size, 0); x += g.TW(ch, l.size); }
				}
				g.baseOn = false; g.NoPlate();
			}
		}
		if (glassPass) return;
		if (g.cv) { if (overlayNative) overlayNative(gx, g.cv, W, H, g.kx, g.ky); }   // the crew's signs, on top, in the same surface
		else if (overlay) overlay(skp, W, H, g.kx, g.ky);
	}

	// ================= the helmet display =================
	// One group, four corners: the plate is moved every step to cover the view exactly, so a texture pixel lands on its
	// screen pixel. Built spanning a box round the eye so its bounds never cull it before the first move.
	UINT SuitHud::HelmetMesh(VESSEL* v)
	{
		if (plateOwner == v && plateIdx != static_cast<UINT>(-1)) return plateIdx;   // this body has its plate already
		// Meshes\Tantra\VisorLayers.msh: group 0 the sun's spot (black, its texture's alpha, a little farther out), group 1
		// the modulator (black, our texture's alpha), group 2 the HUD (FLAG 3, the client's VC HUD surface), group 3 the light
		// layer. The dimming first: the HUD's light is added over the dimmed view, never dimmed itself.
		// The template must live as long as the session (the vessel copies it each time it builds its visual): a global mesh.
		// Taken again for every body - Orbiter frees its global meshes when a session ends, so a handle kept from an earlier
		// session points at nothing (the plate then had no textures to bind, and the display was gone after a restart)
		const MESHHANDLE tpl = oapiLoadMeshGlobal("Tantra\\VisorLayers");
		if (!tpl) { oapiWriteLog(const_cast<char*>("OrbiterCrew: Meshes\\Tantra\\VisorLayers.msh missing - no helmet display")); return plateIdx; }
		plateIdx = v->AddMesh(tpl); plateOwner = v;
		v->SetMeshVisibilityMode(plateIdx, MESHVIS_NEVER);
		return plateIdx;
	}

	// the conformal part of the helmet HUD, drawn inside the scene by the AtmoFX D3D9 client (AtmoFXHud.h; without the fork
	// the calls do nothing and the 2D pages stay as they are): the terrain's contours («СИНТЕТИКА»), the touchdown point
	// while the flight module descends, the selected target's pad, and ПЕРЕЛЁТ's set-down pad with gates down its approach
	void SuitHud::ConformalHud(VESSEL* v, const HudData& d)
	{
		if (!v || !hBody || !surface)
		{
			for (int id = 0; id < 8; ++id)
				if (hudMarks[id]) { AtmoFXHud::Marker(7400 + id, hBody, 0, 0, 0, AtmoFXHud::REMOVE, 0); hudMarks[id] = 0; }
			return;
		}
		const Palette& P = look ? LOOK_PAL[look] : PALS[pal];
		const DWORD c = P.col[CP];
		const VECTOR3 rgb = _V((c & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, ((c >> 16) & 0xFF) / 255.0);
		const double step = alt < 60 ? 5.0 : alt < 400 ? 10.0 : alt < 2000 ? 25.0 : 100.0;   // the contour step by her height
		AtmoFXHud::Frame((synth ? AtmoFXHud::TERRAIN : 0) | AtmoFXHud::MARKERS, rgb, step);
		bool want[8] = {};
		const double R = bodyR > 0 ? bodyR : oapiGetSize(hBody), cl = (std::max)(0.05, std::cos(lat));
		auto set = [&](int id, double la, double lo, double h, int type, double size, double hd) {
			AtmoFXHud::Marker(7400 + id, hBody, lo, la, h, type, size, hd); hudMarks[id] = type + 1; want[id] = true; };
		// 0: the touchdown point while the flight module lets her down (the drift carried over the time to touch)
		if (d.jet && d.jetMode == 2 && alt > 0.5)
		{
			const double t = d.vs < -0.05 ? (std::min)(60.0, alt / -d.vs) : 0.0;
			set(0, lat + gsH.z * t / R, lng + gsH.x * t / (R * cl), 0.0, AtmoFXHud::PAD, 3.0, 0.0);
		}
		// 1: the selected target's pad (a base or a landed vessel)
		if (const Target* s = Selected())
			if (s->base || s->landed)
				set(1, lat + s->north / R, lng + s->east / (R * cl), 0.0, s->base ? AtmoFXHud::PAD : AtmoFXHud::TARGET, s->base ? 8.0 : 4.0, 0.0);
		// 2..6: ПЕРЕЛЁТ's set-down pad and the gates down its approach (the hop brakes to 25 m over the goal, then lets down)
		if (d.apGoal)
		{
			set(2, d.apGoalLat, d.apGoalLng, 0.0, AtmoFXHud::PAD, 4.0, 0.0);
			const double n = (d.apGoalLat - lat) * R, e = std::remainder(d.apGoalLng - lng, 2 * PI) * R * cl, dist = std::hypot(n, e);
			if (dist > 30)
			{
				const double hd = std::atan2(e, n);   // the approach's heading (from her to the goal)
				static const double DG[4] = { 160, 100, 55, 25 };
				for (int k = 0; k < 4; ++k)
				{
					if (DG[k] > dist - 10) continue;
					const double f = (dist - DG[k]) / dist;
					set(3 + k, lat + n * f / R, lng + e * f / (R * cl), 4.0 + 21.0 * DG[k] / 160.0, AtmoFXHud::GATE, 6.0, hd);
				}
			}
		}
		for (int id = 0; id < 8; ++id)
			if (hudMarks[id] && !want[id]) { AtmoFXHud::Marker(7400 + id, hBody, 0, 0, 0, AtmoFXHud::REMOVE, 0); hudMarks[id] = 0; }
	}

	void SuitHud::RegisterVC()
	{
		if (plateIdx == static_cast<UINT>(-1)) return;
		VCHUDSPEC hs{};
		hs.nmesh = plateIdx; hs.ngroup = 2; hs.hudcnt = _V(0, 0, 0.15); hs.size = 0.1;
		oapiVCRegisterHUD(&hs);
	}

	bool SuitHud::HelmDir(VESSEL* v, double yaw, double pitch, double out[9]) const
	{
		if (!helmOk || !v) return false;
		MATRIX3 Rc, R; oapiCameraRotationMatrix(&Rc); v->GetRotationMatrix(R);
		const VECTOR3 cr = _V(Rc.m11, Rc.m21, Rc.m31), cu = _V(Rc.m12, Rc.m22, Rc.m32), cf = _V(Rc.m13, Rc.m23, Rc.m33);
		auto cam = [&](double a, double b, double c, double* o)   // helmet-frame components -> camera space, y down
		{
			const VECTOR3 g = mul(R, helmX * a + helmY * b + helmZ * c);
			o[0] = dotp(g, cr); o[1] = -dotp(g, cu); o[2] = dotp(g, cf);
		};
		const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
		cam(cp * sy, sp, cp * cy, out);                 // the centre's direction
		cam(cy, 0, -sy, out + 3);                       // the panel's right, square to it
		cam(sp * sy, -cp, sp * cy, out + 6);            // its down
		return true;
	}

	bool SuitHud::MfdGlow(int i, double& yaw, double& pitch, double rgb[3], double& level) const
	{
		if (!lightLive || !life || life->Heavy() || !life->Powered()) return false;
		if ((i == 0 && !openR) || (i == 1 && !openL)) return false;
		DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
		if (!vw || !vh) return false;
		double r[4][4]; BuranRects(vw, vh, r);
		const double F = (vh / 2.0) / std::tan((std::max)(0.1, oapiCameraAperture()));
		yaw = std::atan(((r[i][0] + r[i][2]) / 2 - vw / 2.0) / F); pitch = std::atan(-((r[i][1] + r[i][3]) / 2 - vh / 2.0) / F);
		const DWORD c = BURAN_PAL[std::clamp(hudTheme, 0, 3)].col[CP];
		rgb[0] = (c & 0xFF) / 255.0; rgb[1] = ((c >> 8) & 0xFF) / 255.0; rgb[2] = ((c >> 16) & 0xFF) / 255.0;
		const double b = brightAuto ? ambient : brightManual;
		level = 0.35 + 0.45 * (std::min)(1.0, b);   // a screen of thin lines: most of it dark
		return true;
	}

	// the glass layer each step: the mesh carried with the helmet, the inner face shown from her eyes, the outer one (dim)
	// from outside; the hidden face's vertices folded into one point
	void SuitHud::GlassSource(VESSEL* v, const VECTOR3& eyeRest)
	{
		glassEye = eyeRest;
		if (glassOwner == v && glassIdx != static_cast<UINT>(-1)) return;
		if (!glassTpl) glassTpl = oapiLoadMeshGlobal("Tantra\\VisorGlassHud");
		if (!glassTpl) { oapiWriteLog(const_cast<char*>("OrbiterCrew: glass layer - Meshes\\Tantra\\VisorGlassHud.msh missing: the MFDs stay on the view")); return; }
		if (MESHGROUP* g0 = oapiMeshGroup(glassTpl, 0)) { glassRest.resize(g0->nVtx); for (DWORD i = 0; i < g0->nVtx; ++i) glassRest[i] = _V(g0->Vtx[i].x, g0->Vtx[i].y, g0->Vtx[i].z); }
		glassOwner = v; glassIdx = v->AddMesh(glassTpl); glassMeshDm = nullptr; glassShown = -1; glassLogged = false; glassGlow = nullptr;
		v->SetMeshVisibilityMode(glassIdx, MESHVIS_EXTERNAL | MESHVIS_VC);
		glassWanted = true;
	}

	// the glass layer each step: the mesh carried with the helmet, the inner face shown from her eyes, the outer one (dim)
	// from outside; the hidden face's vertices folded into one point
	void SuitHud::GlassFrame(VESSEL* v, VISHANDLE vis, const HelmetView& hv)
	{
		glassLive = false;
		if (!glassWanted || !v || glassOwner != v) return;
		const D3D11Hud::Api* nx = Native();
		if (!nx) return;
		auto fail = [&](const char* why) { if (!glassLogged) { glassLogged = true; oapiWriteLogV("OrbiterCrew: glass layer - %s: the MFDs stay on the view", why); } };
		if (!glassCv) { glassCv = nx->CanvasCreate(GLASS_CW, GLASS_CH, D3D11Hud::LIGHT); if (!glassCv) { fail("no canvas"); return; } }
		DEVMESHHANDLE dm = vis ? v->GetDevMesh(vis, glassIdx) : nullptr;
		if (!dm) { if (vis) fail("no device mesh"); return; }
		if (dm != glassMeshDm)
		{
			if (!oapiSetTexture(dm, 1, nx->CanvasSurface(glassCv))) { fail("texture not bound"); return; }
			glassMeshDm = dm; glassShown = -1;
			oapiWriteLogV("OrbiterCrew: glass layer - the MFDs on the visor's glass (%d vertices, canvas %dx%d)", static_cast<int>(glassRest.size()), GLASS_CW, GLASS_CH);
		}
		const bool inside = hv.helm && hv.show && oapiCameraInternal() && oapiCameraTarget() == v->GetHandle();
		const bool outside = hv.ext && !(oapiCameraInternal() && oapiCameraTarget() == v->GetHandle());
		const int show = inside ? 0 : outside ? 1 : -1;
		// from her eyes the glass is set on the camera itself (it is the eye): no shake between the two
		const VECTOR3 org = hv.hO;
		lastHv = hv;
		// from outside the glass glows a little: a small light of its own on the helmet's front (outside views only)
		if (outside)
		{
			const DWORD c = BURAN_PAL[std::clamp(hudTheme, 0, 3)].col[CP];
			const COLOUR4 col = { (c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, 0 }, none = { 0, 0, 0, 0 };
			const VECTOR3 at = hv.hO + hv.hZ * 0.09;
			if (!glassGlow) { glassGlow = v->AddPointLight(at, 0.8, 0, 0, 10, col, col, none); if (glassGlow) glassGlow->SetVisibility(LightEmitter::VIS_EXTERNAL); }
			if (glassGlow) { glassGlow->SetPosition(at); const_cast<COLOUR4&>(glassGlow->GetDiffuseColour()) = col; const_cast<COLOUR4&>(glassGlow->GetSpecularColour()) = col; glassGlow->SetIntensity(0.35); glassGlow->Activate(true); }
		}
		else if (glassGlow) glassGlow->Activate(false);
		std::vector<NTVERTEX> vt(glassRest.size());
		for (int gi = 0; gi < 2; ++gi)
		{
			if (gi == 0 && show == 0) continue;   // from her eyes: PlaceVisor sets it on the camera, when the view is drawn
			for (size_t i = 0; i < glassRest.size(); ++i)
			{
				VECTOR3 p = org;
				if (gi == show) { const VECTOR3 r = glassRest[i] - glassEye; p = org + hv.hX * r.x + hv.hY * r.y + hv.hZ * r.z; }
				vt[i].x = static_cast<float>(p.x); vt[i].y = static_cast<float>(p.y); vt[i].z = static_cast<float>(p.z);
			}
			GROUPEDITSPEC e{}; e.flags = GRPEDIT_VTXCRD; e.Vtx = vt.data(); e.nVtx = static_cast<DWORD>(vt.size());
			oapiEditMeshGroup(dm, gi, &e);
		}
		glassShown = show;
		glassLive = show == 0 && life && !life->Heavy();   // the light suit's «Буран» (the Pantsir keeps its own layout)
	}

	void SuitHud::HelmetFrame(VESSEL* v, VISHANDLE vis, const HelmetView& hv, const HudData* d)
	{
		if (plateIdx == static_cast<UINT>(-1)) return;
		const bool show = hv.show && oapiCameraInternal() && oapiCameraTarget() == v->GetHandle();   // only her own eyes
		// the helmet's axes a little smoothed (the user, 2026-10-20: the torso's small motions of the clips shook the glass;
		// 40 % of each step's change - a turn of the body still carried, with a little inertia, as a real helmet on a body)
		if (hv.helm && helmOk)
		{
			const double a = 0.6;
			auto mix = [&](const VECTOR3& o, const VECTOR3& n) { const VECTOR3 m = o * (1 - a) + n * a; const double l = length(m); return l > 1e-9 ? m / l : n; };
			helmX = mix(helmX, hv.hX); helmY = mix(helmY, hv.hY); helmZ = mix(helmZ, hv.hZ);
		}
		else { helmX = hv.hX; helmY = hv.hY; helmZ = hv.hZ; }
		helmOk = hv.helm;
		GlassFrame(v, vis, hv);
		onVisor = show; plateVis = vis;
		v->SetMeshVisibilityMode(plateIdx, show ? MESHVIS_VC : MESHVIS_NEVER);
		if (show) { PlaceVisor(v); Modulator(v); LightLayer(v, d); }
		else lightLive = false;
		if (!show && hv.ext && d && glassCv && glassShown == 1 && life && !life->Heavy())   // from outside: the glass's own picture, MFDs at the sides
		{
			const double now = oapiGetSysTime();
			if (glassExtT < 0 || now < glassExtT || now - glassExtT >= 0.1)
			{
				glassExtT = now;
				const D3D11Hud::Api* nx = Native();
				if (nx && nx->Begin(glassCv, true, 0))
				{
					const bool side = mfdSide; mfdSide = true; helmOk = true;
					drawCv = glassCv; drawingLight = glassPass = glassExtPass = true; glassLive = true;
					Draw(nullptr, GLASS_CW, GLASS_CH, *d, v);
					drawingLight = glassPass = glassExtPass = false; drawCv = nullptr; glassLive = false;
					mfdSide = side;
					nx->End(glassCv);
				}
			}
		}
		// the closed visor before her eyes: 0x400 on group 0 (the client draws the drops on the glass only then)
		if (DEVMESHHANDLE dm = vis ? v->GetDevMesh(vis, plateIdx) : nullptr)
		{
			const int g = hv.glass && show ? 1 : 0;
			if (dm != glassDm) { glassDm = dm; glassSet = -1; }
			if (g != glassSet) { GROUPEDITSPEC e = {}; e.flags = g ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG; e.UsrFlag = 0x400; oapiEditMeshGroup(dm, 0, &e); glassSet = g; }
		}
	}

	// The plate is set from the camera itself, not from the head model: it is close to the eye, so a centimetre of
	// difference would shift the whole display. Called each step and again when the display is drawn (the camera is
	// final for the frame then).
	// the plate off the view (the display not drawn: no power, another camera) - its layers would hang where they were last
	void SuitHud::HideVisor(VESSEL* v)
	{
		if (plateIdx == static_cast<UINT>(-1) || !plateVis || !v) return;
		DEVMESHHANDLE dm = v->GetDevMesh(plateVis, plateIdx);
		if (!dm) return;
		NTVERTEX vtx[4]{};
		GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTXCRD; ges.Vtx = vtx; ges.nVtx = 4;
		for (DWORD gi = 0; gi < 4; ++gi) oapiEditMeshGroup(dm, gi, &ges);
	}

	void SuitHud::PlaceVisor(VESSEL* v)
	{
		if (plateIdx == static_cast<UINT>(-1) || !plateVis) return;
		DEVMESHHANDLE dm = v->GetDevMesh(plateVis, plateIdx);
		if (!dm) return;
		DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
		if (!vw || !vh) return;
		VECTOR3 cg; oapiCameraGlobalPos(&cg);
		MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);   // camera frame -> global: columns right, up, forward
		MATRIX3 Rv; v->GetRotationMatrix(Rv);        // vessel frame -> global
		VECTOR3 eye; v->Global2Local(cg, eye);
		const VECTOR3 r = tmul(Rv, _V(Rc.m11, Rc.m21, Rc.m31)), up = tmul(Rv, _V(Rc.m12, Rc.m22, Rc.m32)), f = tmul(Rv, _V(Rc.m13, Rc.m23, Rc.m33));
		// on the visor itself, 0.12 m out: nothing can come between the eye and the helmet's glass, so no wall or rail of
		// a cramped cabin ever cuts through the display (at 0.3 m they did); still beyond the VC near plane (0.1 m)
		const double dist = 0.12, hh = dist * std::tan(oapiCameraAperture()), hw = hh * vw / vh;
		const VECTOR3 c = eye + f * dist;
		const VECTOR3 P[4] = { c - r * hw + up * hh, c + r * hw + up * hh, c + r * hw - up * hh, c - r * hw - up * hh };
		NTVERTEX vtx[4];
		const float U[4] = { 0, 1, 1, 0 }, Vv[4] = { 0, 0, 1, 1 };
		for (int i = 0; i < 4; ++i)
			vtx[i] = { static_cast<float>(P[i].x), static_cast<float>(P[i].y), static_cast<float>(P[i].z),
				static_cast<float>(-f.x), static_cast<float>(-f.y), static_cast<float>(-f.z), U[i], Vv[i] };
		GROUPEDITSPEC ges{};
		ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = vtx; ges.nVtx = 4;
		oapiEditMeshGroup(dm, 2, &ges); oapiEditMeshGroup(dm, 3, &ges);
		NTVERTEX big[4];
		for (int i = 0; i < 4; ++i)
		{
			// a little farther than the HUD's layers (4 %), the same angular size: never coplanar with them - coplanar quads
			// of different corners fight over the depth and flicker
			const VECTOR3 q = eye + ((c - eye) + (P[i] - c) * MOD_MARGIN) * 1.04;
			big[i] = vtx[i]; big[i].x = static_cast<float>(q.x); big[i].y = static_cast<float>(q.y); big[i].z = static_cast<float>(q.z);
		}
		ges.Vtx = big; oapiEditMeshGroup(dm, 1, &ges);
		SunSprite(v, dm, eye, r, up, f, dist);
		// the glass layer, seen from her eyes: on the camera itself, now - the moment the view is drawn (set at the step
		// it lagged the camera a frame: at 4-15 cm from the eye a millimetre is degrees - it shook)
		if (glassShown == 0 && glassMeshDm && !glassRest.empty() && glassOwner == v)
		{
			std::vector<NTVERTEX> vt(glassRest.size());
			for (size_t i = 0; i < glassRest.size(); ++i)
			{
				const VECTOR3 rr = glassRest[i] - glassEye, p = eye + helmX * rr.x + helmY * rr.y + helmZ * rr.z;
				vt[i].x = static_cast<float>(p.x); vt[i].y = static_cast<float>(p.y); vt[i].z = static_cast<float>(p.z);
			}
			GROUPEDITSPEC e{}; e.flags = GRPEDIT_VTXCRD; e.Vtx = vt.data(); e.nVtx = static_cast<DWORD>(vt.size());
			oapiEditMeshGroup(glassMeshDm, 0, &e);
		}
	}

	// The sun's spot: dense over its disc (~1 deg core, a smooth fall-off over three times that), so the eye is not blinded
	// and the signs stay readable. A sprite on the sun's line, on the plate's plane a little farther out (drawn first: the
	// modulator and the light come over it whatever the depth test)
	void SuitHud::SunSprite(VESSEL* v, DEVMESHHANDLE dm, const VECTOR3& eye, const VECTOR3& r, const VECTOR3& up, const VECTOR3& f, double dist)
	{
		const D3D11Hud::Api* nx = Native();
		if (nx && !sunCv) { sunCv = nx->CanvasCreate(256, 256, D3D11Hud::ALPHA); sunDm = nullptr; sunAmp = -1; if (!sunCv) return; }
		if (!nx && !sunSrf)
		{
			sunSrf = oapiCreateSurfaceEx(256, 256, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_ALPHA | OAPISURFACE_NOMIPMAPS);
			sunDm = nullptr; sunAmp = -1;
			if (!sunSrf) return;
		}
		if (dm != sunDm && oapiSetTexture(dm, 3, nx ? nx->CanvasSurface(sunCv) : sunSrf))
		{
			sunDm = dm; sunHAlphaSet = -1;   // a new visual: the filter's flag is set again
			// D3D11: the spot is a filter (group flag 0x100): what lies behind it is multiplied by its transmittance, down to
			// welding glass over the core - the Sun's disc stays a disc seen through dark glass, not a black hole
			if (nx) { GROUPEDITSPEC g = {}; g.flags = GRPEDIT_ADDUSERFLAG; g.UsrFlag = 0x100; oapiEditMeshGroup(dm, 0, &g); }
		}
		if (nx && sunDm && sunHAlphaSet != (sunHAlpha ? 1 : 0))   // grey <-> H-alpha: the flag 0x200 on the filter group
		{
			GROUPEDITSPEC g = {}; g.flags = sunHAlpha ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG; g.UsrFlag = 0x200;
			oapiEditMeshGroup(sunDm, 0, &g); sunHAlphaSet = sunHAlpha ? 1 : 0;
		}
		const double boost = brightAuto ? ambient : brightManual;
		const double amp = 0.7 * (std::min)(1.0, 0.4 + boost);
		if (std::abs(amp - sunAmp) > 1.0 / 255)
		{
			if (nx)
			{
				if (nx->Begin(sunCv, true, 0))   // a soft disc: rings from the faint rim to the dense core, written straight
				{
					nx->SetBlend(sunCv, D3D11Hud::COPY);
					for (int i = 63; i >= 0; --i)
					{
						const double t = i / 63.0, fall = 1 - t * t * (3 - 2 * t), rr = 128.0 / 3 * (1 + 2.0 * t), a = amp * fall;
						if (a < 1.0 / 255) continue;
						nx->Ellipse(sunCv, 128, 128, static_cast<float>(rr), static_cast<float>(rr), static_cast<DWORD>(std::lround(a * 255)) << 24, 0, 0);
					}
					nx->End(sunCv);
				}
			}
			else oapiClearSurface(sunSrf, 0);
			if (oapi::Sketchpad* skp = nx ? nullptr : oapiGetSketchpad(sunSrf))
			{
				if (Ext(skp))
				{
					skp->SetBlendState(oapi::Sketchpad::COPY);
					const int NR = 64;   // fine steps: no visible rings
					for (int i = NR - 1; i >= 0; --i)   // faint rim first, dense core last
					{
						const double t = static_cast<double>(i) / (NR - 1), fall = 1 - t * t * (3 - 2 * t);
						const double rr = 128.0 / 3 * (1 + 2.0 * t), a = amp * fall;
						if (a < 1.0 / 255) continue;   // (a zero colour would mean "no brush")
						const DWORD c = static_cast<DWORD>(std::lround(a * 255)) << 24;
						skp->QuickPen(c); skp->QuickBrush(c);   // the outline as the fill: under COPY a pen of another alpha leaves a rim
						skp->Ellipse(static_cast<int>(128 - rr), static_cast<int>(128 - rr), static_cast<int>(128 + rr), static_cast<int>(128 + rr));
					}
					skp->QuickPen(0); skp->QuickBrush(0);
					skp->SetBlendState();
				}
				oapiReleaseSketchpad(skp);
			}
			sunAmp = amp;
		}
		// where the sun is in the view (in her vessel's frame, as the plate)
		VECTOR3 sg, sl; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sg); v->Global2Local(sg, sl);
		const VECTOR3 q = sl - eye;
		const double qx = dotp(q, r), qy = dotp(q, up), qz = dotp(q, f);
		const double ds = dist * 1.06;   // behind the modulator (1.04) and the HUD layers (1.0)
		VECTOR3 c = eye + f * ds; double hs = 0;   // out of the view or in the shade: nothing (a point)
		if (lastSunlit && qz > 0.05 * length(q))
		{
			c = c + r * (qx / qz * ds) + up * (qy / qz * ds);
			hs = 3 * ds * std::tan(0.02);
		}
		const VECTOR3 P[4] = { c - r * hs + up * hs, c + r * hs + up * hs, c + r * hs - up * hs, c - r * hs - up * hs };
		NTVERTEX vtx[4];
		const float U[4] = { 0, 1, 1, 0 }, Vv[4] = { 0, 0, 1, 1 };
		for (int i = 0; i < 4; ++i)
			vtx[i] = { static_cast<float>(P[i].x), static_cast<float>(P[i].y), static_cast<float>(P[i].z),
				static_cast<float>(-f.x), static_cast<float>(-f.y), static_cast<float>(-f.z), U[i], Vv[i] };
		GROUPEDITSPEC ges{};
		ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = vtx; ges.nVtx = 4;
		oapiEditMeshGroup(dm, 0, &ges);
	}

	// The modulator's texture: black with the dimming as alpha, written straight (COPY: no blending, the alpha is set, not
	// mixed). Shapes are drawn from the faintest to the densest where they nest; every alpha already includes the shade.
	void SuitHud::Modulator(VESSEL* v)
	{
		DEVMESHHANDLE dm = v->GetDevMesh(plateVis, plateIdx);
		if (!dm) return;
		DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
		if (!vw || !vh) return;
		const int w = (std::max)(64, static_cast<int>(vw) / 2), h = (std::max)(64, static_cast<int>(vh) / 2);   // soft shapes: half resolution
		// the D3D11 client: a GPU canvas of its own (no Sketchpad); else the surface as before
		const D3D11Hud::Api* nx = Native();
		if (nx && (!modCv || w != modW || h != modH))
		{
			if (modCv) nx->CanvasRelease(modCv);
			modCv = nx->CanvasCreate(w, h, D3D11Hud::ALPHA);
			modW = w; modH = h; modDm = nullptr;
		}
		if (!nx && (!modSrf || w != modW || h != modH))
		{
			if (modSrf) oapiDestroySurface(modSrf);
			modSrf = oapiCreateSurfaceEx(w, h, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_ALPHA | OAPISURFACE_NOMIPMAPS);
			modW = w; modH = h; modDm = nullptr;
		}
		const SURFHANDLE modTex = nx ? (modCv ? nx->CanvasSurface(modCv) : nullptr) : modSrf;
		if (!modTex) { if (!modLogged) { modLogged = true; oapiWriteLog(const_cast<char*>("OrbiterCrew: visor modulator - no surface")); } return; }
		if (dm != modDm)   // a new visual: bind again (retried until it takes)
		{
			const bool ok = oapiSetTexture(dm, 1, modTex);
			if (ok) { modDm = dm; modKeyed = false; }
			if (!modLogged || ok) { char b[96]; snprintf(b, sizeof b, "OrbiterCrew: visor modulator %dx%d%s, texture bound: %s", w, h, nx ? " (D3D11 canvas)" : "", ok ? "yes" : "NO"); oapiWriteLog(b); modLogged = true; }
		}
		{
			const double now = oapiGetSysTime();
			if (!helmOk && modT >= 0 && now >= modT && now - modT < 1.0 / 15) return;   // soft and slow: 15 times a second is enough (in a helmet fixed to the body: every frame - its edge moves as the head turns)
			modT = now;
		}
		// the view's layout lands in the inner part of the (larger) plate: x_tex = ((x / vw - 0.5) / M + 0.5) * w
		const double M = MOD_MARGIN, sx = w / static_cast<double>(vw) / M, sy = h / static_cast<double>(vh) / M;
		const double ox = w * (0.5 - 0.5 / M), oy = h * (0.5 - 0.5 / M);
		const double boost = brightAuto ? ambient : brightManual;
		const double shade = 0.65 * std::clamp(lastShade, 0.0, 1.0) * (0.5 + 0.5 * (std::min)(1.0, boost));   // the shade: the whole view
		const double glass = life ? 0.5 * life->optDark : 0;   // the browned glass dims the whole view
		const double a0 = 0.10 + 0.45 * (std::min)(1.0, boost);                                   // bands and backings
		{
			// the same picture as last time: nothing to draw
			const double heavy = life && life->Heavy() ? 1.0 : 0.0;
		if (life && !life->Powered())   // the display is dead: no shading of its own (the plate stays clear)
		{
			if (modKeyed && modKey[7] < 0) return;
			if (nx) { if (nx->Begin(modCv, true, 0)) nx->End(modCv); } else oapiClearSurface(modSrf, 0);
			modKeyed = true; modKey[7] = -1; return;
		}
			// in a helmet fixed to the body its window's edge stays on the helmet: where the helmet's forward is in the view
			helmPx[0] = helmPx[1] = 0;
			{ double q[9]; if (HelmDir(v, 0, 0, q) && q[2] > 0.2) { const double F = (vh / 2.0) / std::tan((std::max)(0.1, oapiCameraAperture())); helmPx[0] = std::round(F * q[0] / q[2]); helmPx[1] = std::round(F * q[1] / q[2]); } }
			const double key[11] = { static_cast<double>(w), static_cast<double>(h), shade, glass, heavy ? 0.0 : a0 * shadeK, openL ? 1.0 : 0.0, openR ? 1.0 : 0.0, heavy, static_cast<double>(alertLines), helmPx[0], helmPx[1] };
			bool same = modKeyed;
			for (int i = 0; i < 11 && same; ++i) same = std::abs(key[i] - modKey[i]) < 1.0 / 1024;
			if (same) return;
			std::copy(key, key + 11, modKey); modKeyed = true;
		}
		LARGE_INTEGER mq0, mqf; QueryPerformanceFrequency(&mqf); QueryPerformanceCounter(&mq0);
		struct ModTime { LARGE_INTEGER& a; LARGE_INTEGER& f; SuitHud& h; ~ModTime() { LARGE_INTEGER e; QueryPerformanceCounter(&e); h.perfMod += 1000.0 * (e.QuadPart - a.QuadPart) / f.QuadPart; ++h.perfModN; } } modTime{ mq0, mqf, *this };
		oapi::Sketchpad* skp = nullptr;
		if (nx) { if (!nx->Begin(modCv, true, 0)) { modKeyed = false; return; } nx->SetBlend(modCv, D3D11Hud::COPY); }   // cleared: transparent
		else
		{
			oapiClearSurface(modSrf, 0);   // transparent: last step's shapes must not stay (they smeared across the visor)
			skp = oapiGetSketchpad(modSrf);
			if (!skp) { modKeyed = false; return; }
			if (!Ext(skp)) { oapiReleaseSketchpad(skp); modKeyed = false; return; }
		}
		auto finish = [&]() { if (nx) nx->End(modCv); else { skp->QuickPen(0); skp->QuickBrush(0); skp->SetBlendState(); oapiReleaseSketchpad(skp); } };
		const double base = 1 - (1 - shade) * (1 - glass);   // the whole view: the shade and the glass
		auto with = [&](double a) { return 1 - (1 - std::clamp(a, 0.0, 1.0)) * (1 - base); };
		auto box = [&](double x0, double y0, double x1, double y1, double a)
		{
			if (with(a) < 1.0 / 255) return;   // nothing to dim (and a zero colour would mean "no brush")
			const DWORD c = static_cast<DWORD>(std::lround(with(a) * 255)) << 24;
			const int X0 = static_cast<int>(std::floor(ox + x0 * sx)), Y0 = static_cast<int>(std::floor(oy + y0 * sy)), X1 = static_cast<int>(std::ceil(ox + x1 * sx)), Y1 = static_cast<int>(std::ceil(oy + y1 * sy));
			if (nx) { nx->Rect(modCv, static_cast<float>(X0), static_cast<float>(Y0), static_cast<float>(X1), static_cast<float>(Y1), c, 0, 0); return; }
			skp->QuickPen(c); skp->QuickBrush(c);   // the outline as the fill: under COPY a pen of another alpha leaves a rim
			skp->Rectangle(X0, Y0, X1, Y1);
		};
		if (skp) { skp->SetBlendState(oapi::Sketchpad::COPY); skp->QuickPen(0); }
		const double mx = vw * (MOD_MARGIN - 1) / 2 + 2, my = vh * (MOD_MARGIN - 1) / 2 + 2;   // the margins, in view pixels
		box(-mx, -my, vw + mx, vh + my, 0);   // the shade's tone over the whole plate (skipped when there is none)
		const double k = vh / 720.0;
		if (life && life->Heavy())
		{
			// the Pantsir's computer: a fixed vignette - solid above the rim's scales (to ~47 of 720) and below the tops of its
			// panels and MFDs (from ~490), a soft cosine edge of 60 each; no side backings (its panels are low). Drawn only
			// when something changes (the shade, the glass): no cost per frame
			const int N = 32; const double A = 0.88;
			box(-mx, -my, vw + mx, 47 * k, A);
			box(-mx, 490 * k, vw + mx, vh + my, A);
			for (int i = 0; i < N; ++i)
			{
				const double f = 0.5 + 0.5 * std::cos(PI * (i + 0.5) / N);
				box(-mx, (47 + i * 60.0 / N) * k, vw + mx, (47 + (i + 1) * 60.0 / N) * k + 1, A * f);
				box(-mx, (490 - (i + 1) * 60.0 / N) * k - 1, vw + mx, (490 - i * 60.0 / N) * k, A * f);
			}
			finish();
			return;
		}
		if (nx)
		{
			// the light suit on the D3D11 client: the glass darkens by the helmet's own shape (the user, 2026-10-20: no
			// rectangles) - clear in the middle, deepening towards the brow, the chin and the corners where the plates sit:
			// nested ellipses, flattened so the top and the bottom take most, each a little darker, outermost first
			// the helmet's window (the user's drawing, 2026-10-20): an oval nearly the whole view - its top just under the
			// brow at the middle, its sides reaching the screen's edges a quarter down, its bottom at the chin. Outside it the
			// shell and the seal right at the eyes: dense; inside, the dark fades out towards the open view over a wide band,
			// as something too near the eye to be in focus. Drawn as nested ellipses, outermost first (COPY), many fine steps
			const double A = (std::min)(0.94, (0.6 + 0.3 * (std::min)(1.0, boost)) * shadeK);
			// (the user, 2026-10-20: the other way round - bent by the helmet's shape, fixed): the window's own outline seen
			// from the eye (WIN_TOP / WIN_BOT), drawn through the helmet into the view - nested copies drawn in towards the
			// window's middle (yaw 0, pitch -6), dense at the edge, fading out over a third of the way in. It stays on the
			// helmet as the head turns inside it (the instruments' backing in the day)
			const double Fv = (vh / 2.0) / std::tan((std::max)(0.1, oapiCameraAperture()));
			std::vector<float> xy;
			auto outline = [&](double sc)
			{
				xy.clear();
				auto put = [&](double yd, double pd)
				{
					double q[9];
					if (!HelmDir(v, yd * sc * RAD, (-6 + (pd + 6) * sc) * RAD, q)) return;
					const double z = (std::max)(q[2], 0.05);   // behind the eye: far out to the side
					xy.push_back(static_cast<float>(ox + (vw * 0.5 + Fv * q[0] / z) * sx)); xy.push_back(static_cast<float>(oy + (vh * 0.5 + Fv * q[1] / z) * sy));
				};
				for (int yd = -108; yd <= 108; yd += 4) put(yd, WinEdge(WIN_TOP, yd));
				for (int yd = 108; yd >= -108; yd -= 4) put(yd, WinEdge(WIN_BOT, yd));
			};
			nx->Rect(modCv, 0, 0, static_cast<float>(w), static_cast<float>(h), static_cast<DWORD>(std::lround(with(A) * 255)) << 24, 0, 0);
			const int NE = 48;
			for (int i = 0; i <= NE; ++i)
			{
				const double t = static_cast<double>(i) / NE;                    // 0 the window's edge .. 1 a third in
				const double f = 0.5 + 0.5 * std::cos(PI * std::pow(t, 0.8));     // dense at the edge, a smooth, long fall
				const DWORD c = static_cast<DWORD>(std::lround(with(A * f) * 255)) << 24;
				outline(1.0 - 0.36 * t);
				if (xy.size() >= 6) nx->Polygon(modCv, xy.data(), static_cast<int>(xy.size() / 2), c ? c : 0x01000000u);
			}
			if (false && alertLines > 0)   // (removed, the user: no box) the dangers and messages under the lamps: the glass darkened behind them, so red reads on a bright sky
			{
				const double C0 = vw / 2.0 - 640 * k, X0 = ox + (C0 + 300 * k) * sx, X1 = ox + (C0 + 980 * k) * sx;
				const double Y0 = oy + 132 * k * sy, Y1 = oy + (160 + alertLines * 20) * k * sy;
				for (int i = 10; i >= 0; --i)
				{
					const double e = i * 4 * k * sx, aa = 0.45 + 0.4 * (1 - i / 10.0);   // never lighter than the oval round it
					const DWORD c = static_cast<DWORD>(std::lround(with(aa) * 255)) << 24;
					nx->Rect(modCv, static_cast<float>(X0 - e), static_cast<float>(Y0 - e), static_cast<float>(X1 + e), static_cast<float>(Y1 + e), c, 0, 0);
				}
			}
			finish();
			return;
		}
		// legacy (the Sketchpad path): the shading along the visor's upper edge follows the rim's arc (the helmet's line: the circle the rim's readouts sit
		// on, centre (640, 1560), radius 1492 of the 720 layout): solid above it, a soft edge across it; a low band below
		const double topH = 140 * k, botH = 115 * k;
		const int N = 32;
		{
			const double C0 = vw / 2.0 - 640 * k;   // the layout's centre frame, as the display draws it
			const int MC = 120;
			for (int j = 0; j < MC; ++j)
			{
				const double x0 = -mx + (vw + 2 * mx) * j / MC, x1 = -mx + (vw + 2 * mx) * (j + 1) / MC + 1, u = ((x0 + x1) / 2 - C0) / k;
				const double dx = u - 640, arcY = std::abs(dx) < 1492 ? 1560 - std::sqrt(1492.0 * 1492.0 - dx * dx) : 0.0;
				const double solid = (std::max)(0.0, arcY - 34) * k, edge = 46 * k;   // dark to just above the readouts, soft 46 below
				box(x0, -my, x1, solid, 0.85 * (std::min)(1.1, a0 * shadeK));
				for (int i = 0; i < 12; ++i)
				{
					const double f = 0.5 + 0.5 * std::cos(PI * (i + 0.5) / 12);
					box(x0, solid + i * edge / 12, x1, solid + (i + 1) * edge / 12 + 1, 0.85 * (std::min)(1.1, a0 * shadeK) * f);
				}
			}
		}
		for (int i = 0; i < N; ++i)
		{
			const double x = (i + 0.5) / N, f = 0.5 + 0.5 * std::cos(PI * x);
			box(-mx, vh - (i + 1) * botH / N - 1, vw + mx, vh - i * botH / N, 0.7 * (std::min)(1.1, a0 * shadeK) * f);
		}
		box(-mx, vh, vw + mx, vh + my, 0.7 * (std::min)(1.1, a0 * shadeK));   // below the view: as the band's edge
		(void)topH;
		// feathered backings under the two side panels, faint rim first, dense core last
		auto soft = [&](double x, double y, double ww, double hh)
		{
			for (int i = 7; i >= 0; --i) { const double e = i * 2 * k; box(x - e, y - e, x + ww + e, y + hh + e, a0 * 0.75 * (1 - i / 8.0)); }
		};
		const double R0 = vw - 1280 * k;
		// «Буран»: the glass darkens under its four plates, a wide feather (the plates are turned to the eye: a little room)
		{
			double r[4][4]; BuranRects(vw, vh, r);
			for (int q = 0; q < 4; ++q)
			{
				if ((q == 0 && !openR) || (q == 1 && !openL)) continue;
				const double pad = 10 * k;
				for (int i = 11; i >= 0; --i) { const double e = pad + i * 3 * k; box(r[q][0] - e, r[q][1] - e, r[q][2] + e, r[q][3] + e, (std::min)(0.92, a0 * shadeK * 0.75) * (1 - i / 12.0) * 0.3); }
			}
		}
		(void)soft; (void)R0;   // legacy: the light suit's side panels before «Буран»
		// (the sun's spot is a sprite of its own: SunSprite)
		finish();
	}

	// The light layer: the whole HUD drawn as light on black into our own texture of the screen's size (or half),
	// made into colour + alpha by the shader, shown on group 2 of the visor plate. Sharp 1:1 and independent of
	// Orbiter's PanelMfdHudSize. Logged once: whether the shader, the surfaces and the binding work.
	void SuitHud::LightLayer(VESSEL* v, const HudData* d)
	{
		auto logOnce = [&](const char* what) { if (!lightLogged) { lightLogged = true; char b[160]; snprintf(b, sizeof b, "OrbiterCrew: helmet light layer %dx%d: %s", lightW, lightH, what); oapiWriteLog(b); } };
		const bool wasLive = lightLive;
		lightLive = false;
		if (const D3D11Hud::Api* nx = Native())
		{
			// the D3D11 client: the whole HUD drawn on the GPU into a light canvas, turned into colour + alpha by the client
			if (!d) return;
			DEVMESHHANDLE dm = v->GetDevMesh(plateVis, plateIdx);
			if (!dm) return;
			DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
			if (!vw || !vh) return;
			// 1:1 with the screen: the client's strokes and glyphs are anti-aliased from their exact distance, so the 2x
			// smoothing of the Sketchpad path isn't needed - and a 2x canvas costs ~4x the memory of a 32-bit process
			const int w = static_cast<int>(vw), h = static_cast<int>(vh);
			if (!lightCv || w != lightW || h != lightH)
			{
				if (lightCv) nx->CanvasRelease(lightCv);
				lightW = w; lightH = h; lightDm = nullptr;
				lightCv = nx->CanvasCreate(w, h, D3D11Hud::LIGHT);
				if (!lightCv) { logOnce("D3D11 canvas FAILED"); return; }
			}
			if (dm != lightDm)
			{
				const bool ok = oapiSetTexture(dm, 2, nx->CanvasSurface(lightCv));
				if (ok) lightDm = dm;
				logOnce(ok ? "D3D11 canvas, texture bound" : "texture NOT bound");
				if (!ok) return;
			}
			Mouse(vw, vh);
			const double now = oapiGetSysTime();
			// every frame (the user, 2026-10-20: the tapes and the pitch ladder lagged at 30/s - they move with the view);
			// on the GPU a redraw costs ~1-2 ms. (void)wasLive: the Sketchpad path below still keeps its 30/s
			(void)wasLive;
			lightT = now; drawnSeq = clickSeq;
			LARGE_INTEGER q0, q1, qf; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
			if (!nx->Begin(lightCv, true, 0)) return;
			drawCv = lightCv; drawingLight = true;
			Draw(nullptr, lightW, lightH, *d, v);
			drawingLight = false; drawCv = nullptr;
			nx->End(lightCv);
			if (GlassOn() && d->life && !d->life->Heavy() && nx->Begin(glassCv, true, 0))   // the panels on the glass
			{
				drawCv = glassCv; drawingLight = glassPass = true;
				Draw(nullptr, GLASS_CW, GLASS_CH, *d, v);
				drawingLight = glassPass = false; drawCv = nullptr;
				nx->End(glassCv);
			}
			QueryPerformanceCounter(&q1);
			lightLive = true;
			perfDraw += 1000.0 * (q1.QuadPart - q0.QuadPart) / qf.QuadPart; ++perfN;
			if (perfT0 < 0 || now < perfT0) perfT0 = now;
			if (now - perfT0 >= 5)
			{
				char b[200];
				snprintf(b, sizeof b, "OrbiterCrew: helmet display %dx%d (D3D11 canvas): draw %.2f ms (%.0f/s); modulator %.2f ms (%.0f/s)",
					lightW, lightH, perfN ? perfDraw / perfN : 0, perfN / (now - perfT0), perfModN ? perfMod / perfModN : 0, perfModN / (now - perfT0));
				oapiWriteLog(b);
				perfDraw = perfShader = perfMod = 0; perfN = perfModN = 0; perfT0 = now;
			}
			return;
		}
		gcCore2* core = Core();
		if (!core || !d) return;
		DEVMESHHANDLE dm = v->GetDevMesh(plateVis, plateIdx);
		if (!dm) return;
		DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
		if (!vw || !vh) return;
		if (!ipiTried)
		{
			ipiTried = true;
			ipi = core->CreateIPInterface("Modules/OrbiterCrew/HudLight.hlsl", "PSMain");
			if (!ipi) { logOnce("shader FAILED (Modules/OrbiterCrew/HudLight.hlsl)"); return; }
		}
		if (!ipi) return;
		const int sc = std::clamp(hudScale, 1, 2), w = static_cast<int>(vw) * sc, h = static_cast<int>(vh) * sc;   // 2x: drawn larger, the plate's filtering smooths it down
		if (!lightSrc || w != lightW || h != lightH)
		{
			if (lightSrc) oapiDestroySurface(lightSrc);
			if (lightOut) oapiDestroySurface(lightOut);
			lightW = w; lightH = h;
			lightSrc = oapiCreateSurfaceEx(lightW, lightH, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS);
			lightOut = oapiCreateSurfaceEx(lightW, lightH, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_ALPHA | OAPISURFACE_NOMIPMAPS);
			lightDm = nullptr;
			if (!lightSrc || !lightOut) { logOnce("no surfaces"); return; }
		}
		if (dm != lightDm)
		{
			const bool ok = oapiSetTexture(dm, 2, lightOut);
			if (ok) lightDm = dm;
			logOnce(ok ? "shader ok, texture bound" : "texture NOT bound");
			if (!ok) return;
		}
		// the mouse every frame (no click is lost); the picture 30 times a second, or at once after a click
		Mouse(vw, vh);
		const double now = oapiGetSysTime();
		if (wasLive && lightT >= 0 && now >= lightT && now - lightT < 1.0 / 30 && clickSeq == drawnSeq) { lightLive = true; return; }
		lightT = now; drawnSeq = clickSeq;
		LARGE_INTEGER q0, q1, q2, qf; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
		oapiClearSurface(lightSrc, 0);
		bool drawn = false;
		if (oapi::Sketchpad* skp = oapiGetSketchpad(lightSrc))
		{
			if (Ext(skp))
			{
				drawingLight = true;
				Draw(skp, lightW, lightH, *d, v);
				drawingLight = false;
				drawn = true;
			}
			oapiReleaseSketchpad(skp);
		}
		QueryPerformanceCounter(&q1);
		gcIPInterface* p = static_cast<gcIPInterface*>(ipi);
		p->SetTexture("tSrc", lightSrc, IPF_CLAMP);
		p->SetOutput(0, lightOut);
		if (!p->Execute(false)) { logOnce("shader execute FAILED"); return; }
		QueryPerformanceCounter(&q2);
		lightLive = drawn;
		// the cost, every 5 s
		perfDraw += 1000.0 * (q1.QuadPart - q0.QuadPart) / qf.QuadPart; perfShader += 1000.0 * (q2.QuadPart - q1.QuadPart) / qf.QuadPart; ++perfN;
		if (perfT0 < 0 || now < perfT0) perfT0 = now;
		if (now - perfT0 >= 5)
		{
			char b[200];
			snprintf(b, sizeof b, "OrbiterCrew: helmet display %dx%d: draw %.2f ms, shader %.2f ms (%.0f/s); modulator %.2f ms (%.0f/s)",
				lightW, lightH, perfN ? perfDraw / perfN : 0, perfN ? perfShader / perfN : 0, perfN / (now - perfT0), perfModN ? perfMod / perfModN : 0, perfModN / (now - perfT0));
			oapiWriteLog(b);
			perfDraw = perfShader = perfMod = 0; perfN = perfModN = 0; perfT0 = now;
		}
	}

	// ---- the display's settings, the user's for good ----
	void SuitHud::LoadPrefs()
	{
		prefsLoaded = true;
		g_en = UiEnglish();   // one state with the crew's menu and messages (OrbiterCrew.cfg; not set - English)
		std::ifstream f("Config\\OrbiterCrew\\SuitDisplay.cfg");
		if (!f) return;   // none yet: the scenario's or the defaults stand until the first change
		std::string key;
		while (f >> key)
		{
			if (key == "LOOK") { int x; if (f >> x) look = std::clamp(x, 0, 2); }
			else if (key == "PALETTE") { int x; if (f >> x) pal = std::clamp(x, 0, 2); }
			else if (key == "BRIGHT_AUTO") { int x; if (f >> x) brightAuto = x != 0; }
			else if (key == "BRIGHT") { double x; if (f >> x) brightManual = std::clamp(x, 0.0, 1.5); }
			else if (key == "MAP_MONO") { int x; if (f >> x) mapMono = x != 0; }
			else if (key == "RESOLUTION") { int x; if (f >> x) hudScale = std::clamp(x, 1, 2); }
			else if (key == "HORIZON") { int x; if (f >> x) horizonMode = std::clamp(x, 0, 2); }
			else if (key == "SYNTH") { int x; if (f >> x) synth = x != 0; }
			else if (key == "SUN_FILTER") { int x; if (f >> x) sunHAlpha = x != 0; }
			else if (key == "MFD_SIDE") { int x; if (f >> x) mfdSide = x != 0; }
			else if (key == "HIDE_LADDER") { int x; if (f >> x) hideLadder = x != 0; }
			else if (key == "HUD_MIN") { int x; if (f >> x) hudMin = x != 0; }
			else if (key == "HUD_THEME") { int x; if (f >> x) hudTheme = std::clamp(x, 0, 3); }
			else if (key == "SHADE") { double x; if (f >> x) shadeK = std::clamp(x, 0.0, 2.0); }
			else std::getline(f, key);
		}
	}

	void SuitHud::SaveLanguage(bool english) const
	{
		const char* path = "Config\\OrbiterCrew\\OrbiterCrew.cfg";
		std::vector<std::string> lines; bool put = false;
		{
			std::ifstream f(path); std::string ln;
			while (std::getline(f, ln))
			{
				std::istringstream ls(ln); std::string k; ls >> k;
				if (k == "LANGUAGE") { ln = std::string("LANGUAGE ") + (english ? "en" : "ru") + "   ; the interface's language: ru or en"; put = true; }
				lines.push_back(ln);
			}
		}
		if (!put) lines.push_back(std::string("LANGUAGE ") + (english ? "en" : "ru") + "   ; the interface's language: ru or en");
		std::ofstream f(path);
		for (const auto& l : lines) f << l << "\n";
	}

	void SuitHud::SavePrefs() const
	{
		std::ofstream f("Config\\OrbiterCrew\\SuitDisplay.cfg");
		if (!f) return;
		f << "; OrbiterCrew - the suit computer's display, the user's settings (kept across all scenarios)\n";
		f << "LOOK " << look << "        ; 0 holo, 1 photonics, 2 electroluminescent\n";
		f << "PALETTE " << pal << "     ; holo colours: 0 cyan+orange, 1 orange+cyan, 2 white+orange\n";
		f << "BRIGHT_AUTO " << (brightAuto ? 1 : 0) << "\n";
		f << "BRIGHT " << brightManual << "\n";
		f << "MAP_MONO " << (mapMono ? 1 : 0) << "\n";
		f << "RESOLUTION " << hudScale << "  ; 1 = 1:1 with the screen, 2 = 2x smoothed\n";
		f << "HORIZON " << horizonMode << "     ; pitch ladder: 0 auto (flight, space), 1 always, 2 off\n";
		f << "SYNTH " << (synth ? 1 : 0) << "       ; terrain contours on the ground (the AtmoFX D3D9 client)\n";
		f << "SUN_FILTER " << (sunHAlpha ? 1 : 0) << "  ; the visor's sun filter (D3D11): 0 grey, 1 H-alpha\n";
		f << "HUD_THEME " << hudTheme << "   ; the light suit's display: 0 laser, 1 projector, 2 augmented reality, 3 high contrast\n";
		f << "HIDE_LADDER " << (hideLadder ? 1 : 0) << "   ; the pitch ladder hidden\n";
		f << "HUD_MIN " << (hudMin ? 1 : 0) << "   ; the light suit's HUD down to its top arc\n";
		f << "MFD_SIDE " << (mfdSide ? 1 : 0) << "   ; the MFDs on the helmet's glass: 0 in front, 1 at the sides\n";
		f << "SHADE " << shadeK << "   ; the visor's shading behind the light suit's display, 0 .. 2\n";
	}
}
