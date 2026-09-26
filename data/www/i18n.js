/* The words the browser shows, in both languages, and the machinery that swaps
   them.

   The device holds the setting - one switch, on the settings page or on the
   panel's own screen - and every page follows it live, because the language
   travels in the same settings section the volume and the brightness do. So a
   language changed on the device relabels an open browser tab within a poll,
   and the other way round.

   Why a dictionary rather than two sets of pages: the HTML is served from the
   device's own flash, and a second copy of three pages would cost the space
   twice over for text that is a few kilobytes. It also keeps the Russian text
   in the markup, so a page that somehow runs before this script has loaded
   still reads correctly rather than showing bare keys.

   Strings the device sends - source labels, playback states, its error lines,
   the time-zone names - are not here. They arrive already translated, out of
   the same table the panel reads, so the two faces cannot word the same thing
   differently. */
(() => {
  'use strict';

  // key: [Russian, English]
  const DICT = {
    // The shell every page shares.
    'title.player': ['jRadio — плеер', 'jRadio — player'],
    'title.playlist': ['jRadio — плейлист', 'jRadio — playlist'],
    'title.settings': ['jRadio — настройки', 'jRadio — settings'],
    'nav.player': ['Плеер', 'Player'],
    'nav.playlist': ['Плейлист', 'Playlist'],
    'nav.settings': ['Настройки', 'Settings'],
    'nav.back_to_player': ['Вернуться к плееру', 'Back to the player'],
    'nav.sections': ['Разделы', 'Sections'],
    'theme.toggle': ['Сменить тему', 'Switch theme'],
    /* The flasher site's switch names the language it goes to, each in that
       language, so a reader who cannot read the current one still finds it. */
    'lang.other': ['EN', 'RU'],
    'lang.toggle': ['Switch to English', 'Переключить на русский'],
    'socket.connecting': ['Подключение…', 'Connecting…'],
    'socket.online': ['Подключено', 'Connected'],
    'socket.offline': ['Нет связи', 'No connection'],

    // The player page.
    'player.home': ['jradio — главная', 'jradio — home'],
    'player.sources': ['Источники', 'Sources'],
    'player.playlist_editor': ['Редактор плейлиста', 'Playlist editor'],
    'player.nothing_playing': ['Нет воспроизведения', 'Nothing playing'],
    'player.waiting': ['Ожидание связи', 'Waiting for the device'],
    'player.previous': ['Предыдущий трек', 'Previous track'],
    'player.next': ['Следующий трек', 'Next track'],
    'player.play': ['Воспроизвести', 'Play'],
    'player.pause': ['Пауза', 'Pause'],
    'player.like': ['Нравится', 'Like'],
    'player.dislike': ['Не нравится', 'Dislike'],
    'player.position': ['Позиция в треке', 'Position in the track'],
    'player.volume': ['Громкость', 'Volume'],
    'player.expand': ['Развернуть карточку трека', 'Expand the track card'],
    'player.collapse': ['Свернуть карточку трека', 'Collapse the track card'],
    'player.no_stream_info': ['Параметры потока появятся после подключения',
                              'Stream details appear once it connects'],
    'player.untitled': ['Без названия', 'Untitled'],
    'player.volume_failed': ['Не удалось изменить громкость', 'Could not change the volume'],

    /* The sleep timer. The countdown itself is digits and needs no words; the
       menu is minutes, so one string with the number in it serves every
       entry and a new preset costs nothing. */
    'sleep.label': ['Таймер сна', 'Sleep timer'],
    'sleep.off': ['Выключен', 'Off'],
    'sleep.minutes': ['{n} мин', '{n} min'],
    'sleep.failed': ['Не удалось установить таймер', 'Could not set the timer'],

    /* The alarm clock. The day names are the short forms a calendar uses;
       Monday is shown first while the bit each one carries counts from
       Sunday, the way the C library does. */
    'alarm.group': ['Будильник', 'Alarm clock'],
    'alarm.enabled': ['Включён', 'On'],
    'alarm.time': ['Время', 'Time'],
    'alarm.days': ['Дни', 'Days'],
    'alarm.station': ['Станция', 'Station'],
    'alarm.volume': ['Громкость', 'Volume'],
    'alarm.mon': ['Пн', 'Mon'],
    'alarm.tue': ['Вт', 'Tue'],
    'alarm.wed': ['Ср', 'Wed'],
    'alarm.thu': ['Чт', 'Thu'],
    'alarm.fri': ['Пт', 'Fri'],
    'alarm.sat': ['Сб', 'Sat'],
    'alarm.sun': ['Вс', 'Sun'],
    'alarm.one_day': ['Нужен хотя бы один день', 'At least one day is needed'],
    'alarm.no_stations': ['Плейлист пуст', 'The playlist is empty'],
    /* The player page's indicator: the bell, the time, and which days in
       short - "будильник 07:30, Пн–Пт" reads at a glance. */
    'alarm.at': ['Будильник {time}', 'Alarm {time}'],
    'alarm.every_day': ['каждый день', 'every day'],
    'alarm.weekdays': ['по будням', 'weekdays'],
    'alarm.weekend': ['по выходным', 'weekends'],

    /* The remote control: its card on the settings page, and its own page. */
    'title.remote': ['jRadio — пульт', 'jRadio — remote control'],
    'remote.title': ['Пульт', 'Remote control'],
    'note.remote': ['Любой инфракрасный пульт: на отдельной странице у каждой функции нажимаете «Обучить», затем кнопку на пульте. Таблица хранится на устройстве в remote.csv.',
                    'Any infrared remote: on its own page you press "Learn" beside each function, then the key on the remote. The table lives on the device in remote.csv.'],
    'remote.open': ['Настроить пульт', 'Set up the remote'],
    'note.remote_page': ['Любой инфракрасный пульт: нажмите «Обучить» у функции, затем кнопку на пульте. Одна кнопка — одна функция; таблица хранится на устройстве в remote.csv.',
                         'Any infrared remote: press "Learn" beside a function, then the key on the remote. One key, one function; the table lives on the device in remote.csv.'],
    'remote.keys': ['Кнопки', 'Keys'],
    'remote.missing': ['В этой сборке нет ИК-приёмника: IR_RECEIVER_GPIO не задан.',
                       'This build has no IR receiver: IR_RECEIVER_GPIO is not set.'],
    'remote.learn': ['Обучить', 'Learn'],
    'remote.forget': ['Забыть', 'Forget'],
    'remote.waiting': ['нажмите кнопку на пульте…', 'press the key on the remote…'],
    'remote.press': ['«{function}»: нажмите кнопку на пульте', '"{function}": press the key on the remote'],
    'remote.press_any': ['нажмите кнопку на пульте', 'press the key on the remote'],
    'remote.learned': ['Записано', 'Learned'],
    'remote.timeout': ['Кнопки не дождались', 'No key came'],
    'remote.unknown': ['Кнопка {code} не обучена', 'Key {code} is not learned'],
    'remote.failed': ['Устройство не ответило', 'The device did not answer'],
    'remote.group.sound': ['Звук', 'Sound'],
    'remote.group.navigate': ['Навигация', 'Navigation'],
    'remote.group.extras': ['Ещё', 'More'],
    'remote.group.digits': ['Номер станции', 'Station number'],
    'remote.group.sources': ['Источники', 'Sources'],
    'remote.f.power': ['Сон / пробуждение', 'Sleep / wake'],
    'remote.f.volume_up': ['Громче', 'Volume up'],
    'remote.f.volume_down': ['Тише', 'Volume down'],
    'remote.f.mute': ['Без звука', 'Mute'],
    'remote.f.play_pause': ['Пауза / играть', 'Play / pause'],
    'remote.f.prev': ['Назад (станция, трек)', 'Previous (station, track)'],
    'remote.f.next': ['Вперёд (станция, трек)', 'Next (station, track)'],
    'remote.f.up': ['Вверх по списку', 'Up the list'],
    'remote.f.down': ['Вниз по списку', 'Down the list'],
    'remote.f.ok': ['OK', 'OK'],
    'remote.f.back': ['Назад / на главный', 'Back / home'],
    'remote.f.menu': ['Меню', 'Menu'],
    'remote.f.quick': ['Быстрые настройки', 'Quick panel'],
    'remote.f.list': ['Список станций', 'Station list'],
    'remote.f.sleep': ['Таймер сна', 'Sleep timer'],
    'remote.f.like': ['Нравится (ЯМузыка)', 'Like (Ya.Music)'],
    'remote.f.dislike': ['Не нравится (ЯМузыка)', 'Dislike (Ya.Music)'],
    'remote.f.digit_0': ['0', '0'], 'remote.f.digit_1': ['1', '1'], 'remote.f.digit_2': ['2', '2'],
    'remote.f.digit_3': ['3', '3'], 'remote.f.digit_4': ['4', '4'], 'remote.f.digit_5': ['5', '5'],
    'remote.f.digit_6': ['6', '6'], 'remote.f.digit_7': ['7', '7'], 'remote.f.digit_8': ['8', '8'],
    'remote.f.digit_9': ['9', '9'],
    'remote.f.source_radio': ['Интернет-радио', 'Internet radio'],
    'remote.f.source_usb': ['USB-накопитель', 'USB drive'],
    'remote.f.source_sd': ['SD-карта', 'SD card'],
    'remote.f.source_bluetooth': ['Bluetooth', 'Bluetooth'],
    'remote.f.source_yandex': ['ЯМузыка', 'Ya.Music'],
    'remote.f.source_dlna': ['Медиасервер', 'Media server'],

    /* The hardware editor. Its labels are built by hardware.js rather than
       written in the markup, so every one of them is here: the parts, the
       signals, the values a part can be, the notes on a pin, and the report. */
    /* The flasher page, beside the editor on the flasher site. */
    'title.flasher': ['jRadio — прошивка', 'jRadio — flasher'],
    'nav.flasher': ['Прошивка', 'Flasher'],
    'fl.lead': ['jRadio на ESP32-S3 прямо из браузера: прошивка под ваш дисплей и вашу распайку, без установки ESP-IDF.',
                'jRadio onto an ESP32-S3 from the browser: firmware for your display and your wiring, with no ESP-IDF to install.'],
    'fl.unsupported': ['Этот браузер не умеет работать с последовательным портом. Откройте страницу в Chrome или Edge на компьютере — с телефона, из Firefox и Safari прошить нельзя.',
                       'This browser cannot use a serial port. Open the page in Chrome or Edge on a computer - phones, Firefox and Safari cannot flash.'],
    'fl.board': ['Распайка', 'Wiring'],
    'fl.edit': ['Изменить', 'Edit'],
    'fl.source_draft': ['Из редактора «Оборудование» на этом сайте.', 'From the Hardware editor on this site.'],
    'fl.source_default': ['Плата из README. Если у вас другая распайка или другой дисплей — задайте их в редакторе «Оборудование».',
                          'The README board. If your wiring or display differs, set them in the Hardware editor.'],
    'fl.parts': ['Подключено', 'Fitted'],
    'fl.no_manifest': ['Не удалось прочитать manifest.json: файлов прошивки на сайте нет.', 'Cannot read manifest.json: the site has no firmware files.'],
    'fl.no_build': ['Для дисплея {display} готовой прошивки нет.', 'There is no ready firmware for the {display} display.'],
    'fl.firmware': ['1. Прошивка', '1. Firmware'],
    'fl.firmware_note': ['Программа и распайка. Wi-Fi, настройки, плейлист и иконки станций остаются на месте — эта кнопка для обновлений.',
                         'The program and the wiring. Wi-Fi, settings, the playlist and station icons stay as they are - this is the button for updates.'],
    'fl.flash_firmware': ['Записать прошивку', 'Write the firmware'],
    'fl.erase': ['стереть всю память (на плате была чужая прошивка)', 'erase the whole flash (the board had other firmware)'],
    'fl.erase_note': ['Стирание удаляет и файловую систему: после него запишите и её, кнопкой ниже.',
                      'Erasing removes the file system too: write it afterwards with the button below.'],
    'fl.littlefs': ['2. Файловая система', '2. File system'],
    'fl.littlefs_note': ['Веб-интерфейс и начальные настройки. Нужна при первой установке — без неё у радио нет веб-страниц — и когда новая версия меняет веб-интерфейс.',
                         'The web interface and the initial settings. Needed on a first install - without it the radio has no web pages - and when a new version changes the web interface.'],
    'fl.littlefs_warn': ['Запись стирает сохранённые сети Wi-Fi, настройки, плейлист и иконки станций. На работающем радио сначала сделайте резервную копию (Настройки → Резервная копия), а после записи восстановите её.',
                         'Writing it erases the saved Wi-Fi networks, the settings, the playlist and the station icons. On a working radio take a backup first (Settings - Backup) and restore it afterwards.'],
    'fl.littlefs_agree': ['понимаю, что данные на радио будут стёрты', 'I understand the data on the radio will be erased'],
    'fl.flash_littlefs': ['Записать файловую систему', 'Write the file system'],
    'fl.progress': ['Ход записи', 'Progress'],
    'fl.idle': ['Подключите плату к компьютеру кабелем к её UART-порту и нажмите одну из кнопок.',
                'Connect the board to the computer by its UART port and press one of the buttons.'],
    'fl.log': ['Журнал', 'Log'],
    'fl.port_help': ['Порт не появляется — поставьте драйвер CP210x или CH340; плата не отвечает — зажмите BOOT, коротко нажмите RESET, отпустите BOOT и попробуйте снова. На Linux, если перед этим порт открывали idf.py, esptool или монитор порта, переподключите кабель: они оставляют порт в настройке, на которой Chrome сразу его теряет.',
                     'No port shows up - install the CP210x or CH340 driver; the board does not answer - hold BOOT, tap RESET, let go of BOOT and try again. On Linux, if idf.py, esptool or a serial monitor had the port open before, reconnect the cable: they leave the port set up in a way that makes Chrome lose it at once.'],
    'fl.pick_port': ['Выберите порт платы в окне браузера…', 'Pick the board\'s port in the browser\'s window…'],
    'fl.connecting': ['Подключаемся к плате…', 'Connecting to the board…'],
    'fl.chip': ['Чип', 'Chip'],
    'fl.port': ['Порт', 'Port'],
    'fl.wrong_chip': ['Это не ESP32-S3, а {chip} — прошивка jRadio для него не подходит.', 'This is not an ESP32-S3 but {chip} - jRadio\'s firmware is not for it.'],
    'fl.downloading': ['Загружаем файлы прошивки…', 'Fetching the firmware files…'],
    'fl.erasing': ['Стираем память и записываем…', 'Erasing and writing…'],
    'fl.writing': ['Записываем…', 'Writing…'],
    'fl.missing_file': ['Нет файла {file}', 'The file {file} is missing'],
    'fl.error': ['Ошибка', 'Error'],
    'fl.no_port': ['Порт не выбран.', 'No port was chosen.'],
    'fl.no_answer': ['Плата не отвечает. Зажмите BOOT, нажмите RESET, отпустите BOOT — и попробуйте ещё раз.',
                     'The board does not answer. Hold BOOT, tap RESET, let go of BOOT - and try again.'],
    'fl.port_busy': ['Порт занят или пропал: закройте другие программы и вкладки, работающие с ним, переподключите кабель и попробуйте снова.',
                     'The port is busy or gone: close other programs and tabs using it, reconnect the cable and try again.'],
    'fl.failed': ['Не получилось: {error}', 'It did not work: {error}'],
    'fl.done_firmware': ['Готово: прошивка {version} записана, плата перезагружена. Если это первая установка — запишите и файловую систему.',
                         'Done: firmware {version} is written and the board restarted. On a first install, write the file system too.'],
    'fl.done_littlefs': ['Готово: файловая система записана. Радио поднимет точку доступа jradio-XXXX для настройки Wi-Fi — или восстановите резервную копию.',
                         'Done: the file system is written. The radio opens a jradio-XXXX access point to set up Wi-Fi - or restore your backup.'],
    'title.hardware': ['jRadio — оборудование', 'jRadio — hardware'],
    'nav.hardware': ['Оборудование', 'Hardware'],
    'hw.lead': ['Что подключено к модулю и на какие выводы. Результат — распайка для страницы «Прошивка» или файл board_options.h для сборки прошивки.',
                'What is wired to the module, and to which pins. The result is the wiring for the Flasher page, or a board_options.h to build the firmware with.'],
    'hw.parts': ['Что подключено', 'What is wired'],
    'hw.module': ['Модуль ESP32-S3-WROOM-1 N16R8', 'The ESP32-S3-WROOM-1 N16R8 module'],
    'hw.svg': ['Схема выводов модуля', 'The module\'s pinout'],
    'hw.report': ['Проверка', 'Checks'],
    'hw.report_ok': ['Замечаний нет.', 'Nothing to report.'],
    'hw.bus_card': ['— выводы в карточке шины', '- its pins are in the bus card'],
    'hw.hint.board_name': ['Как называется эта плата — для себя: «стенд», «плата v2»', 'What this board is called - for yourself: "bench", "board v2"'],
    'hw.hint.idle': ['Нажмите ⌖ у сигнала, затем на вывод — или выберите вывод из списка. Нажатие на занятый вывод берёт его сигнал.',
                     'Press ⌖ beside a signal, then a pin - or pick the pin from the list. Clicking a used pin takes its signal.'],
    'hw.hint.picking': ['Куда идёт «{signal}»? Нажмите на вывод. Esc — отмена.',
                        'Where does "{signal}" go? Click a pin. Esc cancels.'],
    'hw.arm': ['Назначить вывод для «{signal}» на схеме', 'Pick a pin for "{signal}" on the picture'],
    'hw.enable': ['{device}: есть на плате', '{device}: on the board'],
    'hw.copy': ['Скопировать', 'Copy'],
    'hw.download': ['Скачать', 'Download'],
    'hw.copied': ['Скопировано', 'Copied'],
    'hw.copy_failed': ['Не удалось скопировать', 'Could not copy'],
    'hw.downloaded': ['board_options.h скачан — положите его в корень проекта и соберите', 'board_options.h downloaded - put it at the project root and build'],
    'hw.import': ['Вставить свой board_options.h', 'Paste your own board_options.h'],
    'hw.import_placeholder': ['#define TFT_CS_GPIO 10', '#define TFT_CS_GPIO 10'],
    'hw.import_apply': ['Применить', 'Apply'],
    'hw.import_ok': ['Файл разобран, редактор обновлён.', 'The file was read; the editor follows it.'],
    'hw.import_unknown': ['Разобран; незнакомые ключи оставлены как есть: {keys}',
                          'Read; unknown keys kept as they are: {keys}'],
    'hw.import_bad': ['Строка {line} не «ключ,значение»: {text}', 'Line {line} is not "key,value": {text}'],
    'hw.reset': ['Сбросить к плате из README', 'Reset to the README board'],
    'hw.reset_done': ['Плата из README', 'The README board'],

    'hw.dev.board': ['Плата', 'Board'],
    'hw.dev.tft': ['Дисплей', 'Display'],
    'hw.dev.encoder': ['Энкодер', 'Encoder'],
    'hw.dev.buttons': ['Кнопки', 'Buttons'],
    'hw.dev.ir': ['ИК-приёмник', 'IR receiver'],
    'hw.dev.features': ['Функции без железа', 'Features without hardware'],
    'hw.f.yandex_music': ['Яндекс Музыка', 'Yandex Music'], 'hw.f.dlna': ['Медиасервер (DLNA)', 'Media server (DLNA)'],
    'hw.dev.dac': ['ЦАП', 'DAC'],
    'hw.dev.amp': ['Усилитель', 'Amplifier'],
    'hw.dev.power': ['Питание периферии', 'Peripheral power'],
    'hw.dev.usb': ['USB-накопитель', 'USB drive'],
    'hw.dev.sd': ['SD-карта', 'SD card'],
    'hw.dev.bluetooth': ['Модуль Bluetooth', 'Bluetooth module'],
    'hw.dev.spi2': ['Шина SPI2', 'SPI2 bus'],
    'hw.dev.spi3': ['Шина SPI3', 'SPI3 bus'],
    'hw.dev.i2s0': ['Шина I2S', 'I2S bus'],
    'hw.dev.uart1': ['Шина UART1', 'UART1 bus'],

    'hw.f.board_name': ['Название платы', 'Board name'],
    'hw.f.display': ['Дисплей', 'Display'],
    'hw.f.spi2_sclk': ['SCLK', 'SCLK'], 'hw.f.spi2_mosi': ['MOSI', 'MOSI'],
    'hw.f.spi3_sclk': ['SCLK', 'SCLK'], 'hw.f.spi3_mosi': ['MOSI', 'MOSI'], 'hw.f.spi3_miso': ['MISO', 'MISO'],
    'hw.f.i2s0_bclk': ['BCLK', 'BCLK'], 'hw.f.i2s0_lrck': ['LRCK', 'LRCK'],
    'hw.f.i2s0_dout': ['DOUT (к ЦАП)', 'DOUT (to the DAC)'],
    'hw.f.uart1_tx': ['TX', 'TX'], 'hw.f.uart1_rx': ['RX', 'RX'],
    'hw.f.tft_cs': ['CS', 'CS'], 'hw.f.tft_dc': ['DC', 'DC'],
    'hw.f.tft_reset': ['RESET', 'RESET'], 'hw.f.tft_backlight': ['Подсветка', 'Backlight'],
    'hw.f.encoder_right': ['Вправо (A)', 'Right (A)'], 'hw.f.encoder_left': ['Влево (B)', 'Left (B)'],
    'hw.f.encoder_button': ['Нажатие', 'Push'], 'hw.f.encoder_pullups': ['Внутренние подтяжки', 'Internal pull-ups'],
    'hw.f.button_sleep': ['Sleep — сон', 'Sleep'], 'hw.f.button_quick_menu': ['Quick_menu — быстрые настройки', 'Quick_menu - quick panel'],
    'hw.f.button_prev': ['Prev — назад', 'Prev'], 'hw.f.button_next': ['Next — вперёд', 'Next'],
    'hw.f.buttons_pullups': ['Внутренние подтяжки', 'Internal pull-ups'],
    'hw.f.dac': ['Микросхема', 'Chip'], 'hw.f.dac_i2s': ['Шина', 'Bus'], 'hw.f.dac_mute': ['Mute (XSMT)', 'Mute (XSMT)'],
    'hw.f.amp_enable': ['Включение', 'Enable'], 'hw.f.amp_on_level': ['Включён высоким уровнем', 'On is high'],
    'hw.f.peripheral_power': ['Ключ питания', 'Power switch'],
    'hw.f.peripheral_power_on_level': ['Включён высоким уровнем', 'On is high'],
    'hw.f.usb_dp': ['D+', 'D+'], 'hw.f.usb_dm': ['D−', 'D-'],
    'hw.f.sd_spi': ['Шина', 'Bus'], 'hw.f.sd_cs': ['CS', 'CS'],
    'hw.f.bluetooth': ['Модуль', 'Module'], 'hw.f.bt_uart': ['Управление', 'Control'], 'hw.f.bt_i2s': ['Звук', 'Audio'],

    /* The short names the picture writes beside a pin. */
    'hw.s.spi2_sclk': ['SPI2 SCLK', 'SPI2 SCLK'], 'hw.s.spi2_mosi': ['SPI2 MOSI', 'SPI2 MOSI'],
    'hw.s.spi3_sclk': ['SPI3 SCLK', 'SPI3 SCLK'], 'hw.s.spi3_mosi': ['SPI3 MOSI', 'SPI3 MOSI'], 'hw.s.spi3_miso': ['SPI3 MISO', 'SPI3 MISO'],
    'hw.s.i2s0_bclk': ['I2S BCLK', 'I2S BCLK'], 'hw.s.i2s0_lrck': ['I2S LRCK', 'I2S LRCK'],
    'hw.s.i2s0_dout': ['I2S DOUT', 'I2S DOUT'],
    'hw.s.uart1_tx': ['UART1 TX', 'UART1 TX'], 'hw.s.uart1_rx': ['UART1 RX', 'UART1 RX'],
    'hw.s.tft_cs': ['TFT CS', 'TFT CS'], 'hw.s.tft_dc': ['TFT DC', 'TFT DC'], 'hw.s.tft_reset': ['TFT RST', 'TFT RST'],
    'hw.s.tft_backlight': ['Подсветка', 'Backlight'],
    'hw.s.encoder_right': ['Энкодер A', 'Encoder A'], 'hw.s.encoder_left': ['Энкодер B', 'Encoder B'], 'hw.s.encoder_button': ['Энкодер ⏎', 'Encoder ⏎'],
    'hw.f.ir_receiver': ['Выход приёмника', 'Receiver output'], 'hw.s.ir_receiver': ['IR', 'IR'],
    'hw.s.button_sleep': ['Sleep', 'Sleep'], 'hw.s.button_quick_menu': ['Quick_menu', 'Quick_menu'], 'hw.s.button_prev': ['Prev', 'Prev'], 'hw.s.button_next': ['Next', 'Next'],
    'hw.s.dac_mute': ['Mute', 'Mute'], 'hw.s.amp_enable': ['Усилитель', 'Amp'], 'hw.s.peripheral_power': ['Питание', 'Power'],
    'hw.s.usb_dp': ['USB D+', 'USB D+'], 'hw.s.usb_dm': ['USB D−', 'USB D-'],
    'hw.s.sd_cs': ['SD CS', 'SD CS'],

    'hw.opt.none': ['нет', 'none'],
    'hw.opt.pcm5102': ['PCM5102', 'PCM5102'],
    'hw.opt.jradio_bt': ['jradio-bt (ESP32 по UART)', 'jradio-bt (an ESP32 over UART)'],
    'hw.opt.st7796s_480_320': ['ST7796S 480×320', 'ST7796S 480×320'], 'hw.opt.st7796s_320_480': ['ST7796S 320×480 (портрет)', 'ST7796S 320×480 (portrait)'],
    'hw.opt.ili9488_480_320': ['ILI9488 480×320', 'ILI9488 480×320'], 'hw.opt.ili9488_320_480': ['ILI9488 320×480 (портрет)', 'ILI9488 320×480 (portrait)'],
    'hw.opt.ili9341_320_240': ['ILI9341 320×240', 'ILI9341 320×240'], 'hw.opt.ili9341_240_320': ['ILI9341 240×320 (портрет)', 'ILI9341 240×320 (portrait)'],
    'hw.opt.st7789_320_240': ['ST7789 320×240', 'ST7789 320×240'], 'hw.opt.st7789_240_320': ['ST7789 240×320 (портрет)', 'ST7789 240×320 (portrait)'],
    'hw.opt.st7789_320_170': ['ST7789 320×170', 'ST7789 320×170'],
    'hw.opt.rst': ['RST модуля', 'The module\'s RST'], 'hw.opt.0': ['I2S0', 'I2S0'], 'hw.opt.1': ['UART1', 'UART1'], 'hw.opt.2': ['SPI2', 'SPI2'], 'hw.opt.3': ['SPI3', 'SPI3'],

    /* Why a pin is greyed or underlined on the picture. */
    'hw.note.pin_psram': ['Занят octal-PSRAM модуля N16R8', 'Taken by the N16R8 module\'s octal PSRAM'],
    'hw.note.pin_console': ['UART0 — консоль и прошивка', 'UART0 - the console and the flashing port'],
    'hw.note.pin_usb': ['Только USB: здесь сидит его контроллер', 'USB only: the controller is hard-wired here'],
    'hw.note.pin_strapping': ['Strapping-вывод: годится под кнопку с подтяжкой, но уровень при старте важен',
                              'A strapping pin: fine for a button with a pull-up, but its level at boot matters'],
    'hw.note.pin_jtag': ['Вывод JTAG: отладчик станет недоступен', 'A JTAG pin: the debugger becomes unavailable'],

    /* The report. */
    'hw.err.pin_conflict': ['GPIO {gpio} занят дважды: {keys}', 'GPIO {gpio} is taken twice: {keys}'],
    'hw.err.pin_missing': ['«{key}»: вывод обязателен', '"{key}": a pin is required'],
    'hw.err.pin_not_on_header': ['«{key}»: GPIO {gpio} не выведен на модуле', '"{key}": GPIO {gpio} is not on the module\'s header'],
    'hw.err.pin_psram': ['«{key}»: GPIO {gpio} занят octal-PSRAM', '"{key}": GPIO {gpio} is taken by the octal PSRAM'],
    'hw.err.pin_console': ['«{key}»: GPIO {gpio} — это консоль и прошивка (UART0)', '"{key}": GPIO {gpio} is the console and flashing port (UART0)'],
    'hw.err.pin_usb': ['«{key}»: GPIO {gpio} принадлежит USB', '"{key}": GPIO {gpio} belongs to USB'],
    'hw.err.usb_pin_fixed': ['«{key}»: контроллер USB сидит на 19/20, другие выводы невозможны', '"{key}": the USB controller is on 19/20, no other pin will do'],
    'hw.err.bus_unwired': ['{device}: на шине {bus} не задан {pin}', '{device}: the {bus} bus has no {pin}'],
    'hw.err.bad_value': ['«{key}»: недопустимое значение «{value}»', '"{key}": "{value}" is not a valid value'],
    'hw.err.sleep_not_rtc': ['Sleep на GPIO {gpio}: будить чип умеют только выводы 0–21, сон предлагаться не будет',
                             'Sleep on GPIO {gpio}: only pins 0-21 can wake the chip, so sleep will not be offered'],
    'hw.err.ir_not_rtc': ['ИК-приёмник на GPIO {gpio}: будить чип из сна умеют только выводы 0–21, пульт разбудить не сможет',
                          'The IR receiver on GPIO {gpio}: only pins 0-21 can wake the chip from sleep, so the remote will not'],
    'hw.err.spi_not_iomux': ['«{key}» на GPIO {gpio}: не IOMUX-вывод SPI2, 40 МГц на экране может не держаться',
                             '"{key}" on GPIO {gpio}: not one of SPI2\'s IOMUX pins, 40 MHz to the panel may not hold'],
    'player.command_failed': ['Команда не выполнена', 'The command did not go through'],
    'player.command_too_long': ['Команда слишком длинная', 'The command is too long'],
    /* The units, which are words rather than symbols in both languages and so
       cannot be left in the markup. */
    'unit.kbps': ['кбит/с', 'kbps'],
    'unit.hz': ['Гц', 'Hz'],
    'unit.dbm': ['дБм', 'dBm'],

    // The list beside the player, whatever it is listing.
    'list.stations': ['Станции', 'Stations'],
    'list.folders': ['Папки', 'Folders'],
    'list.files': ['Файлы', 'Files'],
    'list.stations_list': ['Список станций', 'Station list'],
    'list.folders_list': ['Список папок', 'Folder list'],
    'list.files_list': ['Список файлов', 'File list'],
    'list.count': ['Количество элементов', 'Item count'],
    'list.search': ['Поиск по списку', 'Search the list'],
    'list.empty': ['Список пока пуст', 'Nothing here yet'],
    'list.offline': ['Нет сети — список недоступен', 'No network — the list is unavailable'],
    'list.loading': ['Загружаем станции…', 'Loading stations…'],
    'list.opening_folder': ['Открываем папку…', 'Opening the folder…'],
    'list.searching_server': ['Ищем медиасервер…', 'Looking for a media server…'],
    'list.folder_empty': ['В этой папке ничего нет', 'This folder is empty'],
    'list.no_server': ['Медиасервер не найден в сети', 'No media server on the network'],
    'list.parent': ['.. (наверх)', '.. (up)'],
    'list.open': ['Открыть', 'Open'],
    'list.playing': ['Играет', 'Playing'],
    'list.unsupported': ['Не поддерживается', 'Not supported'],
    'list.read_failed_dlna': ['Не удалось прочитать медиасервер',
                              'Could not read the media server'],
    'list.read_failed_files': ['Не удалось прочитать флешку', 'Could not read the drive'],
    'list.read_failed_stations': ['Не удалось прочитать список станций',
                                  'Could not read the station list'],
    /* Only a fallback: the device sends the state already worded. */
    'state.stopped': ['Остановлено', 'Stopped'],
    'state.connecting': ['Подключение', 'Connecting'],
    'state.playing': ['Воспроизведение', 'Playing'],
    'state.paused': ['Пауза', 'Paused'],
    'state.reconnecting': ['Переподключение', 'Reconnecting'],
    'state.error': ['Ошибка', 'Error'],
    'state.unknown': ['Неизвестное состояние', 'Unknown state'],
    'state.no_source': ['Нет источника', 'No source'],
    'state.no_network': ['Нет сети', 'No network'],

    // Shared by more than one page.
    'common.loading': ['Загрузка…', 'Loading…'],
    'common.saving': ['Сохранение…', 'Saving…'],
    'common.untitled': ['Без названия', 'Untitled'],

    // The playlist editor.
    'playlist.lead': ['Импорт из файла заменяет список целиком, экспорт сохраняет то, что сейчас на экране.',
                      'Importing replaces the whole list; exporting saves what is on screen.'],
    'playlist.add': ['Добавить', 'Add'],
    'playlist.import': ['Импорт', 'Import'],
    'playlist.export': ['Экспорт', 'Export'],
    'playlist.rows': ['Станции плейлиста', 'Playlist stations'],
    'playlist.empty': ['Список пока пуст — добавьте станцию или импортируйте файл',
                       'Nothing here yet — add a station or import a file'],
    'playlist.save': ['Сохранить на устройстве', 'Save to the device'],
    'playlist.note_order': ['Порядок станций меняется перетаскиванием строки за ручку слева; с клавиатуры — стрелками вверх и вниз на ней. Иконка станции снимается кнопкой «Убрать» в её карточке.',
                            'Reorder by dragging a row by the handle on the left; from the keyboard, the up and down arrows on it. A station\'s icon is removed by the "Remove" button in its card.'],
    'playlist.note_format': ['Формат файла: одна станция на строку, поля разделены табуляцией — имя, URL, флаг «имя из списка» (0 или 1).',
                             'File format: one station per line, fields separated by tabs — name, URL, and the "name from the list" flag (0 or 1).'],
    'playlist.name': ['Название', 'Name'],
    'playlist.station_name': ['Название станции', 'Station name'],
    'playlist.stream_url': ['Адрес потока', 'Stream address'],
    'playlist.name_from_list': ['Показывать название из списка, а не из потока',
                                'Show the name from the list rather than from the stream'],
    'playlist.icon': ['Иконка станции', 'Station icon'],
    'playlist.icon_replace': ['Заменить иконку', 'Replace the icon'],
    'playlist.icon_remove': ['Убрать', 'Remove'],
    'playlist.icon_preparing': ['Готовим иконку…', 'Preparing the icon…'],
    'playlist.icon_ready': ['Иконка готова. Нажмите «Сохранить на устройстве», чтобы отправить её.',
                            'The icon is ready. Press "Save to the device" to send it.'],
    'playlist.icon_will_go': ['Иконка снимется при сохранении',
                              'The icon is removed when the list is saved'],
    'playlist.icon_failed': ['Не удалось подготовить иконку', 'Could not prepare the icon'],
    'playlist.icon_too_large': ['Картинка слишком велика: не укладывается в {kb} КБ даже после сжатия. Возьмите поменьше или попроще.',
                                'The picture is too large: it will not fit in {kb} KB even compressed. Try a smaller or a plainer one.'],
    'playlist.icon_missing': ['картинка не отдалась', 'the picture did not come back'],
    'playlist.reorder': ['Переставить станцию', 'Move the station'],
    'playlist.reorder_hint': ['Перетащите, чтобы переставить; стрелки вверх и вниз — то же с клавиатуры',
                              'Drag to reorder; the up and down arrows do the same from the keyboard'],
    'playlist.edit': ['Редактировать станцию', 'Edit the station'],
    'playlist.delete': ['Удалить станцию', 'Delete the station'],
    'playlist.order_changed': ['Порядок изменён — сохраните список на устройстве',
                               'The order changed — save the list to the device'],
    'playlist.need_name': ['Укажите название станции', 'Give the station a name'],
    'playlist.need_url': ['Укажите адрес потока', 'Give the stream an address'],
    'playlist.need_url_first': ['Сначала укажите адрес потока', 'Give the stream an address first'],
    'playlist.name_has_tab': ['Название не может содержать символ табуляции',
                              'The name cannot contain a tab'],
    'playlist.url_has_tab': ['Адрес не может содержать символ табуляции',
                             'The address cannot contain a tab'],
    'playlist.name_too_long': ['Название слишком длинное', 'The name is too long'],
    'playlist.url_too_long': ['Адрес слишком длинный', 'The address is too long'],
    'playlist.test_browser': ['Тест в браузере', 'Test in the browser'],
    'playlist.test_device': ['Тест на устройстве', 'Test on the device'],
    'playlist.stop': ['Остановить', 'Stop'],
    'playlist.trying_browser': ['Пробуем в браузере…', 'Trying in the browser…'],
    'playlist.playing_browser': ['Играет в браузере', 'Playing in the browser'],
    'playlist.playing_device': ['Играет на устройстве', 'Playing on the device'],
    'playlist.sending_device': ['Отправляем на устройство…', 'Sending to the device…'],
    'playlist.stopping': ['Останавливаем…', 'Stopping…'],
    'playlist.browser_cannot': ['Браузер не смог открыть поток. Формат вроде HLS он не играет.',
                                'The browser could not open the stream. It does not play HLS and the like.'],
    'playlist.device_refused': ['Устройство не приняло запрос', 'The device refused the request'],
    'playlist.loaded': ['Загружено с устройства', 'Loaded from the device'],
    'playlist.read_failed': ['Не удалось прочитать файл', 'Could not read the file'],
    'playlist.reading': ['Читаем файл…', 'Reading the file…'],
    'playlist.packing': ['Собираем архив…', 'Building the archive…'],
    'playlist.unsaved_import': ['Несохранённые изменения будут потеряны. Импортировать файл?',
                                'Unsaved changes will be lost. Import the file?'],
    'playlist.check_and_save': ['Проверьте список и сохраните снова.',
                                'Check the list and save again.'],
    'playlist.some_rejected': ['часть строк отклонена.', 'some rows were rejected.'],
    'playlist.loaded_skipped': ['Загружено, {n} строк пропущено (неверный формат)',
                                'Loaded, {n} rows skipped (malformed)'],
    'playlist.load_failed': ['Не удалось загрузить плейлист: {error}',
                             'Could not load the playlist: {error}'],
    'playlist.limit_reached': ['Достигнут предел в {n} станций', 'The limit of {n} stations is reached'],
    'playlist.exported': ['Экспортировано {n} станций', 'Exported {n} stations'],
    'playlist.exported_icons': ['Экспортировано {n} станций и {icons} картинок',
                                'Exported {n} stations and {icons} pictures'],
    'playlist.export_failed': ['Не удалось собрать архив: картинка не отдалась',
                               'Could not build the archive: a picture did not come back'],
    'playlist.rows_skipped': ['{n} строк пропущено', '{n} rows skipped'],
    'playlist.kept_first': ['оставлены первые {n} станций', 'the first {n} stations were kept'],
    'playlist.icons_missing': ['{n} картинок не нашлось', '{n} pictures were not found'],
    'playlist.imported_notes': ['Импортировано {n} станций ({notes})',
                                'Imported {n} stations ({notes})'],
    'playlist.imported': ['Импортировано {n} станций', 'Imported {n} stations'],
    'playlist.sending_icons': ['Отправляем картинки: {n}…', 'Sending pictures: {n}…'],
    'playlist.partial_save': ['Устройство приняло {n} станций из {expected}: часть строк отклонена. ',
                              'The device took {n} stations of {expected}: some rows were rejected. '],
    'playlist.saved': ['Сохранено: {n} станций', 'Saved: {n} stations'],
    'playlist.save_failed': ['Не удалось сохранить: {error}', 'Could not save: {error}'],

    'common.ready': ['Готово', 'Ready'],
    'common.list_empty': ['Список пуст', 'The list is empty'],
    'common.cancel': ['Отмена', 'Cancel'],
    'common.unknown': ['неизвестно', 'unknown'],
    'common.checking': ['Проверка…', 'Checking…'],
    'common.sending': ['Отправка…', 'Sending…'],
    'common.working': ['Выполняем…', 'Working…'],
    'common.saved': ['Сохранено', 'Saved'],
    'common.show': ['Показать', 'Show'],
    'common.hide': ['Скрыть', 'Hide'],
    'common.no_device': ['Нет связи с устройством', 'No connection to the device'],
    'common.command_failed': ['Команда не выполнена', 'The command did not go through'],
    'common.device_refused': ['Устройство не выполнило команду', 'The device did not run the command'],
    'common.save_failed': ['Не удалось сохранить', 'Could not save'],

    // The settings page.
    'settings.lead': ['То же, что на экране устройства, плюс сеть и аккаунт.',
                      'The same as on the device screen, plus the network and the account.'],
    'settings.device': ['Устройство', 'Device'],
    'settings.group_general': ['Общие', 'General'],
    'settings.group_time': ['Время', 'Time'],
    'settings.group_display': ['Экран', 'Display'],
    'settings.language': ['Язык', 'Language'],
    'settings.home_screen': ['Главный экран', 'Home screen'],
    'settings.home_list': ['Список', 'List'],
    'settings.home_feed': ['Лента', 'Feed'],
    'settings.scroll': ['Скроллинг', 'Scrolling'],
    'settings.scroll_bounce': ['Влево-вправо', 'Back and forth'],
    'settings.scroll_left': ['Влево', 'Leftwards'],
    'settings.buffer': ['Буфер', 'Buffer'],
    'settings.buffer_text': ['Текст', 'Text'],
    'settings.buffer_graph': ['График', 'Graph'],
    'settings.autoplay': ['Автовоспроизведение', 'Autoplay'],
    'settings.files_end': ['Конец папки', 'End of folder'],
    'settings.files_end_stop': ['Стоп', 'Stop'],
    'settings.files_end_repeat': ['Сначала', 'Start over'],
    'settings.yandex': ['Яндекс Музыка', 'Yandex Music'],
    'settings.timezone': ['Часовой пояс', 'Time zone'],
    'settings.ntp': ['Сервер времени', 'Time server'],
    'settings.device_name': ['Имя устройства', 'Device name'],
    'settings.device_name_note_wifi': [
      'Так называется сеть Wi‑Fi устройства при первой настройке. Пусто — имя из заводского номера.',
      'What the device\'s Wi‑Fi network is called during first setup. Empty means the built-in name.'],
    'settings.device_name_note': [
      'Так устройство называется в Bluetooth и как сеть Wi‑Fi при первой настройке. Пусто — имя из заводского номера.',
      'What the device is called over Bluetooth and as the Wi‑Fi network during first setup. Empty means the built-in name.'],
    'settings.group_weather': ['Погода', 'Weather'],
    'bt.group': ['Звук на Bluetooth', 'Sound over Bluetooth'],
    'bt.output': ['Отправлять на колонку', 'Send to a speaker'],
    'bt.chosen': ['Колонка:', 'Speaker:'],
    'bt.none': ['не выбрана', 'none chosen'],
    'bt.connected': ['(подключена)', '(connected)'],
    'bt.disconnected': ['(не подключена)', '(not connected)'],
    'bt.scan': ['Найти колонки', 'Find speakers'],
    'bt.scanning': ['Ищем колонки и наушники… Включите на них режим сопряжения.', 'Looking for speakers and headphones… Put them in pairing mode.'],
    'bt.scanning_button': ['Ищем…', 'Looking…'],
    'bt.scan_empty': ['Ничего нового не найдено. Колонка в режиме сопряжения? Уже подключённая на поиск не отвечает.',
                      'Nothing new found. Is the speaker in pairing mode? One already connected does not answer a search.'],
    'bt.phone': ['(модуль занят телефоном)', '(the module is with the phone)'],
    'bt.playing': ['играет', 'playing'],
    'bt.phone_note': ['Пока играет телефон, колонка и поиск ждут: модуль умеет только одно из двух. Выйдите из источника Bluetooth — и звук снова пойдёт на колонку.',
                      'While the phone plays, the speaker and the search wait: the module does one of the two at a time. Leave the Bluetooth source and the sound goes to the speaker again.'],
    'bt.scan_failed': ['Не удалось запустить поиск', 'Could not start the search'],
    'bt.forget': ['Забыть', 'Forget'],
    'bt.unnamed': ['без имени', 'unnamed'],
    'settings.weather': ['Источник', 'Source'],
    'settings.weather_off': ['Выключено', 'Off'],
    'settings.latitude': ['Широта', 'Latitude'],
    'settings.longitude': ['Долгота', 'Longitude'],
    'settings.owm_key': ['Ключ OpenWeatherMap', 'OpenWeatherMap key'],
    /* The key is never sent back to the page: the field stays empty and its
       placeholder says which of the two states the device is in. One word
       each - the field is narrow on a phone, and how to replace a key is in
       the paragraph under the group. */
    'settings.key_set': ['задан', 'set'],
    'settings.key_unset': ['не задан', 'not set'],
    'settings.weather_now': ['Сейчас', 'Now'],
    'weather.no_key': ['нет ключа', 'no key'],
    'weather.waiting': ['ожидание ответа…', 'waiting for an answer…'],
    'weather.failed': ['сервис не ответил', 'the service did not answer'],
    'weather.failed_status': ['сервис ответил {status}', 'the service answered {status}'],
    'weather.key_refused': ['ключ не принят', 'the key was refused'],
    'weather.clear': ['ясно', 'clear'],
    'weather.partly_cloudy': ['переменная облачность', 'partly cloudy'],
    'weather.cloudy': ['облачно', 'cloudy'],
    'weather.fog': ['туман', 'fog'],
    'weather.rain': ['дождь', 'rain'],
    'weather.snow': ['снег', 'snow'],
    'weather.sleet': ['мокрый снег', 'sleet'],
    'weather.thunderstorm': ['гроза', 'thunderstorm'],
    'settings.brightness': ['Яркость', 'Brightness'],
    'settings.screensaver': ['Заставка', 'Screensaver'],
    'settings.screensaver_off': ['Нет', 'None'],
    'settings.screensaver_dim': ['Затемнение', 'Dim'],
    'settings.screensaver_blank': ['Чёрный экран', 'Blank'],
    'settings.screensaver_clock': ['Часы', 'Clock'],
    'settings.screensaver_after': ['Через, с', 'After, s'],
    'settings.idle_brightness': ['Яркость в покое', 'Idle brightness'],
    'settings.flip_vertical': ['Поворот по вертикали', 'Flip vertically'],
    'settings.flip_horizontal': ['Поворот по горизонтали', 'Flip horizontally'],
    'settings.invert_colors': ['Инверсия цветов', 'Invert colours'],
    'note.invert_colors': ['Если картинка выглядит негативом — у вашего дисплея другое стекло; включите.',
                           'If the picture looks like a negative, your display has the other kind of glass - switch this on.'],
    'settings.saving': ['Сохранение…', 'Saving…'],
    'settings.reorder': ['Меняем порядок…', 'Reordering…'],

    // Wi-Fi.
    'wifi.title': ['Wi-Fi', 'Wi-Fi'],
    'wifi.waiting': ['Ожидание данных', 'Waiting for data'],
    'wifi.current': ['Текущая сеть', 'Current network'],
    'wifi.ip': ['IP-адрес', 'IP address'],
    'wifi.saved': ['Сохранённые сети', 'Saved networks'],
    'wifi.saved_empty': ['Сохранённых сетей пока нет', 'No saved networks yet'],
    'wifi.nearby': ['Сети вокруг', 'Networks nearby'],
    'wifi.add': ['Добавить сеть', 'Add a network'],
    'wifi.rescan': ['Обновить список', 'Refresh the list'],
    'wifi.chosen': ['Сеть:', 'Network:'],
    'wifi.ssid': ['Название сети', 'Network name'],
    'wifi.password': ['Пароль', 'Password'],
    'wifi.reveal': ['Показать', 'Show'],
    'wifi.submit': ['Проверить и сохранить', 'Check and save'],
    'wifi.connected': ['Подключено', 'Connected'],
    'wifi.connecting': ['Подключение…', 'Connecting…'],
    'wifi.no_link': ['Нет связи', 'No connection'],
    'wifi.setup_ready': ['Готово к настройке', 'Ready to be set up'],
    'wifi.first': ['первая', 'first'],
    'wifi.disabled_until_reboot': ['выключена до перезагрузки', 'off until the next restart'],
    'wifi.make_first': ['Сделать первой', 'Make it first'],
    'wifi.forget': ['Забыть', 'Forget'],
    'wifi.disconnect': ['Отключиться', 'Disconnect'],
    'wifi.open_network': ['без пароля', 'open'],
    'wifi.other_network': ['Другая сеть…', 'Another network…'],
    'wifi.scanning': ['Ищем сети…', 'Looking for networks…'],
    'wifi.scan_failed': ['Не удалось найти сети', 'No networks found'],
    'wifi.scan_start_failed': ['Не удалось запустить поиск', 'Could not start the search'],
    'wifi.need_ssid': ['Введите название сети', 'Enter the network name'],
    'wifi.busy': ['Идёт подключение — подождите', 'Connecting — please wait'],
    'wifi.bad_password': ['Неверный пароль или ошибка авторизации',
                          'Wrong password, or authorisation failed'],
    'wifi.full': ['Сохранено максимум 5 сетей — сначала забудьте лишнюю',
                  'Five networks are the most it holds — forget one first'],
    'wifi.bad_name': ['Некорректное название сети или пароль',
                      'The network name or the password is not valid'],
    'wifi.write_failed': ['Не удалось записать настройки в память',
                          'Could not write the settings to flash'],
    'wifi.ap_not_found': ['Точка доступа не найдена', 'The access point was not found'],
    'wifi.connect_failed': ['Не удалось подключиться (код {code})',
                            'Could not connect (code {code})'],
    'wifi.forget_confirm': ['Забыть сеть «{ssid}»? Пароль придётся вводить заново.',
                            'Forget "{ssid}"? The password will have to be typed again.'],
    'wifi.forget_active_confirm': ['Забыть сеть «{ssid}»? Устройство отключится от неё, ',
                                   'Forget "{ssid}"? The device will disconnect from it, '],
    'wifi.disconnect_confirm': ['Отключиться от сети «{ssid}»? Устройство не вернётся к ней до перезагрузки, ',
                                'Disconnect from "{ssid}"? It will not return until a restart, '],
    'wifi.page_stops': ['и эта страница перестанет отвечать.', 'and this page will stop answering.'],

    // Yandex Music.
    'yandex.title': ['ЯМузыка', 'Ya.Music'],
    'yandex.checking': ['Проверка…', 'Checking…'],
    'yandex.code_lead': ['Откройте страницу и введите код:', 'Open the page and enter the code:'],
    'yandex.link': ['Привязать аккаунт', 'Link an account'],
    'yandex.cancel': ['Отменить', 'Cancel'],
    'yandex.refresh': ['Обновить станции', 'Refresh the stations'],
    'yandex.forget': ['Отвязать', 'Unlink'],
    'yandex.not_linked': ['Аккаунт не привязан', 'Account not linked'],
    'yandex.linked': ['Аккаунт привязан', 'Account linked'],
    'yandex.requesting': ['Запрашиваем код…', 'Requesting a code…'],
    'yandex.waiting': ['Ожидание подтверждения', 'Waiting for confirmation'],
    'yandex.seconds_left': ['Код действителен ещё {n} с', 'The code is valid for another {n} s'],
    'yandex.no_connection': ['Не удалось связаться с Яндексом', 'Could not reach Yandex'],
    'yandex.expired': ['Код истёк, попробуйте ещё раз', 'The code expired, try again'],
    'yandex.denied': ['Вход не подтверждён', 'Sign-in was not confirmed'],
    'yandex.server_error': ['Неожиданный ответ сервера', 'Unexpected answer from the server'],
    'yandex.storage_failed': ['Не удалось сохранить токен на устройстве',
                              'Could not store the token on the device'],
    'yandex.link_failed': ['Не удалось привязать аккаунт', 'Could not link the account'],
    'yandex.loading': ['Загрузка станций…', 'Loading stations…'],
    'yandex.list_failed': ['Не удалось получить станции', 'Could not fetch the stations'],

    // About and backup.
    'about.title': ['Об устройстве', 'About'],
    'about.firmware': ['Прошивка', 'Firmware'],
    'about.built': ['Собрана', 'Built'],
    'about.web': ['Веб-интерфейс', 'Web UI'],
    'about.module': ['Модуль Bluetooth', 'Bluetooth module'],
    'about.mismatch': ['Версии прошивки и веб-интерфейса не совпадают',
                       'The firmware and the web interface are from different builds'],
    'backup.title': ['Резервная копия', 'Backup'],
    'backup.download': ['Скачать архив', 'Download the archive'],
    'backup.choose': ['Выбрать файл', 'Choose a file'],
    'backup.restore': ['Восстановить', 'Restore'],
    'backup.chosen': ['Выбран файл: {name}', 'Chosen: {name}'],
    'backup.confirm': ['Восстановить настройки из «{name}»? ', 'Restore the settings from "{name}"? '],
    'backup.confirm_tail': ['Текущие настройки будут заменены, устройство перезагрузится.',
                            'The current settings are replaced and the device restarts.'],
    'backup.restored': ['Восстановлено: {files}. Устройство перезагружается…',
                        'Restored: {files}. The device is restarting…'],
    'backup.restored_warn': ['Восстановлено: {files}. ', 'Restored: {files}. '],
    'backup.unreadable': ['Устройство не смогло прочитать: {files}. ',
                          'The device could not read: {files}. '],
    'backup.rebooting': ['Перезагрузка…', 'Restarting…'],
    'backup.send_failed': ['Не удалось отправить файл', 'Could not send the file'],
    'backup.refused': ['Устройство не приняло файл', 'The device did not accept the file'],
    'backup.err_size': ['Файл слишком большой', 'The file is too large'],
    'backup.err_incomplete': ['Файл дошёл не целиком — попробуйте ещё раз',
                              'The file arrived incomplete — try again'],
    'backup.err_memory': ['Устройству не хватило памяти', 'The device ran out of memory'],
    'backup.err_compressed': ['Архив упакован способом, который устройство не понимает. ',
                              'The archive uses packing the device does not understand. '],
    'backup.err_compressed_tail': ['Загрузите архив, скачанный с устройства, или один файл из него',
                                   'Upload an archive downloaded from the device, or one file out of it'],
    'backup.err_damaged': ['Архив повреждён', 'The archive is damaged'],
    'backup.err_malformed': ['Это не архив с настройками', 'This is not a settings archive'],
    'backup.err_empty': ['В архиве нет файлов устройства', 'The archive holds no device files'],
    'backup.err_unknown_file': ['Ожидается архив или один из файлов: ',
                                'An archive is expected, or one of: '],
    'backup.err_contents': ['Содержимое файла не похоже на то, чем он назван',
                            'The file does not hold what its name says'],
    'backup.err_write': ['Не удалось записать файл на устройство',
                         'Could not write the file to the device'],
    /* The explanatory paragraphs under each card. Long, and worth translating
       whole rather than in pieces: they are the only place the page explains
       why a setting behaves the way it does. */
    'note.device': ['Записывается в settings.csv устройства и применяется сразу — экран и кнопки на корпусе показывают то же самое.',
                    'Written to the device\'s settings.csv and applied at once — the screen and the buttons on the case show the same.'],
    'note.backup': ['В архив попадает всё, что устройство знает о себе: сети Wi-Fi, настройки, токен ЯМузыки, ключ погоды и обученные кнопки пульта. Плейлист сохраняется отдельно, на странице плейлиста.',
                    'The archive holds everything the device knows about itself: Wi-Fi networks, settings, the Ya.Music token, the weather key and the keys the remote was taught. The playlist is saved separately, on the playlist page.'],
    'note.restore': ['Восстановить можно весь архив целиком или один файл из него — wifi.json, settings.csv, yandex.json, weather.json или remote.csv. После восстановления устройство перезагрузится.',
                     'Restore the whole archive or a single file out of it — wifi.json, settings.csv, yandex.json, weather.json or remote.csv. The device restarts afterwards.'],
    'note.backup_secret': ['Пароли Wi-Fi, токен ЯМузыки и ключ погоды лежат в архиве открытым текстом: храните его так же, как хранили бы пароль.',
                           'The Wi-Fi passwords, the Ya.Music token and the weather key are in the archive as plain text: keep it the way you would keep a password.'],
    'note.wifi': ['Пароль отправляется устройству один раз и сразу удаляется из поля. Сети вокруг устройство ищет только пока не подключено ни к одной из них.',
                  'The password is sent once and cleared from the field immediately. The device only looks for networks while it is connected to none.'],
    'note.yandex': ['Пароль от Яндекса устройству не нужен: код подтверждается на сайте Яндекса, сюда возвращается только токен доступа.',
                    'The device never needs the Yandex password: the code is confirmed on Yandex\'s own site and only an access token comes back.'],
    'note.versions': ['Прошивка и веб-интерфейс пишутся разными командами, поэтому у них свои версии. Если они разошлись, перепрошейте раздел данных.',
                      'The firmware and the web interface are flashed by different commands, so each has its own version. If they differ, reflash the data partition.'],
  };

  const LANGUAGES = ['ru', 'en'];
  const STORAGE_KEY = 'jradio.language';

  /* The device owns the setting, and it only arrives once the socket has said
     so - a second or two into the page's life. Remembering the last one here
     is what keeps a reload from showing the wrong language for that second and
     then swapping it under the reader. Storage that refuses to answer - a
     private window, a browser with site data switched off - is not a fault
     worth reporting: the page simply starts in Russian, which is where it
     started before this existed. */
  function remembered() {
    try {
      const stored = window.localStorage.getItem(STORAGE_KEY);
      return LANGUAGES.includes(stored) ? stored : 'ru';
    } catch (error) {
      return 'ru';
    }
  }

  let current = remembered();
  const listeners = [];

  /* The Russian half is the fallback rather than the key: a key that reached
     the screen would be a bug shown to the user, and the Russian text is what
     the markup already holds.

     `values` fills `{name}` placeholders, which is how a sentence keeps its
     numbers where its own grammar wants them rather than where Russian
     happens to put them. Counted nouns are left in the genitive plural
     throughout - "5 станций", "1 станций" - which is what this page has always
     done; proper agreement would need a rule per language and is not worth it
     for a line that is read once after a save. */
  function t(key, values) {
    const entry = DICT[key];
    const text = entry === undefined ? key : (current === 'en' ? entry[1] : entry[0]);
    if (values === undefined) return text;
    return text.replace(/\{(\w+)\}/g, (whole, name) =>
      (Object.hasOwn(values, name) ? String(values[name]) : whole));
  }

  /* Elements say which key they carry: `data-i18n` for their text, and one
     attribute each for the three places a string can hide - the label a screen
     reader reads, the hint inside an empty field, and a tooltip. */
  const ATTRIBUTES = [
    ['data-i18n-aria', 'aria-label'],
    ['data-i18n-placeholder', 'placeholder'],
    ['data-i18n-title', 'title'],
  ];

  function apply(root) {
    const scope = root || document;
    if (typeof scope.querySelectorAll !== 'function') return;
    for (const node of scope.querySelectorAll('[data-i18n]')) {
      node.textContent = t(node.getAttribute('data-i18n'));
    }
    for (const [marker, attribute] of ATTRIBUTES) {
      for (const node of scope.querySelectorAll(`[${marker}]`)) {
        node.setAttribute(attribute, t(node.getAttribute(marker)));
      }
    }
  }

  function setLanguage(language) {
    const next = LANGUAGES.includes(language) ? language : 'ru';
    if (next === current) return false;
    current = next;
    try {
      window.localStorage.setItem(STORAGE_KEY, current);
    } catch (error) {
      // See remembered(): nothing here is worth a message.
    }
    /* The document's own language goes with it: it is what a screen reader
       picks a voice from, and what a browser offers to translate. */
    paint();
    for (const listener of listeners) listener(current);
    return true;
  }

  window.jradioI18n = {
    t,
    apply,
    setLanguage,
    language: () => current,
    /* For the parts a page draws itself - a row built in JavaScript is not in
       the markup for `apply` to find, so it is rebuilt when the language
       changes. */
    onChange: (listener) => { listeners.push(listener); },
    /* Test seam and nothing else: the pages never add to the dictionary. */
    define: (entries) => { Object.assign(DICT, entries); },
  };

  /* The first pass has to wait for the markup to exist.
   *
   * This script is loaded from <head>, so it runs while the document is still
   * being parsed and <body> is not there yet: walking it here finds nothing at
   * all. It went unnoticed because the language always *changed* a moment
   * later - the page started in Russian and the device said otherwise - and
   * that change did the walk. The moment the choice was remembered between
   * loads there was no change to ride on, and every page came up in the
   * language its markup was written in. */
  function paint() {
    if (document.documentElement) document.documentElement.lang = current;
    apply(document);
    /* And the tab's name, which is not an element `apply` can walk to. Its
       own marker: as `data-i18n-title` it was also a tooltip on <body>, and
       the page's name followed the cursor everywhere. */
    if (document.body && typeof document.body.getAttribute === 'function') {
      const key = document.body.getAttribute('data-i18n-page');
      if (key) document.title = t(key);
    }
  }

  if (document.readyState === 'loading' &&
      typeof document.addEventListener === 'function') {
    document.addEventListener('DOMContentLoaded', paint);
  } else {
    paint();
  }
})();
