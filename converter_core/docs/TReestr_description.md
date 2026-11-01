# Компонент Registry

Компонент Registry является одним из основных компонентов нового ядра программы Converter. Он формирует набор компонентов, доступных в текущей среде выполнения, определяет правила их совместимости и связывает каждый preset с данными его реализации. Registry состоит из двух уровней: верхний уровень описывает доступные компоненты и правила их использования, нижний уровень описывает тип и данные конкретной реализации. Связь между уровнями определяется сочетанием `Preset ID + Platform ID`.
Базовым классом для реестра является класс TReester. Этот класс содержит массив `TPlatformsList`, который содержит экземпляры класса `TPlatform`. Такая сложная структура необходима по причине того, что набор компонентов должен включать в себя обязательный набор для всех платформ - TPlatformAll. И не обязательные наборы, зависящие от текущей среды. При этом реестр не выполняет определение возможностей среды. В его конструктор передается массив платформ, согласно которому происходит создание реестра для запрошенной конфигурации.
## Верхний уровень Registry

### Набор платформ
    all (ID=1) - обязательный для всех платформ набор. Включает в себя группы
        Video
        Software (available - true, current - true) - ProRes (available - true, current - true), Prores_ks (available - true, current - false), x264 (available - false, current - false)
        Mux (available - true, current - false) - copy (available - true, current - false), mkv (available - true, current - true), mov (available - true, current - false), mp4 (available - true, current - false).
        Audio (available - true, current - true) - copy (available - true, current - false), pcm (available - true, current - true), aac_fdk (available - true, current - false), ac3 (available - true, current - false)
    darwin_MacPro61(ID=2)
        Audio - aac_at (available - false, current - false)
        (Compute - opencl, metal)
    darwin_iMacPro11(ID=3)
        Video
        videotoolbox (available - true, current - true) - ProRes (available - true, current - true), h264 (available - true, current - false), hevc (available - true, current - false).
        Audio - aac_at (available - false, current - false)
        (Compute - opencl, metal, vulkan)
    darwin_MacPro71(ID=4)
        Video
        videotoolbox (available - true, current - true) - ProRes (available - true, current - true), h264 (available - true, current - false), hevc (available - true, current - false).
        Audio - aac_at (available - false, current - false)
        (Compute - opencl, metal, vulkan)
    linux_vaapi(ID=5)
        Video (available - true, current - false) - h264 (available - true, current - true), hevc (available - true, current - false), hevc_10 (available - true, current - false), av1 (available - true, current - false), av1_10 (available - true, current - false).
        (Compute - opencl, hip, vulkan)
    linux_nvenc7(ID=6)
         Video (available - true, current - false) - h264 (available - true, current - true), hevc (available - true, current - false), hevc_10 (available - true, current - false), av1 (available - true, current - false), av1_10 (available - true, current - false).
        (Compute - opencl, cuda, vulkan)
    linux_qsv_dgx1(ID=7)
         Video (available - true, current - false) - h264 (available - true, current - true), hevc (available - true, current - false), hevc_10 (available - true, current - false), av1 (available - true, current - false), av1_10 (available - true, current - false).
        (Compute - opencl, oneapi, vulkan)
    linux_vulkan_nvidia(ID=8)
        Video (available - true, current - true) - prores (available - false, current - false)
        (Compute - vulkan)
    windows_nvenc7(ID=9)
         Video (available - true, current - false) - h264 (available - true, current - true), hevc (available - true, current - false), hevc_10 (available - false, current - false), av1 (available - true, current - false), av1_10 (available - false, current - false).
        (Compute - opencl, cuda, vulkan)
    windows_qsv_dgx1(ID=10)
         Video (available - true, current - false) - h264 (available - true, current - true), hevc (available - true, current - false), hevc_10 (available - false, current - false), av1 (available - true, current - false), av1_10 (available - false, current - false).
        (Compute - opencl, oneapi, vulkan)
    windows_vulkan_nvidia(ID=11)
        Video (available - true, current - true) - prores (available - true, current - true)
        (Compute - vulkan)
    HQ-converter (ID=12)
        Video - определяется из внешнего источника.

Многократные повторения объясняются необходимостью задать правильные ID платформы, которые необходимы для получения правильных строк обработки FFmpeg. В будущем реестр платформ будет расширен для описания доступных compute компонентов, как показано в скобках.

Каждый из экземпляров класса `TPlatform` класса `TPlatform`sList включает поля ID, Name, ItemsCount, Available, Current и класс TCodecsList, который содержит экземпляры TCodec. Их описание далее:
- ID - integer - идентификатор платформы
- Name - string - имя платформы для отображения в интерфейсе
- ItemsCount - integer - количество экземпляров в TCodecsList. Нужно для удобства просмотра содержимого массива
- Available - bool - определяет доступность данной платформы. Может быть переопределено на этапе проверки возможностей среды (пример - наличие видеокарты не гарантирует, что FFmpeg собран с ее поддержкой).
- Current - bool - определяет выбранную платформу. нужно если присутствуют несколько платформ с подобными наборами.
- Класс TCodecList - TCodec - описывает группу кодеков данной платформы.

Группы кодеков и энкодеры описаны в описании платформ.

