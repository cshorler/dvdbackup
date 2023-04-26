#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <dvdread/dvd_reader.h>

#include "dvdbackup.h"

typedef enum {
	DVDBACKUP_LOGGER_LEVEL_TRACE = 8,
	DVDBACKUP_LOGGER_LEVEL_PROGRESS
} dvdbackup_logger_level_t;

void DVDLog(void *priv, const dvd_logger_cb *logcb, int level, const char *fmt, ...);

#define LOG(ctx, level, ...) \
  DVDLog(ctx, &ctx->logcb, level, __VA_ARGS__)
#define Log0(ctx, ...) LOG(ctx, DVD_LOGGER_LEVEL_ERROR, __VA_ARGS__)
#define Log1(ctx, ...) LOG(ctx, DVD_LOGGER_LEVEL_WARN,  __VA_ARGS__)
#define Log2(ctx, ...) LOG(ctx, DVD_LOGGER_LEVEL_INFO,  __VA_ARGS__)
#define Log3(ctx, ...) LOG(ctx, DVD_LOGGER_LEVEL_DEBUG, __VA_ARGS__)
#define Log4(ctx, ...) LOG(ctx, DVDBACKUP_LOGGER_LEVEL_TRACE, __VA_ARGS__)
#define Log5(ctx, ...) LOG(ctx, DVDBACKUP_LOGGER_LEVEL_PROGRESS, __VA_ARGS__)

#endif // _LOGGER_H_
