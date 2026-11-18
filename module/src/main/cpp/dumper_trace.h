#ifndef ZYGISK_IL2CPPDUMPER_TRACE_H
#define ZYGISK_IL2CPPDUMPER_TRACE_H

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include "log.h"

// Architecture-independent, app-local diagnostics for Android 14.
// Never records traffic, certificates, app data, or credentials.
// This helper is deliberately best-effort: logging failure must not stop a game.
static inline void DumperTrace(const char *app_data_dir, const char *fmt, ...) {
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    LOGI("DUMPER_TRACE: %s", line);
    if (!app_data_dir || !*app_data_dir) return;

    char dir[1024] = {};
    if (snprintf(dir, sizeof(dir), "%s/files", app_data_dir) >=
        static_cast<int>(sizeof(dir))) return;

    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        LOGW("DUMPER_TRACE: could not create files directory, errno=%d", errno);
        return;
    }

    char path[1100] = {};
    if (snprintf(path, sizeof(path), "%s/dumper-status.txt", dir) >=
        static_cast<int>(sizeof(path))) return;

    FILE *f = fopen(path, "a");
    if (!f) {
        LOGW("DUMPER_TRACE: could not write status, errno=%d", errno);
        return;
    }
    fprintf(f, "pid=%ld %s\n", static_cast<long>(getpid()), line);
    fclose(f);
}
#endif