Каждый из экземпляров класса TCodec класса TCodecsList включает поля ID, Name, ItemsCount, Available, Current и класс TEncodersList, который содержит экземпляры TEncoder. Их описание далее:
- ID - integer - идентификатор группы кодеков
- Name - string - имя группы кодеков для отображения в интерфейсе
- ItemsCount - integer - количество экземпляров в TEncodersLisr. Нужно для удобства просмотра содержимого массива
- Available - bool - определяет доступность данной группы кодеков. Может быть переопределено на этапе проверки возможностей среды (пример - наличие видеокарты не гарантирует, что FFmpeg собран с ее поддержкой).
- Current - bool - определяет выбранную группу кодеков. нужно если присутствуют несколько групп с подобными наборами. Используется GUI для определения выбранной группы кодеков из списка.
- Класс TEncodersList - TEncoder - описывает энкодеры данной группы кодеков.

Группы кодеков и энкодеры описаны в описании платформ.

### Правила `TEncoder`

Значения правил `TEncoder` по умолчанию приведены в таблице:

| Encoder | MOV | MKV | MP4 | PCM | Audio copy | AAC | AC3 | Chapters | Subtitles | Metadata | One pass | Pre-pass |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ProRes | true | false | false | true | false | false | false | true | false | false | true | true |
| ProRes_ks | true | false | false | true | false | false | false | true | false | false | true | true |
| x264 | true | false | false | true | false | false | false | true | false | false | true | true |
| copy | false | true | false | true | true | true | true | true | true | true | true | false |
| mov | true | false | false | true | false | false | false | true | false | false | true | true |
| mkv | false | true | false | true | true | true | true | true | true | true | true | true |
| mp4 | false | false | true | true | false | true | true | true | true | true | true | false |
| H264 | false | true | true | true | false | true | true | true | true | true | true | true |
| HEVC | false | true | true | true | false | true | true | true | true | true | true | true |
| HEVC_10 | false | true | true | true | false | true | true | true | true | true | true | true |
| AV1 | false | true | false | true | false | true | true | true | true | true | true | true |
| AV1_10 | false | true | false | true | false | true | true | true | true | true | true | true |
| HQ | true | true | false | true | true | true | true | true | true | true | true | true |

Каждый из экземпляров класса TEncoder класса TEncodersList включает поля ID, Name, ItemsCount, Available, Current, ContainerMovAvailable, ContainerMkvAvailable, ContainerMp4Available, AudioPcmAvailable, AudioCopyAvailable, AudioAacAvailable, AudioAc3Available, MediaChaptersAvailable, MediaSubtitlesAvailable,MediaMetaDataAvailable, FiltersOnePass, FiltersTwoPass и класс TPresetsList, который содержит экземпляры TPreset. Их описание далее:
- ID - integer - идентификатор энкодера
- Name - string - имя энкодера для отображения в интерфейсе
- ItemsCount - integer - количество экземпляров в TPresetsList. Нужно для удобства просмотра содержимого массива
- Available - bool - определяет доступность данного энкодера. Может быть переопределено на этапе проверки возможностей среды (пример - наличие видеокарты не гарантирует, что FFmpeg собран с ее поддержкой).
- Current - bool - определяет выбранный энкодер. Используется GUI для определения выбранного энкодера из списка.
- ContainerMovAvailable - bool - определяет если доступен контейнер mov
- ContainerMkvAvailable — bool - определяет если доступен контейнер mkv
- ContainerMp4Available - bool - определяет если доступен контейнер mp4
- AudioPcmAvailable - bool - определяет если разрешен энкодер pcm
- AudioCopyAvailable - bool - определяет если разрешен режим копирования аудио дорожек, когда возможно
- AudioAacAvailable - bool - определяет если разрешен энкодер aac
- AudioAc3Available -bool - определяет если разрешен энкодер ac3
- MediaChaptersAvailable - bool - определяет если разрешен импорт глав
- MediaSubtitlesAvailable - bool - определяет если разрешен импорт субтитров
- MediaMetaDataAvailable - bool - определяет если разрешен импорт мета-данных
- FiltersOnePass - bool - определяет если разрешен один проход для фильтров
- FilterTwoPass - bool - определяет если разрешен отдельный проход для фильтров
- Класс TPresetsList - TPreset - описывает пресеты данного энкодера.

