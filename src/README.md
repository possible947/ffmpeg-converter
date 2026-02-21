---
---
---

# 📘 **README.md**

```markdown
# ffmpeg_converter

`ffmpeg_converter` — кроссплатформенный CLI‑инструмент для перекодирования видео с использованием внешнего бинарника **ffmpeg**.  
Проект построен на модульной архитектуре: заголовки лежат в модулях, а реализация — в платформенных каталогах.

---

## 🚀 Возможности

- Формирование оптимизированных команд ffmpeg
- Поддержка ProRes и других кодеков
- Нормализация аудио (peak, loudnorm, 2‑pass)
- Прогресс‑бар с FPS и ETA
- Кроссплатформенная архитектура (Linux, macOS, Windows)
- Чистая модульная структура

---

## 📂 Структура проекта

```
src/
  core/           # Заголовки базовой логики
  utils/          # Заголовки утилит
  progress/       # Заголовки прогресс-бара
  audio/          # Заголовки аудио-логики
  video/          # Заголовки видео-логики
  ffmpeg_cmd/     # Заголовки формирования команд ffmpeg

  platform/
    linux/        # Реализация модулей для Linux (.c)
    macos/        # Реализация модулей для macOS (.c)
    windows/      # Реализация для Windows (позже)

  cli/
    linux/        # CLI для Linux
    macos/        # CLI для macOS
    windows/      # CLI для Windows
```

---

## 🛠 Сборка

### Linux

```bash
mkdir build
cd build
cmake ..
cmake --build . --target linux_cli
```

### macOS (MacPorts)

```bash
mkdir build
cd build
cmake ..
cmake --build . --target macos_cli
```

### Windows  
| `h265_mi50` | строка | Кодек H.265 с настройкой MI‑50 (пример: `--codec h265_mi50`) |

### 🎬 8. Конвертация с использованием H.265 MI‑50

```bash
./ffmpeg_converter \
  --input "input.mov" \
  --codec "h265_mi50" \
  --output "output_hevc.mkv"
```
> **Новый кодек**: `h265_mi50` – H.265 с предустановкой MI‑50, автоматически применяет параметры для оптимального качества и размера.
| `h265_mi50` | строка | Кодек H.265 с настройкой MI‑50 (пример: `--codec h265_mi50`) |
(в разработке)

---

## 📦 Зависимости

### Обязательные
- **ffmpeg** (внешний бинарник)
- **jansson**

### Linux
- ffmpeg в PATH
- libjansson.so

### macOS (MacPorts)
- `/opt/local/bin/ffmpeg`
- `/opt/local/lib/libjansson.dylib`

---

## 🧩 Архитектура

Проект использует **INTERFACE‑библиотеки** для модулей.  
Это позволяет:

- хранить `.h` в модулях,
- хранить `.c` в `src/platform/<platform>/`,
- собирать разные бинарники для разных ОС,
- не тянуть лишние зависимости.

---

## 🧰 Параметры CLI

| Параметр | Тип | Описание |
|---------|------|----------|
| `--input` | строка | Путь к входному файлу |
| `--output` | строка | Путь к выходному файлу |
| `--codec` | строка | Кодек видео (`copy`, `prores`, `prores_ks`) |
| `--profile` | число | Профиль ProRes (0–5) |
| `--audio-norm` | строка | Режим нормализации (`none`, `peak_norm`, `peak_norm_2pass`, `loudness_norm`, `loudness_norm_2pass`) |
| `--gain` | число | Усиление (для peak_norm_2pass) |
| `--I` | число | Целевой Integrated Loudness (для loudnorm_2pass) |
| `--TP` | число | True Peak |
| `--LRA` | число | Loudness Range |
| `--thresh` | число | Порог loudnorm |
| `--offset` | число | Смещение loudnorm |
| `--deblock` | число | 0 — нет, 2 — weak, 3 — strong |
| `--dry-run` | флаг | Показать команду ffmpeg без запуска |

---

## 📚 Примеры использования

### ▶️ 1. Конвертация с копированием видеопотока и нормализацией аудио

```bash
./ffmpeg_converter \
  --input "input.mov" \
  --codec "copy" \
  --audio-norm "peak_norm" \
  --output "output.mov"
```

---

### 🎚 2. Двухпроходная нормализация громкости (EBU R128)

```bash
./ffmpeg_converter \
  --input "input.mov" \
  --codec "prores_ks" \
  --profile 3 \
  --audio-norm "loudness_norm_2pass" \
  --output "output.mov"
```

---

### 🎞 3. Применение фильтра Deblock

```bash
./ffmpeg_converter \
  --input "input.mov" \
  --codec "prores" \
  --profile 2 \
  --deblock 3 \
  --output "clean.mov"
```

---

### 🔊 4. Изменение уровня громкости (peak_norm_2pass)

```bash
./ffmpeg_converter \
  --input "input.wav" \
  --audio-norm "peak_norm_2pass" \
  --gain -3.0 \
  --output "normalized.wav"
```

---

### 🧪 5. Просмотр сформированной команды ffmpeg (debug)

```bash
./ffmpeg_converter --dry-run \
  --input "input.mov" \
  --codec "prores_ks" \
  --profile 4 \
  --output "test.mov"
```

---

### 📈 6. Отображение прогресса кодирования

```bash
./ffmpeg_converter --input in.mov --output out.mov
```

---

### 🧩 7. Использование нестандартного пути к ffmpeg

```bash
FFMPEG=/opt/local/bin/ffmpeg8 ./ffmpeg_converter \
  --input in.mov \
  --output out.mov
```

---

## 📄 Лицензия

MIT (или другая — укажи при необходимости)
```

---

