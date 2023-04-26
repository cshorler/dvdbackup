#include <alloca.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include <dvdread/dvd_reader.h>

#include <libpq-fe.h>

#include "dvdbackup.h"
#include "logger.h"

#define CONNINFO_MAX_SZ 128

void dvdbackup_logdb_init(PGconn **conn) {
	char conninfo[CONNINFO_MAX_SZ];

	uid_t uid = geteuid();
	struct passwd *pw = getpwuid(uid);

	snprintf(conninfo, CONNINFO_MAX_SZ, "port=5432 dbname=%s", pw ? pw->pw_name : "BAD_USER");
	*conn = PQconnectdb(conninfo);
}

void dvdbackup_logdb_exit(PGconn *conn) {
	PQfinish(conn);
}

void dvdbackup_logdb(void *priv, int lvl, const char *fmt, va_list ap_) {
	char loglevel[9];
	char pid_s[10];
	const char * values[3];
	PGresult *res;
	app_data_t *pApp = priv;
	char *buf = NULL;
	int bufsz = 0;

	va_list ap;

	va_copy(ap, ap_);
	bufsz = vsnprintf(buf, 0, fmt, ap);
	va_end(ap);

	if (bufsz < 0) {
		fprintf(stderr, "%s:%d\t failed to calculate buffer size\n", __FILE__, __LINE__);
		return;
	}

	bufsz++;
	buf = alloca(bufsz);
	if (!buf) {
		fprintf(stderr, "%s:%d\t failed to allocate buffer\n", __FILE__, __LINE__);
		return;
	}

	va_copy(ap, ap_);
	bufsz = vsnprintf(buf, bufsz, fmt, ap);
	va_end(ap);

	if (bufsz < 0) {
		fprintf(stderr, "%s:%d \t failed to copy format string\n", __FILE__, __LINE__);
		return;
	}

	switch (lvl) {
		case DVD_LOGGER_LEVEL_INFO:
			strncpy(loglevel, "INFO", 9);
			break;
		case DVD_LOGGER_LEVEL_ERROR:
			strncpy(loglevel, "ERROR", 9);
			break;
		case DVD_LOGGER_LEVEL_WARN:
			strncpy(loglevel, "WARN", 9);
			break;
		case DVD_LOGGER_LEVEL_DEBUG:
			strncpy(loglevel, "DEBUG", 9);
			break;
		case DVDBACKUP_LOGGER_LEVEL_TRACE:
			strncpy(loglevel, "TRACE", 9);
			break;
		case DVDBACKUP_LOGGER_LEVEL_PROGRESS:
			strncpy(loglevel, "PROGRESS", 9);
			break;
		default:
			strncpy(loglevel, "UNK", 9);
	}

	snprintf(pid_s, 10, "%d", pApp->pid);

	values[0] = pid_s;
	values[1] = loglevel;
	values[2] = buf;
	res = PQexecParams(pApp->conn, "INSERT INTO dvdbackup_tbl (pid, lvl, msg) VALUES ($1::integer, $2::text, $3::text)", 3, NULL, values, NULL, NULL, 0);

}