### Доступные presets
    группа Software (ID 1, 2)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        ProRes - LT (ID 1) - true - false
        ProRes - Standard (ID 2) - true - true
        ProRes - HQ (ID 3) - true - false
        ProRes - 4444 (ID 4) - false - false
        ProRes_ks - LT (ID 5) - true - false
        ProRes_ks - Standard (ID 6) - true - true
        ProRes_ks - HQ (ID 7) - true - false
        ProRes_ks - 4444 (ID 8) - true - false
        Mux - copy (ID 9) - true - false
        Mux - mkv (ID 10) - true - true
        Mux - mov (ID 11) - true - false
        Mux - mp4 (ID 12) - true - false
        x264 - lossless_10 (ID 13) - false - false
    группа Audio_Output (ID 1, 2)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        Audio - copy (ID 14) - true - false
        Audio - pcm (ID 15) - true - true
        Audio - aac (ID 16) - true - false
        Audio - ac3 (ID 17) - true - false
        Audio - aac_at (ID 18) - false - false
    группа Videotoolbox (ID 3, 4)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        ProRes - LT (ID 1) - true - false
        ProRes - Standard (ID 2) - true - true
        ProRes - HQ (ID 3) - true - false
        ProRes - 4444 (ID 4) - true - false
        H264 - default (ID 19) - true - true
        H264 - low (ID 20) - true - false
        H264 - balanced (ID 21) - true - false
        H264 - high (ID 22) - true - false
        HEVC - default (ID 23) - true - true
        HEVC - low (ID 24) - true - false
        HEVC - balanced (ID 25) - true - false
        HEVC - high (ID 26) - true - false
    группа VAAPI. (ID 5)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        H264 - default (ID 19) - true - true
        H264 - low (ID 20) - true - false
        H264 - balanced (ID 21) - true - false
        H264 - high (ID 22) - true - false
        HEVC - default (ID 23) - true - true
        HEVC - low (ID 24) - true - false
        HEVC - balanced (ID 25) - true - false
        HEVC - high (ID 26) - true - false
        HEVC_10 - default (ID 27) - true - true
        HEVC_10 - low (ID 28) - true - false
        HEVC_10 - balanced (ID 29) - true - false
        HEVC_10 - high (ID 30) - true - false
        AV1 - default (ID 31) - true - true
        AV1 - low (ID 32) - true - false
        AV1 - balanced (ID 33) - true - false
        AV1 - high (ID 34) - true - false
        AV1_10 - default (ID 35) - true - true
        AV1_10 - low (ID 36) - true - false
        AV1_10 - balanced (ID 37) - true - false
        AV1_10 - high (ID 38) - true - false
    группа LINUX_NVENC7 (ID 6)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        H264 - default (ID 19) - true - true
        H264 - low (ID 20) - true - false
        H264 - balanced (ID 21) - true - false
        H264 - high (ID 22) - true - false
        HEVC - default (ID 23) - true - true
        HEVC - low (ID 24) - true - false
        HEVC - balanced (ID 25) - true - false
        HEVC - high (ID 26) - true - false
        HEVC_10 - default (ID 27) - true - true
        HEVC_10 - low (ID 28) - true - false
        HEVC_10 - balanced (ID 29) - true - false
        HEVC_10 - high (ID 30) - true - false
        AV1 - default (ID 31) - true - true
        AV1 - low (ID 32) - true - false
        AV1 - balanced (ID 33) - true - false
        AV1 - high (ID 34) - true - false
        AV1_10 - default (ID 35) - true - true
        AV1_10 - low (ID 36) - true - false
        AV1_10 - balanced (ID 37) - true - false
        AV1_10 - high (ID 38) - true - false
    группа WINDOWS_NVENC7 (ID 9)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        H264 - default (ID 19) - true - true
        H264 - low (ID 20) - true - false
        H264 - balanced (ID 21) - true - false
        H264 - high (ID 22) - true - false
        HEVC - default (ID 23) - true - true
        HEVC - low (ID 24) - true - false
        HEVC - balanced (ID 25) - true - false
        HEVC - high (ID 26) - true - false
        HEVC_10 - default (ID 27) - false - false
        HEVC_10 - low (ID 28) - false - false
        HEVC_10 - balanced (ID 29) - false - false
        HEVC_10 - high (ID 30) - false - false
        AV1 - default (ID 31) - true - true
        AV1 - low (ID 32) - true - false
        AV1 - balanced (ID 33) - true - false
        AV1 - high (ID 34) - true - false
        AV1_10 - default (ID 35) - false - false
        AV1_10 - low (ID 36) - false - false
        AV1_10 - balanced (ID 37) - false - false
        AV1_10 - high (ID 38) - false - false
    группа LINUX_QSV_DGX1 (ID 7)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        H264 - default (ID 19) - true - true
        H264 - low (ID 20) - true - false
        H264 - balanced (ID 21) - true - false
        H264 - high (ID 22) - true - false
        HEVC - default (ID 23) - true - true
        HEVC - low (ID 24) - true - false
        HEVC - balanced (ID 25) - true - false
        HEVC - high (ID 26) - true - false
        HEVC_10 - default (ID 27) - true - true
        HEVC_10 - low (ID 28) - true - false true
        HEVC_10 - balanced (ID 29) - true - false
        HEVC_10 - high (ID 30) - true - false true
        AV1 - default (ID 31) - true - true
        AV1 - low (ID 32) - true - false
        AV1 - balanced (ID 33) - true - false
        AV1 - high (ID 34) - true - false
        AV1_10 - default (ID 35) - true - true
        AV1_10 - low (ID 36) - true - false
        AV1_10 - balanced (ID 37) - true - false
        AV1_10 - high (ID 38) - true - false
    группа WINDOWS_QSV_DGX1 (ID 10)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        H264 - default (ID 19) - true - true
        H264 - low (ID 20) - true - false
        H264 - balanced (ID 21) - true - false
        H264 - high (ID 22) - true - false
        HEVC - default (ID 23) - true - true
        HEVC - low (ID 24) - true - false
        HEVC - balanced (ID 25) - true - false
        HEVC - high (ID 26) - true - false
        HEVC_10 - default (ID 27) - false - false
        HEVC_10 - low (ID 28) - false - false
        HEVC_10 - balanced (ID 29) - false - false
        HEVC_10 - high (ID 30) - false - false
        AV1 - default (ID 31) - true - true
        AV1 - low (ID 32) - true - false
        AV1 - balanced (ID 33) - true - false
        AV1 - high (ID 34) - true - false
        AV1_10 - default (ID 35) - false - false
        AV1_10 - low (ID 36) - false - false
        AV1_10 - balanced (ID 37) - false - false
        AV1_10 - high (ID 38) - false - false
    группа VULKAN (ID 8)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        ProRes - default (ID 39) - false - false
    группа VULKAN (ID 11)
        энкодер - пресет - ID - Available - Current
        -----------------------------------------
        ProRes - default (ID 39) - true - true

