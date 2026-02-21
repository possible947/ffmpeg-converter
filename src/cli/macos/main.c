#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <termios.h>
#include <limits.h>

#include "converter.h"
#include "progress.h"
#define BUFFER_SIZE 4096

// ------------------------------------------------------------
//  CLI Callbacks
// ------------------------------------------------------------

static void cli_on_file_begin(const char* filename, int index, int total) {
    progress_end();
    printf("\n[%d/%d] Processing: %s\n", index, total, filename);
}

static void cli_on_file_end(const char* filename, ConverterError status) {
    progress_end();
    if (status == ERR_OK)
        printf("Completed: %s\n", filename);
    else
        printf("Error on %s: %s\n", filename, converter_error_string(status));
}

static void cli_on_stage(const char* stage) {
    progress_end();
    printf("Stage: %s\n", stage);
}

static void cli_on_progress_encode(float percent, float fps, float eta) {
    progress_update(percent, fps, eta);
}

static void cli_on_progress_analysis(float percent, float eta) {
    progress_update(percent, 0, eta);
}

static void cli_on_message(const char* text) {
    progress_end();
    printf("%s\n", text);
}

static void cli_on_error(const char* text, ConverterError code) {
    progress_end();
    printf("ERROR: %s (%s)\n", text, converter_error_string(code));
}

static void cli_on_complete(void) {
    progress_end();
    printf("\nAll files processed.\n");
}

// ------------------------------------------------------------
//  Help / usage
// ------------------------------------------------------------

static void print_usage(void) {
    printf("Usage: ffmpeg_converter [options] file1 file2 ...\n\n");
    printf("Options:\n");
    printf("  -c, --codec <copy|prores|prores_ks>\n");
    printf("  -p, --profile <lt|standard|hq|4444>\n");
    printf("  -d, --deblock <none|weak|strong>\n");
    printf("  -a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>\n");
    printf("  -g, --genre <edm|rock|hiphop|classical|podcast>\n");
    printf("      (genre is used only with loudnorm2)\n");
    printf("  --overwrite        overwrite output files\n");
    printf("  -o, --output <directory> set output directory\n");
    printf("  -h, --help         show this help\n\n");
    printf("Examples:\n");
    printf("  ffmpeg_converter input.mov\n");
    printf("  ffmpeg_converter -c prores_ks -p hq input.mov\n");
    printf("  ffmpeg_converter -a loudnorm2 -g rock input1.mov input2.mov\n\n");
}

// ------------------------------------------------------------
//  Argument parsing (non-interactive mode)
// ------------------------------------------------------------

