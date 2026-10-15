# Phase 3.2: локальная проверка macOS, 2026-10-15

## Вердикт и границы проверки

**Phase 3.2 не завершена.** Чистая сборка успешна, оба полных запуска
доступного CTest-набора успешны (5/5), но производственные маршруты имеют
воспроизводимые ошибки, которые эти тесты не обнаруживают.

Проверен HEAD `6122aa3`. Изучены исходная реализация policy (`8a423dd`),
follow-up `723d13f`, `df96bba`, `d9d5cc4`, `f4b3fe8`, `654db24`,
`34273e1`, `c1b6d16`, `4b8f93b` и соответствующие тесты. Нормативный
`docs/v3.0-Phase3.2.md` по-прежнему имеет статус **Draft Specification**.
Предыдущие отчеты не использованы как доказательство работоспособности.

Функциональные исходники, CMake и Pascal не изменялись. Исправлений для запуска
штатной тестовой инфраструктуры не понадобилось. При начале работы уже был
изменен `.github/copilot-instructions.md`; этот файл не редактировался.
Единственное добавление вне игнорируемого `build/` — настоящий отчет.

Доказательства:

```text
R = /Users/viktorrazumovsky/Git/ffmpeg-converter
B = R/build/phase32-local-20261015
E = R/build/phase32-evidence-20261015
F = E/fixtures
C = B/bin/ffmpeg_converter
```

Все сокращения путей ниже раскрываются через эти определения.
В `E/scenarios/<scenario>/` сохранены `command.txt`, `exit.json`, `stdout`,
`stderr`, независимые `manual-probe-*/` и `trace/`. В trace каждый вызов имеет
полный argv, cwd, время, exit code, stdout и stderr, а также снимки media-файлов
на стадиях обработки. `failure-diagnostics.json` объединяет полные логи
основных MOV/M4V падений; `mov-m4v-diagnostic-table.txt` содержит последовательности
команд и путей. Эти локальные артефакты не предназначены для коммита.

Для CLI-трассировки использованы прозрачные обертки через уже существующие
переменные окружения инструментов: они вызывают настоящие бинарники с тем же
argv, сохраняют и передают их stdout/stderr и код возврата. Производственные
маршруты, selections и Policy не заменялись. GUI запускался штатным app binary,
без этих оберток, и управлялся через Accessibility/System Events. Ошибки
MOV/M4V подтверждены также этим независимым GUI-запуском.

Автоматизация UI сама по себе возвращает 0 и при ошибке конвертации:
результаты ниже определены по журналу приложения и отдельному ffprobe, а не
по exit code AppleScript. Аналогично `check_outputs.py` сохраняет результаты
всех assertions, а его exit 0 не означает отсутствие FAIL в JSON.

## A. Build and environment

### Среда

| Параметр | Фактическое значение |
|---|---|
| macOS | 12.7.6, build 21H1320 |
| Architecture | x86_64 |
| CPU | Intel Xeon E5-2697 v2, 2.70 GHz |
| C / Objective-C compiler | AppleClang 14.0.0, clang-1400.0.29.202, Xcode toolchain |
| CMake | 3.31.12 |
| Build Profile | macOS / FFmpeg 8.1, штатное значение CMake cache |
| FFmpeg | `R/src/platform/macos/bin/ffmpeg`, `32537a4-8.1`, собран clang 17.0.6 |
| ffprobe | `R/src/platform/macos/bin/ffprobe`, `32537a4-8.1` |
| Обязательные FFmpeg features | `--enable-libfdk-aac`, `--enable-libsoxr` присутствуют |
| mkvmerge | `/opt/local/bin/mkvmerge`, v102.0 “Little Houses” |
| MP4Box | `/opt/local/bin/MP4Box`, GPAC 26.07-revrelease |
| HQ runtime | подготовлен в `third_party/hq_converter/`, CMake feature enabled |
| GTK | установлен gtk4 4.22.4; штатные GTK targets на Darwin не создаются |
| Системный FFmpeg / ffprobe | `/opt/local/bin/{ffmpeg,ffprobe}`, 9.0.2, не использовались для производственной media-матрицы |

Полные версии и build configurations: `E/environment.txt`.
CLI использовал bundled 8.1; в app проверены bundled ffmpeg/ffprobe/MP4Box и
`mkvmerge --version`. GUI берет их из `Contents/Resources/bin/`.
Сборка упаковала зависимости MP4Box и mkvmerge.

Отсутствующих **обязательных верхнеуровневых runtime dependencies** не найдено.
Это не означает исправность всех encoders: VideoToolbox-пробы на этой машине
отказывают (`-12903`, `-12908`, `-12905`); соответствующие аппаратные
encoders не доступны. HQ SVT binaries существуют и являются x86_64 Mach-O,
но реальные задания SVT не создают video track. Причина внутри внешнего
runtime не установлена; нельзя записывать ее как доказанную “missing dependency”.
Ничего не устанавливалось и не обновлялось.

### Команды и результаты

Использована штатная CMake-конфигурация из корня и macOS workflow, но новый,
ранее не существовавший build directory:

```sh
cmake -S . -B build/phase32-local-20261015 -DCMAKE_PREFIX_PATH=/opt/local
cmake --build build/phase32-local-20261015 --parallel 4
ctest --test-dir build/phase32-local-20261015 --show-only=json-v1
ctest --test-dir build/phase32-local-20261015 --output-on-failure -V \
  --output-junit "$E/ctest.xml"
```

`--build` без ограничения target собрал CLI, Cocoa GUI, helper `hq_worker`,
все библиотеки и все зарегистрированные C-тесты из чистого дерева.

| Операция | Результат |
|---|---|
| Чистая конфигурация | PASS, exit 0 |
| Полная чистая сборка | PASS, exit 0, stderr пуст |
| `macos_cli` | PASS, `B/bin/ffmpeg_converter` |
| `macos_gui_native` | PASS, `B/bin/ffmpeg_converter_gui_macos.app` |
| Все доступные C-тесты | PASS, построены 5 executables |
| CLI `--help`, `--codecs-list` | PASS как smoke-запуски |
| Запуск настоящего Cocoa GUI | PASS, окно и controls доступны через Accessibility |

Логи: `configure.{stdout,stderr,exit}`, `build.{stdout,stderr,exit}`,
`build-targets.txt`, `cli-help.*`, `cli-codecs.*`, `gui.*` в E.
Установка в существующую пользовательскую `.app` не выполнялась; проверялось
только новое приложение из B. После проверки запущенный экземпляр закрыт.

## B. Automated tests

Первый полный CTest: **5 PASS, 0 FAIL, 0 SKIPPED**, 8.91 s.
Поскольку direct mux test при отсутствии tool-adjacent binaries может взять
`ffmpeg` из PATH, весь набор дополнительно запущен с явно закрепленным 8.1:

```sh
FFMPEG="$R/src/platform/macos/bin/ffmpeg" \
FFPROBE="$R/src/platform/macos/bin/ffprobe" \
MKVMERGE=/opt/local/bin/mkvmerge MP4BOX=/opt/local/bin/MP4Box \
ctest --test-dir "$B" --output-on-failure -V \
  --output-junit "$E/ctest-ffmpeg81.xml"
```

Второй полный CTest: **5 PASS, 0 FAIL, 0 SKIPPED**, 3.84 s.
JUnit, полный verbose stdout/stderr и exit сохранены для обоих запусков.

| Тест | Статус на macOS | Что действительно проверено |
|---|---|---|
| `MuxPolicyTests` | PASS | Чистые функции Policy, flags, defaults, codec/container tuples; без media и UI |
| `SelectionCatalogTests` | PASS | Production selection catalog, группы/encoders/presets; без выполнения mux |
| `PresetLoaderTests` | PASS | 41 assertions preset loader |
| `PresetLoaderExtendedTests` | PASS | 21 assertions, malformed JSON, fallback, substitution, benchmark |
| `MuxReplacementIsolationTests` | PASS | Real-media direct `mux_run_postprocess*`, изоляция replacement metadata, MKV/MOV, subtitles, chapters |
| `LinuxPresetArgsTests` | NOT BUILT | Target объявлен только внутри Linux guard в `src/converter/CMakeLists.txt` |
| `InputVideoInfoTests` | NOT BUILT | Исходник есть, target ошибочно/намеренно Linux-only; на macOS не зарегистрирован |
| `CliM4VPolicyTests` | NOT BUILT | Linux guard в `src/cli/CMakeLists.txt`; production parsing с mocked availability, не end-to-end CLI |
| `SubtitlePreservationTests` | NOT BUILT | Linux guard; существующий real-media subtitle helper test не запускается на macOS |
| `GtkAudioIsolationTests` | NOT BUILT | GTK установлен, но `src/gui/` и target Linux-only; изменение gating вне этой проверки |
| `RuntimeProbeCatalogTests` | NOT BUILT | Linux-only |
| `test_runtime_capabilities` | NOT BUILT | Linux-only executable; не является macOS CTest |
| Отдельные macOS GUI/bridge/M4V Creator CTests | NOT PRESENT | Live GUI проверен отдельно, не следует приписывать это CTest |
| Отдельные CTest names `MUX-T001`–`MUX-T018` | NOT PRESENT | Нормативные номера встречаются внутри assertions; соответствие ниже |

Недоступные targets не обозначены PASS или SKIPPED. В CTest не было
инструментальных падений, требующих исправления инфраструктуры.

### Почему зеленый media-test не доказывает исправность Add Track

`src/mux/test_mux_replacement.c:250-318` создает source и replacement с
MPEG-4 video и PCM audio, затем напрямую вызывает `mux_run_postprocess()` /
`mux_run_postprocess_for_preset()`. Он **не вызывает** `add_track_execute()`,
не выполняет production audio-preparation и не проходит проверку replacement
codec. Для MOV test передает исходный MKV как intermediate и ожидает **два**
PCM audio streams, хотя production processed-audio Policy использует только
первую исходную дорожку. Это допустимый тест нижнего уровня, но не тест
производственного MOV/Add Track маршрута.

`src/cli/test_cli_m4v_policy.c` проверяет production parser, но mock-ит runtime
availability/filesystem/progress и не запускает конвертацию. В его audio/video
isolation matrix Copy отсутствует. `test_subtitle_preservation.c` проверяет
helper и собственную FFmpeg-команду, а не весь Add Track.

Дополнительно: helper `resolve()` в `test_mux_policy.c:15-23` задает
**linux/8.1** для большинства нормативных assertions, даже при запуске binary
на macOS. Цикл всех OS/version profiles проверяет M4V AAC tuple, а HQ/list
assertions также используют native default profile. Это не полная
нормативная матрица для каждого Build Profile.

### Точное соответствие MUX-T001–MUX-T018

`P` ниже — `src/mux/policy/test_mux_policy.c:test_normative_mux_matrix()`,
CTest `MuxPolicyTests`. `I` — `src/mux/test_mux_replacement.c`,
CTest `MuxReplacementIsolationTests`. Ссылки на P/I не означают отдельный
CTest для каждого нормативного номера.

| Requirement | Test name → source / assertions | Реально проверяемое поведение | Производственная проверка здесь |
|---|---|---|---|
| T001 Copy all audio | P → `test_normative_mux_matrix`; I → `verify_output` | P: ALL flag; I: 2 source audio tracks без replacement audio | PASS: `mux-mkv-copy`, обе AAC packet sequences идентичны source |
| T002 PCM first track | P → same function | Только FIRST flag, не orchestration | PASS: `mux-mkv-pcm`, 1 PCM, доминирует source 440 Hz, не 550/880/990 Hz |
| T003 AAC first track | P → same function | FIRST flag | PASS: `mux-mkv-aac`, 1 AAC, source 440 Hz |
| T004 AAC+AC3 first track | P → same function | FIRST flag; не проверяет реальный split | PASS: `mux-mkv-aac+ac3`, AAC+AC3, FFmpeg split от `0:a:0`, первый output 440 Hz |
| T005 MOV rejects invalid audio | P → same function | Resolver отвергает Copy | PASS: `reject-mov-aac`, production Add Track отвергает AAC, output отсутствует |
| T006 M4V rejects invalid audio | P → same function; `CliM4VPolicyTests:test_m4v_audio` NOT BUILT | Resolver отвергает PCM, parser test также содержит Copy | PASS: `reject-m4v-pcm`, ошибка до media execution |
| T007 M4V rejects ProRes replacement | P → same function | Resolver отвергает replacement ProRes | PASS: `reject-m4v-prores`, реальный probe ProRes и отказ Policy |
| T008 MOV accepts ProRes/H264/HEVC | P → same function | Три resolver tuples; I использует MPEG-4 и обходит production validation | PASS для трех реальных replacement codecs: `mux-mov-prores-replacement`, `mux-mov-pcm`, `mux-mov-hevc-replacement`; source H264 |
| T009 MKV accepts ProRes replacement | P → same function | Resolver tuple | PASS: `mux-mkv-prores-replacement`, валидный MKV/ProRes/PCM |
| T010 Copy ProRes → MOV | P → same function, extension assertion | Resolver и suffix helper, не production naming/probe | PASS: `copy-mixed`, `source-prores_converted.mov` |
| T011 Copy non-ProRes → MKV | P → same function | H264/HEVC resolver tuples | PASS: `copy-mixed`, H264/HEVC выходят в отдельные MKV |
| T012 preserve chapters | I → `verify_output` | 1 source chapter, source title, отсутствие replacement chapter | PASS на проверенных MKV/MOV/M4V outputs; direct I не доказывает orchestration |
| T013 preserve subtitles | I → `verify_output`, `verify_subtitle`; `SubtitlePreservationTests` NOT BUILT | Direct MKV/MOV subtitle payload; helper tests отдельно | PASS MKV/mov_text→SRT и C M4V; FAIL production MOV и GUI Creator: subtitle отсутствует |
| T014 HQ ProRes → MOV+PCM | P → same function | `resolve_hq(PRORES,NULL)` | PASS выбора и принудительного PCM в GUI при выбранном AAC; FAIL execution с AV1 source, `hq-prores-av1` |
| T015 HQ non-ProRes → MKV | P → same function | Только H264 HQ tuple, не HEVC/AV1 media | PASS x264/x265; FAIL локальные SVT jobs до handoff; AV1 дополнительно отвергается production codec parser |
| T016 GTK matches CLI | P assertion с меткой T016; `GtkAudioIsolationTests:test_mux_default` NOT BUILT | P проверяет пересечение Copy choices, но не GTK widget; GTK test проверяет M4V defaults/isolation | NOT BUILT для GTK, parity не доказана |
| T017 macOS matches CLI | P assertion с меткой T017 | Только список M4V choices/defaults; **никакого Cocoa-кода тест не вызывает** | FAIL общей parity: HQ ProRes choices, HQ preset reset, corrupt queue UI gating |
| T018 no MKV regressions | I → main assertion с меткой T018 | Direct MKV replacement succeeds, production preparation отсутствует | PASS generic MKV для 4 audio modes и ProRes replacement; всеобщая “no regression” не доказана, SVT baseline FAIL |

