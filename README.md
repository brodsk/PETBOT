# PETBOT

Облачный мозг и веб-панель управления для домашнего робота PETBOT на базе LILYGO T3 V1.6.1 / ESP32.

## Что уже есть

- **Веб-панель:** чат с PETBOT, шесть настроений, кнопки команд и статус связи.
- **Облачный API:** чат через Vercel AI Gateway, health-check и API команд устройства.
- **Хранилище:** отдельные таблицы `petbot_state` и `petbot_events` в Supabase; существующие таблицы PETIT.SOT не меняются.
- **Прошивка v2.4:** `firmware/PETBOT_ESP32.ino` — OLED-лицо, Wi-Fi, автономные эмоции, микродвижения глаз, вариативные звуковые сигналы и короткие мелодии через пьезодинамик (без синтеза речи), heartbeat с настроением и диагностикой оборудования.
- **Безопасность:** API платы защищён отдельным `PETBOT_DEVICE_TOKEN`; ключ ИИ остаётся только на сервере.

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

## Автономные эмоции и фактическое оборудование

Локальный эмоциональный движок работает без облачного ИИ: потеря Wi-Fi вызывает звуковое недовольство, длительный обрыв или бездействие переводит PETBOT в сонное состояние, восстановление связи вызывает удивление, а команды дают краткую реакцию. Пьезодинамик передаёт эмоции только короткими писками, переливами и низкими звуковыми сигналами — без слов и синтеза речи. Звуки чаще появляются во время бодрствования и реже во сне. Команды `SOUND:OFF` / `MUTE` отключают и фоновые звуки. Облачное настроение — временный импульс, а не постоянная блокировка локальной логики.

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