static int parse_args(int argc, char** argv, ConvertOptions* opts,
                      const char** files, int* file_count)
{
    strcpy(opts->codec, "prores_ks");
    opts->profile   = 2;  // standard
    opts->deblock   = 1;  // none
    strcpy(opts->audio_norm, "peak_norm_2pass");
    opts->genre     = 1;  // edm
    opts->overwrite = 0;
    opts->output_dir[0] = '\0';
    opts->output_dir_status = 0;

    *file_count = 0;

    for (int i = 1; i < argc; i++) {

        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage();
            return 0;
        }

        if (!strcmp(argv[i], "--codec") || !strcmp(argv[i], "-c")) {
            if (i + 1 >= argc) return 0;
            i++;

            if (!strcmp(argv[i], "copy"))
                strcpy(opts->codec, "copy");
            else if (!strcmp(argv[i], "prores"))
                strcpy(opts->codec, "prores");
            else if (!strcmp(argv[i], "prores_ks"))
                strcpy(opts->codec, "prores_ks");
            else return 0;

            continue;
        }

        if (!strcmp(argv[i], "--profile") || !strcmp(argv[i], "-p")) {
            if (i + 1 >= argc) return 0;
            i++;

            if (!strcmp(argv[i], "lt")) opts->profile = 1;
            else if (!strcmp(argv[i], "standard")) opts->profile = 2;
            else if (!strcmp(argv[i], "hq")) opts->profile = 3;
            else if (!strcmp(argv[i], "4444")) opts->profile = 4;
            else return 0;

            continue;
        }

        if (!strcmp(argv[i], "--deblock") || !strcmp(argv[i], "-d")) {
            if (i + 1 >= argc) return 0;
            i++;

            if (!strcmp(argv[i], "none")) opts->deblock = 1;
            else if (!strcmp(argv[i], "weak")) opts->deblock = 2;
            else if (!strcmp(argv[i], "strong")) opts->deblock = 3;
            else return 0;

            continue;
        }

        if (!strcmp(argv[i], "--audio-norm") || !strcmp(argv[i], "-a")) {
            if (i + 1 >= argc) return 0;
            i++;

            if (!strcmp(argv[i], "none"))
                strcpy(opts->audio_norm, "none");
            else if (!strcmp(argv[i], "peak"))
                strcpy(opts->audio_norm, "peak_norm");
            else if (!strcmp(argv[i], "peak2"))
                strcpy(opts->audio_norm, "peak_norm_2pass");
            else if (!strcmp(argv[i], "loudnorm"))
                strcpy(opts->audio_norm, "loudness_norm");
            else if (!strcmp(argv[i], "loudnorm2"))
                strcpy(opts->audio_norm, "loudness_norm_2pass");
            else return 0;

            continue;
        }

        if (!strcmp(argv[i], "--genre") || !strcmp(argv[i], "-g")) {
            if (i + 1 >= argc) return 0;
            i++;

            if (!strcmp(argv[i], "edm")) opts->genre = 1;
            else if (!strcmp(argv[i], "rock")) opts->genre = 2;
            else if (!strcmp(argv[i], "hiphop")) opts->genre = 3;
            else if (!strcmp(argv[i], "classical")) opts->genre = 4;
            else if (!strcmp(argv[i], "podcast")) opts->genre = 5;
            else return 0;

            continue;
        }

        if (!strcmp(argv[i], "--overwrite")) {
            opts->overwrite = 1;
            continue;
        }

        if (argv[i][0] != '-') {
            files[*file_count] = argv[i];
            (*file_count)++;
            continue;
        }
        
        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) return 0;
            i++;
            strncpy(opts->output_dir, argv[i], sizeof(opts->output_dir)-1);
            // Проверяем директорию
            struct stat st;
            if (stat(opts->output_dir, &st) == 0 && S_ISDIR(st.st_mode) && access(opts->output_dir, W_OK) == 0) {
                opts->output_dir_status = 1;
            } else {
                opts->output_dir_status = 0;
                fprintf(stderr, "Warning: Output directory is not writable or doesn't exist: %s\n", opts->output_dir);
            }
            continue;
        }

        return 0;
    }

    return 1;
}

// ------------------------------------------------------------
//  Summary
// ------------------------------------------------------------

static void print_summary(const ConvertOptions* opts, const char** files, int file_count) {
    printf("\033[1;1H\033[2J");
    printf("\n=== Summary ===\n");
    printf("Codec:        %s\n", opts->codec);

    if (strcmp(opts->codec, "copy") != 0) {
        const char* profile_str = "none";
        switch (opts->profile) {
            case 1: profile_str = "lt"; break;
            case 2: profile_str = "standard"; break;
            case 3: profile_str = "hq"; break;
            case 4: profile_str = "4444"; break;
        }
        printf("Profile:      %s\n", profile_str);

        const char* deblock_str = "none";
        switch (opts->deblock) {
            case 1: deblock_str = "none"; break;
            case 2: deblock_str = "weak"; break;
            case 3: deblock_str = "strong"; break;
        }
        printf("Deblock:      %s\n", deblock_str);
    } else {
        printf("Profile:      (copy)\n");
        printf("Deblock:      (copy)\n");
    }

    printf("Audio norm:   %s\n", opts->audio_norm);

    if (!strcmp(opts->audio_norm, "loudness_norm_2pass")) {
        const char* genre_str = "none";
        switch (opts->genre) {
            case 1: genre_str = "edm"; break;
            case 2: genre_str = "rock"; break;
            case 3: genre_str = "hiphop"; break;
            case 4: genre_str = "classical"; break;
            case 5: genre_str = "podcast"; break;
        }
        printf("Genre:        %s\n", genre_str);
    }

    printf("Overwrite:    %s\n", opts->overwrite ? "yes" : "no");
    if (opts->output_dir[0] != '\0')
        printf("Output dir:   %s\n", opts->output_dir);
    else
        printf("Output dir:   (same as input)\n");
    
    if (opts->output_dir[0] != '\0') {
        if (opts->output_dir_status)
            printf("Dir status:   OK\n");
        else
            printf("Dir status:   ERROR (directory missing or not writable)\n");
    }

    printf("\nFiles (%d):\n", file_count);
    for (int i = 0; i < file_count; ++i) {
        // Если путь содержит пробелы, показываем в кавычках для читаемости
        if (strchr(files[i], ' ') != NULL) {
            printf("  \"%s\"\n", files[i]);
        } else {
            printf("  %s\n", files[i]);
        }
    }
    printf("===============\n");
}

