# Экран настроек с раскрывающимися группами — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Реализовать на ILI9341 интерактивный экран настроек с раскрывающимися группами, немедленным применением параметров и сохранением в `/littlefs/config/settings.csv`.

**Architecture:** Чистая модель групп, строк и навигации будет размещена в отдельном host-тестируемом модуле `ui_settings_model`. Слой `device_settings` загрузит четыре параметра из существующего CSV-хранилища и атомарно применит/сохранит изменения. `ui.c` будет отвечать только за LVGL-виджеты, перевод действий энкодера в операции модели и вызов callback применения.

**Tech Stack:** ESP-IDF 5.5.4, C17, FreeRTOS, LVGL 9.5, LittleFS, существующие `settings_csv`, `board` и UI input queue.

## Global Constraints

- Экран имеет разрешение 320×240 и использует существующий шрифт Cyrillic.
- Одновременно раскрыта не более чем одна группа.
- Изменение параметра применяется и сохраняется немедленно.
- Путь настроек: `/littlefs/config/settings.csv`.
- Неизвестные строки CSV сохраняются при изменении известных ключей.
- Вызовы LVGL выполняются только из существующей UI-задачи.
- До production-кода для каждой новой модели поведения добавляется падающий host-тест.

---

### Task 1: Модель групп и навигации

**Files:**
- Create: `components/ui/include/ui_settings_model.h`
- Create: `components/ui/ui_settings_model.c`
- Modify: `components/ui/CMakeLists.txt`
- Create: `tests/test_ui_settings_model.c`
- Modify: `tests/run_host_tests.sh`

**Interfaces:**
- `ui_settings_model_init(ui_settings_model_t *model)` создаёт закрытый список групп и курсор на группе `Язык`.
- `ui_settings_model_move(ui_settings_model_t *model, int direction)` перемещает курсор на соседнюю доступную строку и ограничивает его текущей группой.
- `ui_settings_model_activate(ui_settings_model_t *model)` раскрывает/закрывает группу, если курсор на заголовке, и возвращает `UI_SETTINGS_MODEL_CHANGED`.
- `ui_settings_model_selected(const ui_settings_model_t *model)` возвращает стабильный идентификатор текущей строки.
- `ui_settings_model_row_count` и `ui_settings_model_row_at` возвращают плоское представление видимых строк для отрисовки.

- [ ] **Step 1: Write failing tests** для порядка `Язык → Общие → Экран`, раскрытия `Язык`, перемещения только по заголовку и полю, закрытия по заголовку, а также перехода из одной группы в другую.
- [ ] **Step 2: Run the focused test and verify the expected failure**:
  `./tests/run_host_tests.sh ui_settings_model` должен завершиться ошибкой из-за отсутствующего заголовка/реализации модели.
- [ ] **Step 3: Implement the minimal model** с перечислениями групп/строк и единственным `expanded_group`.
- [ ] **Step 4: Run focused and full host tests** и убедиться, что курсор не выходит за границы видимых строк.
- [ ] **Step 5: Commit** `feat: add grouped settings navigation model`.

### Task 2: Хранилище и применение настроек

**Files:**
- Create: `components/settings/include/device_settings.h`
- Create: `components/settings/device_settings.c`
- Modify: `components/settings/CMakeLists.txt`
- Create: `tests/test_device_settings.c`
- Modify: `tests/run_host_tests.sh`

**Interfaces:**
- `device_settings_init(device_settings_t *settings)` загружает значения или defaults: `ru`, `text`, `0`, `0`.
- `device_settings_set_language`, `device_settings_set_home_screen`, `device_settings_set_flip_vertical`, `device_settings_set_flip_horizontal` валидируют, меняют модель и вызывают `settings_csv_set` с ключами `language`, `home_screen`, `display_flip_vertical`, `display_flip_horizontal`.
- `device_settings_get` возвращает неизменяемую копию модели для UI.
- `device_settings_apply_loaded(const device_settings_t *settings)` применяется после загрузки дисплея без записи файла.

