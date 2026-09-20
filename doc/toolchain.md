# Установка среды разработки

[← README](../README.md) · [English](toolchain.en.md)

Проект собирается ESP-IDF 5.5.x под цель `esp32s3`. Самый простой способ —
VS Code с расширением ESP-IDF: оно само скачает фреймворк, компилятор и создаст
окружение Python. Установка из терминала без VS Code описана в конце.

Если среда уже стоит — переходите к [«Сборке и прошивке»](build.md).

## Что понадобится

- Linux, macOS или Windows 10/11, 64 бита.
- Около 5 ГБ на диске: ESP-IDF, тулчейн и каталог сборки.
- Интернет на время установки и первой сборки.
- USB-кабель **с проводами данных** — кабель «только зарядка» выглядит так же,
  но порта в системе не даёт.
- Python 3.9 или новее, **установленный до расширения**: расширение не приносит
  интерпретатор с собой, а без него установка обрывается на середине с
  ошибками, в которых про Python ни слова.

## 1. Системные пакеты

### Linux (Debian, Ubuntu)

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv \
                    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
                    libusb-1.0-0
```

`python3-venv` обязателен: и ESP-IDF, и компонент littlefs создают себе
отдельные окружения Python.

Чтобы работать с портом без `sudo`, добавьте себя в группу и **перелогиньтесь**:

```bash
sudo usermod -aG dialout $USER
```

На Arch и Fedora группа может называться иначе — посмотрите владельца порта:
`ls -l /dev/ttyUSB0`.

### macOS

```bash
xcode-select --install
brew install cmake ninja dfu-util python
```

### Windows

1. Поставьте Python с [python.org](https://www.python.org/downloads/windows/)
   и при установке отметьте **«Add python.exe to PATH»**.
2. Проверьте в консоли: `python --version`. Если вместо версии открывается
   Microsoft Store, выключите псевдонимы в «Параметры → Приложения →
   Дополнительные параметры → Псевдонимы выполнения приложения» — обе строки с
   `python`.
3. Git и компилятор ставить не нужно — их приносит установщик ESP-IDF. Если
   система не показывает COM-порт платы, поставьте драйвер её моста USB-UART
   (CP210x, CH34x или FTDI).

Под Windows работает всё, кроме хостовых тестов: им нужны POSIX-shell и gcc,
то есть Linux, macOS или WSL.

## 2. VS Code и расширение ESP-IDF

1. Поставьте [VS Code](https://code.visualstudio.com/) и откройте в нём папку
   проекта. VS Code предложит рекомендованные расширения — ESP-IDF и C/C++;
   согласитесь.
2. Установите ESP-IDF через расширение: `Ctrl+Shift+P` → **ESP-IDF: Open
   ESP-IDF Installation Manager**. Откроется установщик; выберите версию
   **v5.5.5**. Путь установки оставьте по умолчанию (`~/esp/` или
   `%USERPROFILE%\esp\`) — главное, **без пробелов и кириллицы**, на них
   спотыкается часть инструментов.
3. Дождитесь конца установки (около 2 ГБ), затем `Ctrl+Shift+P` →
   **ESP-IDF: Select Current ESP-IDF Version** и выберите установленную.
   Проверка — **ESP-IDF: Doctor Command**.

Сообщение «File …/build/project_description.json cannot be found» при первом
открытии — не ошибка: файл появится после первой сборки.

## 3. Первая сборка и прошивка

- `Ctrl+Shift+B` — сборка.
- **Terminal → Run Task** — остальное: `ESP-IDF: Flash`, `ESP-IDF: Monitor`,
  `ESP-IDF: Build, Flash & Monitor`, `Host tests`.
- **На новую плату — `ESP-IDF: First flash (app + data)`**: пишет и прошивку,
  и раздел данных с веб-интерфейсом и списком станций. Обычная прошивка
  данные не трогает, а на новой плате их ещё нет.

Цель `esp32s3` уже задана в проекте, `set-target` не нужен. Задачи вызывают
[`tools/idf.sh`](../tools/idf.sh) (под Windows — [`tools/idf.ps1`](../tools/idf.ps1)):
скрипт сам находит установленный ESP-IDF, в том числе поставленный
установщиком, и активирует его. Порт определяется автоматически, если плата
одна; при нескольких укажите её переменной `ESPPORT`.

Скрипт ищет именно 5.5.5, а не «любую 5.5», чтобы задачи VS Code и терминал
собирали одной версией. Собрать другой намеренно — переменная `JRADIO_IDF` с
путём к ней.

Что делать дальше — в [«Сборке и прошивке»](build.md).

## 4. Без VS Code, из терминала

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git v5.5.5/esp-idf
cd v5.5.5/esp-idf && ./install.sh esp32s3
```

