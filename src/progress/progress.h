#ifndef PROGRESS_H
#define PROGRESS_H

// Начать прогресс (опционально, но полезно)
void progress_start(void);

// Обновить прогресс (percent 0–100, fps >=0, eta >=0)
void progress_update(double percent, double fps, double eta);

// Завершить прогресс (перевод строки, сброс состояния)
void progress_end(void);

#endif