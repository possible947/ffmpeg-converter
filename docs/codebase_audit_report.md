# Отчёт о проверке кодовой базы ffmpeg-converter

> Дата: 2026-04-27  
> Ветка: `copilot/check-functional-parity`

---

## Введение

Репозиторий содержит две параллельные реализации конвертера:
- **C/CMake** (`src/`) — нативная реализация с платформо-специфичными модулями для Linux, macOS и Windows.
- **Free Pascal/FPC** (`fpc/`) — независимая реализация на Pascal, зеркалирующая тот же C API.

Три платформы реализованы в одной ветке через `#ifdef`/`{$IFDEF}`. GUI: на Linux — GTK4 (C), на macOS — Cocoa/AppKit (C + Pascal bundle), на Windows — Lazarus/LCL (Pascal).

---

## Раздел I. Linux — C-реализация

### 1.1 Сборочная система
- Управляется CMake. Таргеты: `linux_cli`, `linux_gui` (GTK4), `linux_probe`.
- Bundled binaries размещаются в `src/platform/linux/bin/` — приоритет перед системным PATH. ✅

### 1.2 Резольв путей к бинарникам
- **Порядок поиска**: `FFMPEG`/`FFMPEG_BIN` env → bundled binary (каталог процесса, `bin/` подкаталог, `src/platform/linux/bin/`) → системный PATH. ✅
- Используется `/proc/self/exe` для определения каталога процесса. ✅
- Возвращает `""` при полном неуспехе (контракт `converter_platform.h`). ✅

