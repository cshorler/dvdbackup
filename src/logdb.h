#ifndef LOGDB_H_
#define LOGDB_H_

#include <dvdread/dvd_reader.h>
#include <libpq-fe.h>

void dvdbackup_logdb_init(PGconn **);
void dvdbackup_logdb_exit(PGconn *);
void dvdbackup_logdb(void *, dvd_logger_level_t, const char *, va_list);

#endif // LOGDB_H_