Каждый из экземпляров класса TPreset класса TPresetsList включает поля ID, Name, Available, Current и запись PresetSet. Их описание далее:
- ID - integer - идентификатор пресета.
- Name - string - имя пресета для отображения в интерфейсе
- Available - bool - определяет доступность данной группы кодеков. Может быть переопределено на этапе проверки возможностей среды (пример - наличие видеокарты не гарантирует, что FFmpeg собран с ее поддержкой).
- Current - bool - определяет выбранную группу кодеков. нужно если присутствуют несколько групп с подобными наборами.
    PresetSet - запись, извлеченная из реестра нижнего уровня с описанием реализации пресета.

    1. Current=true допускается только для Available=true.
    2. В пределах одного списка может быть не более одного Current=true.
    3. Отсутствие доступной реализации не приводит к fallback.
    4. Все значения Available и Current имеют исходные значения,
    определённые статическим Registry.
    5. Runtime validation может отключать значения Available.
    6. Валидатор параметров может изменять Current и effective
    availability в соответствии с зависимостями выбранного набора.
    7. После формирования Registry каждый доступный TPreset содержит
    полностью определённый PresetSet.
    8. После формирования runtime Registry нижний статический Registry
    больше не используется во время обработки.


### Формирование runtime Registry
    1. происходит создание экземпляра реестра и в его конструктор передается массив платформ, для которых должны быть включены компоненты и определены правила реализации.
    2. конструктор обращается к процедуре инициализации - метод CreateInitialSets, который создает экземпляры необходимых классов. Статически описывает их поля в соответствии с показанными выше списками. на этом конструктор закончил.
    3. после управление передается классу реестр.
    4. класс реестр создает экземпляр TPlatformsList.
    5. класс реестр последовательно заполняет TPlatformList экземплярами TPlatform, соответствующими запрошенным.
        5.1 класс реестр создает экземпляр TPlatformAll, который включает в себя группы кодеков Software и Audio_Output. На данном этапе определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.2 если запрошена платформа darwin_MacPro61(ID=2) то реестр меняет значение Audio.aac_at.available на true.
        5.3 если запрошена платформа darwin_iMacPro11(ID=3) или darwin_MacPro71(ID=4) то реестр меняет значение Audio.aac_at.available на true, создает новый экземпляр класса `TPlatform`DarviniMacPro11 или TPlatformDarvinMacPro71. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.4 если запрошена платформа linux_vaapi(ID=5) то реестр создает новый экземпляр класса `TPlatform`LinuxVAAPI. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.5 если запрошена платформа linux_nvenc7(ID=6) то реестр создает новый экземпляр класса `TPlatform`LinuxNVENC7. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.6 если запрошена платформа linux_qsv_dgx1(ID=7) то реестр создает новый экземпляр класса `TPlatform`LinuxDGX1. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.7 если запрошена платформа linux_vulkan(ID=8) то реестр создает новый экземпляр класса `TPlatform`LinuxVulkan. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.8 если запрошена платформа windows_nvenc7(ID=9) то реестр создает новый экземпляр класса `TPlatform`WindowsNVENC7. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.9 если запрошена платформа windows_qsv_dgx1(ID=10) то реестр создает новый экземпляр класса `TPlatform`WindowsDGX1. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.10 если запрошена платформа windows_vulkan(ID=11) то реестр создает новый экземпляр класса `TPlatform`WindowsVulkan. Для новой группы кодеков определены доступные и текущие значения для энкодеров и пресетов как значения по умолчанию. Новый экземпляр добавляется в TPlatformsList.
        5.11 если запрошена платформа HQ-converter (ID=12), то реестр обращается к методу HQPresetLoader, который выполняет чтение набора данных из файла параметров HQ (он всегда доступен по одному пути относительно самой программы) и создает TPlatformHQ, который возвращает в реестр. Новый экземпляр добавляется в TPlatformsList.
        5.12 реестр проходит полученный TPlatformsList и для каждого пресета создает запись PresetSet, которую заполняет значениями, извлеченными из реестра нижнего уровня.
    6. в конце реестр возвращает ссылку на полученный TPlatformsList как результат своей работы.
    7. реестр вызывает деструктор для освобождения ресурсов и заканчивает работу.

### `HQPresetLoader`

