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
    'settings.yandex': ['Яндекс Музыка', 'Yandex Music'],
    'settings.timezone': ['Часовой пояс', 'Time zone'],
    'settings.ntp': ['Сервер времени', 'Time server'],
    'settings.brightness': ['Яркость', 'Brightness'],
    'settings.flip_vertical': ['Поворот по вертикали', 'Flip vertically'],
    'settings.flip_horizontal': ['Поворот по горизонтали', 'Flip horizontally'],
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
    'note.time': ['Часов у устройства нет: время оно спрашивает у сервера, когда появляется сеть. Поле сервера можно очистить — вернётся pool.ntp.org.',
                  'The device has no clock of its own: it asks a server once there is a network. Clearing the server field puts pool.ntp.org back.'],
    'note.backup': ['В архив попадает всё, что устройство знает о себе: сети Wi-Fi, настройки и токен ЯМузыки. Плейлист сохраняется отдельно, на странице плейлиста.',
                    'The archive holds everything the device knows about itself: Wi-Fi networks, settings and the Ya.Music token. The playlist is saved separately, on the playlist page.'],
    'note.restore': ['Восстановить можно весь архив целиком или один файл из него — wifi.json, settings.csv или yandex.json. После восстановления устройство перезагрузится.',
                     'Restore the whole archive or a single file out of it — wifi.json, settings.csv or yandex.json. The device restarts afterwards.'],
    'note.backup_secret': ['Пароли Wi-Fi и токен ЯМузыки лежат в архиве открытым текстом: храните его так же, как хранили бы пароль.',
                           'The Wi-Fi passwords and the Ya.Music token are in the archive in clear text: keep it the way you would keep a password.'],
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
    /* And the tab's name, which is not an element `apply` can walk to. */
    if (document.body && typeof document.body.getAttribute === 'function') {
      const key = document.body.getAttribute('data-i18n-title');
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