//-------------------------------------------------------------
// main menu function
// ------------------------------------------------------------

/* ---------- вспомогательные функции ---------- */

/* Очистка экрана (ANSI) */
static void clear_screen(void)
{
    printf("\033[H\033[J");
}

/* Чтение одного символа + Enter.
   Возвращает введённый символ, либо '\n' если пользователь нажал только Enter. */
static int read_choice(void)
{
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin))
        return EOF;

    for (char *p = buf; *p != '\0'; ++p) {
        if (*p == '\n')
            return '\n';
        if (!isspace((unsigned char)*p))
            return *p;
    }
    return '\n';
}

/* --------------------------------------------------------------------- */
/*  Ввод пути к каталогу для сохранения файлов.
    При пустом вводе используется $HOME/ffmpeg_converter.
    Проверяется существование директории, иначе возвращается -1.   */
static int read_output_dir(char *out_buf, size_t bufsize, int *status)
{
    char tmp[PATH_MAX];
    const char *home;

    printf("output directory (default: $HOME/ffmpeg_converter):\n> ");
    if (!fgets(tmp, sizeof(tmp), stdin))
        return -1;               /* EOF / ошибка */

    /* Удаляем завершающий '\n' */
    tmp[strcspn(tmp, "\r\n")] = '\0';

    if (tmp[0] == '\0') {           /* пустой ввод → путь по умолчанию   */
        home = getenv("HOME");
        if (!home) return -1;
        snprintf(tmp, sizeof(tmp), "%s/ffmpeg_converter", home);
    }

    /* Проверяем существование каталога и создаём его при необходимости   */
    struct stat st;
    if (stat(tmp, &st) != 0) {
        if (errno == ENOENT) {      /* каталога нет – создаём */
            if (mkdir(tmp, 0755) != 0) {
                perror("mkdir");
                return -1;
            }
        } else {                    /* другая ошибка stat */
            perror("stat");
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr,
                "Error: '%s' exists but is not a directory.\n",
                tmp);
        return -1;
    }

    /* Проверяем доступность для записи */
    if (access(tmp, W_OK) != 0) {
        perror("access");
        *status = 0;
        return -1;
    }

    *status = 1;
    /* Копируем путь в выходной буфер */
    strncpy(out_buf, tmp, bufsize - 1);
    out_buf[bufsize - 1] = '\0';
    return 0;                       /* успешно */
}