Отдельного внимания требует обработка запроса на добавление HQ. HQ внешний проект, который разрабатывается отдельно и имеет свой файл пресетов. Комбинации пресетов HQ идеально ложатся в существующую модель, но определение их требует чтения набора из файла пресетов HQ. Таким образом необходимо расширить реестр и после этапа 5.10 выполнить чтение набора данных из файла пресетов HQ. Текущую структуру набора данных я отображаю ниже, но это является только примером.
        Codec Group - HQ-converter
        Encoders - x264, x265, svt-hevc, svt-av1, FFmpeg-prores, FFmpeg-prores_ks
        Presets:
            x264 - fullhd, uhd4k, youtube4k
            x265 - Medium, Slow, Music, UHD, UHD_grain
            svt-hevc - balanced_4k, balanced_grain
            svt-av1 - balanced
            FFmpeg-prores - standard
            FFmpeg-prores_ks - HQ

    Несколько важных замечаний относительно полей available и current. Все энкодеры и пресеты из файла пресетов являются available = true. файл пресетов HQ не определяет current и реестр должен сам проставить current = true для первых пунктов списков, остальные пометив как current = false. Я не стал отмечать это в таблице ввиду простого правила определения. Все пресеты HQ имеют один ID = 40. Они будут получать свой экземпляр PresetSet.

## Инварианты верхнего уровня
1. TEncoder является единственным источником статической policy совместимости encoder с container, audio, media и filter processing modes.
2. TConverterParameters не содержит hardcoded policy для конкретных encoder ID; он применяет правила выбранного TEncoder и общие cross-field dependencies.
3. Runtime validation имеет право уменьшить Available, но не имеет права сделать доступной возможность, запрещённую статическим Registry.
4. TPreset.PresetSet определяет implementation выбранного preset и не определяет policy допустимости, принадлежащую TEncoder.

Описание верхнего уровня Registry на этом завершено.

## Нижний уровень Registry

Реестр нижнего уровня служит для описания конкретных реализаций функционала, определённого реестром верхнего уровня.

Разделение реестра на два уровня необходимо для строгого разделения правил поведения ядра и способов реализации этих правил.

Верхний уровень определяет:

- какие компоненты существуют;
- какие компоненты доступны;
- какие значения являются текущими;
- какие сочетания параметров разрешены;
- какие контейнеры, режимы обработки аудио, медиа-компоненты и способы обработки фильтров разрешены выбранному энкодеру.

Нижний уровень не принимает решений о допустимости пользовательского набора параметров. Его задача состоит исключительно в описании того, **как должна быть реализована уже разрешённая операция**.

Таким образом:

```text
Upper Registry
    = WHAT IS ALLOWED

Lower Registry
    = HOW IT IS IMPLEMENTED
```

Изменение правил поведения верхнего уровня не должно требовать изменения реализации операции нижнего уровня, если сама реализация не изменилась. Аналогично изменение команды FFmpeg, используемого инструмента или способа выполнения реализации не должно изменять правила поведения верхнего уровня.

---

### Общая структура

Реестр нижнего уровня логически представлен массивом в формате:

```text
Preset ID -> Implementation Class
```

`Preset ID` является ключом элемента массива.

Значением является экземпляр базового класса или одного из его специализированных потомков. Класс-значение является контейнером данных: он содержит поля, необходимые получателю для выполнения конкретного типа реализации, и не выполняет обработку самостоятельно.

Разные типы реализации могут иметь различный состав данных. Например:

```text
FFmpeg video encoder
FFmpeg audio encoder
HQ video encoder
FFmpeg muxer
mkvmerge muxer
MP4Box muxer
audio filter
video filter
```

Поэтому базовый класс определяет только минимальный общий контракт, а его потомки расширяют набор полей в соответствии с назначением реализации.

Получатель конкретного класса-значения обязан знать его тип и интерпретировать содержащиеся в нём данные в соответствии с этим типом.

---

### Реализации, зависящие от платформы

Один и тот же `Preset ID` может иметь различные реализации на разных платформах.

Поэтому значение нижнего реестра может содержать массив platform-specific вариантов:

```text
Preset ID
    |
    +-- Platform ID -> implementation
    |
    +-- Platform ID -> implementation
    |
    +-- Platform ID -> implementation
```

`Platform ID` полностью идентифицирует проверенный вариант среды выполнения.

Дополнительное разрешение реализации по `Device`, `FeatureSet`, версии драйвера, версии аппаратной архитектуры или другим runtime-характеристикам нижним реестром не выполняется.

Если требуется новая проверенная реализация среды, она должна быть заранее описана отдельным `Platform ID` в реестре верхнего уровня.

Таким образом выбор конкретной реализации выполняется по двум уже определённым значениям:

```text
Preset ID
+
Platform ID
=
implementation
```

Fallback на другую платформенную реализацию не выполняется.

Если для сочетания `Preset ID + Platform ID` реализация отсутствует, данная комбинация считается недоступной.

---

### Формирование runtime Registry

Нижний реестр используется только на этапе формирования runtime Registry.

Для каждого доступного `TPreset` верхнего реестра выполняется разрешение соответствующей реализации нижнего уровня.

После успешного разрешения `TPreset` получает собственный экземпляр `PresetSet`, содержащий полностью определённые данные реализации для текущей платформы.

После завершения формирования runtime Registry нижний статический реестр больше не используется процессом обработки.

То есть:

```text
Static Upper Registry
        +
Static Lower Registry
        +
Current Platform
            |
            v
      Runtime Registry
            |
            v
     TPreset.PresetSet
```

Core-классы во время обработки файлов не выполняют повторный поиск реализации в нижнем статическом реестре.

Они используют уже сформированный `PresetSet`.

---

### Инварианты нижнего уровня

