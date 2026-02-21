# LIBRARY API SPECIFICATION  
Версия: 1.0  
Назначение: формальное описание API библиотеки конвертации медиафайлов.

---

# 1. Назначение библиотеки

Библиотека предоставляет программный интерфейс для выполнения следующих операций:

- анализ аудио (peak 2-pass, loudnorm 2-pass);
- нормализация аудио (peak, loudnorm);
- перекодирование видео (copy, prores, prores_ks);
- применение deblock-фильтров;
- отображение прогресса анализа и кодирования;
- обработка очереди файлов.

Библиотека является платформенно-независимым ядром и используется в CLI и GUI.

---

# 2. Архитектура

Библиотека построена вокруг объекта `Converter`, содержащего:

- параметры обработки (`ConvertOptions`);
- набор callback-функций (`ConverterCallbacks`);
- внутреннее состояние;
- флаг остановки;
- результаты анализов.

Все операции выполняются через API-функции, описанные ниже.

---

# 3. Структуры данных

## 3.1 ConvertOptions

```c
typedef struct {
    // VIDEO
    char codec[32];     // "copy", "prores", "prores_ks"
    int  profile;       // 0=none, 1=lt, 2=standard, 3=hq, 4=4444
    int  deblock;       // 1=none, 2=weak, 3=strong

    // AUDIO NORMALIZATION
    char audio_norm[32]; // "none", "peak_norm", "peak_norm_2pass",
                         // "loudness_norm", "loudness_norm_2pass"

    // LOUDNORM 2-PASS GENRE
    int genre;          // 0=none, 1..5 (EDM, Rock, Hip-Hop, Classical, Podcast)

    // INTERNAL PARAMETERS FOR 2-PASS
    double gain;            // peak_norm_2pass
    double I_target;        // loudnorm_2pass
    double TP_target;
    double LRA_target;
    double measured_I;
    double measured_TP;
    double measured_LRA;
    double measured_thresh;
    double measured_offset;

    // OUTPUT
    int overwrite;          // 0=skip if exists, 1=force overwrite (CLI=0)
} ConvertOptions;
```

---

## 3.2 ConverterError

```c
typedef enum {
    ERR_OK = 0,

    // FILE ERRORS
    ERR_INPUT_NOT_FOUND,
    ERR_INPUT_NOT_REGULAR,
    ERR_INPUT_NOT_READABLE,

    // OUTPUT ERRORS
    ERR_OUTPUT_EXISTS,
    ERR_SKIP_FILE,

    // ANALYSIS ERRORS
    ERR_PEAK_ANALYSIS_FAILED,
    ERR_LOUDNORM_ANALYSIS_FAILED,

    // FFMPEG ERRORS
    ERR_FFMPEG_FAILED,
    ERR_FFPROBE_FAILED,

    // SYSTEM ERRORS
    ERR_POPEN_FAILED,
    ERR_PCLOSE_FAILED,

    // INTERNAL
    ERR_INVALID_OPTIONS,
    ERR_UNKNOWN
} ConverterError;
```

---

## 3.3 ConverterCallbacks

```c
typedef struct {
    // FILE EVENTS
    void (*on_file_begin)(
        const char* filename,
        int index,
        int total
    );

    void (*on_file_end)(
        const char* filename,
        ConverterError status
    );

    // STAGE EVENTS
    void (*on_stage)(
        const char* stage_name
    );

    // PROGRESS: ENCODING
    void (*on_progress_encode)(
        float percent,
        float fps,
        float eta_seconds
    );

    // PROGRESS: ANALYSIS
    void (*on_progress_analysis)(
        float percent,
        float eta_seconds
    );

    // MESSAGES
    void (*on_message)(
        const char* text
    );

    // ERRORS
    void (*on_error)(
        const char* text,
        ConverterError code
    );

    // QUEUE COMPLETE
    void (*on_complete)(void);

} ConverterCallbacks;
```

---

# 4. API-функции

## 4.1 Создание и уничтожение контекста

```c
Converter* converter_create(void);
void converter_destroy(Converter* c);
```

---

## 4.2 Назначение callback-функций

```c
void converter_set_callbacks(
    Converter* c,
    const ConverterCallbacks* cb
);
```

Все поля структуры могут быть `NULL`.

---

## 4.3 Установка параметров обработки

```c
ConverterError converter_set_options(
    Converter* c,
    const ConvertOptions* opts
);
```

Возвращает:

- `ERR_OK` — параметры приняты;
- `ERR_INVALID_OPTIONS` — параметры некорректны.

---

## 4.4 Запуск обработки очереди файлов

```c
ConverterError converter_process_files(
    Converter* c,
    const char** files,
    int file_count
);
```

Поведение:

- выполняет проверку входных файлов;
- выполняет анализ peak/loudnorm при необходимости;
- генерирует имя выходного файла автоматически;
- проверяет существование выходного файла;
- выполняет кодирование с прогрессом;
- вызывает callbacks;
- учитывает флаг остановки.

---

## 4.5 Принудительная остановка обработки

```c
void converter_stop(Converter* c);
```

Устанавливает внутренний флаг остановки.  
Анализ и кодирование должны завершиться корректно.

---

## 4.6 Получение текстового описания ошибки

```c
const char* converter_error_string(ConverterError err);
```

Возвращает статическую строку.

---

# 5. Потоковая модель

- `converter_process_files()` должен выполняться в отдельном потоке при использовании в GUI.
- Все callbacks вызываются из того же потока, что и процесс обработки.
- GUI обязан перенаправлять обновления интерфейса в главный поток (например, через `g_idle_add()`).

---

# 6. Правила обработки файлов

1. Если входной файл не существует → `ERR_INPUT_NOT_FOUND`.
2. Если выходной файл существует и `overwrite=0` → `ERR_OUTPUT_EXISTS`.
3. Если выбран `peak_norm_2pass` → выполняется peak-анализ.
4. Если выбран `loudness_norm_2pass` → выполняется loudnorm-анализ.
5. Если анализ завершился ошибкой → соответствующий код ошибки.
6. Если ffmpeg завершился ошибкой → `ERR_FFMPEG_FAILED`.
7. После завершения всех файлов вызывается `on_complete()`.

---

# 7. Генерация имени выходного файла

Имя формируется автоматически:

- `*_converted.mkv` для codec=copy  
- `*_converted.mov` для codec=prores/prores_ks  

Поведение соответствует CLI.

---

# 8. Требования к окружению

- ffmpeg и ffprobe должны быть доступны в PATH.
- jansson должен быть доступен для парсинга JSON.
- библиотека не выполняет поиск бинарников.

---

# 9. Ограничения

- библиотека не выполняет многопоточную обработку нескольких файлов одновременно;
- библиотека не изменяет входные файлы;
- библиотека не предоставляет API для ручного задания выходного пути.

---

# 10. Версионирование

Версия API фиксируется в этом документе.  
Изменения должны сопровождаться обновлением версии.