Локальная дополнительная проверка outputs (`E/output-assertions.json`):
27 PASS / 4 FAIL. Четыре FAIL — потеря subtitle и global comment в MOV,
потеря global comment в двух C M4V controls. Это не новые CTests.

## C. Functional matrix

### Fixtures и способы запуска

Fixtures сгенерированы bundled FFmpeg 8.1. Их полные команды и stdout/stderr
сохранены в `E/fixture-generation/`, генератор — `E/verify_media.py`.

- `source.mkv`: H264, 320×192, две AAC дорожки с частотами 440/550 Hz,
  source subtitle, source chapter, source title/comment, source attachment.
- `replacement.mkv`: H264, 352×192, audio 880/990 Hz, собственные subtitles,
  chapters, tags и attachment с отличимыми `replacement_` markers.
- `source-prores.mov`: ProRes + две PCM дорожки.
- `source-hevc.mkv`, `source-av1.mkv`: дополнительные codecs из source.
- `source-movtext.mp4`: H264/AAC и настоящий mov_text.
- `source-hq.mkv`: 32.25 s, достаточно для HQ `seg_min=30`; source и HQ x264
  result имеют одинаковые 736 video packets и duration 32.25 s.
- `source-av1-hq.mkv`: AV1 source достаточной длины для HQ.
- `corrupt.mkv`: читаемый обычный файл с текстом вместо media.

Общие CLI формы (конкретный argv каждого запуска сохранен отдельно):

```sh
"$C" -c hq_converter --encoder x264 --overwrite -o OUTPUT "$F/source-hq.mkv"
"$C" -c software --encoder prores_ks --preset hq --audio-output AUDIO \
  --overwrite -o OUTPUT "$F/source.mkv"
"$C" -c mux --encoder MODE --video-track REPLACEMENT --audio-output AUDIO \
  --overwrite -o OUTPUT SOURCE
```

GUI sequence: Choose output → Add files → выбрать группу/encoder/audio →
Add track для replacement modes → Start. При выборе Add Track сначала
выявлено отдельное UI gating issue: Start остается disabled до повторного
выбора audio. Для проверки исполнения после этого **штатно повторно выбран
audio mode**, без модификации приложения. Логи сохраняют disabled-состояние
перед этим действием.

### UI → Policy → Execution → Output

`4 audio` = PCM, Copy, AAC, AAC+AC3. `E/scenarios/NAME` обозначено как `S/NAME`.
GUI evidence — `E/ui-matrix.*`, `E/ui-copy-queue.*`, `E/ui-runs/NAME/`.

