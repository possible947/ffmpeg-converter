# Подробное описание проекта: ffmpeg_converter

Версия: рабочая копия в репозитории

Цель документа: дать разработчику полное представление о структуре репозитория, архитектуре кода, ключевых модулях, опциях командной строки и особенностях реализации.

--------------------------------------------------------------------------------

## Краткий обзор

`ffmpeg_converter` — кроссплатформенный CLI‑инструмент для формирования и выполнения команд `ffmpeg` с набором преднастроек: поддержка ProRes (`prores`, `prores_ks`), `copy` и `h265_mi50` (H.265 VAAPI), нормализация аудио (peak, loudness), двухпроходный анализ (peak 2‑pass, loudnorm 2‑pass), прогресс‑бар и интерактивное текстовое меню.

Также есть GUI для Linux на GTK4 с выбором параметров, списком файлов и прогрессом обработки.

Проект организован модульно: заголовки (.h) хранятся в отдельных модулях, реализации платформенных частей — в `src/platform/<os>/`.

--------------------------------------------------------------------------------

## Структура папок (корневой `src/`)

- `src/converter/`
  - `converter.h` — публичный API для модуля конвертера (типы `ConvertOptions`, `ConverterCallbacks`, `Converter` и сигнатуры функций).
  - `converter.c` — реализация: проверка файлов, двухпроходный анализ (peak/loudnorm), сборка команды `ffmpeg`, запуск `ffmpeg` с парсингом прогресса.

- `src/cli/`
  - `linux/` — `main.c` — реализация CLI для Linux (парсинг аргументов, интерактивное меню, связывание колбэков с `progress`).
  - `macos/`, `windows/` — заготовки CLI для других платформ.

- `src/gui/`
  - `gui_main.c`, `gui_window.c`, `gui_callbacks.c` — GTK4 GUI (Linux), взаимодействие с `converter` через коллбэки, выбор параметров и файлов.

- `src/platform/`
  - `linux/progress.c`, `macos/progress.c`, `windows/progress.c` — платформенные реализации прогресс‑индикатора.
  - `dummy.c` — вспомогательный файл, подключаемый в сборке платформы.

- `src/progress/` — `progress.h` (интерфейс прогресс‑барa).
- `src/ffmpeg_cmd/` — заголовки (интерфейсы) для формирования команд ffmpeg (модульная точка расширения).
- `src/core/`, `src/utils/`, `src/audio/`, `src/video/` — модули, объявленные как INTERFACE в CMake (include dirs). Могут содержать заголовки и вспомогательные CMakeLists.

В корне репозитория:
- `CMakeLists.txt` — корневая CMake конфигурация (подключает `src/`).
- `src/CMakeLists.txt` — модульная CMake конфигурация (INTERFACE библиотеки, подкаталоги `converter`, `platform`, `cli`).

--------------------------------------------------------------------------------

## Сборка и зависимости

- Требования: `cmake` (>=3.16), компилятор (gcc/clang), `ffmpeg` в PATH (для запуска и анализа), библиотека `jansson` (для парсинга JSON, используемого loudnorm).
- Для GUI на Linux требуется `gtk4`.
- Быстрая сборка для Linux:

```bash
mkdir build
cd build
cmake ..
cmake --build . --target linux_cli
```

- Сборка GUI (Linux):

```bash
cmake --build . --target linux_gui
```

- На macOS используется таргет `macos_cli` и дополнительно пути MacPorts (`/opt/local/include`, `/opt/local/lib`) прописаны в `src/cli/CMakeLists.txt`.

--------------------------------------------------------------------------------

## Основные файлы и их роль