1. `Preset ID` идентифицирует реализацию элемента верхнего реестра.

2. `Platform ID` идентифицирует конкретный проверенный вариант среды выполнения.

3. Для одного статического сочетания:

```text
Preset ID + Platform ID
```

может существовать не более одной реализации соответствующего типа.

4. Нижний реестр не определяет, разрешено ли использование preset. Это определяется верхним Registry и последующей runtime validation.

5. Нижний реестр не выполняет fallback при отсутствии реализации.

6. После успешного формирования runtime Registry каждый `TPreset` с `Available = true` должен иметь полностью сформированный `PresetSet`.

7. Невозможность сформировать `PresetSet` для preset, который был отмечен как доступный, должна привести к изменению его состояния на недоступное либо к ошибке формирования Registry в соответствии с правилами вызывающего компонента.

8. Core-классы не должны повторно определять платформу, аппаратную архитектуру или другую информацию среды для выбора реализации.

9. Нижний Registry содержит данные реализации, но не выполняет её. Выполнение является ответственностью соответствующего Core-класса.

10. Классы-значения нижнего Registry являются контейнерами данных и самостоятельно не запускают внешние инструменты.

11. `Platform ID` является логическим ключом поиска внутри массива `Values`, но не является физическим индексом массива. Каждый элемент `Values` содержит собственный `Platform ID`. Registry последовательно ищет элемент, `Platform ID` которого соответствует текущей платформе. Отсутствие такого элемента означает отсутствие реализации данного `Preset ID` для этой платформы.

12. Наличие реализации и наличие дополнительных данных реализации являются независимыми понятиями:

    - `TOptions` отсутствует: реализация данного `Preset ID` не определена.
    - `TOptions` существует, но в `Values` отсутствует элемент с требуемым `Platform ID`: реализация данного preset для этой платформы отсутствует.
    - Элемент `Values` с требуемым `Platform ID` существует, а дополнительные данные имеют значение `null`: реализация определена и не требует дополнительных декларативных параметров.
    - Элемент `Values` с требуемым `Platform ID` существует и содержит дополнительные данные: реализация определена и содержит platform-specific параметры.

13. `Platform ID = 1` обозначает принадлежность реализации к обязательному `TPlatformAll`. Он не является wildcard и не означает автоматическую доступность реализации на любой платформе. Наличие реализации в нижнем Registry само по себе не делает её доступной: соответствующая платформа должна присутствовать в сформированном runtime Registry.

14. Для behavior-driven implementations, которым не нужны дополнительные декларативные параметры, `Values` всё равно содержит минимальный элемент с `Platform ID` и `Tool ID`; значение дополнительных данных равно `null`. Это позволяет однозначно отличить существующую реализацию без дополнительных параметров от отсутствующей реализации.

15. Нижний Registry содержит только данные, необходимые для выбора и настройки реализации. Последовательность действий, вызовы инструментов, создание временных файлов и многоэтапная обработка принадлежат соответствующему Core-классу и не должны кодироваться в Registry.

---

### Идентификаторы инструментов и назначений

Для определения типа реализации используется отдельный `Tool ID`.

`Tool ID`, `Preset ID` и `Platform ID` имеют различное назначение и не должны интерпретироваться как взаимозаменяемые значения.

```text
Preset ID
    -> какую реализацию верхнего Registry мы разрешаем

Platform ID
    -> для какой проверенной среды используется вариант реализации

Tool ID
    -> каким инструментом и с каким назначением реализация выполняется
```

Реестр инструментов и назначений содержит:

```text
ID | Tool        | Назначение
---+-------------+----------------
00 | not defined |
01 | FFmpeg      | video encoder
02 | FFmpeg      | audio encoder
03 | fdk_aac     | audio encoder
04 | aac_at      | audio encoder
05 | HQ          | video encoder
06 | FFmpeg      | muxer
07 | Mux         | muxer
10 | FFmpeg      | audio filter
11 | FFmpeg      | video filter
```

Один физический инструмент может иметь несколько `Tool ID`, если он используется в различных ролях.

Например FFmpeg в качестве video encoder и FFmpeg в качестве muxer считаются разными типами реализации, несмотря на использование одного исполняемого файла.

---

### Базовый класс значения

Базовый class TOptions содержит минимальный набор общих полей:

```text
Preset ID
Values
```

`Values` представляет набор platform-specific реализаций данного preset. Физический индекс элемента массива не связан со значением `Platform ID`. Каждый элемент `Values` хранит собственный `Platform ID`, который используется Registry как логический ключ при последовательном поиске реализации для текущей платформы.

Каждая найденная platform-specific реализация содержит `Tool ID` и, при необходимости, дополнительные данные, необходимые соответствующему Core-классу.

Состав остальных полей определяется конкретным потомком class-value.

---

### FFmpeg Video Encoder

`TFFmpegVideoEncoder` является специализированным потомком базового class-value и описывает реализацию video encoding средствами FFmpeg.

Его platform-specific value содержит:

```text
Platform ID
Tool ID
before_options
after_options
filter_options
encoder_options
```

Назначение полей:

**Platform ID**
Определяет платформенную реализацию данного preset.

**Tool ID**
Определяет используемый инструмент и его назначение. Для `TFfmpegVideoEncoder` используется `Tool ID = 01`, соответствующий FFmpeg в роли video encoder.