| Scenario | macOS UI | Policy | Execution path | Output / status | Evidence |
|---|---|---|---|---|---|
| HQ x264 → MKV | 4 audio; выбран PCM, preset fullhd | MKV/PCM | HQ worker → `.ts` video track → Add Track → MKV post-mux | PASS: H264+PCM, subtitle/attachment; ffprobe 0; GUI 4,693,592 bytes | S/hq-x264; ui-hq-x264.*; build/p32/u1 |
| HQ x265 → MKV | 4 audio; PCM, Medium | MKV/PCM | HQ → `x265.Medium_final.mkv` → Add Track | PASS: HEVC+PCM, subtitle/attachment; ffprobe 0; 4,600,293 bytes | S/hq-x265; ui-runs/ui-hq-x265; build/p32/u2 |
| HQ SVT-HEVC → MKV | 4 audio; PCM, balanced_4k | HEVC может идти в MKV | HQ сообщает `run stopped without ready video track`; Add Track не достигнут | FAIL baseline локально; output отсутствует | S/hq-svt-hevc/task-snapshots; ui-runs/ui-hq-svt-hevc |
| HQ SVT-AV1 → MKV | 4 audio; PCM, balanced | AV1 не представлен в `MuxVideoCodec`, parser вернет UNSUPPORTED | В этом запуске HQ останавливается раньше handoff; отдельный реальный AV1 replacement также отвергнут | FAIL baseline; output отсутствует; нельзя утверждать, что AV1 handoff проверен успешно | S/hq-svt-av1; S/mux-mkv-av1-replacement; ui-runs/ui-hq-svt-av1 |
| HQ ProRes, H264 source | Лишние Copy/AAC/AAC+AC3; в GUI выбран AAC | `resolve_hq` принудительно MOV+PCM | HQ ProRes video → Add Track → MOV | Выбор execution PASS: PCM, не AAC; файл ProRes/PCM valid, 14,386,618 bytes. Preservation не PASS: subtitle потерян | ui-runs/ui-hq-prores-normalized; build/p32/u10 |
| HQ ProRes, AV1 source | Те же лишние choices | MOV+PCM | HQ video готов; подготовка source в MOV падает до post-mux | FAIL: 0 bytes, ffprobe 1 | S/hq-prores-av1; ui-runs/ui-hq-prores-av1 |
| Software ProRes + PCM | PCM доступен | Отдельного Software-context в mux Policy нет | converter напрямую, без Add Track | PASS: MOV/ProRes/PCM, ffprobe 0 | S/software-prores-pcm |
| Software ProRes + Copy | **Лишний Copy** | Software path не запрашивает mux Policy | converter реально делает `-c:a copy`, ALL audio | UI FAIL; ожидание нормализации в PCM **не подтверждено**: валидный MOV с двумя AAC, не PCM | S/software-prores-copy; ui-runs/ui-software-copy; build/p32/u6 |
| Software ProRes + AAC | AAC доступен | Software-context отсутствует | converter, audio `aac_at` | PASS: valid MOV/ProRes/AAC | S/software-prores-aac |
| Software ProRes + AAC+AC3 | dual доступен | Software-context отсутствует | converter, split первой audio | PASS: valid MOV/ProRes/AAC+AC3 | S/software-prores-aac+ac3 |
| Mux MKV + Copy | 4 audio | MKV, ALL source audio | Add Track → source preparation → mkvmerge | PASS: 2 source AAC, без replacement audio; packet hashes совпадают | S/mux-mkv-copy |
| Mux MKV + PCM | 4 audio | MKV, FIRST | Тот же общий путь | PASS: 1 PCM, source 440 Hz | S/mux-mkv-pcm |
| Mux MKV + AAC | 4 audio | MKV, FIRST | Тот же общий путь | PASS: 1 AAC, source 440 Hz | S/mux-mkv-aac |
| Mux MKV + AAC+AC3 | 4 audio | MKV, FIRST → dual | Тот же общий путь | PASS: AAC+AC3, источник первая audio | S/mux-mkv-aac+ac3 |
| Mux MOV + PCM, H264 source | Только PCM | Только PCM | preparation MOV → mkvmerge temp MKV → ffmpeg MOV remux | Файл valid, H264/PCM, 187,954 bytes; **FAIL preservation**: нет subtitle/comment | S/mux-mov-pcm |
| Mux MOV + PCM, AV1 source | Только PCM; Add Track требует дополнительного refresh | MOV+PCM с H264 replacement разрешен | preparation ошибочно копирует source AV1 в MOV | FAIL: empty `.mov`, attachment verification ffprobe 1 | S/mux-mov-av1-pcm; ui-runs/ui-mux-mov-av1 |
| Mux M4V + AAC, H264 source | AAC / AAC+AC3 | AAC explicit сохранен | preparation M4V → mkvmerge MKV → C M4V Creator | valid H264/AAC/mov_text; 132,525 bytes; comment потерян | S/mux-m4v-aac |
| Mux M4V + AAC+AC3, H264 source | Те же 2 choices | AAC+AC3 | Тот же путь | valid H264/AAC+AC3, 220,621 bytes на пути без `+` | S/mux-m4v-dual |
| Mux M4V без audio-output | В UI первый item AAC+AC3 | Default AAC+AC3 | Две audio дорожки, C Creator реально достигнут | Default PASS; valid файл 220,621 bytes, comment потерян | S/mux-m4v-default; ui-matrix.* |
| Mux M4V + AAC / dual / default, ProRes source | Choices корректны | H264 replacement и AAC/dual разрешены | preparation пытается сохранить source ProRes в ipod/M4V; затем mkvmerge получает empty `.m4v` | FAIL во всех 3 случаях | S/mux-m4v-prores-*; ui-runs/ui-mux-m4v-prores |
| Copy mixed H264/ProRes/HEVC + corrupt | Без corrupt: только PCM. С corrupt: Start disabled, choices остаются stale/лишними | Пересечение валидных источников PCM; execution per file | CLI обрабатывает valid → corrupt failure → valid → valid | CLI PASS продолжения: MKV/MOV/MKV valid; UI FAIL доступности очереди | S/copy-mixed; ui-copy-queue.* |
| M4V Creator, GUI button, H264/mov_text input | Отдельный options dialog, по умолчанию dual | Mux Policy не вызывается | Native Objective-C AppleM4VCreator, не `src/m4v/m4v.c` | valid H264/AAC+AC3, 198,153 bytes, но source subtitle потерян | ui-runs/ui-creator; build/p32/u9 |

Отдельная path-sensitive ошибка: `S/mux-m4v-aac+ac3` с `+` в имени output
directory падает в MP4Box: `Requested URL is not valid or cannot be found`.
Тот же explicit audio mode в `S/mux-m4v-dual` без `+` проходит. Это **другой**
дефект, не доказательство неверного M4V audio default и не тот же routing failure.

### Software / HQ video-setting isolation

В настоящем UI Software `prores_ks / hq / deblock strong` сохранены при
последовательном PCM → Copy → AAC → AAC+AC3; codec, encoder, preset, filter,
filter preset и deblock не сбросились. Отдельных controls quality/tune этот
Cocoa UI не предоставляет; их сохранение как пользовательских настроек нельзя
объявлять проверенным. CLI isolation unit с такими assertions на macOS не built.

В HQ x265 выбран `Slow`, затем audio изменен на PCM: preset сбросился на
`Medium`. Это воспроизведено в UI, а не выведено только из исходников.
`populatePresetPopup` в HQ-ветке всегда выбирает item 0
(`src/gui_macos_native/main.m:1067-1080`), а audio callback вызывает эту
перестройку через `updateDependentControls`.

### Общий MKV: проверка происхождения потоков

Для четырех audio modes отдельно проверены реальные результаты:

- Все video packet SHA256 совпадают с replacement, width 352, video не
  перекодирован.
- Copy сохраняет обе source AAC дорожки с неизменными packet hashes.
- PCM/AAC/dual используют первую source audio: отдельные tone markers
  отличают ее от второй source и обеих replacement audio.
- Source subtitle payload извлечен и содержит `source_SUBTITLE`, не replacement.
- Source chapter/title/comment/attachment сохранены.
- Replacement subtitles/chapters/attachments/global tags/video title и markers
  не попадают в probe результата.
- `source-movtext.mp4 → Mux MKV` дает `subrip`, а не несовместимый raw mov_text;
  файл открывается ffprobe.

Флаги mkvmerge для replacement: `--no-audio --no-subtitles --no-attachments
--no-chapters --no-global-tags --no-track-tags --no-buttons`, source передается
с `--no-video`. Metadata не объявляется проверенной только по этим flags:
выполнена отдельная проверка output JSON и subtitle payload.

## D. Confirmed failures and regressions

Не все локальные FAIL можно доказанно приписать конкретному Phase 3.2 commit.
Ниже разделены подтвержденные ошибки текущего HEAD и неподтвержденная
атрибуция внешнего HQ runtime. Функциональных исправлений после обнаружения
baseline failures не выполнялось.

### D1. Общий MOV defect: локализация, не HQ-specific

Точные воспроизводящие команды: `S/mux-mov-av1-pcm/command.txt` и
`S/hq-prores-av1/command.txt`. Оба также воспроизведены в GUI.

| Роль | Mux MOV + PCM | HQ ProRes → MOV + PCM |
|---|---|---|
| source | `F/source-av1.mkv` | `F/source-av1-hq.mkv` |
| replacement | `F/replacement.mkv` (H264) | `R/build/p32/v/task_1/ffmpeg-prores_ks.hq_final.mov` |
| intermediate | `S/mux-mov-av1-pcm/output/source-av1_converted.mov` | `R/build/p32/v/source-av1-hq_converted.mov` |
| final | Тот же путь, что intermediate | Тот же путь, что intermediate |
| `.mov.mux_tmp.mkv` | Не создан: до mkvmerge не дошло | Не создан |
| final file exists / size | Да / 0 bytes | Да / 0 bytes |
| CLI exit | 1 | 1 |
| source preparation FFmpeg exit | 234 | 234 |
| attachment ffprobe exit | 1 | 1 |
| отдельный ручной ffprobe | exit 1, файл не открывается | exit 1, файл не открывается |