/* Обработка пути из ввода пользователя */
static int process_input_path(const char *input, char *output, size_t out_size)
{
    if (!input || !output || out_size == 0) {
        return 0;
    }
    
    size_t len = strlen(input);
    if (len == 0) {
        output[0] = '\0';
        return 1;
    }
    
    char *temp = malloc(len + 1);
    if (!temp) {
        return 0;
    }
    
    int j = 0;
    int in_quotes = 0;
    char quote_char = 0;
    int escape_next = 0;
    
    for (size_t i = 0; i < len; i++) {
        if (escape_next) {
            // Обрабатываем escape-символ
            temp[j++] = input[i];
            escape_next = 0;
            continue;
        }
        
        if (input[i] == '\\') {
            // Если это экранирующий слэш
            if (i + 1 < len) {
                // Проверяем следующий символ
                if (input[i + 1] == ' ' || input[i + 1] == '\\' || 
                    input[i + 1] == '\'' || input[i + 1] == '"') {
                    escape_next = 1;
                    continue;
                }
            }
            // Иначе оставляем как есть
            temp[j++] = input[i];
            continue;
        }
        
        // Обработка кавычек
        if (!in_quotes && (input[i] == '\'' || input[i] == '"')) {
            in_quotes = 1;
            quote_char = input[i];
            continue;
        }
        
        if (in_quotes && input[i] == quote_char) {
            in_quotes = 0;
            continue;
        }
        
        // Обычный символ
        temp[j++] = input[i];
    }
    
    temp[j] = '\0';
    
    // Убираем начальные и конечные пробелы
    char *start = temp;
    char *end = temp + j - 1;
    
    while (start <= end && isspace((unsigned char)*start)) {
        start++;
    }
    
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    
    size_t final_len = end - start + 1;
    
    if (final_len == 0) {
        free(temp);
        return 0;
    }
    
    if (final_len < out_size) {
        strncpy(output, start, final_len);
        output[final_len] = '\0';
        free(temp);
        return 1;
    } else {
        // Буфер слишком мал
        strncpy(output, start, out_size - 1);
        output[out_size - 1] = '\0';
        free(temp);
        return 1;
    }
}

/*  Функция чтения списка файлов.  Если пользователь
 *  перетаскивает файл в терминал, оболочка обычно
 *  оборачивает путь в одинарные кавычки.  В этом
 *  случае кавычки удаляются перед сохранением имени.
 */
int read_input_list(char ***out_files, int max_cnt, int *count)
{
    char line[1024];
    char **files = malloc(sizeof(char*) * max_cnt);
    if (!files) return -1;
    
    int idx = 0;

    printf("Enter file names (you can drag & drop files). Finish with empty line:\n");

    while (idx < max_cnt) {
        printf("File %d: ", idx + 1);
        fflush(stdout);  // Важно: сбросить буфер вывода
        
        if (!fgets(line, sizeof(line), stdin)) {
            // Если EOF (Ctrl+D), выходим
            break;
        }

        /* Убираем завершающий '\n' */
        line[strcspn(line, "\r\n")] = '\0';

        /* Пустая строка – конец списка */
        if (line[0] == '\0') {
            break;
        }

        /* Отмена ввода */
        if ((strcmp(line, "c") == 0) || (strcmp(line, "C") == 0)) {
            for (int i = 0; i < idx; i++) free(files[i]);
            free(files);
            return -1;
        }

        /* Обрабатываем путь - убираем кавычки и экранирование */
        char processed_path[1024];
        if (!process_input_path(line, processed_path, sizeof(processed_path))) {
            printf("Error processing path\n");
            continue;
        }

        /* Проверяем существование файла */
        struct stat st;
        if (stat(processed_path, &st) != 0) {
            printf("File not found: '%s' (error: %s)\n", 
                   processed_path, strerror(errno));
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            printf("Not a regular file: %s\n", processed_path);
            continue;
        }

        /* Выделяем память под имя файла */
        char *fname = strdup(processed_path);
        if (!fname) {
            for (int i = 0; i < idx; i++) free(files[i]);
            free(files);
            return -1;
        }

        files[idx++] = fname;
        printf("✓ Added: %s\n", processed_path);
    }

    *out_files = files;
    *count = idx;
    
    if (idx > 0) {
        printf("\nSuccessfully added %d file(s)\n", idx);
    }
    
    return 0;
}
/* ---------- основная логика меню ---------- */

