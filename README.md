# ffmpeg_converter

**Версия 1.0**

`ffmpeg_converter` — кроссплатформенный инструмент с CLI и GUI для формирования и запуска оптимизированных команд `ffmpeg` с поддержкой ProRes, копирования видеопотока, H.265 VAAPI (h265_mi50) и нескольких режимов нормализации аудио (включая двухпроходную EBU R128).

Особенности:

- Формирование команд `ffmpeg` для различных сценариев (copy, `prores`, `prores_ks`, `h265_mi50`).
- Поддержка нормализации аудио: peak, peak 2-pass, loudness (loudnorm) и loudness 2-pass.
- Отображение прогресса кодирования (percent, FPS, ETA).
- Интерактивное текстовое меню и удобный CLI для пакетной обработки файлов.
- **GTK4 GUI для Linux и macOS** с визуальным выбором параметров и прогресс-баром.
- Модульная архитектура: заголовки и платформенные реализации разделены.

Требования
---------

- `ffmpeg` в `PATH` (или указать переменную окружения `FFMPEG` для нестандартного пути).
- `jansson` (для парсинга JSON, используемого в loudnorm анализе).
- `gtk4` (для GUI на Linux и macOS):
  - **Linux**: `sudo apt install libgtk-4-dev` (Debian/Ubuntu) или аналог для других дистрибутивов
  - **macOS**: `sudo port install gtk4 +quartz -x11` (MacPorts) или `brew install gtk4` (Homebrew)
    - ⚠️ Важно: для нативной работы на macOS используйте вариант `+quartz`, а не `+x11`
- CMake + компилятор (gcc/clang) для сборки.

Быстрая сборка
--------------

**Linux:**

```bash
mkdir build
cd build
cmake ..
cmake --build . --target linux_cli  # CLI
cmake --build . --target linux_gui  # GUI
```

**macOS:**

```bash
mkdir build
cd build
cmake ..
cmake --build . --target macos_cli  # CLI
cmake --build . --target macos_gui  # GUI
```

*Примечание: GUI собирается автоматически, если GTK4 найден в системе (`ENABLE_GUI=ON` по умолчанию).*

Использование
-------------

Примеры запуска (в корне проекта после сборки):

```bash
./linux_cli/ffmpeg_converter --input input.mov --output out.mov

# Пример: prores_ks, профиль hq, loudness 2-pass
./linux_cli/ffmpeg_converter -c prores_ks -p hq -a loudnorm2 -g rock input.mov

# Пример: H.265 VAAPI (h265_mi50) с устройством по умолчанию
./linux_cli/ffmpeg_converter -c h265_mi50 input.mov
```

GUI (Linux):

```bash
./src/gui/ffmpeg_converter_gui
```

GUI (macOS):

```bash
./src/gui/ffmpeg_converter_gui_macos
```

Дополнительная документация и примеры параметров находятся в модуле: [src/README.md](src/README.md).

Подробный обзор компонентов проекта: [PROJECT_OVERVIEW_DETAILED.md](PROJECT_OVERVIEW_DETAILED.md).

Install/build commands for Linux/macOS/Windows: [docs/install-linux.md](docs/install-linux.md), [docs/install-macos.md](docs/install-macos.md), [docs/install-windows.md](docs/install-windows.md).

Подробности по Pascal converter library: [fpc/converter/CONVERTER_LIBRARY_DETAIL.md](fpc/converter/CONVERTER_LIBRARY_DETAIL.md).

Структура проекта (основные папки)
---------------------------------

- `src/` — исходники и модули.
- `src/converter/` — основной модуль конвертации (`converter.h`, `converter.c`).
- `src/cli/` — реализация CLI для платформ.
- `src/platform/` — платформенные реализации (Linux/macOS/Windows).
- `src/progress/` — интерфейс прогресс‑индикатора.

Советы и заметки
-----------------

- Для корректной работы loudness 2-pass требуется доступ к `ffmpeg` и `jansson`.
- Для `h265_mi50` используется VAAPI. Устройство по умолчанию: `/dev/dri/renderD128`. Можно переопределить через `VAAPI_DEVICE`.
- Для загрузки кадров в VAAPI применяется фильтр `-vf "format=nv12,hwupload"` (не влияет на обработку звука).
- Рекомендуется тестировать на небольших файлах перед пакетной обработкой.
- Для улучшения CI можно добавить простые unit‑тесты для генерации имени файла и парсинга прогресса.

Лицензия
--------

MIT. При необходимости укажите и добавьте файл `LICENSE`.

Контрибьютинг
-------------

PR и issue приветствуются. Описывайте шаги воспроизведения и прикладывайте пример команды `ffmpeg`.