Первичная ошибка FFmpeg:

```text
[mov @ ...] av1 only supported in MP4 and AVIF.
Could not write header (incorrect codec parameters ?): Invalid argument
Nothing was written into output file, because at least one of its streams received no packets.
```

`add_track_execute()` задает `work_opts.codec="copy"` и
`work_opts.preset=policy.mode` (`src/add_track/add_track.c:138-151`).
Это **подготовка исходного видео**, не replacement. При policy MOV она
пытается скопировать source AV1 в MOV, хотя готовый replacement H264/ProRes
в MOV допустим. `converter_process_files()` сообщает per-file failure через
callbacks, но возвращает `ERR_OK` для завершенной очереди
(`src/converter/converter.c:1251-1274`), после чего Add Track продолжает.

`mux_run_postprocess_for_preset()` вызывает attachment probe на
`opts->intermediate_file` (`src/mux/mux.c:628-635`). Его точный argv:

```sh
"$R/src/platform/macos/bin/ffprobe" -v error \
  -show_entries stream=codec_type -of json INTERMEDIATE
```

Полный ffprobe stderr для первого запуска:

```text
[mov,mp4,m4a,3gp,3g2,mj2 @ 0x7ff05bf05040] moov atom not found
/Users/viktorrazumovsky/Git/ffmpeg-converter/build/phase32-evidence-20261015/scenarios/mux-mov-av1-pcm/output/source-av1_converted.mov: Invalid data found when processing input
```

Полный ffprobe stderr для HQ:

```text
[mov,mp4,m4a,3gp,3g2,mj2 @ 0x7fb729705040] moov atom not found
/Users/viktorrazumovsky/Git/ffmpeg-converter/build/p32/v/source-av1-hq_converted.mov: Invalid data found when processing input
```

**Категория E: первичный сбой подготовки intermediate с пустым файловым
артефактом; post-mux лишь обнаруживает последствие.** Это не исправный MOV,
проверяемый по другому пути, и не MKV-specific attachment verification на
исправном MOV. Probe проверяет фактический intermediate; проблема в его
создании и ошибочно продолженном execution.

В обоих падающих запусках **команды финального MOV remux нет**: функция
возвращается до нее. Контрольный `S/mux-mov-pcm` с H264 source действительно
выполняет:

```sh
ffmpeg -y -nostdin -i FINAL.mov.mux_tmp.mkv \
  -map 0:v:0 -map '0:a?' -map_metadata 0 -map_chapters 0 \
  -c copy -movflags use_metadata_tags -f mov FINAL.mov
```

Его точный argv с абсолютными путями — в trace и
`mov-m4v-diagnostic-table.txt`. Этот результат valid, но теряет subtitles и
source global comment. Следовательно “MOV исправлен, потому что создается”
было бы также неверным выводом.

### D2. Общий M4V routing defect

Все три команды сохранены в:

```text
S/mux-m4v-prores-aac/command.txt
S/mux-m4v-prores-aac+ac3/command.txt
S/mux-m4v-prores-default/command.txt
```

Общие роли: source=`F/source-prores.mov`,
replacement=`F/replacement.mkv` (H264).
Для каждого scenario intermediate и final имеют один путь
`S/SCENARIO/output/source-prores_converted.m4v`.

Ожидание: source video не обязан соответствовать final container, поскольку
в final используется replacement H264; аудио AAC / AAC+AC3 по Policy,
финальный валидный M4V. Фактически все три запуска exit 1, final artifact
существует, **0 bytes**.

Реальный порядок:

1. Probe replacement → H264; Policy разрешает requested audio, default
   нормализуется именно в AAC+AC3.
2. Converter готовит source с `-c:v copy` в `.m4v` (FFmpeg ipod muxer).
   Он отказывает: `Could not find tag for codec prores in stream #0,
   codec not currently supported in container`, exit **234**.
3. Queue-функция возвращает успех завершения; Add Track продолжает.
4. Attachment ffprobe пустого `.m4v` возвращает **0**, stderr пуст,
   фактического валидного H264/HEVC+audio результата нет. Отдельный ручной
   ffprobe тоже возвращает 0 и эвристически видит raw MPEG-4 video без
   необходимых stream parameters. Это важный пример, почему одного exit 0
   недостаточно для validity.
5. Source metadata probe того же empty `.m4v`.
6. mkvmerge получает replacement и `--no-video INTERMEDIATE.m4v`, exit **2**.
7. **C M4V Creator / MP4Box не вызваны ни разу.** Финальный M4V не создан.

Общий вид фактического mkvmerge:

```sh
mkvmerge -o FINAL.m4v.mux_tmp.mkv.postmux.PID.TOKEN.0.tmp.mkv \
  --title '' --no-audio --no-subtitles --no-attachments --no-chapters \
  --no-global-tags --no-track-tags --no-buttons --language 0:und \
  --track-name 0: --video-tracks 0 REPLACEMENT \
  --no-video FINAL.m4v
```

Полный stdout/stderr каждого вызова сохранен в trace. Диагностическое сообщение:

```text
Error: The type of file '.../source-prores_converted.m4v' could not be recognized.
post-mux failed: mkvmerge returned error
```

**Подтверждение с уточнением:** mkvmerge действительно получает путь с final
`.m4v`, но не потому, что уже готовый результат Creator передается назад.
Creator еще не запускался. Перепутаны **контейнер/имя промежуточной подготовки
и контейнер/имя конечного результата** в Add Track work options. В самой
команде mux.c переменная intermediate используется как intermediate.
Сам suffix `.m4v` не запрещен mkvmerge: на H264 source соответствующий
контроль проходит. Ошибка — раннее применение final container к source,
которое затем маскируется продолжением после подготовки с ошибкой.

### D3. Прочие подтвержденные ошибки / ограничения

