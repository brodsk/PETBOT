# PETBOT

Облачный мозг и веб-панель управления для домашнего робота PETBOT на базе LILYGO T3 V1.6.1 / ESP32.

## Что уже есть

- **Веб-панель:** чат с PETBOT, шесть настроений, кнопки команд и статус связи.
- **Облачный API:** чат через Vercel AI Gateway, health-check и API команд устройства.
- **Хранилище:** отдельные таблицы `petbot_state` и `petbot_events` в Supabase; существующие таблицы PETIT.SOT не меняются.
- **Прошивка v3.0.0:** `firmware/PETBOT_ESP32.ino` — локальная Vector-inspired модель поведения: любопытство, энергия, настроение, привязанность, возбуждение, социальная потребность и уверенность. Состояния сохраняются в NVS и влияют на выбор эмоций; реакции и автономное поведение работают без облачного ИИ. OLED-лицо, микродвижения взгляда и вариативные сигналы пьезо сохранены. Микрофонный вход по умолчанию выключен; камеры и моторы пока нет.
- **Безопасность:** API платы защищён отдельным `PETBOT_DEVICE_TOKEN`; ключ ИИ остаётся только на сервере.

## v3.3.0 — физическое управление без новых деталей

- **Кнопка BOOT (GPIO0):** один клик = погладить, два клика = поиграть, три и больше = «тыкать» (он обижается), долгое нажатие (1,5 с) = уснуть или проснуться.
- **Сенсорная площадка (по желанию):** провод или кусочек фольги на GPIO4 работает как ёмкостный датчик касания ESP32, никакого модуля не нужно. Включение: `TOUCH:ON` в Serial Monitor (не касаясь провода, идёт калибровка), проверка значений: `TOUCH:READ`, выключение: `TOUCH:OFF`. Пока погладишь, он мурчит.
- Перед использованием проверь по схеме своей платы, что GPIO4 выведен на гребёнку и свободен.
- Не держи BOOT нажатой при включении или сбросе платы: так ESP32 входит в режим прошивки.

## v3.2.0 — голос PETBOT

Звук больше не набор фиксированных писков. Прошивка собирает «фразы» из сегментов (плавные глиссандо, вибрато, мурчание, трели) с небольшой случайностью, поэтому два одинаковых настроения звучат по-разному. Голос работает в отдельной задаче FreeRTOS и не блокирует лицо, Wi-Fi и моргание. Drives окрашивают голос: уставший робот говорит ниже и медленнее, возбуждённый выше и быстрее, а чем сильнее привязанность, тем дольше он мурчит. Команды Serial Monitor: `VOICE:HAPPY|CURIOUS|SURPRISED|LOVE|ANNOYED|SLEEPY|SAD`, `PURR`. Как и раньше, пьезо воспроизводит одну частоту за раз, без громкости и речи.

## v3.1.0 — мозг и характер связаны

- Плата отправляет в heartbeat семь drives; чат видит их (если плата онлайн последние 2 минуты) и отвечает в соответствии с реальным состоянием робота.
- ИИ возвращает JSON `{reply, mood, impulse}`: `mood` — эмоция самого PETBOT, `impulse` (`CHAT`, `AFFECTION`, `PRAISE`, `SCOLD`) меняет drives на плате через команду `CHAT_EVENT`. Разговор теперь снимает потребность в общении.
- Повторная одинаковая эмоция доходит до платы; команда, поставленная в очередь с панели, чатом не затирается.
- Исправлено: `"command":null` разбирался как команда `poll_after_ms` и каждые 5 секунд сбрасывал таймер бездействия; в `Serial.printf` был литерал `\\n` вместо перевода строки.
- Прошивку v3.1.0 нужно скомпилировать (GitHub Actions) и проверить на плате; серверная часть проверена автотестом с моками.

## Настройка окружения Vercel

В проекте, который обслуживает сайт PETBOT, добавьте переменные для Production и Preview:

- `SUPABASE_URL` — URL проекта Supabase.
- `SUPABASE_SERVICE_ROLE_KEY` — service-role secret из Supabase. Никогда не помещайте его в HTML, прошивку или публичный репозиторий.
- `PETBOT_DEVICE_TOKEN` — длинный случайный секрет для авторизации платы. Используйте один и тот же токен в Vercel и в локальной прошивке.

Также должен быть настроен `AI_GATEWAY_API_KEY` для облачного чата. После изменения переменных сделайте redeploy. Не добавляйте секреты в этот README или GitHub.

## Подключение платы

1. В Arduino IDE выберите ESP32 Dev Module и правильный COM-порт.
2. Установите библиотеки **Adafruit GFX Library**, **Adafruit SSD1306** и **WiFiManager** (автор tzapu) через Library Manager в Arduino IDE.
3. Откройте `firmware/PETBOT_ESP32.ino`; секрет в исходник вписывать не нужно.
4. Загрузите прошивку на LILYGO T3 V1.6.1 и откройте Serial Monitor на 115200 baud.
5. Один раз передайте строку `TOKEN:<ваш_существующий_PETBOT_DEVICE_TOKEN>` (подставьте реальное значение вместо поясняющего текста). Прошивка сохранит токен во внутренней памяти ESP32 (NVS); он не попадёт в GitHub. Команда `STATUS` покажет, сохранён ли токен и подключён ли Wi-Fi, не раскрывая сам секрет.
6. При первом запуске подключитесь телефоном или компьютером к Wi-Fi `PETBOT-Setup` (пароль `PETBOT-4827`), затем откройте `http://192.168.4.1`, выберите домашнюю сеть и введите её пароль. После сохранения PETBOT запомнит сеть во внутренней памяти ESP32.
7. При последующих включениях PETBOT автоматически подключается к сохранённой сети. При настроенном API плата отправляет heartbeat каждые 15 секунд и опрашивает команды каждые 5 секунд.