**before_options**
Содержит параметры FFmpeg, которые должны быть расположены перед соответствующим `-i`.

**after_options**
Содержит параметры FFmpeg, которые должны быть расположены после определения входного файла.

**filter_options**
Содержит параметры фильтрации или filtergraph, являющиеся частью данной реализации video encoder. Поле может быть пустым.

**encoder_options**
Содержит основной набор параметров выбора и настройки video encoder.

Например:

```text
-c:v hevc_nvenc -preset ...
```

`encoder_options` не содержит имя или путь исполняемого файла FFmpeg, имя входного файла и имя выходного файла. Исполняемый инструмент определяется через `Tool ID`, а входные и выходные файлы определяются runtime-состоянием задания.

Разделение параметров на `before`, `after`, `filter` и `encoder` необходимо потому, что различные FFmpeg video implementations требуют разного расположения аргументов относительно входного файла, а аппаратные encoder-ы могут дополнительно требовать подготовки аппаратного устройства, преобразования pixel format или video frames.

После формирования runtime Registry выбранный `TFFmpegVideoEncoder` materializes соответствующую текущей платформе реализацию в `TPreset.PresetSet`.

---

### Экземпляры `TFFmpegVideoEncoder`

ProRes - LT (ID 1)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores -profile:v 1"
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 1 -allow_sw 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 1 -allow_sw 1"
ProRes - Standard (ID 2)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores -profile:v 2"
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 2 -allow_sw 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 2 -allow_sw 1"
ProRes - HQ (ID 3)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores -profile:v 3"
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 3 -allow_sw 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 3 -allow_sw 1"
ProRes - 4444 (ID 4)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores -profile:v 4"
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 4 -allow_sw 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_videotoolbox -profile:v 4 -allow_sw 1"
ProRes_ks - LT (ID 5)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_ks -profile:v lt"
ProRes_ks - Standard (ID 6)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_ks -profile:v standard"
ProRes_ks - HQ (ID 7)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_ks -profile:v hq"
ProRes_ks - 4444 (ID 8)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v prores_ks -profile:v 4444"
x264 - lossless_10 (ID 13)
    1.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
H264 - default (ID 19)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v h264_vaapi -rc_mode auto"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p7 -qp 22 -spatial-aq 1 -temporal-aq 1"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p7 -qp 22 -spatial-aq 1 -temporal-aq 1"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1"
H264 - low (ID 20)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -b:v {bpp:0.05} -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -b:v {bpp:0.05} -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v h264_vaapi -rc_mode CQP -qp 28"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p1 -qp 22"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset fast"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p1 -qp 22 -spatial-aq 1 -temporal-aq 1"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset veryfast -extbrc 1"
H264 - balanced (ID 21)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -b:v {bpp:0.08} -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -b:v {bpp:0.08} -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v h264_vaapi -rc_mode CQP -qp 25"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p4 -qp 22"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset medium"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p4 -qp 22 -spatial-aq 1 -temporal-aq 1"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset medium -look_ahead 1 -look_ahead_depth 40 -extbrc 1"
H264 - high (ID 22)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -b:v {bpp:0.12} -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_videotoolbox -b:v {bpp:0.12} -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v h264_vaapi -rc_mode CQP -qp 22"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p7 -qp 22"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset slow"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_nvenc -preset p7 -qp 22 -spatial-aq 1 -temporal-aq 1"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1"
HEVC - default (ID 23)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -tag:v hvc1 -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -tag:v hvc1 -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode auto"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset p7 -cq 25 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset slower -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset medium -cq 25 -lookahead_level auto"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
HEVC - low (ID 24)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -b:v {bpp:0.030} -tag:v hvc1 -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -b:v {bpp:0.030} -tag:v hvc1 -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode CQP -qp 28"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset p1 -cq 25"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset fast"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset p1 -cq 25 -lookahead_level 0"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset fast -g 240 -bf 4"
HEVC - balanced (ID 25)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -b:v {bpp:0.050} -tag:v hvc1 -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -b:v {bpp:0.050} -tag:v hvc1 -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode CQP -qp 25"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset p4 -cq 25"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset medium"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset p4 -cq 25 -lookahead_level auto"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset medium -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
HEVC - high (ID 26)
    3.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -b:v {bpp:0.075} -tag:v hvc1 -spatial_aq 1"
    4.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_videotoolbox -b:v {bpp:0.075} -tag:v hvc1 -spatial_aq 1"
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode CQP -qp 22"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset p7 -cq 25"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset slow"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_nvenc -preset medium -cq 25 -lookahead_level auto"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
HEVC_10 - default (ID 27)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode auto -profile:v main10"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_nvenc -preset p7 -cq 25 -lookahead_level auto -profile:v main10"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset slower -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 -profile:v main10"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
HEVC_10 - low (ID 28)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode CQP -qp 28 -profile:v main10"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_nvenc -preset p1 -cq 25 -profile:v main10"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset fast -profile:v main10"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
HEVC_10 - balanced (ID 29)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode CQP -qp 25 -profile:v main10"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_nvenc -preset p4 -cq 25 -profile:v main10"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset medium -profile:v main10"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
HEVC_10 - high (ID 30)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v hevc_vaapi -rc_mode CQP -qp 22 -profile:v main10"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_nvenc -preset p7 -cq 25 -profile:v main10"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v hevc_qsv -global_quality 25 -preset slow -profile:v main10"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
AV1 - default (ID 31)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode auto"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p6 -cq 30 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p6 -cq 30 -lookahead_level auto"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
AV1 - low (ID 32)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode CQP -global_quality 28"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p1 -cq 30 -lookahead_level 0"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset veryfast"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p1 -cq 30 -lookahead_level 0"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset veryfast -extbrc 1"
AV1 - balanced (ID 33)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode CQP -global_quality 25"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p4 -cq 30 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset medium"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p4 -cq 30 -lookahead_level auto"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset medium -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
AV1 - high (ID 34)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=nv12,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode CQP -global_quality 22"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p7 -cq 30 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset slow"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_nvenc -preset p7 -cq 30 -lookahead_level auto"
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
AV1_10 - default (ID 35)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode auto"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_nvenc -preset p6 -cq 30 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
AV1_10 - low (ID 36)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode CQP -global_quality 28"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_nvenc -preset p1 -cq 30 -lookahead_level 0"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset veryfast"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
AV1_10 - balanced (ID 37)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode CQP -global_quality 25"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_nvenc -preset p4 -cq 30 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset medium"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
AV1_10 - high (ID 38)
    5.  ID: 01
        before_options: "-vaapi_device {vaapi_device}"
        after_options: ""
        filter_options: "-vf \"format=p010le,hwupload\""
        encoder_options: "-c:v av1_vaapi -rc_mode CQP -global_quality 22"
    6.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_nvenc -preset p7 -cq 30 -lookahead_level auto"
    7.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: "-vf \"format=p010le\""
        encoder_options: "-c:v av1_qsv -global_quality 28 -preset slow"
    9.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    10. ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