int run_menu(ConvertOptions* opts, const char*** files_ptr, int* file_count)
{
    int step   = 1;   /* текущий шаг */
    int codec  = 1;   /* 1 – copy (default) */
    int profile= 2;   /* 2 – standard (default) */
    int deblock= 1;   /* 1 – none (default) */
    int audio_norm = 3;/* 3 – peak 2-pass (default) */
    int genre = 1;     /* 1 – EDM (default) */
    int overwrite = 0; /* 0 – no */
    char output_dir[PATH_MAX];
    output_dir[0] = '\0';
    int output_dir_status = 0;
    char **temp_files = NULL;
    int temp_file_count = 0;
    int result = -1;  /* результат по умолчанию - ошибка */

    while (step != 10 && step != 0) {
        switch (step) {
            case 1:   /* выбор кодека */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                printf("select codec\n");
                printf("----------------------\n");
                printf("  1. copy (default)\n");
                printf("  2. prores\n");
                printf("  3. prores_ks\n");
                printf("----------------------\n");
                printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
                {
                    int ch = read_choice();
                    if (ch == '\n') { step = 4; }
                    else if (ch == '1') { codec = 1; step = 4; }
                    else if (ch == '2') { codec = 2; step = 2; }
                    else if (ch == '3') { codec = 3; step = 2; }
                    else if (ch == 'c' || ch == 'C') { 
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1; 
                    }
                    else if (ch == 'b' || ch == 'B') { step = 1; }
                    else { printf("Invalid choice\n"); }
                }
                break;

            case 2:   /* выбор профиля */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                printf("select profile\n");
                printf("-----------------------\n");
                printf("  1. lt\n");
                printf("  2. standard (default)\n");
                printf("  3. hq\n");
                printf("  4. 4444\n");
                printf("-----------------------\n");
                printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
                {
                    int ch = read_choice();
                    if (ch == '\n') { profile = 2; step = 3; }
                    else if (ch == '1') { profile = 1; step = 3; }
                    else if (ch == '2') { profile = 2; step = 3; }
                    else if (ch == '3') { profile = 3; step = 3; }
                    else if (ch == '4') { profile = 4; step = 3; }
                    else if (ch == 'c' || ch == 'C') { 
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1; 
                    }
                    else if (ch == 'b' || ch == 'B') { step = 1; }
                    else { printf("Invalid choice\n"); }
                }
                break;

            case 3:   /* выбор deblock */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                printf("select deblock\n");
                printf("---------------------------\n");
                printf("  1. none (default)\n");
                printf("  2. weak (4K content)\n");
                printf("  3. strong (1080p content)\n");
                printf("---------------------------\n");
                printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
                {
                    int ch = read_choice();
                    if (ch == '\n') { deblock = 1; step = 4; }
                    else if (ch == '1') { deblock = 1; step = 4; }
                    else if (ch == '2') { deblock = 2; step = 4; }
                    else if (ch == '3') { deblock = 3; step = 4; }
                    else if (ch == 'c' || ch == 'C') { 
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1; 
                    }
                    else if (ch == 'b' || ch == 'B') { step = 2; }
                    else { printf("Invalid choice\n"); }
                }
                break;

            case 4:   /* выбор аудио‑нормализации */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                printf("select audio normalization\n");
                printf("---------------------------------\n");
                printf("  1. none\n");
                printf("  2. peak\n");
                printf("  3. peak 2-pass (default)\n");
                printf("  4. loudness normalization\n");
                printf("  5. loudness normalization 2-pass\n");
                printf("---------------------------------\n");
                printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
                {
                    int ch = read_choice();
                    if (ch == '\n') { audio_norm = 3; step = 6; }
                    else if (ch == '1') { audio_norm = 1; step = 6; }
                    else if (ch == '2') { audio_norm = 2; step = 6; }
                    else if (ch == '3') { audio_norm = 3; step = 6; }
                    else if (ch == '4') { audio_norm = 4; step = 6; }
                    else if (ch == '5') { audio_norm = 5; step = 5; } /* переход к жанру */
                    else if (ch == 'c' || ch == 'C') { 
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1; 
                    }
                    else if (ch == 'b' || ch == 'B') { step = 3; }
                    else { printf("Invalid choice\n"); }
                }
                break;

            case 5:   /* выбор жанра */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                printf("select audio normalization genre\n");
                printf("---------------------------------\n");
                printf("  1. EDM (default)\n");
                printf("  2. Rock\n");
                printf("  3. HipHop\n");
                printf("  4. Classical\n");
                printf("  5. Podcast\n");
                printf("---------------------------------\n");
                printf("select: number->choice,Enter->(default),c->cancel,b->back\n>");
                {
                    int ch = read_choice();
                    if (ch == '\n') { genre = 1; step = 6; }
                    else if (ch == '1') { genre = 1; step = 6; }
                    else if (ch == '2') { genre = 2; step = 6; }
                    else if (ch == '3') { genre = 3; step = 6; }
                    else if (ch == '4') { genre = 4; step = 6; }
                    else if (ch == '5') { genre = 5; step = 6; }
                    else if (ch == 'c' || ch == 'C') { 
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1; 
                    }
                    else if (ch == 'b' || ch == 'B') { step = 4; }
                    else { printf("Invalid choice\n"); }
                }
                break;

            case 6:   /* overwrite */
                printf("\nchoice if overwrite files: yes/No\n");
                printf("select:y/n,Enter->(default),c->cancel,b->back\n>");
                {
                    int ch = read_choice();
                    if (ch == '\n') { overwrite = 0; step = 7; }
                    else if (ch == 'y' || ch == 'Y') { overwrite = 1; step = 7; }
                    else if (ch == 'n' || ch == 'N') { overwrite = 0; step = 7; }
                    else if (ch == 'c' || ch == 'C') { 
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1; 
                    }
                    else if (ch == 'b' || ch == 'B') { step = 5; }
                    else { printf("Invalid choice\n"); }
                }
                break;
                
            case 7:   /* Select Output folder */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                {
                    if (read_output_dir(output_dir, sizeof(output_dir), &output_dir_status) == 0) {
                        step = 8;
                    } else {
                        step = 0;          // пользователь отменил / ошибка
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1;
                    }
                }
                break;
                
            case 8:   /* Select input files string */
                clear_screen();
                printf("----ffmpeg_converter_simple_gui----\n\n");
                {
                    const int MAX_FILES = 128;
                    int file_cnt = 0;
                    char **file_list = NULL;

                    if (read_input_list(&file_list, MAX_FILES, &file_cnt) == 0) {
                        temp_files = file_list;
                        temp_file_count = file_cnt;
                        step = 9;
                    } else {
                        step = 0;
                        if (temp_files) {
                            for (int i = 0; i < temp_file_count; i++) free(temp_files[i]);
                            free(temp_files);
                        }
                        return -1;
                    }
                }
                break;
                
            case 9:   /* завершение */
                {
                    // Устанавливаем опции
                    switch (codec) {
                        case 1: strcpy(opts->codec, "copy"); break;
                        case 2: strcpy(opts->codec, "prores"); break;
                        case 3: strcpy(opts->codec, "prores_ks"); break;
                    }
                    opts->profile   = profile;
                    opts->deblock   = deblock;
                    switch (audio_norm) {
                        case 1: strcpy(opts->audio_norm, "none"); break;
                        case 2: strcpy(opts->audio_norm, "peak_norm"); break;
                        case 3: strcpy(opts->audio_norm, "peak_norm_2pass"); break;
                        case 4: strcpy(opts->audio_norm, "loudness_norm"); break;
                        case 5: strcpy(opts->audio_norm, "loudness_norm_2pass"); break;
                    } 
                    opts->genre     = genre;
                    opts->overwrite = overwrite;
                    strncpy(opts->output_dir, output_dir, sizeof(opts->output_dir)-1);
                    opts->output_dir_status = output_dir_status;
                    
                    // Передаем файлы
                    *files_ptr = (const char**)temp_files;
                    *file_count = temp_file_count;
                    
                    result = 0;  // Успех
                    step = 10;
                }
                break;
        }
    }
    
    // Если произошла ошибка до завершения, очищаем память
    if (result < 0 && temp_files) {
        for (int i = 0; i < temp_file_count; i++) {
            free(temp_files[i]);
        }
        free(temp_files);
    }
    
    return result;
}