| Сценарий / ожидается | Фактически / stderr или журнал | Scope / точка отказа |
|---|---|---|
| HQ SVT-HEVC, SVT-AV1 должны повторить MKV baseline | CLI и GUI FAIL: `run stopped without ready video track`; session output states `absent`; ни Add Track, ни post-mux не достигнуты | Внешний prepared HQ runtime; run report `terminal=fail`, `tasks=0`. Низкоуровневый encoder stderr интерфейс не предоставил; точная причина не доказана |
| AV1 replacement для MKV/HQ non-ProRes | Реальный ffprobe читает `av1`, затем Add Track возвращает invalid options; output отсутствует | `mux_video_codec_parse`, `src/mux/policy/mux_policy.c:82-93`, не распознает AV1; `mux_probe_video_codec` возвращает ошибку до `resolve_hq` |
| Software ProRes не должен показывать Copy; ранее ожидалась PCM normalization | UI показывает Copy. В production реально `-c:a copy`, две AAC, valid MOV | UI bug подтвержден, но **core normalization в PCM отсутствует** на этом HEAD; converter non-Copy-video ветка не вызывает mux Policy |
| HQ ProRes UI только MOV+PCM | UI показывает все 4 audio; выбранный AAC принудительно заменяется PCM в Add Track | UI неверен; execution normalization в `mux_policy_resolve_hq` корректна |
| Audio change не должен менять HQ preset | x265 Slow → смена audio → Medium | `main.m:1067-1080`, unconditional item 0 при rebuild |
| Add Track должен сразу разблокировать Start | Track выбран, Start=false до смены audio | `main.m:922-936`, handler обновляет path/label, не вызывает `updateDependentControls` |
| Corrupt Copy item не должен блокировать всю UI-очередь | CLI продолжает остальные файлы; GUI Start=false и stale audio choices | `main.m:1121-1144`, probe failure превращается в invalid всей UI selection |
| MOV сохраняет поддерживаемые subtitles/comment | Source содержит SRT; valid final MOV не содержит subtitle и source comment | Подготовка в MOV до mkvmerge; direct mux test обходит этот шаг |
| C M4V сохраняет practical source metadata | Subtitle/chapter сохранены, `source_GLOBAL` отсутствует | Общая preparation → post-mux цепочка; не ошибка выбора AAC default |
| GUI M4V Creator сохраняет supported subtitle | Output valid H264/AAC+AC3, chapter присутствует, source mov_text отсутствует | Отдельная Objective-C pipeline, video copy с `-sn`, позднего subtitle preservation нет |
| Пути с `+` должны работать | MP4Box `Requested URL is not valid or cannot be found`; контроль без `+` работает | GPAC importer аргументы в `src/m4v/m4v.c:536-565`; отдельный path-sensitive defect |

Вспомогательные неуспешные попытки не смешаны с регрессиями:
1-second HQ fixture был отвергнут из-за штатного `seg_min=30`;
слишком длинный work path дал `runtime socket did not appear` (AF_UNIX
path length), после сокращения путей x264/x265/ProRes проходят.
Внешние runtime config не менялись.

При подготовке AV1 fixture bundled `libsvtav1` отдельно завершился SIGSEGV
(shell 139). AV1 fixture затем успешно создан через bundled `libaom-av1`.
Это реальный локальный encoder failure, но его тождество с отказом
standalone HQ SVT **не установлено**.

## E. Architecture verification

### Реальные маршруты

| Входная поверхность | Фактический маршрут |
|---|---|
| CLI Software / Cocoa Software | `converter_set_options` → `converter_process_files` → FFmpeg; Add Track/mux Policy не участвуют |
| Copy | `resolve_mux_policy_for_codec` без I/O → per-file `input_video_info_probe` → `mux_policy_resolve` → per-file naming/FFmpeg |
| Add Track / Mux MKV | probe replacement → `mux_policy_resolve` → converter source preparation → `mux_run_postprocess_for_preset` → `mux_run_postprocess` → mkvmerge |
| Mux MOV | Тот же путь → временный MKV → `mux_remux_container` / FFmpeg MOV, если подготовка не оборвалась |
| Mux M4V | Тот же путь → временный MKV → `m4v_create_from_input` / MP4Box, если подготовка не оборвалась |
| HQ | CLI `main.c:235-432` или bridge `converter_bridge.m:390-515` → isolated HQ worker → session `video_track.path` → **тот же Add Track** |
| C M4V Creator | `src/m4v/m4v.c:m4v_create_from_input`; для mux M4V реально используется; отдельная CLI direct branch существует, но старое `-c m4v` parser не принимает |
| macOS “Apple m4v creator” | `main.m:681-783` → bridge `startAppleM4VForFiles` → Objective-C `AppleM4VCreator`; не C M4V module, не shared mux Policy |

Старый CLI help рекламирует `-c m4v`, но `creator-direct` получает
`unavailable codec group/encoder: m4v/prores_ks`. `-c mux --encoder m4v`
без track тоже не Creator: он корректно требует replacement. CLI listing
также показывает legacy mux encoder names, тогда как live GUI показывает
`copy / mkv / mov / m4v`. Это разные проблемы exposure и выполнения.

### Где принимаются решения

| Правило | Существует в Policy | Применяется UI | Применяется execution | Проверяется тестом |
|---|---|---|---|---|
| Build profile platform/version | `mux_build_profile_default/resolve`, CMake definition | Да в mux choices | Да в Copy/Add Track/post-mux | P валидирует tuples; platform/version не меняют матрицу решений |
| MOV только PCM | `mux_policy_resolve` | Да для Mux MOV; **нет** для HQ ProRes | Да для Add Track | P PASS; real negative test PASS |
| M4V AAC/dual, default dual | Resolver / list helper | Да, default первый item | Да CLI, Add Track, C Creator options | P PASS; CLI parser test NOT BUILT; реальные outputs PASS default |
| HQ ProRes принудительно MOV/PCM | `mux_policy_resolve_hq` | **Нет**, UI использует MKV audio list | Да, проверено выбранным AAC → PCM | P плюс live normalization; AV1 source preparation FAIL |
| Copy container/extension per source | Resolver / extension helper | Пересечение choices для valid queue | Да, name и command path | P только helpers; real mixed CLI PASS |
| Software ProRes запрет Copy | **Нет Software-specific policy context** | Нет | Нет PCM normalization | Существующий CTest это не проверяет |
| Audio Copy ALL / processed FIRST | Policy flags | Choices косвенно | Converter mapping; direct mux сам audio не обрабатывает | P flags; real packet/tone checks; I не проверяет production preparation |
| Subtitles compatible per container | `mux_subtitle_policy_resolve` | Не отдельный выбор UI | Converter Copy, mux remux, C M4V helper; не ObjC Creator | P compatibility; I direct PASS; real MOV/ObjC FAIL |
| Chapters / metadata / attachments | Policy actions | UI не управляет | Copy mapping, post-mux remux; attachment preflight; C M4V final helper | I direct preservation; real MKV PASS, MOV/M4V metadata FAIL |
| Replacement только video | На уровне shared execution contract | Track path | mkvmerge exclusion flags | I direct и production packet/tag checks PASS MKV |

**Build Profile:** действительно выбирает compile-time platform/version и
передается Policy. Но platform/version сейчас только валидируются; различий
allowed decisions для 8.1/9.0 или OS в resolver нет. Availability остается
в runtime probes/catalog. Поэтому нельзя говорить, что одна структура Build
Profile уже выражает все платформенные ограничения приложения.

**Mux Policy:** авторитетна для tuples/defaults Copy/Add Track/HQ и
container-aware subtitle decisions. Однако еще не является единственным
источником всех правил:

- Software ProRes container выбирается `codec_uses_mov_container`, audio
  решения — audio orchestrator, без Software-context в mux Policy.