- [ ] **Step 1: Write failing tests** для defaults, чтения существующих ключей, сохранения неизвестной строки, отклонения неверных enum/boolean значений и немедленного вызова записи.
- [ ] **Step 2: Run focused tests** и зафиксировать ожидаемое падение на отсутствующем API.
- [ ] **Step 3: Implement CSV-backed model** поверх `settings_csv_get/set`; не удалять неизвестные строки и использовать уже существующую временную замену файла.
- [ ] **Step 4: Run focused/full host tests**.
- [ ] **Step 5: Commit** `feat: add persistent device settings model`.

### Task 3: Применение ориентации и экранных параметров

**Files:**
- Modify: `components/board/include/board.h` или существующий публичный board header с API дисплея
- Modify: `components/board/board.c`
- Modify: `components/ui/ui.c`
- Modify: `components/ui/include/ui_settings_view.h`
- Modify: `components/ui/ui_settings_view.c`
- Modify: `main/main.c` или текущая точка инициализации платы

**Interfaces:**
- Добавить `board_display_set_rotation(bool flip_vertical, bool flip_horizontal)`; вызов остаётся в board/display layer.
- `ui_settings_view` получает модель настроек и формирует подпись заголовка, значения полей и состояние переключателей.
- `ui.c` вызывает `device_settings_*` после выбора и сразу обновляет LVGL-виджеты.

- [ ] **Step 1: Write failing view/model integration tests** для русских/английских подписей, строк `Текст`/`Лента`, состояний двух переключателей и операции применения ориентации.
- [ ] **Step 2: Run focused tests** и убедиться, что текущий read-only экран не удовлетворяет тестам.
- [ ] **Step 3: Replace the read-only rows** в `ui_create_settings_screen` на фиксированный набор заголовков/полей с тёмным фоном групп и более светлым фоном полей; применять `ui_font_cyrillic_14` через наследование.
- [ ] **Step 4: Update `ui_handle_input`**: F2 закрывает настройки; поворот вызывает `ui_settings_model_move`; нажатие вызывает `ui_settings_model_activate` или изменяет выбранное поле; заголовок раскрытой группы закрывает её.
- [ ] **Step 5: Apply immediately**: после успешного изменения вызвать `board_display_set_rotation` для ориентации, перерисовать язык и обновить выбранное значение главного экрана; при ошибке записи откатить модель и показать короткое сообщение.
- [ ] **Step 6: Run host tests and full ESP-IDF build**.
- [ ] **Step 7: Commit** `feat: implement interactive grouped settings screen`.

### Task 4: Инициализация, регрессии и документация

**Files:**
- Modify: точка инициализации приложения, где создаётся LittleFS и дисплей
- Modify: `README.md`
- Modify: `tests/run_host_tests.sh` только если требуется общий счётчик наборов

- [ ] **Step 1: Load `device_settings` after LittleFS mount** и применить ориентацию до первого `lv_screen_load`.
- [ ] **Step 2: Verify settings screen from the main menu**: encoder rotation, open/close, dropdown selection, toggle selection, F2 exit.
- [ ] **Step 3: Run `./tests/run_host_tests.sh`**; все существующие и новые наборы должны пройти.
- [ ] **Step 4: Run ESP-IDF verification**: `idf.py reconfigure`, `idf.py build`, `idf.py size`; проверить, что LittleFS image собирается и IRAM не превышает доступный объём.
- [ ] **Step 5: Update README** with the three groups, CSV keys and immediate-apply behavior.
- [ ] **Step 6: Commit** `test: verify grouped settings on device`.

## Проверка полноты

Спецификация покрыта задачами следующим образом: структура списка и курсор —
Task 1; CSV и немедленное сохранение — Task 2; выпадающие списки, переключатели,
светлый фон и применение ориентации — Task 3; загрузка при старте, регрессии,
сборка и документация — Task 4.
