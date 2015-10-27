#ifndef __REDIS_NGINX_ADAPTER_H
#define __REDIS_NGINX_ADAPTER_H

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

void redis_nginx_init(void);
redisAsyncContext *redis_nginx_open_context(const char *host, int port, int database, redisAsyncContext **context);
redisAsyncContext *redis_nginx_open_context_unix(const char *path, int database, redisAsyncContext **context);
void redis_nginx_force_close_context(redisAsyncContext **context);
void redis_nginx_close_context(redisAsyncContext **context);
void redis_nginx_ping_callback(redisAsyncContext *ac, void *rep, void *privdata);

// added by nokiddin
void redis_nginx_log(int loglevel, const char* fmt, ...);
void redis_nginx_controller();


/* Compatible with ngx_log.h log levels */
#define REDIS_LOG_EMERG             1
#define REDIS_LOG_ALERT             2
#define REDIS_LOG_CRIT              3
#define REDIS_LOG_ERR               4
#define REDIS_LOG_WARN              5
#define REDIS_LOG_NOTICE            6
#define REDIS_LOG_INFO              7
#define REDIS_LOG_DEBUG             8



#endif // __REDIS_NGINX_ADAPTER_H