- [src/converter/converter.h](src/converter/converter.h#L1) — определяет `ConvertOptions`, `ConverterCallbacks`, `ConverterError` и API:
  - `converter_create()`, `converter_destroy()`
  - `converter_set_callbacks()`
  - `converter_set_options()`
  - `converter_process_files()`
  - `converter_stop()`
  - `converter_error_string()`

- [src/converter/converter.c](src/converter/converter.c#L1) — содержит реализацию:
  - Проверку входного файла (`stat`, `access`).
  - Генерацию имени выходного файла (`make_output_name`).
  - Анализ аудио: `peak_two_pass()` (использует `ffmpeg -af volumedetect`), `loudnorm_two_pass()` (использует `loudnorm` с `print_format=json` + `jansson` парсинг).
  - Построение команды `ffmpeg` в `build_ffmpeg_cmd()` (включая видео‑кодек, фильтры deblock, аудиофильтры, loudnorm параметры при 2‑pass).
  - Запуск `ffmpeg` с `-progress pipe:1` и парсингом ключевых полей (`out_time_ms`, `fps`, `progress=end`) в `run_ffmpeg_encode_with_progress()`.
  - Основной цикл `converter_process_files()` обрабатывает очередь файлов, вызывает стадии анализа/кодирования и отправляет события через коллбэки.

- [src/cli/linux/main.c](src/cli/linux/main.c#L1) — реализация CLI:
  - Парсинг опций командной строки (также предоставляет интерактивное меню в `run_menu`).
  - Формирование `ConvertOptions` по аргументам/меню.
  - Установка `ConverterCallbacks` (связь с `progress_update`, `progress_end`, сообщения в stdout).

- [src/progress/progress.h](src/progress/progress.h#L1) — интерфейс прогресс‑барa (start/update/end).

- `src/platform/CMakeLists.txt` — выбирает одну из реализаций `src/platform/<os>/progress.c` и собирает статическую библиотеку `platform`.

--------------------------------------------------------------------------------

## Формат и опции командной строки

CLI поддерживает (см. `src/cli/linux/main.c`):

- `-c, --codec <copy|prores|prores_ks|h265_mi50>` — видео кодек. `copy` означает копирование видеопотока; `prores` и `prores_ks` используют соответствующие кодеки; `h265_mi50` использует VAAPI (`hevc_vaapi`).
- `-p, --profile <lt|standard|hq|4444>` — профиль ProRes (1..4 в `ConvertOptions.profile`).
- `-d, --deblock <none|weak|strong>` — включение фильтра де-блокирования (1..3).
- `-a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>` — режим нормализации аудио.
  - `peak_norm_2pass` и `loudness_norm_2pass` запускают этап анализа (2‑pass) перед финальной командой.
- `-g, --genre <edm|rock|hiphop|classical|podcast>` — применяется при `loudnorm2` для выбора целевых параметров (I, TP, LRA).
- `--overwrite` — принудительная перезапись выходного файла.
- `-o, --output <directory>` — указать директорию для выходных файлов.
- Дополнительно в README есть `--dry-run` (debug) для вывода команды без выполнения (см. `src/README.md`).

Параметры в `ConvertOptions` включают внутренние поля для двухпроходной обработки: `gain`, `I_target`, `TP_target`, `LRA_target`, `measured_*`, `measured_thresh`, `measured_offset`.

--------------------------------------------------------------------------------

## Callback API (сообщение/прогресс/ошибки)

`ConverterCallbacks` (см. `converter.h`) предоставляет коллбэки:

- `on_file_begin(const char* filename, int index, int total)`
- `on_file_end(const char* filename, ConverterError status)`
- `on_stage(const char* stage_name)` — текущая стадия (`peak analysis`, `loudnorm analysis`, `encoding`)
- `on_progress_encode(float percent, float fps, float eta_seconds)`
- `on_progress_analysis(float percent, float eta_seconds)`
- `on_message(const char* text)`
- `on_error(const char* text, ConverterError code)`
- `on_complete(void)`

Это позволяет легко интегрировать `converter` в GUI или другую обёртку, подписавшись на события.

--------------------------------------------------------------------------------

## Коды ошибок

Перечислены в `converter.h` как `ConverterError`. Основные значения:

- `ERR_OK` — успех
- `ERR_INPUT_NOT_FOUND`, `ERR_INPUT_NOT_REGULAR`, `ERR_INPUT_NOT_READABLE` — проблемы с входным файлом
- `ERR_OUTPUT_EXISTS`, `ERR_SKIP_FILE` — поведение при существующем файле
- `ERR_PEAK_ANALYSIS_FAILED`, `ERR_LOUDNORM_ANALYSIS_FAILED` — неудача анализа
- `ERR_FFMPEG_FAILED`, `ERR_FFPROBE_FAILED` — ошибки внешних инструментов
- `ERR_POPEN_FAILED`, `ERR_PCLOSE_FAILED` — ошибки при запуске/закрытии процессов
- `ERR_INVALID_OPTIONS` — неверные опции

Функция `converter_error_string()` возвращает текстовое представление кода ошибки.

--------------------------------------------------------------------------------

## Особенности реализации и важные замечания

- Анализ loudnorm парсится из JSON вывода `ffmpeg` (включая `print_format=json`) с помощью `jansson`.
- Прогресс кодирования извлекается из `ffmpeg -progress pipe:1` (строки `out_time_ms`, `fps`, `progress`).
- Формирование имени выходного файла делается функцией `make_output_name()` с учётом `output_dir` и безопасных ограничений длин строк.
- Модульная CMake структура использует `INTERFACE` библиотеки для заголовков: это позволяет подключать include dirs без добавления лишних объектных модулей.
- В `src/cli/CMakeLists.txt` встречаются переменные `${PLATFORM_LINUX_SRC}` и `${PLATFORM_MACOS_SRC}` — в текущей структуре они не определены явно, но сборка проходит за счёт явного добавления `PLATFORM` библиотеки. Их можно убрать/почистить при рефакторинге.

- Для `h265_mi50` используется VAAPI: команда добавляет `-vaapi_device`. По умолчанию устройство `/dev/dri/renderD128`, можно переопределить через переменную окружения `VAAPI_DEVICE`.
- Для VAAPI добавляется `-vf "format=nv12,hwupload"`, чтобы загрузить кадры в устройство; deblock отключен.

- Исправление (обновлено): таргет `macos_cli` теперь линкуется с найденной библиотекой `jansson` через переменную `${JANSSON_LIB}` (раньше в CMake использовалось буквальное имя `jansson`). Это делает поведение согласованным с Linux/Windows и позволяет CMake корректно использовать найденную библиотеку (включая MacPorts/пользовательские пути).

--------------------------------------------------------------------------------

## Частые задачи для разработчика

- Добавить тесты для `make_output_name()` и парсинга `-progress`/`loudnorm` вывода.
- Вынести вызовы `popen()` в обёртку, чтобы легко мокировать `ffmpeg` при тестировании.
- Улучшить обработку длинных путей и edge‑cases для Windows (учесть `\\` и MAX_PATH).
- Добавить CI (GitHub Actions) для сборки `linux_cli` и статической проверки кода (clang‑format, cppcheck).

--------------------------------------------------------------------------------

## Windows (MSYS2) — сборка и рекомендации

Проект можно собирать под Windows с использованием среды MSYS2 (MinGW‑w64). Рекомендуемый рабочий сценарий:

1. Запустите MINGW64 shell (MSYS2). Обновите систему и установите необходимые пакеты:

```bash
pacman -Syu
pacman -S --needed base-devel mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-jansson mingw-w64-x86_64-make
```

2. Убедитесь, что `ffmpeg` и библиотеки из `/mingw64/bin` доступны в `PATH` внутри MINGW64 shell (обычно это уже так).

3. Рекомендуемая генерация сборки (в корне репозитория):

```bash
mkdir build
cd build
# В MINGW64 shell можно использовать генератор MSYS Makefiles
cmake -G "MSYS Makefiles" ..
cmake --build . --target windows_cli
```

Если CMake не находит `jansson`, укажите префикс вручную:

```bash
cmake -G "MSYS Makefiles" -DCMAKE_PREFIX_PATH=/mingw64 ..
```

4. Запуск готового бинарника (пример):

```bash
./windows_cli/ffmpeg_converter.exe -c prores_ks -p hq -a loudnorm2 input.mov
```

Особенности и замечания для Windows/MSYS2:

- В файловой логике проекта учтены обратные слэши (`\\`) под `_WIN32` в `make_output_name()`; это помогает корректно формировать имена при использовании нативных Windows путей.
- Код CLI использует POSIX‑функции и `termios` в заголовках — в среде MSYS2 это обычно доступно, но при сборке с нативным MSVC потребуется отдельная портировка (этот репозиторий ориентирован на MinGW/MSYS сборку для Windows).
- CMake‑настройки уже используют `find_library(JANSSON_LIB jansson)` и линкуют `windows_cli` с `${JANSSON_LIB}` — это совместимо с пакетами MSYS2 (`mingw-w64-x86_64-jansson`).
- Если планируется собирать с помощью MSVC (Visual Studio), потребуется:
  - заменить POSIX‑зависимые участки (например, `unistd.h`, `termios`) или добавить условную реализацию для Windows API;
  - настроить поиск и линкирование `jansson`/`ffmpeg` под MSVC.

---

Проверка Windows (MSYS2)

В ходе проверки исходников и CMake файлов для Windows (цель — сборка в MSYS2 / MinGW‑w64) обнаружено, что проект уже совместим с MSYS2 без изменений:

- `src/cli/windows/main.c` использует POSIX‑заголовки (`unistd.h`, `termios.h`) — в окружении MSYS2/MINGW64 они доступны, поэтому правки не потребовались.
- `src/platform/CMakeLists.txt` корректно выбирает `src/platform/windows/progress.c` при условии `WIN32` (MinGW генерирует соответствующую конфигурацию в CMake), а `src/cli/CMakeLists.txt` линкует `windows_cli` с найденной библиотекой `${JANSSON_LIB}`.

Вывод: никаких изменений в коде под MSYS2 вносить не пришлось; добавлены и протестированы инструкции и workflow для автоматической сборки в MSYS2 (см. `.github/workflows/windows-msys2.yml`).

--------------------------------------------------------------------------------

--------------------------------------------------------------------------------

## Быстрые ссылки (ключевые файлы)

- [CMake root](CMakeLists.txt)
- [Модули / includes](src/CMakeLists.txt)
- [Converter API](src/converter/converter.h#L1)
- [Converter impl](src/converter/converter.c#L1)
- [Linux CLI](src/cli/linux/main.c#L1)
- [Progress interface](src/progress/progress.h#L1)
- [Platform CMake](src/platform/CMakeLists.txt#L1)

--------------------------------------------------------------------------------

Если нужно — могу:

- Сконвертировать этот файл в `README` с большей видимостью в GitHub (замена текущего краткого README).
- Добавить примеры команд и сниппеты использования `converter` из C API.
- Настроить GitHub Actions для автосборки `linux_cli` и базовой проверки кода.

Конец описания.
