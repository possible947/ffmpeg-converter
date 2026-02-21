#include <stdio.h>
#include <string.h>
#include "progress.h"

static int progress_active = 0;

static void format_eta(double eta, char *buf, size_t sz) {
    if (eta <= 0) {
        snprintf(buf, sz, "ETA --:--:--");
        return;
    }
    int t = (int)eta;
    int h = t / 3600;
    int m = (t % 3600) / 60;
    int s = t % 60;
    snprintf(buf, sz, "ETA %02d:%02d:%02d", h, m, s);
}

static void draw_progress(double percent, double fps, double eta_seconds) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int width = 40;
    int filled = (int)(percent * width / 100.0);

    char eta_buf[32];
    format_eta(eta_seconds, eta_buf, sizeof(eta_buf));

    printf("\r");

    if (fps > 0)
        printf("fps=%.0f ", fps);

    printf("[");
    for (int i = 0; i < filled; i++) printf("#");
    for (int i = filled; i < width; i++) printf("-");
    printf("] %3.0f%% %s", percent, eta_buf);

    fflush(stdout);
}

void progress_start(void) {
    progress_active = 1;
}

void progress_update(double percent, double fps, double eta) {
    progress_active = 1;
    draw_progress(percent, fps, eta);
}

void progress_end(void) {
    if (progress_active) {
        printf("\n");
        fflush(stdout);
        progress_active = 0;
    }
}