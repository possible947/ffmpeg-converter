# M4V_DETAILED_ANALYSIS.md

Detailed analysis of `src/m4v/m4v.c` for evaluating Windows adaptation of the
Apple M4V creation module, with a full function-by-function breakdown of every
Linux dependency and a concrete porting roadmap.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Architecture Analysis](#2-current-architecture-analysis)
   - 2.1 [Current File Structure](#21-current-file-structure)
   - 2.2 [Code Distribution](#22-code-distribution)
3. [Detailed Function-by-Function Analysis](#3-detailed-function-by-function-analysis)
   - 3.1 [Callback Functions (lines 15–31)](#31-callback-functions-lines-1531)
   - 3.2 [Helper Functions (lines 33–111)](#32-helper-functions-lines-33111)
   - 3.3 [Command Execution (lines 113–157)](#33-command-execution-lines-113157)
   - 3.4 [FPS Parsing (lines 159–214)](#34-fps-parsing-lines-159214)
   - 3.5 [Timestamp & JSON (lines 216–307)](#35-timestamp--json-lines-216307)
   - 3.6 [Temp Directory (lines 309–337)](#36-temp-directory-lines-309337)
   - 3.7 [Options Initialization (lines 339–351)](#37-options-initialization-lines-339351)
   - 3.8 [Output Name Generation (lines 353–379)](#38-output-name-generation-lines-353379)
   - 3.9 [Input Validation (lines 381–433)](#39-input-validation-lines-381433)
   - 3.10 [Main M4V Creation (lines 435–644)](#310-main-m4v-creation-lines-435644)
4. [Critical Linux-Only Dependencies](#4-critical-linux-only-dependencies)
   - 4.1 [Unconditional Import (line 13)](#41-unconditional-import-line-13)
   - 4.2 [Unconditional Probe Calls (lines 400, 492–494)](#42-unconditional-probe-calls-lines-400-492494)
   - 4.3 [Hardcoded /tmp/ (line 317)](#43-hardcoded-tmp-line-317)
   - 4.4 [mkdtemp() POSIX Only (line 318)](#44-mkdtemp-posix-only-line-318)
   - 4.5 [Shell rm -rf Command (line 335)](#45-shell-rm--rf-command-line-335)
   - 4.6 [Hardcoded '/' Path Separator (line 366)](#46-hardcoded--path-separator-line-366)
   - 4.7 [Path Construction with '/' (line 374)](#47-path-construction-with--line-374)
5. [Headers Analysis](#5-headers-analysis)
6. [Windows-Specific Implementation Details](#6-windows-specific-implementation-details)
   - 6.1 [Temp Directory on Windows](#61-temp-directory-on-windows)
   - 6.2 [Recursive Directory Removal on Windows](#62-recursive-directory-removal-on-windows)
   - 6.3 [Binary Resolution on Windows](#63-binary-resolution-on-windows)
   - 6.4 [File Existence Check on Windows](#64-file-existence-check-on-windows)
   - 6.5 [Command Execution on Windows](#65-command-execution-on-windows)
   - 6.6 [Shell Quoting on Windows](#66-shell-quoting-on-windows)
7. [Proposed New File Structure](#7-proposed-new-file-structure)
8. [Platform Abstraction Interface](#8-platform-abstraction-interface)
   - 8.1 [Portability Classification Summary](#81-portability-classification-summary)
   - 8.2 [Common Wrapper Signatures](#82-common-wrapper-signatures)
9. [CMakeLists.txt Changes](#9-cmakeliststxt-changes)
10. [Implementation Phases](#10-implementation-phases)
11. [Risk Assessment](#11-risk-assessment)

---

## 1. Executive Summary

### 🚨 КРИТИЧЕСКОЕ ОТКРЫТИЕ: m4v.c ТОЛЬКО ДЛЯ LINUX!

`src/m4v/m4v.c` **не компилируется** ни на Windows, ни на macOS в нынешнем
виде. Модуль содержит **семь категорий** жёстко зашитых зависимостей
от Linux/POSIX, три из которых являются немедленными блокерами компиляции.

#### Ключевые проблемы

| № | Проблема | Строка | Серьёзность |
|---|----------|--------|-------------|
| 1 | `#include "linux/runtime_probe.h"` без `#if` guard | 13 | 🔴 BLOCKER |
| 2 | `linux_get_preferred_ffprobe_bin()` без fallback | 400 | 🔴 BLOCKER |
| 3 | `linux_get_preferred_ffmpeg_bin()` без fallback | 492 | 🔴 BLOCKER |
| 4 | `linux_get_preferred_ffprobe_bin()` без fallback | 493 | 🔴 BLOCKER |
| 5 | `linux_get_preferred_mp4box_bin()` без fallback | 494 | 🔴 BLOCKER |
| 6 | Hardcoded `/tmp/m4v_mux_XXXXXX` | 317 | 🔴 BLOCKER |
| 7 | `mkdtemp()` — POSIX only | 318 | 🔴 BLOCKER |
| 8 | `rm -rf` via `system()` | 335–336 | 🔴 BLOCKER |

#### Текущий статус платформенной поддержки

| Платформа | Статус компиляции | Статус выполнения |
|-----------|-------------------|-------------------|
| Linux     | ✅ Компилируется | ✅ Работает |
| macOS     | ❌ Не компилируется | ❌ — |
| Windows   | ❌ Не компилируется | ❌ — |

#### Сложность Windows адаптации: **ОЧЕНЬ ВЫСОКАЯ**

Оценка трудозатрат: **26–43 рабочих часа** (без учёта тестирования
конечного продукта).

#### Рекомендация

| Вариант | Описание | Трудозатраты |
|---------|----------|-------------|
| **A — отключить** | Исключить `m4v.c` из Windows/macOS сборок через CMake (`#if defined(__linux__)`) | 2–4 ч |
| **B — полный port** | Добавить platform abstraction layer по образцу `mux.c` | 26–43 ч |

Для краткосрочных релизов рекомендуется **Вариант A**; для полной
кросс-платформенной поддержки — **Вариант B**.

---

## 2. Current Architecture Analysis

### 2.1 Current File Structure

```
src/m4v/
├── m4v.h          (43 lines  — public API header)
├── m4v.c          (644 lines — full implementation)
└── CMakeLists.txt
```

**Назначение:** Создание Apple M4V файлов через 5-шаговый пайплайн:
1. Копирование видеодорожки (ffmpeg)
2. Энкодинг AAC аудио (ffmpeg + libfdk\_aac)
3. Энкодинг AC3 аудио (ffmpeg)
4. Мультиплексирование MP4Box
5. Добавление глав (mp4box + ffprobe)

**Платформа:** **Linux only** (hardcoded зависимости — см. раздел 4).

`m4v.c` включает следующие заголовки:

| Header | Строка | Тип | Windows |
|--------|--------|-----|---------|
| `"m4v.h"` | 1 | Свой | ✅ OK |
| `<jansson.h>` | 3 | Portable | ✅ OK |
| `<ctype.h>` | 4 | Portable | ✅ OK |
| `<errno.h>` | 5 | Portable | ✅ OK |
| `<stdio.h>` | 6 | Portable | ✅ OK |
| `<stdlib.h>` | 7 | Portable | ✅ OK |
| `<string.h>` | 8 | Portable | ✅ OK |
| `<sys/stat.h>` | 9 | POSIX | 🟡 Wrap (`_stat` on MSVC) |
| `<sys/wait.h>` | 10 | POSIX | ❌ Not on MSVC |
| `<unistd.h>` | 11 | POSIX | ❌ Not on MSVC |
| `"linux/runtime_probe.h"` | 13 | Linux-only | ❌ BLOCKER |

### 2.2 Code Distribution

```
m4v.c  (644 lines)
├── lines   1–13   includes (mixed — 3 POSIX, 1 Linux-only)
├── lines  15–31   emit_*() callbacks                          (PORTABLE)
├── lines  33–54   copy_string(), is_regular_file()            (POSIX — wrap)
├── lines  56–111  shell_quote(), shell_quote_double()         (Shell — adapt)
├── lines 113–157  run_command_capture()                       (POSIX popen — wrap)
├── lines 159–214  parse_rate_to_fps(), probe_fps_for_input()  (mixed)
├── lines 216–307  make_chapter_timestamp(), chapter JSON      (PORTABLE)
├── lines 309–337  make_temp_dir(), remove_temp_dir()          (LINUX ONLY!)
├── lines 339–351  m4v_default_options()                       (PORTABLE)
├── lines 353–379  m4v_make_output_name()                      (mostly portable)
├── lines 381–433  m4v_validate_input_supported()              (LINUX ONLY!)
└── lines 435–644  m4v_create_from_input()                     (LINUX ONLY!)
```

| Категория | Доля | Описание |
|-----------|------|----------|
| Portable logic | ~35% | Callbacks, JSON parsing, math, struct init |
| POSIX API | ~25% | `popen`, `stat`, `mkdir`, `unlink` — wrappable |
| Linux-specific | ~30% | Runtime probe calls, hardcoded `/tmp` |
| Shell operations + path handling | ~10% | `rm -rf`, `system()`, `/` separator |

---

## 3. Detailed Function-by-Function Analysis

### 3.1 Callback Functions (lines 15–31) — PORTABLE

Три вспомогательные функции перенаправляют события в пользовательский
`ConverterCallbacks`. Системных вызовов нет, платформенных типов нет.

| Функция | Строки | Тип | Статус |
|---------|--------|------|--------|
| `emit_message()` | 15–19 | Pure logic | ✅ COPY AS-IS |
| `emit_stage()` | 21–25 | Pure logic | ✅ COPY AS-IS |
| `emit_error()` | 27–31 | Pure logic | ✅ COPY AS-IS |

---

### 3.2 Helper Functions (lines 33–111) — MIXED

| Функция | Строки | Тип | Портируемость | Проблема |
|---------|--------|------|--------------|----------|
| `copy_string()` | 33–43 | Pure logic | ✅ COPY AS-IS | — |
| `is_regular_file()` | 45–54 | POSIX `stat` | 🟡 WRAP | `stat()`, `S_ISREG`, `access(R_OK)` |
| `shell_quote()` | 56–85 | Shell quoting | 🟡 ADAPT | POSIX single quotes |
| `shell_quote_double()` | 87–111 | Shell quoting | ✅ PORTABLE | Works for Windows too |

#### `is_regular_file()` — строки 45–54

```c
static int is_regular_file(const char *path)
{
    struct stat st;
    return path &&
           path[0] != '\0' &&
           stat(path, &st) == 0 &&
           S_ISREG(st.st_mode) &&
           access(path, R_OK) == 0;
}
```

**Проблемы:**
- `S_ISREG` недоступен на MSVC без MinGW
- `access(R_OK)` недоступен на MSVC (нужен `_access`)

**Windows:** `GetFileAttributesA()` + проверка `FILE_ATTRIBUTE_DIRECTORY`.

#### `shell_quote()` — строки 56–85

```c
out[out_pos++] = '\'';
while (*input) {
    if (*input == '\'') {
        /* escape as '\'' */
    }
}
```

**Проблемы:**
- Использует POSIX single-quote escaping (`'\''`)
- `cmd.exe` на Windows не понимает одинарные кавычки
- Для `cmd.exe` нужны двойные кавычки с экранированием `"` → `\"`

---

### 3.3 Command Execution (lines 113–157) — POSIX

```c
static int run_command_capture(const char *cmd, ...)
{
    FILE *fp = popen(cmd, "r");
    ...
    status = pclose(fp);
    if (WIFEXITED(status))            // <-- POSIX macro
        return WEXITSTATUS(status);   // <-- POSIX macro
}
```

| Зависимость | Деталь | Windows |
|-------------|--------|---------|
| `popen()` | Запускает дочерний процесс | `_popen()` MinGW/MSVC CRT |
| `pclose()` | Закрывает поток | `_pclose()` MinGW/MSVC CRT |
| `WIFEXITED` | Проверка кода возврата | ❌ Нет на MSVC без `<sys/wait.h>` |
| `WEXITSTATUS` | Получение кода возврата | ❌ Нет на MSVC без `<sys/wait.h>` |

**Статус:** 🟡 WRAP — добавить `platform_popen` / `platform_pclose` +
заменить `WIFEXITED`/`WEXITSTATUS` на прямое сравнение кода возврата
(на Windows `_pclose` возвращает exit code напрямую без POSIX упаковки).

---

### 3.4 FPS Parsing (lines 159–214) — MIXED

| Функция | Строки | Тип | Статус |
|---------|--------|------|--------|
| `parse_rate_to_fps()` | 159–178 | Pure math | ✅ COPY AS-IS |
| `probe_fps_for_input()` | 180–214 | `popen` + shell | 🟡 WRAP |

`probe_fps_for_input()` использует `shell_quote()` и `2>/dev/null`
(POSIX shell redirect). На Windows нужно `2>nul`.

---

### 3.5 Timestamp & JSON (lines 216–307) — PORTABLE

| Функция | Строки | Тип | Статус |
|---------|--------|------|--------|
| `make_chapter_timestamp()` | 216–241 | Pure math | ✅ COPY AS-IS |
| `build_chapter_text_from_json()` | 243–307 | jansson + `fopen` | ✅ COPY AS-IS |

`fopen` и `fprintf` переносимы. `jansson` поддерживает Windows.

---

### 3.6 🚨 CRITICAL: Temp Directory (lines 309–337) — LINUX ONLY!

```c
static int make_temp_dir(char *path, size_t path_sz)
{
    char templ[1024];
    char *made;
    snprintf(templ, sizeof(templ), "/tmp/m4v_mux_XXXXXX");  /* HARDCODED /tmp */
    made = mkdtemp(templ);                                   /* POSIX only     */
    if (!made)
        return 0;
    copy_string(path, path_sz, made);
    return 1;
}

static void remove_temp_dir(const char *dir)
{
    char cmd[2048];
    char quoted[1536];
    shell_quote(dir, quoted, sizeof(quoted));
    snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);  /* shell rm -rf  */
    system(cmd);                                       /* system() call */
}
```

**Проблемы:**

| # | Проблема | Строка | Серьёзность |
|---|----------|--------|-------------|
| 1 | `/tmp/` hardcoded — не существует на Windows | 317 | 🔴 BLOCKER |
| 2 | `mkdtemp()` — POSIX only (нет в MSVC CRT) | 318 | 🔴 BLOCKER |
| 3 | `rm -rf` shell command — не переносимо | 335 | 🔴 BLOCKER |
| 4 | `system()` — запускает `cmd.exe` на Windows | 336 | 🔴 BLOCKER |

**Решение для каждой платформы:**

| Шаг | Linux | macOS | Windows |
|-----|-------|-------|---------|
| Базовый temp path | `/tmp` | `getenv("TMPDIR")` → `/tmp` | `GetTempPathW()` |
| Создание уникальной папки | `mkdtemp()` | `mkdtemp()` | `CreateDirectoryW()` + UUID |
| Удаление папки | `nftw()` или `rm -rf` | `nftw()` | `SHFileOperationW()` или рекурсивный `RemoveDirectoryW()` |

---

### 3.7 Options Initialization (lines 339–351) — PORTABLE

```c
void m4v_default_options(M4VOptions *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->video_track_index = 0;
    opts->audio_track_index = 0;
    opts->aac_quality       = 5;
    opts->ac3_bitrate_kbps  = 640;
    opts->add_chapters      = 1;
    copy_string(opts->audio_lang, sizeof(opts->audio_lang), "rus");
}
```

**Статус:** ✅ COPY AS-IS — нет системных вызовов, только struct init.

---

### 3.8 Output Name Generation (lines 353–379) — MOSTLY PORTABLE

```c
ConverterError m4v_make_output_name(...)
{
    slash = strrchr(input_file, '/');          /* hardcoded '/' separator */
    name  = slash ? slash + 1 : input_file;
    ...
    snprintf(out_file, ..., "%s/%s.m4v", output_dir, base);  /* hardcoded '/' */
}
```

**Статус:** 🟡 ADAPT

| Аспект | Деталь |
|--------|--------|
| `strrchr(input_file, '/')` | Windows принимает оба сепаратора в большинстве Win32 API, но не в `strrchr` — нужно искать как `'/'`, так и `'\\'` |
| `snprintf("%s/%s.m4v", ...)` | Работает на Windows (принимает `/`), но лучше использовать `PATH_SEPARATOR` |

**Минимальное исправление:** добавить поиск `'\\'` если `'/'` не найден.

---

### 3.9 Input Validation (lines 381–433) — CRITICAL LINUX DEPENDENCY!

```c
ConverterError m4v_validate_input_supported(...)
{
    ...
    ffprobe_bin = linux_get_preferred_ffprobe_bin();  /* LINE 400: UNCONDITIONAL! */
    shell_quote(ffprobe_bin, quoted_tool, sizeof(quoted_tool));
    snprintf(cmd, ...,
             "%s ... 2>/dev/null",                    /* POSIX shell redirect */
             quoted_tool, quoted_input);
    ...
}
```

**Проблемы:**

| Строка | Функция | Проблема |
|--------|---------|----------|
| 400 | `m4v_validate_input_supported()` | Прямой вызов `linux_get_preferred_ffprobe_bin()` |
| 405 | `snprintf(cmd, ...)` | Hardcoded `2>/dev/null` (POSIX) |

**Эффект:** Компиляция немедленно падает на Windows/macOS из-за отсутствия
символа `linux_get_preferred_ffprobe_bin`.

---

### 3.10 Main M4V Creation (lines 435–644) — 209 LINES, VERY COMPLEX!

```c
ConverterError m4v_create_from_input(...)
{
    ...
    ffmpeg_bin  = linux_get_preferred_ffmpeg_bin();   /* LINE 492: UNCONDITIONAL! */
    ffprobe_bin = linux_get_preferred_ffprobe_bin();  /* LINE 493: UNCONDITIONAL! */
    mp4box_bin  = linux_get_preferred_mp4box_bin();   /* LINE 494: UNCONDITIONAL! */
    ...
    if (!make_temp_dir(work_dir, sizeof(work_dir))) { /* /tmp hardcoded inside   */
    ...
    remove_temp_dir(work_dir);                        /* rm -rf shell inside      */
    ...
    if (!overwrite && access(output_file, F_OK) == 0) { /* POSIX access()        */
    if (overwrite)
        unlink(output_file);                             /* POSIX unlink()        */
}
```

**5-шаговый пайплайн и его переносимость:**

| Шаг | Команда | Переносимость | Проблема |
|-----|---------|---------------|----------|
| 1 — video copy | `ffmpeg -c:v copy` | ✅ Portable | Нужен правильный path |
| 2 — AAC encode | `ffmpeg -c:a libfdk_aac` | ✅ Portable | Нужен правильный path |
| 3 — AC3 encode | `ffmpeg -c:a ac3` | ✅ Portable | Нужен правильный path |
| 4 — MP4Box mux | `mp4box -new -add` | ⚠️ Mostly portable | Нужен правильный path |
| 5 — chapters | `mp4box -chap` + ffprobe | ⚠️ Mostly portable | Нужен правильный path |

**Дополнительные POSIX зависимости в main функции:**

| Строка | API | Windows |
|--------|-----|---------|
| 587 | `access(output_file, F_OK)` | `_access()` или `GetFileAttributesA()` |
| 593 | `unlink(output_file)` | `_unlink()` или `DeleteFileA()` |

---

## 4. Critical Linux-Only Dependencies

### 4.1 Unconditional Import (line 13)

```c
#include "linux/runtime_probe.h"   /* LINE 13 — NO #if GUARDS! */
```

**Влияние:**

| Платформа | Результат |
|-----------|-----------|
| Linux | ✅ Заголовок существует |
| macOS | ❌ Компиляция падает — файл не существует |
| Windows | ❌ Компиляция падает — файл не существует |

🔴 **BLOCKER:** Необходимо добавить `#if defined(__linux__)` guard или
заменить на кросс-платформенный вызов.

---

### 4.2 Unconditional Probe Calls (lines 400, 492–494)

| Строка | Функция | Вызов | Влияние |
|--------|---------|-------|---------|
| 400 | `m4v_validate_input_supported()` | `linux_get_preferred_ffprobe_bin()` | ❌ Ломает валидацию |
| 492 | `m4v_create_from_input()` | `linux_get_preferred_ffmpeg_bin()` | ❌ Ломает создание |
| 493 | `m4v_create_from_input()` | `linux_get_preferred_ffprobe_bin()` | ❌ Ломает валидацию |
| 494 | `m4v_create_from_input()` | `linux_get_preferred_mp4box_bin()` | ❌ Ломает мультиплексирование |

🔴 **BLOCKER:** Все четыре вызова требуют либо `#if defined(__linux__)` guard,
либо замены на кросс-платформенный API резолвинга бинарных файлов.

---

### 4.3 Hardcoded /tmp/ (line 317)

```c
snprintf(templ, sizeof(templ), "/tmp/m4v_mux_XXXXXX");  /* LINE 317 */
```

**Влияние:**

| Платформа | Результат |
|-----------|-----------|
| Linux | ✅ `/tmp/` существует |
| macOS | ⚠️ `/tmp` — симлинк; правильно использовать `$TMPDIR` |
| Windows | ❌ Путь не существует |

🔴 **BLOCKER:** Необходима платформенная функция получения temp директории.

---

### 4.4 mkdtemp() POSIX Only (line 318)

```c
made = mkdtemp(templ);  /* LINE 318 — POSIX ONLY */
```

**Влияние:**

| Платформа | Результат |
|-----------|-----------|
| Linux | ✅ Доступна в glibc |
| macOS | ✅ Доступна в libc |
| Windows MSVC | ❌ Нет в CRT |
| Windows MinGW | ⚠️ Не гарантировано |

🔴 **BLOCKER:** На Windows необходимо `GetTempPath()` + `CreateDirectory()`
с UUID или счётчиком.

---

### 4.5 Shell rm -rf Command (line 335)

```c
snprintf(cmd, sizeof(cmd), "rm -rf %s", quoted);  /* LINE 335 */
system(cmd);                                       /* LINE 336 */
```

**Влияние:**

| Платформа | Результат |
|-----------|-----------|
| Linux | ✅ Работает |
| macOS | ✅ Работает |
| Windows | ❌ `cmd.exe` не знает команду `rm -rf` |

🔴 **BLOCKER:** Необходима рекурсивная функция удаления директории
без shell:
- Windows: `SHFileOperationW()` или рекурсивный обход `FindFirstFile` +
  `DeleteFile` + `RemoveDirectory`
- POSIX: `nftw()` или `ftw()` + `unlinkat`/`rmdir`

---

### 4.6 Hardcoded '/' Path Separator (line 366)

```c
slash = strrchr(input_file, '/');  /* LINE 366 */
```

**Влияние:**

| Платформа | Результат |
|-----------|-----------|
| Linux | ✅ Работает |
| macOS | ✅ Работает |
| Windows | ⚠️ Работает только для forward-slash путей |

На Windows пути могут содержать `\` — нужно также искать `strrchr(input_file, '\\')`.

**Статус:** ⚠️ LOW — функциональная проблема, не блокер компиляции.

---

### 4.7 Path Construction with '/' (line 374)

```c
snprintf(out_file, out_file_sz, "%s/%s.m4v", output_dir, base);  /* LINE 374 */
```

**Влияние:**

| Платформа | Результат |
|-----------|-----------|
| Linux | ✅ Работает |
| macOS | ✅ Работает |
| Windows | ⚠️ Win32 API принимает `/` в большинстве случаев |

**Статус:** ⚠️ LOW — лучше использовать `PATH_SEPARATOR` или
`platform_path_join()` для единообразия, но не блокер.

---

## 5. Headers Analysis

| Header | Строка | Тип | Платформа | Windows |
|--------|--------|-----|-----------|---------|
| `"m4v.h"` | 1 | Свой | All | ✅ OK |
| `<jansson.h>` | 3 | Third-party | All | ✅ Portable |
| `<ctype.h>` | 4 | C standard | All | ✅ Portable |
| `<errno.h>` | 5 | C standard | All | ✅ Portable |
| `<stdio.h>` | 6 | C standard | All | ✅ Portable (`_popen`/`_pclose` нужны) |
| `<stdlib.h>` | 7 | C standard | All | ✅ Portable |
| `<string.h>` | 8 | C standard | All | ✅ Portable |
| `<sys/stat.h>` | 9 | POSIX | Linux/macOS | 🟡 `_stat` / `_S_IFREG` на MSVC |
| `<sys/wait.h>` | 10 | POSIX | Linux/macOS | ❌ Нет на MSVC — нужен `#ifdef` |
| `<unistd.h>` | 11 | POSIX | Linux/macOS | ❌ Нет на MSVC — нужен `#ifdef` |
| `"linux/runtime_probe.h"` | 13 | Linux-only | Linux | ❌ 🔴 BLOCKER |

**Минимальный набор изменений заголовков:**

```c
/* Portable */
#include "m4v.h"
#include <jansson.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform-specific — wrap */
#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>       /* _access, _unlink */
#else
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

/* Runtime probe — platform-specific */
#if defined(__linux__)
#  include "linux/runtime_probe.h"
#elif defined(_WIN32)
#  include "windows/runtime_probe.h"   /* future */
#endif
```

---

## 6. Windows-Specific Implementation Details

### 6.1 Temp Directory on Windows

```c
/* Windows replacement for make_temp_dir() */
static int make_temp_dir(char *path, size_t path_sz)
{
    char base[MAX_PATH];
    char unique[MAX_PATH];
    DWORD len;

    len = GetTempPathA(sizeof(base), base);
    if (len == 0 || len >= sizeof(base))
        return 0;

    /* Use GetTempFileNameA trick: create a temp file, delete it, use name as dir */
    if (!GetTempFileNameA(base, "m4v", 0, unique))
        return 0;

    DeleteFileA(unique);   /* remove the temp file */

    if (!CreateDirectoryA(unique, NULL))
        return 0;

    strncpy(path, unique, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}
```

### 6.2 Recursive Directory Removal on Windows

```c
/* Windows replacement for remove_temp_dir() */
static void remove_temp_dir(const char *dir)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char child[MAX_PATH];

    if (!dir || dir[0] == '\0')
        return;

    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 ||
                strcmp(fd.cFileName, "..") == 0)
                continue;
            snprintf(child, sizeof(child), "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                remove_temp_dir(child);
            else
                DeleteFileA(child);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}
```

### 6.3 Binary Resolution on Windows

На Linux `linux_get_preferred_ffmpeg_bin()` и аналоги из
`src/platform/linux/runtime_probe.c` ищут бинарные файлы через
environment variables, bundled copies, и `PATH`.

**Windows:** создать аналогичные функции в
`src/platform/windows/runtime_probe.c`:

```c
const char *windows_get_preferred_ffmpeg_bin(void);
const char *windows_get_preferred_ffprobe_bin(void);
const char *windows_get_preferred_mp4box_bin(void);
```

Порядок поиска для каждого инструмента:
1. Environment variable (`FFMPEG`, `FFMPEG_BIN`)
2. Директория исполняемого файла (`GetModuleFileNameA`)
3. System `PATH` с `.exe` суффиксом (`SearchPathA`)

### 6.4 File Existence Check on Windows

```c
/* Windows replacement for is_regular_file() */
static int is_regular_file(const char *path)
{
    DWORD attr;
    if (!path || path[0] == '\0')
        return 0;
    attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
```

### 6.5 Command Execution on Windows

```c
/* Cross-platform popen wrapper */
#if defined(_WIN32)
#  define PLATFORM_POPEN  _popen
#  define PLATFORM_PCLOSE _pclose
   /* Windows: pclose returns exit code directly, no WIFEXITED needed */
   static int platform_wait_exit(int status) { return status; }
#else
#  define PLATFORM_POPEN  popen
#  define PLATFORM_PCLOSE pclose
   static int platform_wait_exit(int status) {
       return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
   }
#endif
```

Shell redirect `2>/dev/null` нужно заменить на `2>nul` под Windows:

```c
#if defined(_WIN32)
#  define NULL_REDIRECT "2>nul"
#else
#  define NULL_REDIRECT "2>/dev/null"
#endif
```

### 6.6 Shell Quoting on Windows

`cmd.exe` использует двойные кавычки, а не одинарные. Существующая
`shell_quote_double()` подходит для путей без специальных символов.

Для полной защиты под Windows рекомендуется:

```c
/* Windows: wrap in double quotes; escape " and \ before final " */
static void shell_quote_win(const char *input, char *out, size_t out_sz)
{
    /* Use existing shell_quote_double() — adequate for file paths */
    shell_quote_double(input, out, out_sz);
}
```

---

## 7. Proposed New File Structure

После полного портирования рекомендуется следующая структура:

```
src/m4v/
├── m4v.h                              (unchanged — public API)
├── m4v.c                              (refactored — no platform ifdefs in business logic)
├── CMakeLists.txt                     (updated)
└── platform/
    ├── m4v_platform.h                 (common interface)
    ├── m4v_platform_posix.c           (Linux + macOS implementation)
    └── m4v_platform_windows.c         (Windows implementation)
```

**`m4v_platform.h` interface:**

```c
#ifndef M4V_PLATFORM_H
#define M4V_PLATFORM_H

#include <stddef.h>

/* Temp directory management */
int  m4v_platform_make_temp_dir(char *path, size_t path_sz);
void m4v_platform_remove_temp_dir(const char *dir);

/* Binary resolution */
const char *m4v_platform_get_ffmpeg_bin(void);
const char *m4v_platform_get_ffprobe_bin(void);
const char *m4v_platform_get_mp4box_bin(void);

/* File operations */
int  m4v_platform_is_regular_file(const char *path);
int  m4v_platform_file_exists(const char *path);
void m4v_platform_unlink(const char *path);

/* Command execution */
FILE *m4v_platform_popen(const char *cmd, const char *mode);
int   m4v_platform_pclose(FILE *fp);
int   m4v_platform_pclose_exit_code(int raw_status);

/* Shell quoting */
void m4v_platform_shell_quote(const char *input, char *out, size_t out_sz);

/* Null device redirect string */
const char *m4v_platform_null_redirect(void);

/* Path separator */
char m4v_platform_path_sep(void);

#endif /* M4V_PLATFORM_H */
```

---

## 8. Platform Abstraction Interface

### 8.1 Portability Classification Summary

| Функция | Строки | Категория | Действие |
|---------|--------|-----------|----------|
| `emit_message()` | 15–19 | ✅ PORTABLE | Оставить как есть |
| `emit_stage()` | 21–25 | ✅ PORTABLE | Оставить как есть |
| `emit_error()` | 27–31 | ✅ PORTABLE | Оставить как есть |
| `copy_string()` | 33–43 | ✅ PORTABLE | Оставить как есть |
| `is_regular_file()` | 45–54 | 🟡 WRAP | `m4v_platform_is_regular_file()` |
| `shell_quote()` | 56–85 | 🟡 ADAPT | `m4v_platform_shell_quote()` |
| `shell_quote_double()` | 87–111 | ✅ PORTABLE | Оставить как есть |
| `run_command_capture()` | 113–157 | 🟡 WRAP | `m4v_platform_popen/pclose` + убрать WIFEXITED |
| `parse_rate_to_fps()` | 159–178 | ✅ PORTABLE | Оставить как есть |
| `probe_fps_for_input()` | 180–214 | 🟡 WRAP | `m4v_platform_null_redirect()` |
| `make_chapter_timestamp()` | 216–241 | ✅ PORTABLE | Оставить как есть |
| `build_chapter_text_from_json()` | 243–307 | ✅ PORTABLE | Оставить как есть |
| `make_temp_dir()` | 309–324 | ❌ LINUX ONLY | `m4v_platform_make_temp_dir()` |
| `remove_temp_dir()` | 326–337 | ❌ LINUX ONLY | `m4v_platform_remove_temp_dir()` |
| `m4v_default_options()` | 339–351 | ✅ PORTABLE | Оставить как есть |
| `m4v_make_output_name()` | 353–379 | 🟡 ADAPT | Добавить поиск `\\` |
| `m4v_validate_input_supported()` | 381–433 | ❌ LINUX ONLY | Заменить probe calls + redirect |
| `m4v_create_from_input()` | 435–644 | ❌ LINUX ONLY | Заменить probe calls + unlink/access |

### 8.2 Common Wrapper Signatures

```c
/* POSIX implementation (m4v_platform_posix.c) */
int m4v_platform_make_temp_dir(char *path, size_t path_sz) {
    char templ[1024];
    char *made;
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0') tmpdir = "/tmp";
    snprintf(templ, sizeof(templ), "%s/m4v_mux_XXXXXX", tmpdir);
    made = mkdtemp(templ);
    if (!made) return 0;
    strncpy(path, made, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}

/* Windows implementation (m4v_platform_windows.c) */
int m4v_platform_make_temp_dir(char *path, size_t path_sz) {
    char base[MAX_PATH], unique[MAX_PATH];
    if (!GetTempPathA(sizeof(base), base)) return 0;
    if (!GetTempFileNameA(base, "m4v", 0, unique)) return 0;
    DeleteFileA(unique);
    if (!CreateDirectoryA(unique, NULL)) return 0;
    strncpy(path, unique, path_sz - 1);
    path[path_sz - 1] = '\0';
    return 1;
}
```

---

## 9. CMakeLists.txt Changes

Текущий `src/m4v/CMakeLists.txt` необходимо обновить для поддержки
платформенных источников:

```cmake
# Current (Linux-only)
add_library(m4v STATIC m4v.c)

# Proposed (cross-platform)
set(M4V_SOURCES m4v.c)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND M4V_SOURCES platform/m4v_platform_windows.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND M4V_SOURCES platform/m4v_platform_posix.c)
else()  # Linux
    list(APPEND M4V_SOURCES platform/m4v_platform_posix.c)
endif()

add_library(m4v STATIC ${M4V_SOURCES})
target_include_directories(m4v PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)
target_link_libraries(m4v PRIVATE jansson)
```

**Краткосрочное решение (Вариант A — отключить на других платформах):**

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(m4v STATIC m4v.c)
    target_link_libraries(m4v PRIVATE jansson)
else()
    # m4v module is Linux-only; skip on Windows and macOS
    add_library(m4v INTERFACE)
    message(STATUS "m4v: skipped (Linux only)")
endif()
```

---

## 10. Implementation Phases

### Вариант A — Отключить на других платформах (2–4 часа)

1. Обернуть `#include "linux/runtime_probe.h"` в `#if defined(__linux__)`
2. Обернуть весь `m4v.c` в `#if defined(__linux__)` или
   добавить CMake условие для исключения из сборки
3. Обновить `CMakeLists.txt` для пропуска `m4v` на Windows/macOS
4. Проверить сборку на Linux (ничего не сломалось) и убедиться,
   что Windows/macOS собираются без m4v

### Вариант B — Полный port (26–43 часа)

#### Phase 1 — Platform Abstraction Layer (8–12 ч)

1. Создать `src/m4v/platform/m4v_platform.h` с полным набором сигнатур
2. Создать `src/m4v/platform/m4v_platform_posix.c`
   - `m4v_platform_make_temp_dir()` с `getenv("TMPDIR")`
   - `m4v_platform_remove_temp_dir()` через `nftw()` или `rm -rf`
   - `m4v_platform_is_regular_file()` через `stat()`/`S_ISREG`
   - `m4v_platform_popen/pclose/pclose_exit_code()`
   - `m4v_platform_shell_quote()` (single-quote style)
   - `m4v_platform_null_redirect()` → `"2>/dev/null"`
   - `m4v_platform_unlink()` → `unlink()`
3. Создать `src/m4v/platform/m4v_platform_windows.c`
   - `m4v_platform_make_temp_dir()` через `GetTempPath/GetTempFileName`
   - `m4v_platform_remove_temp_dir()` — рекурсивный `FindFirstFile/DeleteFile`
   - `m4v_platform_is_regular_file()` через `GetFileAttributesA`
   - `m4v_platform_popen/pclose/pclose_exit_code()` через `_popen/_pclose`
   - `m4v_platform_shell_quote()` (double-quote style для `cmd.exe`)
   - `m4v_platform_null_redirect()` → `"2>nul"`
   - `m4v_platform_unlink()` → `DeleteFileA()`

#### Phase 2 — Binary Resolution for Windows (6–10 ч)

4. Создать `src/platform/windows/runtime_probe.h` с:
   - `windows_get_preferred_ffmpeg_bin()`
   - `windows_get_preferred_ffprobe_bin()`
   - `windows_get_preferred_mp4box_bin()`
5. Создать `src/platform/windows/runtime_probe.c` — поиск по
   env variable, директории exe, `PATH` с `.exe` суффиксом

#### Phase 3 — Refactor m4v.c (8–14 ч)

6. Заменить `#include "linux/runtime_probe.h"` на
   `#include "platform/m4v_platform.h"` и платформенный probe header
7. Заменить все `linux_get_preferred_*()` на
   `m4v_platform_get_*_bin()` / `#if defined(__linux__)` / `#elif defined(_WIN32)`
8. Заменить `make_temp_dir()` и `remove_temp_dir()` на
   `m4v_platform_make_temp_dir()` / `m4v_platform_remove_temp_dir()`
9. Заменить `popen/pclose/WIFEXITED/WEXITSTATUS` на обёртки
10. Заменить `access(F_OK)` на `m4v_platform_file_exists()`
11. Заменить `unlink()` на `m4v_platform_unlink()`
12. Заменить hardcoded `2>/dev/null` на `m4v_platform_null_redirect()`
13. Исправить поиск `/` сепаратора — добавить `\\`

#### Phase 4 — CMakeLists.txt & Build Validation (4–7 ч)

14. Обновить `src/m4v/CMakeLists.txt` для платформенных источников
16. Верифицировать Linux build (регрессий нет)
17. Верифицировать macOS build
18. Верифицировать Windows build (MSYS2/MinGW или MSVC)

---

## 11. Risk Assessment

| Риск | Вероятность | Влияние | Митигация |
|------|-------------|---------|-----------|
| `mkdtemp` аналог на Windows создаёт race condition | Средняя | Высокое | Использовать `GetTempFileName` + `CreateDirectory` атомарно |
| `_popen` на Windows требует `cmd.exe` в PATH | Низкая | Высокое | Документировать зависимость |
| libfdk_aac недоступен в Windows сборках ffmpeg | Высокая | Высокое | Документировать требование; добавить проверку |
| MP4Box отсутствует в Windows PATH | Высокая | Высокое | Документировать требование; добавить fallback |
| `shell_quote_double` не защищает от `%` expansion в cmd.exe | Средняя | Среднее | Экранировать `%` → `%%` в Windows реализации |
| Рекурсивное удаление директории через Win32 — edge cases | Средняя | Среднее | Тестировать на путях с пробелами и unicode |
| `2>nul` в cmd.exe не работает если cmd.exe не найден | Низкая | Низкое | Добавить fallback без redirect |

### Итоговая оценка трудозатрат

| Фаза | Вариант A | Вариант B |
|------|-----------|-----------|
| Platform Abstraction Layer | — | 8–12 ч |
| Binary Resolution | — | 6–10 ч |
| Refactor m4v.c | — | 8–14 ч |
| CMakeLists.txt + Validation | 2–4 ч | 4–7 ч |
| **ИТОГО** | **2–4 ч** | **26–43 ч** |

### Рекомендация

Для немедленного устранения блокера сборки (CI/CD, macOS/Windows):
→ **Вариант A** (2–4 ч) — добавить CMake guard и `#if defined(__linux__)`.

Для полноценной кросс-платформенной поддержки:
→ **Вариант B** (26–43 ч) — по образцу уже задокументированного
`MUX_WINDOWS_ADAPTATION_PLAN.md`.