/* Проверка всех файлов перед началом конвертации */
static int verify_all_files(const char** files, int file_count)
{
    int valid_files = 0;
    
    printf("\nVerifying files...\n");
    
    for (int i = 0; i < file_count; i++) {
        struct stat st;
        
        if (stat(files[i], &st) != 0) {
            printf("  ❌ File not found: %s\n", files[i]);
            printf("      Error: %s\n", strerror(errno));
        } else if (!S_ISREG(st.st_mode)) {
            printf("  ❌ Not a regular file: %s\n", files[i]);
        } else if (access(files[i], R_OK) != 0) {
            printf("  ❌ File not readable: %s\n", files[i]);
        } else {
            printf("  ✓ OK: %s\n", files[i]);
            valid_files++;
        }
    }
    
    printf("\nFound %d valid file(s) out of %d\n", valid_files, file_count);
    
    if (valid_files == 0) {
        printf("No valid files to process.\n");
        return 0;
    }
    
    if (valid_files < file_count) {
        printf("Continue with %d file(s)? [y/N]: ", valid_files);
        int ch = getchar();
        while (getchar() != '\n'); // Очищаем буфер ввода
        
        if (ch != 'y' && ch != 'Y') {
            return 0;
        }
    }
    
    return valid_files;
}

// ------------------------------------------------------------
//  main
// ------------------------------------------------------------