- macOS non-mux UI всегда просит `MUX_MODE_MKV` audio list
  (`main.m:1178-1203`), включая HQ ProRes. Это не второй полный resolver,
  но неверное применение контекста Policy, создающее лишние choices.
- HQ final naming по-прежнему зависит от `hq_catalog_is_prores_codec` и
  подстановки `final_opts.codec="prores_ks"` в Add Track, а не только policy
  container. Подготовка ошибочно использует final mode.
- CLI имеет собственное преобразование selection group/encoder в execution
  codec и специальные M4V defaults (defaults берутся из Policy).
- Direct `mux_run_postprocess` не проверяет policy tuple; он низкоуровневый
  MKV assembler. Именно его напрямую вызывает I.
- GUI M4V Creator обходит shared Policy/C M4V module и имеет собственную
  video whitelist, AAC selection и preservation behavior.

### Probes и инструменты

Полный текстовый индекс native callsites сохранен в
`E/production-callsite-index.txt`, selections/defaults — в
`E/selection-default-index.txt`. Значимые production места:

- `converter.c`: Copy preflight, attachment check, output naming, duration
  probe, stream mappings, финальная FFmpeg-команда.
- `input_video_info.c:153-201`: production codec/pixel/color/fps ffprobe.
- `subtitle_preservation.c:10-104`: ffprobe subtitle list →
  `mux_subtitle_policy_resolve` → explicit mappings.
- `mux.c:84-185`: FPS и replacement codec; `187-290`: source metadata и
  attachments; `293-337`: post-mux output validation; `395-524`: единственный
  общий mkvmerge assembler; `597-685`: preset routing / C M4V Creator.
- `m4v.c`: FPS/codec/color ffprobe, video/audio extraction, MP4Box,
  disposition/chapters, metadata/subtitle postprocessing.
- `converter/platform/converter_macos.c`: tool resolution и VideoToolbox
  source info; `platform/macos/runtime_probe.c`: runtime availability/tool
  discovery. В этой реализации getter environment приоритетнее bundled path;
  общий порядок из prose нельзя безусловно переносить на этот файл.
- `main.m:updateDependentControls`: UI Copy queue probe;
  `apple_m4v_creator.m:203-307`: собственные native Creator probes;
  bridge задает bundled tool environment.
- HQ adapter передает source во внешний prepared runtime; его внутренние
  tools не являются C mux implementation. Они не изменялись.

## F. Минимальный план исправлений — не выполнен

Порядок ниже сохраняет Build Profile → Mux Policy → общий Add Track/Mux;
не предусматривает Pascal, нового HQ-specific mux или нового execution path.

1. **M4V routing.** Отделить безопасный source-preparation intermediate от
   final container, используя существующий MKV preparation/post-mux маршрут.
   Не отправлять исходный ProRes в M4V до замены video. Учитывать результат
   однофайловой подготовки до post-mux, не ломая per-file continuation Copy.
   Файлы: `src/add_track/add_track.c`, `src/converter/converter.c`.
   Отдельно устранить подтвержденный GPAC path issue, не смешивая с routing:
   `src/m4v/m4v.c`.

2. **MOV post-mux and verification.** Применить тот же исправленный intermediate
   contract к MOV; проверять именно существующий валидный intermediate и не
   маскировать FFmpeg failure ошибкой attachment probe. После этого повторить
   source subtitle/comment preservation и финальную stream validation.
   Файлы: `src/add_track/add_track.c`, `src/converter/converter.c`,
   `src/mux/mux.c`. Не “чинить” проблему удалением проверки attachments.

3. **macOS UI restrictions.** Для HQ запрашивать choices с реальным
   policy context (ProRes → MOV/PCM). Для Software ProRes не использовать
   Mux MOV restrictions вслепую: это ошибочно убрало бы работающие AAC/dual.
   Сначала выразить недостающее ограничение Copy в общей Policy и применить
   его в UI, а не добавлять вторую таблицу в Cocoa. Сохранить HQ selected
   preset при audio updates, обновлять controls после Add Track и не превращать
   corrupt Copy item в глобальный UI отказ.
   Файлы: `src/mux/policy/mux_policy.h`, `src/mux/policy/mux_policy.c`,
   `src/gui_macos_native/main.m`.

4. **Отсутствующие/неверные тесты и AV1 coverage.** Расширить существующий
   real-media test production-вызовом Add Track; добавить AV1 source → MOV,
   ProRes source → M4V, audio first/all, failure propagation, MOV subtitles
   и metadata, mixed queue. Не выдавать direct mux checks за end-to-end.
   Сделать переносимые InputVideoInfo/subtitle/CLI tests доступными на macOS,
   отделив genuinely Linux-specific cases. AV1 parser/HQ resolver должен
   принимать AV1 для MKV и иметь отдельный тест (но не M4V/MOV whitelist).
   Файлы: `src/mux/test_mux_replacement.c`, `src/mux/CMakeLists.txt`,
   `src/mux/policy/mux_policy.h`, `src/mux/policy/mux_policy.c`,
   `src/mux/policy/test_mux_policy.c`, `src/converter/CMakeLists.txt`,
   `src/cli/CMakeLists.txt`, `src/cli/test_cli_m4v_policy.c`.

   Настоящий Cocoa regression test для choices/preset/Add Track refresh
   сейчас отсутствует: для него потребуется отдельный test source и регистрация
   в `src/gui_macos_native/CMakeLists.txt`; конкретное имя нового файла не
   объявляется уже существующим. Исправление standalone GUI Creator parity
   потребует отдельной оценки `src/gui_macos_native/apple_m4v_creator.m` и
   `src/gui_macos_native/converter_bridge.m`, а не скрытого включения в routing fix.

HQ SVT baseline остается отдельным блокером: нужны первичные encoder diagnostics
внешнего prepared release. Без них нельзя назвать минимальные файлы исправления
внешнего runtime или объявить текущий сбой доказанной регрессией C mux. До
выяснения и повторного PASS обоих SVT сценариев завершение Phase 3.2 не заявлять.

## Критерии проверки

| Критерий | Итог |
|---|---|
| Чистая локальная macOS сборка CLI/GUI/tests | Выполнено |
| Полный доступный CTest | Выполнено дважды, включая pinned FFmpeg 8.1 |
| Все четыре ранее подтвержденных HQ MKV сценария повторно запущены | Выполнено в CLI и GUI; x264/x265 PASS, SVT-HEVC/SVT-AV1 FAIL до handoff |
| MOV defect воспроизведен и локализован | Выполнено, общий Add Track preparation/error propagation |
| M4V defect воспроизведен и локализован | Выполнено, 3 audio variants + GUI |
| Software ProRes UI defect | Подтвержден; прежнее предположение о core PCM normalization опровергнуто |
| UI → Policy → Execution → Output matrix | Составлена, UI failures отделены от working normalization/output |
| MUX-T001–MUX-T018 → реальные тесты | Сопоставлены, пробелы и non-production paths явно указаны |
| Функциональные исправления до отчета | Не вносились |
| Pascal | Не изменялся |

