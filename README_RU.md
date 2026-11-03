# ESP32 + RFM95 GNARL Прошивка

RileyLink-совместимая прошивка для **TTGO LoRa32 V1.6.1** (плата по умолчанию в этом форке)
с радиомодулем SX1276. Реализует протокол RFSpy/GNARL для связи с инсулиновыми помпами
Medtronic через iAPS/Loop/AndroidAPS. Поддерживаются также Heltec WiFi LoRa 32 V2 и ESP32-C6.

## Благодарности

Проект использует код из:
- [GNARL](https://github.com/ecc1/gnarl/) - оригинальная реализация GNARL
- [Pickle](https://github.com/d3xr/pickle/) - порт на ESP32

## Поддерживаемое оборудование

### TTGO LoRa32 V1.6.1 (T3, SX1276 на плате) — по умолчанию

- **MCU:** ESP32 Xtensa LX6 dual-core
- **Радио:** SX1276 в режиме OOK @ 868.25 МГц (встроен, отдельный RFM95 не нужен)
- **Питание:** разъём JST 1.25мм для LiPo + зарядка TP4054

| Функция | GPIO |
|---------|------|
| LED | 25 |
| Кнопка | 0 |
| LORA_SCK | 5 |
| LORA_MOSI | 27 |
| LORA_MISO | 19 |
| LORA_CS | 18 |
| LORA_RST | **23** |
| LORA_DIO0 | 26 |
| LORA_DIO1 | 33 |
| LORA_DIO2 | 32 |
| BATTERY_ADC | 35 (делитель 100k/100k) |
| OLED_SDA / SCL | 21 / 22 |

Плата TTGO выбрана по умолчанию в `include/module.h` (`#define BOARD_TTGO_LORA32_V161`).
Для сборки под Heltec V2 закомментируйте эту строку.

### ESP32 Heltec WiFi LoRa 32 V2 (альтернатива)

- **MCU:** ESP32 Xtensa LX6 dual-core @ 80 MHz
- **Радио:** RFM95 (SX1276) в режиме OOK @ 868.25 МГц
- **BLE:** NimBLE стек

| Функция | GPIO |
|---------|------|
| LED | 25 |
| Кнопка | 0 |
| LORA_SCK | 5 |
| LORA_MOSI | 27 |
| LORA_MISO | 19 |
| LORA_CS | 18 |
| LORA_RST | 14 |
| LORA_DIO0 | 26 |
| LORA_DIO2 | 32 |
| BATTERY_ADC | 37 |

### ESP32-C6 Super Mini + RFM95 (альтернатива)

- **MCU:** ESP32-C6 RISC-V single-core @ 160 MHz
- **Радио:** RFM95 (SX1276) в режиме OOK @ 868.25 МГц
- **LED:** WS2812 RGB

| Функция | GPIO |
|---------|------|
| LED (WS2812) | 8 |
| Кнопка | 9 |
| LORA_SCK | 6 |
| LORA_MOSI | 3 |
| LORA_MISO | 2 |
| LORA_CS | 5 |
| LORA_RST | 7 |
| LORA_DIO2 | 14 |
| BATTERY_ADC | 0 |

Для сборки под ESP32-C6:
```bash
idf.py set-target esp32c6
idf.py build
```

## Требования

- **ESP-IDF:** v5.2+ (проверено на v5.4.1)
- **Python:** 3.8+
- **CMake:** 3.16+

## Готовая прошивка из GitHub Actions (без установки ESP-IDF)

В репозитории настроена сборка прошивки в облаке (`.github/workflows/firmware.yml`).
На выходе — единый файл `rileylink-ttgo.bin`, содержащий bootloader + partition table + приложение.

### Разовая настройка

1. **Settings → Secrets and variables → Actions → New repository secret**:
   - `PUMP_ID` — серийный номер вашей помпы (например `123456`)
   - `PUMP_FREQUENCY` — частота (опционально, по умолчанию `868250000`)
2. Запустить сборку: **Actions → firmware → Run workflow** — готовый `.bin` появится
   в артефактах сборки. Либо поставить тег для полноценного релиза:
   ```bash
   git tag v0.1 && git push origin v0.1
   ```
   Файл появится на странице **Releases**.

### Прошивка платы (2 команды)

```bash
pip install esptool
esptool.py --port /dev/ttyUSB0 write_flash 0x0 rileylink-ttgo.bin
```

- Порт уточните: `ls /dev/ttyUSB* /dev/ttyACM*` (Linux), `ls /dev/cu.usbserial-*` (macOS),
  `COMx` в Windows.
- Если прошивка не стартует — зажмите кнопку **BOOT** на плате при запуске esptool.
- **Перед включением прикрутите антенну 868 МГц!**

## Установка (сборка из исходников)

### 1. Установка ESP-IDF

```bash
# Клонировать ESP-IDF
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf

# Установить инструменты
cd ~/esp/esp-idf
./install.sh esp32

# Добавить в shell (добавить в ~/.bashrc или ~/.zshrc)
alias get_idf='. ~/esp/esp-idf/export.sh'
```

### 2. Настройка параметров помпы

В репозитории лежит `include/pump_config.h` с плейсхолдером (`PUMP_ID "000000"`).
При сборке через GitHub Actions файл автоматически перезаписывается из
workflow input `pump_id` или секрета `PUMP_ID` — менять его руками не нужно.

Для локальной сборки отредактируйте `include/pump_config.h`:

```c
#ifndef PUMP_CONFIG_H
#define PUMP_CONFIG_H

#define PUMP_ID         "123456"      // Серийный номер помпы
#define PUMP_FREQUENCY  868250000     // Рабочая частота (Гц)

#endif
```

**Не коммитьте реальный серийный номер помпы в git** — оставляйте в репозитории
только плейсхолдер `000000`. Если случайно закоммитили — смените его обратно
на плейсхолдер и считайте старый коммит скомпрометированным (номер остаётся в истории).

### 3. Сборка и прошивка

```bash
# Активировать окружение ESP-IDF
get_idf

# Перейти в проект
cd esp32rfm95

# Сборка
idf.py build

# Прошивка (замените порт на ваш)
idf.py -p /dev/cu.usbserial-0001 flash

# Мониторинг serial
idf.py -p /dev/cu.usbserial-0001 monitor

# Сборка + Прошивка + Мониторинг
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

### 4. Чистая сборка

При изменении `sdkconfig.defaults`:

```bash
idf.py fullclean
idf.py build
```

## Конфигурация

### Опции (`include/module.h`)

```c
// Опции отладки (раскомментировать для включения)
// #define BLE_DEBUG      // Дамп BLE пакетов
// #define RADIO_DEBUG    // Дамп TX/RX пакетов

// Имя BLE устройства (макс. 8 символов)
#define BLE_NAME        "MYGN"

// Light sleep при отключённом BLE (раскомментировать для включения)
// #define BLE_SLEEP
```

### Энергосбережение (`sdkconfig.defaults`)

- Tickless idle для автоматического light sleep
- Параметры BLE оптимизированы для экономии энергии
- Уровень логирования — только ERROR

## OLED-экран (TTGO)

На бортовой SSD1306 128×64 каждую секунду выводится статус:

```
MYGN     BLE:CONN     <- имя + состояние BLE
------------------
Batt 4.02V     87%    <- батарея
Freq 868.250 MHz      <- частота радио
Pump -78dBm 5s        <- RSSI помпы + время последнего ответа
Pump 123456           <- серийник помпы из прошивки
Link 98% ok           <- доля успешных приёмов
Up 02:34:17           <- аптайм
```

## Использование

1. Прошить устройство
2. Открыть iAPS/Loop/AndroidAPS
3. Добавить новое устройство RileyLink (отображается как "RileyLink")
4. Устройство автоматически подключится и синхронизируется с помпой

## Решение проблем

### Устройство не найдено в приложении
- Проверьте включён ли BLE на телефоне
- Перезагрузите устройство
- Проверьте serial монитор на ошибки

### Помпа не отвечает
- Проверьте, что при сборке workflow был указан правильный `pump_id`
  (input при запуске или secret `PUMP_ID`); на OLED-экране строка `Pump …` показывает
  серийник, зашитый в прошивку
- Проверьте частоту (обычно 868250000 Гц для EU)
- Убедитесь что помпа в зоне досягаемости (~1-2 метра)
- Смотрите строку `Pump -XXdBm Ns` на экране: RSSI и время последнего ответа помпы

### Watchdog timeout
- Проверьте `CONFIG_ESP_TASK_WDT_TIMEOUT_S=30` в sdkconfig.defaults
- Выполните `idf.py fullclean && idf.py build`

## Файлы

| Файл | Описание |
|------|----------|
| `main/main.c` | Точка входа, инициализация |
| `main/gnarl.c` | Обработчик протокола RFSpy |
| `main/ble.c` | NimBLE GATT сервисы |
| `main/adc.c` | Мониторинг батареи |
| `main/oled.c` | Драйвер SSD1306 и экран статуса |
| `components/radio/rfm95.c` | Драйвер RFM95 |
| `components/medtronic/` | Протокол Medtronic |
| `include/module.h` | GPIO пины, выбор платы (TTGO по умолчанию) |
| `include/pump_config.h` | Настройки помпы (плейсхолдер; реальный ID задаётся в CI) |

## Лицензия

Основано на проекте GNARL. См. файл LICENSE.