int main(int argc, char** argv) {
    const char** files = NULL;
    int file_count = 0;
    Converter* c = NULL;
    int result = 0;

    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        print_usage();
        return 0;
    }

    c = converter_create();
    if (!c) {
        printf("Failed to create converter.\n");
        return 1;
    }

    ConverterCallbacks cb = {
        .on_file_begin         = cli_on_file_begin,
        .on_file_end           = cli_on_file_end,
        .on_stage              = cli_on_stage,
        .on_progress_encode    = cli_on_progress_encode,
        .on_progress_analysis  = cli_on_progress_analysis,
        .on_message            = cli_on_message,
        .on_error              = cli_on_error,
        .on_complete           = cli_on_complete
    };

    converter_set_callbacks(c, &cb);

    ConvertOptions opts;
    int menu_result = 0;
  
    if (argc == 1) {
        // Интерактивный режим
        menu_result = run_menu(&opts, &files, &file_count);
        if (menu_result < 0) {
            printf("Menu cancelled by user.\n");
            result = 1;
            goto cleanup;
        } else if (menu_result == 0 && file_count == 0) {
            printf("No files selected.\n");
            result = 1;
            goto cleanup;
        }
    } else {  
        // Режим командной строки
        const char* arg_files[BUFFER_SIZE];
        if (!parse_args(argc, argv, &opts, arg_files, &file_count)) {
            printf("Invalid options. Try again\n");
            result = 1;
            goto cleanup;
        }
        files = arg_files;
    }

    if (file_count == 0) {
        print_usage();
        result = 1;
        goto cleanup;
    }

    print_summary(&opts, files, file_count);

    // После print_summary, перед converter_process_files:
int valid_files = verify_all_files(files, file_count);
if (valid_files == 0) {
    result = 1;
    goto cleanup;
}

// Обновляем количество файлов, если некоторые были невалидны
if (valid_files < file_count) {
    printf("Will process %d valid file(s)\n", valid_files);
    file_count = valid_files;
}

converter_set_options(c, &opts);
ConverterError err = converter_process_files(c, files, file_count);
  
    result = (err == ERR_OK ? 0 : 1);

cleanup:
    // Очистка памяти, если использовалось меню
    if (argc == 1 && files) {
        for (int i = 0; i < file_count; i++) {
            free((void*)files[i]);
        }
        free((void*)files);
    }
    
    if (c) {
        converter_destroy(c);
    }

    return result;
}
