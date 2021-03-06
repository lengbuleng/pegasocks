#include "pgs_log.h"
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>

static char *log_levels[] = { "DEBUG", "INFO", "WARN", "ERROR" };
static char *log_colors[] = { "\e[01;32m", "\e[01;32m", "\e[01;35m",
			      "\e[01;31m" };

pgs_logger_msg_t *pgs_logger_msg_new(char *msg, pgs_tid tid)
{
	pgs_logger_msg_t *ptr = pgs_malloc(sizeof(pgs_logger_msg_t));
	ptr->msg = msg;
	ptr->tid = tid;
	return ptr;
}
void pgs_logger_msg_free(pgs_logger_msg_t *lmsg)
{
	pgs_free(lmsg->msg);
	pgs_free(lmsg);
}

void pgs_logger_debug_buffer(pgs_logger_t *logger, unsigned char *buf, int size)
{
	char hexbuf[2 * size + 1];
	for (int i = 0; i < size; i++) {
		sprintf(hexbuf + i * 2, "%02x", (int)buf[i]);
	}
	hexbuf[2 * size] = '\0';
	pgs_logger_debug(logger, "%s", hexbuf);
}

pgs_logger_t *pgs_logger_new(pgs_mpsc_t *mpsc, LOG_LEVEL level, bool isatty)
{
	pgs_logger_t *ptr = pgs_malloc(sizeof(pgs_logger_t));
	ptr->level = level;
	ptr->mpsc = mpsc;
	ptr->tid = (pgs_tid)pthread_self();
	ptr->isatty = isatty;
	return ptr;
}

void pgs_logger_free(pgs_logger_t *logger)
{
	pgs_free(logger);
}

void pgs_logger_log(LOG_LEVEL level, pgs_logger_t *logger, const char *fmt, ...)
{
	va_list args;

	if (level < logger->level) {
		return;
	}

	// construct string, then send to mpsc
	// LEVEL date-time tid: MSG
	char msg[MAX_MSG_LEN - 64];
	char datetime[64];
	va_start(args, fmt);
	vsprintf(msg, fmt, args);
	va_end(args);

	time_t t;
	struct tm *now;
	time(&t);
	now = localtime(&t);
	strftime(datetime, sizeof(datetime), TIME_FORMAT, now);

	char *m = pgs_malloc(sizeof(char) * MAX_MSG_LEN);

	if (logger->isatty) {
		sprintf(m, "%s%s [thread-%04d] %s: \e[0m%s", log_colors[level],
			datetime, (int)(logger->tid % 10000), log_levels[level],
			msg);

	} else {
		sprintf(m, "%s [thread-%04d] %s: %s", datetime,
			(int)(logger->tid % 10000), log_levels[level], msg);
	}
	pgs_logger_msg_t *_msg = pgs_logger_msg_new(m, logger->tid);

	pgs_mpsc_send(logger->mpsc, _msg);
}

// directly send to log file
// called from main thread
void pgs_logger_main_log(LOG_LEVEL level, FILE *output, const char *fmt, ...)
{
	va_list args;
	// LEVEL date-time: MSG
	char msg[MAX_MSG_LEN - 64];
	char datetime[64];
	va_start(args, fmt);
	vsprintf(msg, fmt, args);
	va_end(args);

	time_t t;
	struct tm *now;
	time(&t);
	now = localtime(&t);
	strftime(datetime, sizeof(datetime), TIME_FORMAT, now);

	if (isatty(fileno(output))) {
		fprintf(output, "%s%s [thread-main] %s: \e[0m%s\n",
			log_colors[level], datetime, log_levels[level], msg);
	} else {
		fprintf(output, "%s [thread-main] %s: %s\n", datetime,
			log_levels[level], msg);
	}

	fflush(output);
}

pgs_logger_server_t *pgs_logger_server_new(pgs_logger_t *logger, FILE *output)
{
	pgs_logger_server_t *ptr = pgs_malloc(sizeof(pgs_logger_server_t));
	ptr->logger = logger;
	// update thread id
	ptr->logger->tid = (pgs_tid)pthread_self();
	ptr->output = output;
	return ptr;
}

void pgs_logger_server_free(pgs_logger_server_t *server)
{
	if (server->output != stderr && server->output != NULL) {
		fclose(server->output);
	}
	pgs_logger_free(server->logger);
	pgs_free(server);
}

// drain log and write to output
void pgs_logger_server_serve(pgs_logger_server_t *server)
{
	// FIXME: busy loop and sleep here
	// may use condvar and mutex
	while (1) {
		pgs_logger_msg_t *msg = pgs_mpsc_recv(server->logger->mpsc);
		if (msg != NULL) {
			fprintf(server->output, "%s\n", msg->msg);
			fflush(server->output);
			pgs_logger_msg_free(msg);
		} else {
			sleep(1);
		}
	}
}

void *start_logger(void *ctx)
{
	pgs_logger_server_serve((pgs_logger_server_t *)ctx);

	return 0;
}
