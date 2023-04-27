#ifndef _DVDLOGGER_H_
#define _DVDLOGGER_H_

#include <dvdread/dvd_reader.h>

#include "dvdbackup.h"

typedef enum {
	DVDBACKUP_LOGGER_LEVEL_TRACE = 8,
	DVDBACKUP_LOGGER_LEVEL_PROGRESS
} dvdbackup_logger_level_t;

void DVDLog(void *priv, const dvd_logger_cb *logcb, int level, const char *fmt, ...);

#define XLOG(ctx, level, ...) \
  DVDLog(ctx, &ctx->logcb, level, __VA_ARGS__)
#define XLog0(ctx, ...) XLOG(ctx, DVD_LOGGER_LEVEL_ERROR, __VA_ARGS__)
#define XLog1(ctx, ...) XLOG(ctx, DVD_LOGGER_LEVEL_WARN,  __VA_ARGS__)
#define XLog2(ctx, ...) XLOG(ctx, DVD_LOGGER_LEVEL_INFO,  __VA_ARGS__)
#define XLog3(ctx, ...) XLOG(ctx, DVD_LOGGER_LEVEL_DEBUG, __VA_ARGS__)
#define XLog4(ctx, ...) XLOG(ctx, DVDBACKUP_LOGGER_LEVEL_TRACE, __VA_ARGS__)
#define XLog5(ctx, ...) XLOG(ctx, DVDBACKUP_LOGGER_LEVEL_PROGRESS, __VA_ARGS__)

#endif // _DVDLOGGER_H_
