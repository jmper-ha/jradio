# jradio

Проект интернет-радио и аудиоплеера на ESP32-S3 (ESP-IDF). Реализована
базовая плата с ILI9341, энкодером, функциональными кнопками, PCM5102 по I2S,
Wi-Fi provisioning, web-сервером, LVGL и интернет-радио.

## Требования

- ESP-IDF 5.5.x;
- target `esp32s3`;
- Python-инструменты ESP-IDF;
- плата ESP32-S3 с 16 МБ flash и PSRAM.

## Сборка

Сначала активируйте ESP-IDF в своей системе, затем выполните:

```bash
idf.py set-target esp32s3
idf.py build
```

Для записи прошивки и LittleFS используйте выбранный последовательный порт:

```bash
idf.py -p PORT flash
idf.py -p PORT littlefs-flash
```

`littlefs-flash` записывает каталог `data` целиком: веб-файлы из `data/www`
и начальные настройки из `data/config`.
Обычная прошивка приложения не должна затирать пользовательские данные LittleFS.

## Конфигурация

- `data/config/stations.csv` — список интернет-радиостанций;
- `data/config/settings.csv` — настройки в формате `key,value`;
- `data/config/wifi.json` — создаётся устройством и намеренно не входит в Git.
- `data/www/` — `index.html`, `app.js` и `style.css` веб-сервера.

Не добавляйте в репозиторий реальные Wi-Fi-пароли, токены или другие ключи.

## VS Code

В проекте есть задачи в `.vscode/tasks.json`. Они используют `idf.py` из
окружения VS Code/терминала и не содержат локальных абсолютных путей.