### 1.3 Работа со сложными именами файлов
- **Критическая проблема** ⚠️: `build_ffmpeg_cmd` в `converter.c` использует простую двойную кавычку без экранирования: `strcat(cmd, "\""); strcat(cmd, input); strcat(cmd, "\" ");`
- Файлы с именами, содержащими `"`, `$`, обратный апостроф (`` ` ``), `\n`, `!` — вызовут некорректное поведение или shell-инъекцию при передаче в `popen()`.
- **Контраст**: модули `mux` и `m4v` в C используют правильную функцию `platform_shell_quote()` с single-quote обёрткой на POSIX. Несогласованность внутри одного проекта.
- Размер буфера `cmd[16384]` в `build_ffmpeg_cmd` с `strcat` без проверки границ — при длинных путях возможно тихое усечение или переполнение. ⚠️

### 1.4 Детекция GPU и VAAPI
- Реальное зондирование кодеков через тестовые команды ffmpeg. ✅
- Автоматический поиск оптимального DRI render-ноды через `linux_probe_codec_support`. ✅
- Специальный путь декодирования AV1 с фолбэком: QSV → libdav1d → native av1 decoder. ✅

### 1.5 Функциональность
- Все режимы (copy, prores, prores_ks, VAAPI, NVENC, AMF, QSV, Vulkan, mux, m4v) реализованы. ✅

---

## Раздел II. Linux — Pascal-реализация

### 2.1 Сборочная система
- Управляется `fpc/build/Makefile`. CMake **не** включает FPC Linux таргет.
- Команда сборки: `fpc -Fu... fpc/cli/ffmpeg_converter.lpr`. Нет унифицированной точки входа (в отличие от Windows, где есть `windows_build.ps1`). ⚠️

### 2.2 Резольв путей к бинарникам
- **Порядок**: env vars → `ResolveFromExeDir` (каталог исполняемого файла) → repo Windows bin (dead-code на Linux) → `command -v` через shell. ✅
- `ResolveFromExeDir` ищет бинарник в том же каталоге — требование «локальных бинарников рядом с программой» выполнено. ✅

### 2.3 Работа со сложными именами файлов
- **Проблема** ⚠️: `QuoteForShell` (`path_utils.pas`) использует двойную кавычку с `\"` для встроенных кавычек: `'"' + StringReplace(S, '"', '\"', [rfReplaceAll]) + '"'`.
- На POSIX-оболочке `/bin/sh -c` внутри двойных кавычек интерпретируются `$VAR`, `` `cmd` ``, `\n`, `!`. Файлы вида `My $100 Film.mp4` или `file with backtick\`.mp4` будут обработаны некорректно.
- **Правильный POSIX-вариант**: single-quote обёртка с `'\''` для встроенного апострофа (именно так делают модули mux/m4v в C).
- `mux_postprocess.pas` и `apple_m4v_creator.pas` используют этот же `QuoteForShell` — ошибка транслируется во все подмодули.

### 2.4 AV1 декодирование
- **Отсутствует** ⚠️: Pascal `converter_cmd_builder.pas` не реализует логику выбора AV1-декодера (QSV → libdav1d → native), которая есть в C. Все входные файлы обрабатываются без специального декодера.

### 2.5 Выбор AAC-кодировщика
- **Частичный паритет** ⚠️: на non-Windows Pascal всегда использует `aac -q:a 2`. C-реализация на macOS выбирает `aac_at` (нативный Apple AAC) > `libfdk_aac` > `aac`. На Linux оба варианта используют `aac`, паритет соблюдён.

### 2.6 VAAPI по умолчанию
- Pascal `converter_core.pas` хардкодит `/dev/dri/renderD128` как дефолтный render-нод. C использует результат `linux_probe_codec_support`, который находит реально работающий нод. ⚠️

### 2.7 Инструмент `linux_probe.pas`
- Проверяет только существование `/dev/dri/renderD128` и `/dev/dri/card0`. Не выполняет тестовые encode-операции. Значительно слабее C-реализации, где `linux_probe_codec_support` делает реальные ffmpeg-тесты. ⚠️

---

## Раздел III. macOS — C-реализация

### 3.1 Сборочная система
- CMake таргеты: `macos_cli`, `macos_native_gui` (Cocoa AppKit).
- Скрипт `fpc/build/package_macos_app.sh` собирает `.app` bundle с bundled ffmpeg/ffprobe/MP4Box. ✅

### 3.2 Резольв путей к бинарникам
- **Критическая проблема** ⚠️: `platform_get_ffmpeg_bin()` и `platform_get_ffprobe_bin()` проверяют только env vars и bundled binary (`../Resources/bin/`). **Нет фолбэка на PATH**.
- Если пользователь установил ffmpeg через Homebrew (`/opt/homebrew/bin/ffmpeg`) или MacPorts и не установил переменную окружения — бинарник **не будет найден**, вернётся `""`.
- `platform_get_mkvmerge_bin()` и `platform_get_mp4box_bin()` наоборот хардкодят статические пути MacPorts/Homebrew. Несогласованность.
- Pascal-реализация для macOS исправляет это через `ResolveBundledMacTool` + массив MacPorts/Homebrew кандидатов + `command -v` как финальный фолбэк. ✅

### 3.3 Работа со сложными именами файлов
- Та же проблема в `build_ffmpeg_cmd` (двойная кавычка без экранирования). ⚠️
- `macos_get_video_info` (внутренняя функция в `converter_macos.c`) также использует `"\"%s\""` — не экранирует специальные символы. ⚠️

### 3.4 Вычисление битрейта VideoToolbox
- Формула `macos_calc_hevc_vt_bitrate_kbps` с sub-linear scaling и зондированием через ffprobe. ✅
- Pascal `converter_cmd_builder.pas` для `hevc_videotoolbox`: если `hevc_vt_bitrate_kbps > 0`, использует его значение, иначе — константу `35000k`. Pascal-сторона не вычисляет битрейт самостоятельно — значение должно быть передано в `TConvertOptions`. ⚠️

### 3.5 Валидация аудио фильтров
- `platform_validate_audio_filters` на macOS возвращает `1` если ffmpeg существует, **без проверки наличия soxr**. Если установлен ffmpeg без libsoxr — потенциальный тихий сбой. ⚠️

---

## Раздел IV. macOS — Pascal-реализация

### 4.1 Статус
- Согласно `fpc/README.md` и `fpc/DESCRIPTION.md`: macOS Pascal — **вторичный путь**. Основной — C native GUI. Тем не менее Pascal-код компилируется под macOS через `{$IFDEF DARWIN}`.

### 4.2 Резольв путей
- `tool_paths.pas` имеет `{$IFDEF DARWIN}` блоки с `ResolveBundledMacTool` и кандидатами MacPorts/Homebrew. Значительно полнее macOS C-реализации. ✅

### 4.3 mux/m4v постобработка — отсутствует
- **Проблема** ⚠️: `ffmpeg_converter.lpr` вызывает `RunMuxPostprocess` и `RunM4VPostprocess` только в `{$IFDEF Linux}`. На macOS шаг ffmpeg-copy выполнится, а mkvmerge/MP4Box — нет. Молчаливое неполное выполнение.
- `IsCodecAllowedOnCurrentPlatform` в `cli_args.pas` для macOS разрешает `mux`, создавая ложные ожидания.

### 4.4 Кодеки VideoToolbox в Pascal CLI
- `hevc_videotoolbox` и `prores_videotoolbox` присутствуют в `converter_cmd_builder.pas`, но **отсутствуют в `IsCodecAllowedOnCurrentPlatform`** — недоступны через CLI.

---

## Раздел V. Windows — C-реализация

### 5.1 Сборочная система
- CMake + MSVC (`windows_build.ps1`). Bundled binaries в `src/platform/windows/bin/`. ✅
- Скрипт `windows_build.ps1` поддерживает `-BuildFPC`, `-GUIOnly`, `-FPCOnly` флаги. ✅

### 5.2 Резольв путей к бинарникам
- `windows_get_process_dir` использует `GetModuleFileNameW` → UTF-8 конвертация. ✅
- Поиск: env vars → bundled (каталог процесса, `bin/`, 8 уровней вверх до `src\platform\windows\bin\`) → `PathFindOnPathA`. ✅

### 5.3 Работа со сложными именами файлов (Unicode)
- **Критическая проблема** ⚠️: `platform_stat_is_regular_file` и `platform_stat_is_directory` используют `GetFileAttributesA` (ANSI API). Файлы с именами на кириллице, CJK и других Unicode-символах не будут найдены.
- `platform_popen` корректно использует `_wpopen` с UTF-8 → UTF-16 конвертацией. ✅
- **Несогласованность**: проверка существования файла через ANSI API, а выполнение команды — через Wide API.
- `windows_is_executable_file` в `runtime_probe.c` также использует `GetFileAttributesA`. ⚠️

### 5.4 Shell-квотинг
- `platform_escape_path_for_command` (converter) использует double-quote с Microsoft CommandLineToArgvW-совместимым escaping для backslash. ✅
- `mux_platform_windows.c` реализует полноценный cmd.exe escaping с backslash-удвоением. ✅
- `build_ffmpeg_cmd` (converter.c) — та же проблема: прямой `strcat` без escaping. ⚠️

---

## Раздел VI. Windows — Pascal-реализация

### 6.1 Сборочная система
- FPC CLI: `fpc_converter_windows` CMake custom target. ✅
- Lazarus GUI: `fpc_gui_windows` CMake custom target (требует `lazbuild`). ✅
- Полный PowerShell скрипт `windows_build.ps1`. ✅

### 6.2 Резольв путей к бинарникам
- `ResolveFromExeDir` + `ResolveFromRepoWindowsBin` (обходит до 8 уровней вверх). ✅
- Статические кандидаты для MP4Box: `C:\Program Files\GPAC\mp4box.exe`, Hybrid и другие. ✅
- `windows_mkvmerge.pas`: использует `GetModuleFileNameA` (ANSI) для пути к exe — **потенциальная проблема** если путь к exe содержит Unicode. ⚠️

### 6.3 Работа со сложными именами файлов (Unicode)
- `GetUTF8Arguments` через `CommandLineToArgvW` + UTF-16 → UTF-8. ✅
- `SetConsoleCP(CP_UTF8)` при старте. ✅
- `fs_utils.pas` Windows: `GetFileAttributesW` (Wide API). ✅
- `windows_file_utils.pas`: `AnsiToWide`/`WideToAnsi` + `GetFileAttributesW`. ✅
- **Проблема**: `QuoteForShell` использует double-quote с `\"` — на Windows cmd.exe стандартный escaping для `"` — это `""`, а не `\"`. Однако, поскольку команды передаются через `TProcess` → `cmd.exe /c "..."`, фактическое поведение зависит от конкретного контекста. ⚠️

### 6.4 Детекция GPU
- `windows_probe.pas`: реальные тесты NVENC/AMF/QSV/Vulkan через `ProbeEncoder`. ✅
- `form_windows.pas`: `DetectWindowsHardware` с `ProbeVulkanDeviceCount` — отдельная проверка каждого устройства. ✅

### 6.5 Проблема в `ProbeEncoder`
- `'"' + FfmpegBin + '" ...'` — bare double-quote без escaping. Путь к ffmpeg.exe с пробелами будет сломан если содержит дополнительные `"`. ⚠️

---

## Раздел VII. Межплатформенный функциональный паритет

| Функция | Linux C | Linux Pascal | macOS C | macOS Pascal | Windows C | Windows Pascal |
|---|---|---|---|---|---|---|
| copy/prores/prores_ks | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| VAAPI (h264/hevc) | ✅ | ✅ | N/A | N/A | N/A | N/A |
| NVENC (h264/hevc) | ✅ | частично | N/A | N/A | ✅ | ✅ |
| AMF | ✅ | ❌ нет в CLI | N/A | N/A | ✅ | ✅ |
| QSV | ✅ | ❌ нет в CLI | N/A | N/A | ✅ | ✅ |
| Vulkan ProRes | ✅ | ✅ | N/A | N/A | ✅ | ✅ |
| VideoToolbox | N/A | N/A | ✅ | в коде, не в CLI | N/A | N/A |
| mux (mkvmerge) | ✅ | ✅ | ✅ | ⚠️ неполный | ✅ | ✅ |
| m4v (MP4Box) | ✅ | ✅ | ✅ | ⚠️ неполный | ✅ | ✅ |
| AV1 input decode | ✅ | ❌ отсутствует | ✅ | ❌ отсутствует | ✅ | ❌ отсутствует |
| 2-pass loudnorm | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| AAC динамический выбор | ✅ | частично | ✅ aac_at | ❌ нет aac_at | ✅ | fdk/native |
| Unicode имена файлов | ✅ | ⚠️ QuoteForShell | ✅ | ⚠️ QuoteForShell | ⚠️ GetFileAttrsA | ✅ |
| Bundled binaries | ✅ | ✅ | ⚠️ нет PATH | ✅ | ✅ | ✅ |

---

## Раздел VIII. Рекомендации по устранению недочётов

### Приоритет 1 — Критические (влияют на корректность работы)

**P1.1. Небезопасное shell-квотирование в `build_ffmpeg_cmd` (C, все платформы)**

`src/converter/converter.c`, функция `build_ffmpeg_cmd`: замените прямое
```c
strcat(cmd, "\""); strcat(cmd, input); strcat(cmd, "\" ");
```
на вызов `platform_escape_path_for_command(input)` — такой же подход уже применён в модулях `mux` и `m4v`. Применить ко всем вхождениям: путь к ffmpeg, input, output, `hw_device`.

**P1.2. Небезопасная `QuoteForShell` в Pascal на POSIX**

`fpc/common/path_utils.pas`: функция использует double-quote, которая не защищает от `$`, `` ` ``, `\`, `!` в POSIX. Необходимо ввести раздельную логику:
```pascal
{$IFDEF Windows}
  // текущая реализация корректна для Windows
{$ELSE}
  // single-quote обёртка с '\'' для апострофа
  Result := '''' + StringReplace(S, '''', '''\\''''', [rfReplaceAll]) + '''';
{$ENDIF}
```
Это исправит все места, где используется `QuoteForShell`: `converter_cmd_builder.pas`, `converter_analysis.pas`, `mux_postprocess.pas`, `apple_m4v_creator.pas`, `converter_runner.pas`.

**P1.3. `GetFileAttributesA` вместо W в C Windows**

`src/converter/platform/converter_windows.c`, функции `platform_stat_is_regular_file` и `platform_stat_is_directory`: заменить `GetFileAttributesA` на `GetFileAttributesW` с предварительным `MultiByteToWideChar(CP_UTF8, ...)`, аналогично тому, как реализовано в `platform_popen` этого же файла и в Pascal `fs_utils.pas`.

То же касается `src/platform/windows/runtime_probe.c`, функция `windows_is_executable_file`.

---

### Приоритет 2 — Высокие (функциональный паритет и корректность)

**P2.1. Отсутствие PATH-фолбэка в macOS C binary resolution**

`src/converter/platform/converter_macos.c`, `platform_get_ffmpeg_bin` и `platform_get_ffprobe_bin`: после проверки bundled binary добавить поиск по PATH через `access()` для известных путей Homebrew (`/opt/homebrew/bin/ffmpeg`, `/usr/local/bin/ffmpeg`) и MacPorts (`/opt/local/bin/ffmpeg`). Сделать аналогично Pascal `tool_paths.pas::ResolveBinary`.

**P2.2. mux/m4v постобработка на macOS Pascal CLI**

`fpc/cli/ffmpeg_converter.lpr`: убрать ограничение `{$IFDEF Linux}` вокруг вызовов `RunMuxPostprocess`/`RunM4VPostprocess`, заменить на `{$IFNDEF Windows}` (mux/m4v работают и на macOS). Либо добавить `{$IFDEF Darwin}` блок явно. Обновить `IsCodecAllowedOnCurrentPlatform` соответственно.

**P2.3. Отсутствие AV1 decoder selection в Pascal**

`fpc/converter/converter_cmd_builder.pas`: перед `-i` добавить логику зондирования AV1 декодера аналогично C (проверить наличие `av1_qsv` или `libdav1d` через `ffmpeg -decoders`). Можно вынести в `fpc/platform/linux_probe.pas` и `windows_probe.pas`.

**P2.4. Безопасность буфера в `build_ffmpeg_cmd` (C)**

Заменить все `strcat` в `build_ffmpeg_cmd` на `strncat` с отслеживанием оставшегося места, или перейти на динамический буфер (realloc). Альтернатива — увеличить буфер до 65536 с проверкой на переполнение.

---

### Приоритет 3 — Средние (качество и стабильность)

**P3.1. Кэширование `ResolveToolPaths` в Pascal**

`fpc/common/tool_paths.pas`: добавить глобальный кэш аналогично C (где `platform_init` выполняется один раз). Сейчас `ResolveToolPaths` вызывается на каждый файл и каждую analysis pass.

**P3.2. Автоопределение VAAPI render-ноды в Pascal**

`fpc/converter/converter_core.pas`, Linux VAAPI секция: вместо хардкода `/dev/dri/renderD128` использовать `linux_probe.pas::GetVaapiRenderNode` (уже существующая функция).

**P3.3. Детекция soxr на macOS (C)**

`src/converter/platform/converter_macos.c`, `platform_validate_audio_filters`: выполнять реальную проверку, как на Linux:
```c
snprintf(cmd, sizeof(cmd), "\"%s\" -hide_banner -filters 2>/dev/null | grep -q soxr", ffmpeg);
return (system(cmd) == 0) ? 1 : 0;
```

**P3.4. Детекция AMF/QSV в Pascal Linux**

`fpc/cli/cli_args.pas`: Linux список кодеков (`IsCodecAllowedOnCurrentPlatform`) включает `mux`, `h264_vaapi`, `hevc_vaapi` — но не `h264_nvenc`, `hevc_nvenc`, `h264_amf`, `hevc_amf`, `h264_qsv`, `hevc_qsv`, которые поддерживаются C Linux. Добавить зондирование через `linux_probe.pas` или подобный механизм.

**P3.5. `GetModuleFileNameA` в `windows_mkvmerge.pas`**

`fpc/platform/windows_mkvmerge.pas`: заменить `GetModuleFileNameA` на `GetModuleFileNameW` + `WideCharToMultiByte(CP_UTF8)`, как сделано в C `windows_get_process_dir`.

**P3.6. VideoToolbox кодеки в Pascal macOS CLI**

`fpc/cli/cli_args.pas`: добавить `hevc_videotoolbox` и `prores_videotoolbox` в список допустимых кодеков для macOS в `IsCodecAllowedOnCurrentPlatform`. Они уже реализованы в `converter_cmd_builder.pas`.

**P3.7. AAC `aac_at` в Pascal macOS**

`fpc/converter/converter_cmd_builder.pas`: добавить `{$IFDEF DARWIN}` ветку с проверкой наличия `aac_at` (через `ffmpeg -encoders | grep aac_at`), аналогично C. На macOS `aac_at` даёт лучшее качество при меньшем CPU overhead.

---

### Приоритет 4 — Незначительные и документационные

**P4.1. CMake FPC таргеты для Linux/macOS**

В корневом `CMakeLists.txt` добавить `fpc_converter_linux` / `fpc_converter_macos` CMake custom targets аналогично `fpc_converter_windows`. Это упростит CI/CD.

**P4.2. Двойной `{$IFDEF Darwin}` + `{$IFNDEF Linux}` в `converter_core.pas`**

Строки 282–288: два блока подряд запрещают VAAPI на macOS (`{$IFDEF Darwin}` и `{$IFNDEF Linux}`). Второй блок делает первый избыточным. Оставить один `{$IFNDEF Linux}`.

**P4.3. `DirWritable` Windows в Pascal fs_utils**

`fpc/common/fs_utils.pas` Windows: функция `DirWritable` пишет временный файл `.ffc_write_test` через `FileCreate` (ANSI). Для Unicode-путей использовать `CreateFileW` или `windows_file_utils.DirIsWritable`.

**P4.4. `CanWriteDir` в `process_utils.pas` Windows**

`fpc/common/process_utils.pas`: функция `CanWriteDir` на Windows использует `DirectoryExists` из `SysUtils`, что на FPC может работать через ANSI. Использовать `windows_file_utils.DirIsWritable`.

---

## Сводная таблица найденных проблем

| # | Платформа | Вариант | Категория | Описание | Приоритет |
|---|---|---|---|---|---|
| 1 | Все | C | Безопасность | `build_ffmpeg_cmd`: bare double-quote без escaping | P1 |
| 2 | Linux/macOS | Pascal | Безопасность | `QuoteForShell`: double-quote небезопасна на POSIX | P1 |
| 3 | Windows | C | Корректность | `GetFileAttributesA` vs Unicode имена файлов | P1 |
| 4 | macOS | C | Функциональность | Нет PATH-фолбэка для ffmpeg/ffprobe | P2 |
| 5 | macOS | Pascal | Функциональность | mux/m4v постобработка не вызывается на macOS | P2 |
| 6 | Все | Pascal | Паритет | Отсутствует AV1 decoder selection | P2 |
| 7 | Все | C | Надёжность | Фиксированный буфер + `strcat` без bounds check | P2 |
| 8 | Linux | Pascal | Паритет | Хардкод `/dev/dri/renderD128` вместо авто-детекции | P3 |
| 9 | macOS | C | Корректность | `platform_validate_audio_filters` не проверяет soxr | P3 |
| 10 | macOS | Pascal | Паритет | `hevc_videotoolbox`/`prores_videotoolbox` не в CLI | P3 |
| 11 | macOS | Pascal | Паритет | Нет `aac_at` выбора (только `aac`) | P3 |
| 12 | Linux | Pascal | Паритет | AMF/QSV/NVENC не в списке допустимых кодеков CLI | P3 |
| 13 | Windows | Pascal | Корректность | `GetModuleFileNameA` в mkvmerge lookup | P3 |
| 14 | Все | Pascal | Производительность | `ResolveToolPaths` без кэша (вызов на каждый файл) | P3 |
| 15 | Windows | Pascal | Корректность | `QuoteForShell` с `\"` вместо `""` в cmd.exe | P3 |
| 16 | Все | C | Избыточность | Двойной Darwin+!Linux VAAPI блок в converter_core.pas | P4 |
