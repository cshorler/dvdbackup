#include <stdio.h>
#include <stdarg.h>

#include <dvdread/dvd_reader.h>

#include "logger.h"

static void DVDLogProgress(FILE *stream, const char *fmt, va_list ap_) {
	va_list ap;

	fprintf(stream, "\r");
	fprintf(stream, "dvdbackup: ");

	va_copy(ap, ap_);
	vfprintf(stream, fmt, ap);
	va_end(ap);

	/* TBC: is stderr unbuffered on all platforms? */
	fflush(stream);
}

static void DVDLogDefault(FILE *stream, const char *fmt, va_list ap_) {
	va_list ap;

	fprintf(stream, "dvdbackup: ");

	va_copy(ap, ap_);
	vfprintf(stream, fmt, ap);
	va_end(ap);

	fprintf(stream, "\n");
}

void DVDLog(void *priv, const dvd_logger_cb *logcb, int level, const char *fmt, ... )
{
	va_list ap;
	va_start(ap, fmt);
	if(logcb && logcb->pf_log)
		logcb->pf_log(priv, level, fmt, ap);
	else
	{
		if (pApp->last_level != level) {
			if (pApp->last_level == DVDBACKUP_LOGGER_LEVEL_PROGRESS) fprintf(stderr, "\n");
			pApp->last_level = level;
		}
		switch (level) {
			case DVD_LOGGER_LEVEL_ERROR:
			case DVD_LOGGER_LEVEL_DEBUG:
			case DVD_LOGGER_LEVEL_WARN:
			case DVDBACKUP_LOGGER_LEVEL_TRACE:
				DVDLogDefault(stderr, fmt, ap);
				break;
			case DVDBACKUP_LOGGER_LEVEL_PROGRESS:
				DVDLogProgress(stderr, fmt, ap);
				break;
			default:
				DVDLogDefault(stdout, fmt, ap);
		}
	}
	va_end(ap);
}