Не отправляйте токен в GitHub и не публикуйте его в скриншотах или логах.

## Первое подключение к Wi-Fi

Прошивка использует WiFiManager: при первом запуске создаёт точку доступа `PETBOT-Setup` с паролем `PETBOT-4827`. Подключитесь к ней, откройте `http://192.168.4.1` и сохраните домашние Wi-Fi-данные. WiFiManager хранит их в NVS ESP32, поэтому пароль сети не хранится в исходнике и не попадает в GitHub. Точку настройки защищает временный пароль; после подключения PETBOT использует домашнюю сеть.

## Поведенческий движок v3.0.0

PETBOT использует небольшую локальную модель внутренних drives, вдохновлённую общими принципами companion-роботов. Это не копия закрытого Emotion Engine Anki Vector: параметры и политика PETBOT реализованы самостоятельно. Семь шкал: любопытство, энергия, настроение, привязанность, возбуждение, социальная потребность и уверенность. События повышают/снижают шкалы; политика поведения выбирает выражение, анимацию и звуковой мотив. Случайность ограничена и используется только для вариативности. Команды Serial Monitor: `NEEDS`, `EVENT:SOUND`, `EVENT:SUCCESS`, `EVENT:FAIL`, `PET`, `WAKE`, `SLEEP`, `STATUS`.

## Автономные эмоции и фактическое оборудование

Локальный эмоциональный движок работает без облачного ИИ и не должен засыпать только из-за потери Wi-Fi. После ограниченной попытки настройки сети PETBOT продолжает запуск в offline-режиме; облачный чат и команды при этом недоступны. Пьезодинамик передаёт эмоции короткими писками и низкими сигналами — без речи. Команды `SOUND:OFF` / `MUTE` отключают звуки. Облачное настроение — временный импульс, а не постоянная блокировка локальной логики.

Сейчас прошивка не может достоверно измерять заряд только потому, что аккумулятор вставлен в разъём: сначала нужно подтвердить схему и ADC-пин этой ревизии платы. УФ-датчик и камера пока не подключены к логике, потому что их модели и интерфейсы не определены. Сервопривод и четыре мотора отключены; неизвестная сломанная плата дрона не считается безопасным драйвером.

## LoRa и моторы

LoRa (433 MHz на текущей плате) намеренно не включена в эту первую облачную прошивку. Сначала проверяем Wi-Fi связь сайта и платы; радио добавим отдельным этапом, после проверки антенны и региональных ограничений.

Моторы намеренно не управляются. Нужны подтверждённый драйвер, отдельное корректное питание и аварийная остановка. Кнопки `Осмотреться`, `Спать` и `Проснуться` пока демонстрируют безопасные облачные команды/эмоции, а не физическое движение.

## Roadmap: PETBOT OS

The next development phase combines PETBOT's existing cloud-controlled personality with selected companion features inspired by projects such as QBIT. PETBOT remains a separate implementation; QBIT firmware and assets are not copied.

### Phase 1 — reliable core

- Keep the LILYGO T3 V1.6.1 / ESP32 firmware compatible with the current OLED wiring and cloud API.
- Report the actual firmware version in heartbeat data.
- Preserve the existing device token in NVS and never expose it in the browser or repository.
- Verify firmware compilation and end-to-end heartbeat/command delivery before adding new hardware features.

### Phase 2 — messages and notifications

- Add a validated, short text-message command to the existing authenticated device API.
- Show incoming messages on the OLED without blocking Wi-Fi, heartbeat, or command polling.
- Keep the current expressive face as the default idle screen and return to it after a notification.
- Record notification events in the existing PETBOT event history.

### Phase 3 — richer personality and controls

- Add a small, versioned animation/gesture catalogue designed for PETBOT's 128x64 OLED and available memory.
- Add notification history and animation controls to the existing web dashboard.
- Consider weather, timers, and smart-home integration only after the core interaction is tested.

### Compatibility and safety

- Do not flash QBIT firmware onto the LILYGO board: QBIT targets different hardware and pin assignments.
- Do not enable LoRa, motors, UV sensing, or battery readings without confirming hardware and testing the implementation.
- Keep device authentication server-side and do not put secrets in frontend code.
- Treat every firmware change as untested until it has compiled and been checked on the physical board.

## Локальная разработка

Проект рассчитан на Vercel. Основная панель — `index.html`, API — `api/`, прошивка — `firmware/`.


## План аппаратного расширения

Подробный порядок закупки, ограничения GPIO и безопасная схема расширения описаны в [`docs/HARDWARE_ROADMAP.md`](docs/HARDWARE_ROADMAP.md). Приоритет: драйвер двух DC-моторов и отдельное питание, датчики падения вниз и препятствия спереди, бампер/сенсор касания, микрофонный модуль с аналоговым выходом или I2S, затем отдельная камера-плата ESP32-S3 с PSRAM. Не подключайте моторы напрямую к GPIO и не подавайте 5 В на входы ESP32.