**Итог:** первичная локальная проверка выполнена; положительного заключения
о завершении Phase 3.2 нет. Исправность четырех HQ baseline сценариев в полном
объеме не подтверждена. MOV/M4V root cause подтвержден на реальных production
routes; исправлять его следует отдельно после этого отчета.

## G. Follow-up: реализация и финальная локальная проверка macOS (2026-10-15)

Этот раздел дополняет исходный отчет после реализации исправлений. Он не
переписывает baseline выше и не трактует локальный результат как завершение
Phase 3.2 на всех платформах.

Tested source HEAD: `4e5b650` (verified code commit; documentation follows).

### Исправления

- Add Track готовит исходное аудио/метаданные в безопасном MKV intermediate,
  использует строгую однофайловую обработку и не запускает post-mux после
  ошибки подготовки или невалидного intermediate. Обычная обработка очереди
  сохраняет прежнее продолжение после ошибки отдельного файла.
- Mux проверяет подготовленный intermediate и финальный результат через
  ffprobe. AV1 replacement разрешен для MKV, но не для MOV/M4V; HQ `x264` и
  `x265` корректно классифицируются Policy.
- MP4Box получает безопасно staged importer inputs при путях с `+`, без
  изменения конечного output path.
- Native Cocoa применяет Policy к фактическому HQ encoder: ProRes фиксирует
  PCM и отключает audio selector; смена audio не сбрасывает выбранный HQ
  encoder/preset. После Add Track controls обновляются и Start остается
  выключенным для недопустимого replacement codec.
- Production Add Track тесты расширены на MOV/M4V, AV1, HQ handoff, ошибки и
  пути со специальными символами. Переносимые input-info, subtitle,
  Mux-policy и CLI M4V tests зарегистрированы для macOS.

### Сборка и автоматические тесты

- Clean configure/build: `build/phase32-final-clean-20261015` — успешно для
  CLI, native GUI и тестовых targets.
- Полный доступный CTest: **8/8** дважды — с pinned bundled FFmpeg 8.1 и без
  переопределения tool paths.
- Команды чистой сборки и двух CTest runs:

  ```sh
  cmake -S . -B build/phase32-final-clean-20261015 -DCMAKE_PREFIX_PATH=/opt/local
  cmake --build build/phase32-final-clean-20261015 --parallel 4
  ctest --test-dir build/phase32-final-clean-20261015 --output-on-failure

  FFMPEG="$PWD/src/platform/macos/bin/ffmpeg" \
  FFPROBE="$PWD/src/platform/macos/bin/ffprobe" \
  MKVMERGE=/opt/local/bin/mkvmerge \
  MP4BOX=/opt/local/bin/MP4Box \
  ctest --test-dir build/phase32-final-clean-20261015 --output-on-failure
  ```

- Среда: AppleClang 14.0.0, CMake 3.31.12, bundled FFmpeg/ffprobe 8.1,
  mkvmerge 102.0, GPAC/MP4Box 26.07.
- Не собраны в этой macOS-конфигурации: LinuxPresetArgsTests,
  RuntimeProbeCatalogTests и Linux-only runtime-capabilities executable —
  платформенно gated Linux tests; GtkAudioIsolationTests — Linux/GTK-only.
  Отдельного Cocoa CTest target в проекте нет; вместо него выполнена
  детерминированная local GUI procedure ниже с сохраненными logs/outputs.
  Pascal test sources отсутствуют, Pascal/Windows tests вне scope.

### Production CLI и Cocoa

- CLI matrix: **20/20** — 16 успешных production workflows и 4 ожидаемых
  ранних отказа. Проверены MKV Copy/PCM/AAC/AAC+AC3, AV1 replacement для MKV,
  MOV с H.264/AV1 sources, M4V AAC/dual/default для H.264/ProRes sources,
  HQ x264/x265/ProRes и отклонение недопустимых MOV/M4V combinations.
- CLI harness: `python3 build/phase32-final-20261015/run_cli_matrix.py`.
- Native Cocoa matrix: **8/8** реальных conversions — MKV Copy, MOV PCM,
  M4V AAC/AAC+AC3/default и HQ x264/x265/ProRes. Отдельные UI assertions
  подтвердили сохранение HQ x265 preset, ProRes PCM-only/disabled audio
  selector и блокировку Start для ProRes replacement в M4V с восстановлением
  Start для допустимого MKV.
- Детерминированная локальная процедура Cocoa: запускать
  `build/phase32-final-clean-20261015/bin/ffmpeg_converter_gui_macos.app`;
  для каждой строки матрицы выбрать выходную папку и ровно один указанный
  source fixture; выбрать Mux mode/audio, при Mux replacement mode добавить
  указанный replacement track и проверить Start после выбора; запустить и
  сверить application log и созданный файл. Прогнать MKV/Copy, MOV/PCM,
  M4V/AAC, M4V/AAC+AC3, M4V/default, HQ/x264, HQ/x265 и HQ/ProRes.
  Для проверки default M4V сначала выбрать MOV/PCM, затем M4V, не меняя audio:
  ожидается AAC+AC3. Отдельно для M4V выбрать ProRes replacement и убедиться,
  что Start выключен; перейти на MKV и убедиться, что Start включен. В HQ
  сначала выбрать x265/Slow и сменить audio — encoder/preset должны сохраниться;
  затем выбрать HQ ProRes — audio должен быть только PCM и selector disabled.
  Для каждого запущенного workflow независимо проверить output через ffprobe.
- Все выходы CLI и GUI независимо разобраны ffprobe: codec/count, непустой
  контейнер, chapters, subtitles, attachments, source metadata и audio
  dispositions. В CLI дополнительно проверены language/disposition data,
  first-source audio для processed modes, все source audio tracks для Copy,
  replacement video packets и отсутствие replacement auxiliary streams.
  CLI M4V также прошел с путем, содержащим пробел, `+`, скобки и
  CJK-символы.

Локальные матрицы и per-case evidence сохранены в
`build/phase32-final-evidence-20261015/cli-matrix-summary.json` и
`build/phase32-final-evidence-20261015/gui/gui-matrix-summary.json`.

### Границы вывода

Локальные HQ x264 → MKV, x265 → MKV и ProRes → MOV+PCM прошли в CLI и GUI.
SVT-HEVC/SVT-AV1 не запускались: этот macOS host не поддерживает требуемый
AVX2. Внешний prepared HQ runtime не изменялся; локальный SVT результат не
используется как критерий этой стабилизации.

Pascal и `.github/copilot-instructions.md` не включены в исправления этой
задачи; Pascal оставлен без изменений. GTK/Linux/Windows, Software conversion,
VideoToolbox, прочие hardware/one-pass backends и полная межплатформенная
проверка остаются вне этого результата. Поэтому вывод ограничен: **macOS
stabilization slice verified; Phase 3.2 as a whole is not declared complete**.