ProRes - default (ID 39)
    8.  ID: 01
        before_options: ""
        after_options: ""
        filter_options: ""
        encoder_options: ""
    11. ID: 01
        before_options: "-init_hw_device vulkan=vk:{vulkan_device} -filter_hw_device vk"
        after_options: ""
        filter_options: "-vf \"format=yuv422p10le,hwupload\""
        encoder_options: "-c:v prores_ks_vulkan -profile:v standard"

### Экземпляры `TFFmpegAudioEncoder`

Audio - copy (ID 14)
    1.  ID: 02
        filter_options: ""
        encoder_options: "-c:a copy"
Audio - pcm (ID 15)
    1.  ID: 02
        filter_options: "aresample=resampler=soxr:precision=28:cheby=1"
        encoder_options: "-c:a pcm_s16le -ar 48000"
Audio - aac (ID 16)
    1.  ID: 02
        filter_options: "aresample=resampler=soxr:precision=28:cheby=1"
        encoder_options: "-c:a libfdk_aac -b:a 320k -ar 48000"
Audio - ac3 (ID 17)
    1.  ID: 02
        filter_options: "aresample=resampler=soxr:precision=28:cheby=1"
        encoder_options: "-c:a:1 ac3 -b:a:1 640k -ar:a:1 48000"
Audio - aac_at (ID 18)
    2.  ID: 02
        filter_options: "aresample=resampler=soxr:precision=28:cheby=1"
        encoder_options: "-c:a aac_at -b:a 320k -ar 48000"
    3.  ID: 02
        filter_options: "aresample=resampler=soxr:precision=28:cheby=1"
        encoder_options: "-c:a aac_at -b:a 320k -ar 48000"
    4.  ID: 02
        filter_options: "aresample=resampler=soxr:precision=28:cheby=1"
        encoder_options: "-c:a aac_at -b:a 320k -ar 48000"
### Экземпляры `TFFmpegMuxer`
Mux - mov (ID 11)
    1.  ID: 06
        Muxer_options: "-c copy -movflags use_metadata_tags -f mov"

Сборка контейнеров mkv и mp4 в задаче сборки файла, отделённой от кодирования, производится отдельным программным компонентом Mux. Для работы Mux достаточно параметров, определённых в верхнем Registry; последовательность действий определяется собственным алгоритмом Mux. Поэтому элементы `Values` для этих presets содержат только `Platform ID` и `Tool ID`, а дополнительные данные реализации имеют значение `null`.

Mux - copy (Preset ID 9)
    Platform ID: 1
    Tool ID: 07
    VariantData: null

Mux - mkv (Preset ID 10)
    Platform ID: 1
    Tool ID: 07
    VariantData: null

Mux - mp4 (Preset ID 12)
    Platform ID: 1
    Tool ID: 07
    VariantData: null

HQ является внешним проектом. Его вызов требует только выбранную комбинацию Encoder - Preset из динамически сформированного верхнего Registry. Дополнительных параметров реализации нижний Registry не определяет. Все HQ presets верхнего Registry используют один `Preset ID = 40`, а их реализация принадлежит платформе `hq-converter` с `Platform ID = 12`.

HQ_converter - all (Preset ID 40)
    Platform ID: 12
    Tool ID: 05
    VariantData: null

## Статус спецификации

Архитектура Registry зафиксирована. Пустые строки в массивах реализаций являются placeholders для последующего извлечения проверенных параметров из текущей реализации Converter. Они не должны заполняться предположительными значениями.