В каждой новой сессии терминала:

```bash
source ~/esp/v5.5.5/esp-idf/export.sh
cd путь/к/jradio
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

`export.sh` действует только в текущей сессии — так и задумано. Не добавляйте
его в `.bashrc`: он ставит свой Python впереди системного.

### Windows: «idf.py не распознано как имя командлета»

Это не поломка. `idf.py` работает только вместе со своим окружением, а его
добавляет в `PATH` скрипт активации — и только в текущем окне терминала.
Варианты, от простого к сложному:

1. **Собирать задачами VS Code** (`Terminal → Run Task → ESP-IDF: Build`) —
   `tools\idf.ps1` находит и активирует среду сам.
2. **Позвать тот же скрипт руками** из любого терминала в папке проекта:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\idf.ps1 build
   ```

3. **Активировать среду в этой сессии**, если нужен именно `idf.py`:

   ```powershell
   . C:\Espressif\frameworks\esp-idf-v5.5.5\export.ps1
   idf.py build
   ```

   Точка и пробел в начале обязательны. Если PowerShell отказывается
   выполнять скрипт — сначала
   `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`. У установки
   через Installation Manager активацию делает
   `Microsoft.PowerShell_profile.ps1` рядом с папкой `esp-idf`.
4. **Открыть терминал расширения**: `Ctrl+Shift+P` → `ESP-IDF: Open ESP-IDF
   Terminal` — там среда уже активирована.

Переменные `IDF_PATH` и `IDF_TOOLS_PATH` прописывать навсегда не нужно: они
говорят, *где* среда, но в `PATH` её не добавляют, а при обновлении версии
начинают указывать на старую.

## 5. Порт устройства

- **Linux:** `/dev/ttyUSB*` или `/dev/ttyACM*` — смотря какой мост на плате.
  Надёжный способ: `ls /dev/tty{ACM,USB}*` до подключения и после.
- **macOS:** `/dev/cu.usbserial-*` или `/dev/cu.usbmodem*`.
- **Windows:** `COM*`, номер — в диспетчере устройств.

Нужен **UART-порт** платы, а не её USB-разъём: тот отдан флешке. Порт занимает
тот, кто открыл его первым — если прошивка не начинается, закройте монитор.

## 6. Если что-то пошло не так

| Сообщение | Причина | Что делать |
|---|---|---|
| Установщик падает на середине (Windows) | нет системного Python или в PATH заглушка Store | поставить Python с python.org с «Add to PATH», выключить псевдонимы Store, запустить заново |
| `idf.py: command not found`, «Имя idf.py не распознано» | среда не активирована в этом терминале | `source …/export.sh`, под Windows `. …\export.ps1`, или задачи VS Code |
| `Permission denied: '/dev/ttyUSB0'` | пользователь не в группе порта | `usermod -aG dialout` и перелогиниться |
| `Failed to connect to ESP32-S3` | кабель без данных, порт занят, плата не в режиме загрузки | сменить кабель; закрыть монитор; зажать BOOT, нажать RESET, отпустить BOOT |
| `Could not find a version that satisfies the requirement littlefs-python` | моргнула сеть на первой сборке | повторить `idf.py build` |
| Экран пустой, устройство работает | залито только приложение | `ESP-IDF: First flash (app + data)` |
| `ESP-IDF Python virtual environment … not found` в задаче VS Code | установка через Installation Manager, окружение лежит не там, где ищет `export.ps1` | запустить задачу из «ESP-IDF Terminal» |

## 7. Что нужно не всем

- **Node.js** — для тестов браузерного JavaScript и для
  [`tools/gen_ui_fonts.sh`](../tools/gen_ui_fonts.sh) (генерация шрифтов через
  `npx lv_font_conv`). Для сборки не нужен: готовые шрифты лежат в репозитории.
- **Python-пакеты руками ставить не нужно**: и ESP-IDF, и littlefs работают в
  своих окружениях.
