#include <ngx_core.h>
#include <ngx_http.h>
#include <signal.h>
#include <redis_nginx_adapter.h>
#include <ngx_event.h>

#define SELECT_DATABASE_COMMAND "SELECT %d"
#define PING_DATABASE_COMMAND "PING"

int redis_nginx_event_attach(redisAsyncContext *ac);
void redis_nginx_cleanup(void *privdata);
void redis_nginx_ping_callback(redisAsyncContext *ac, void *rep, void *privdata);


void
redis_nginx_init(void)
{
    signal(SIGPIPE, SIG_IGN);
}

void
redis_nginx_controller()
{
	while(1){
		ngx_process_events_and_timers(ngx_cycle);
	}
}

void 
redis_nginx_log(int loglevel, const char* fmt, ...)
{
	char buf[512];
	va_list va;
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf)/sizeof(char), fmt, va);
	va_end(va);
	ngx_log_error(loglevel,  ngx_cycle->log, 0, buf);
}

void
redis_nginx_select_callback(redisAsyncContext *ac, void *rep, void *privdata)
{
    redisAsyncContext **context = privdata;
    redisReply *reply = rep;
    if ((reply == NULL) || (reply->type == REDIS_REPLY_ERROR)) {
        if (context != NULL) {
            *context = NULL;
        }
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not select redis database");
        redisAsyncFree(ac);
    }
}


redisAsyncContext *
redis_nginx_open_context(const char *host, int port, int database, redisAsyncContext **context)
{
    redisAsyncContext *ac = NULL;

    if ((context == NULL) || (*context == NULL) || (*context)->err) {
        ac = redisAsyncConnect(host, port);
        if (ac == NULL) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not allocate the redis context for %s:%d", host, port);
            return NULL;
        }

        if (ac->err) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not create the redis context for %s:%d - %s", host, port, ac->errstr);
            redisAsyncFree(ac);
            return NULL;
        }

        redis_nginx_event_attach(ac);

        if (context != NULL) {
            *context = ac;
        }

        redisAsyncCommand(ac, redis_nginx_select_callback, context, SELECT_DATABASE_COMMAND, database);
    } else {
        ac = *context;
    }

    return ac;
}


redisAsyncContext *
redis_nginx_open_context_unix(const char *path, int database, redisAsyncContext **context)
{
    redisAsyncContext *ac = NULL;

    if ((context == NULL) || (*context == NULL) || (*context)->err) {
        ac = redisAsyncConnectUnix(path);
        if (ac == NULL) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not allocate the redis context for %s", path);
            return NULL;
        }

        if (ac->err) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not create the redis context for %s - %s", path, ac->errstr);
            redisAsyncFree(ac);
            return NULL;
        }

        redis_nginx_event_attach(ac);

        if (context != NULL) {
            *context = ac;
        }

        redisAsyncCommand(ac, redis_nginx_select_callback, context, SELECT_DATABASE_COMMAND, database);
    } else {
        ac = *context;
    }

    return ac;
}


void
redis_nginx_force_close_context(redisAsyncContext **context)
{
    if ((context != NULL) && (*context != NULL)) {
        redisAsyncContext *ac = *context;
        if (!ac->err) {
            redisAsyncDisconnect(ac);
        }
        *context = NULL;
    }
}


void
redis_nginx_close_context(redisAsyncContext **context)
{
    if ((context != NULL) && (*context != NULL)) {
        redisAsyncContext *ac = *context;
        if (!ac->err) {
            redisAsyncCommand(ac, redis_nginx_ping_callback, NULL, PING_DATABASE_COMMAND);
        }
        *context = NULL;
    }
}
    
void
redis_nginx_ping_callback(redisAsyncContext *ac, void *rep, void *privdata)
{
    void *data = ac->data;
    void (*callback) (void *) = privdata;
    redisAsyncDisconnect(ac);
    if (callback != NULL) {
        callback(data);
    }
}


void
redis_nginx_read_event(ngx_event_t *ev)
{
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: redis_nginx_read_event");
    ngx_connection_t *connection = (ngx_connection_t *) ev->data;
    redisAsyncHandleRead(connection->data);
}


void
redis_nginx_write_event(ngx_event_t *ev)
{
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: redis_nginx_write_event");
    ngx_connection_t *connection = (ngx_connection_t *) ev->data;
    redisAsyncHandleWrite(connection->data);
}


int redis_nginx_fd_is_valid(int fd) {
    return (fd > 0) && ((fcntl(fd, F_GETFL) != -1) || (errno != EBADF));
}


void
redis_nginx_add_read(void *privdata)
{
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: redis_nginx_add_read");
    ngx_connection_t *connection = (ngx_connection_t *) privdata;
    if (!connection->read->active && redis_nginx_fd_is_valid(connection->fd)) {
        connection->read->handler = redis_nginx_read_event;
        connection->read->log = connection->log;
        if (ngx_add_event(connection->read, NGX_READ_EVENT, NGX_CLEAR_EVENT) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not add read event to redis");
        }
    }
}


void
redis_nginx_del_read(void *privdata)
{
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: redis_nginx_del_read");
    ngx_connection_t *connection = (ngx_connection_t *) privdata;
    if (connection->read->active && redis_nginx_fd_is_valid(connection->fd)) {
        if (ngx_del_event(connection->read, NGX_READ_EVENT, 0) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not delete read event to redis");
        }
    }
}


void
redis_nginx_add_write(void *privdata)
{
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: redis_nginx_add_write");
    ngx_connection_t *connection = (ngx_connection_t *) privdata;
    if (!connection->write->active && redis_nginx_fd_is_valid(connection->fd)) {
        connection->write->handler = redis_nginx_write_event;
        connection->write->log = connection->log;
        if (ngx_add_event(connection->write, NGX_WRITE_EVENT, NGX_CLEAR_EVENT) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not add write event to redis");
        }
    }
}


void
redis_nginx_del_write(void *privdata)
{
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: redis_nginx_del_write");
    ngx_connection_t *connection = (ngx_connection_t *) privdata;
    if (connection->write->active && redis_nginx_fd_is_valid(connection->fd)) {
        if (ngx_del_event(connection->write, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not delete write event to redis");
        }
    }
}


void
redis_nginx_cleanup(void *privdata)
{
    if (privdata) {
        ngx_connection_t *connection = (ngx_connection_t *) privdata;
        redisAsyncContext *ac = (redisAsyncContext *) connection->data;
        if (ac->err) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: connection to redis failed - %s", ac->errstr);
            /**
             * If the context had an error but the fd still valid is because another context got the same fd from OS.
             * So we clean the reference to this fd on redisAsyncContext and on ngx_connection, avoiding close a socket in use.
             */
            if (redis_nginx_fd_is_valid(ac->c.fd)) {
                ac->c.fd = -1;
                connection->fd = NGX_INVALID_FILE;
            }
        }

        if ((connection->fd != NGX_INVALID_FILE)) {
            redis_nginx_del_read(privdata);
            redis_nginx_del_write(privdata);
            ngx_close_connection(connection);
        } else {
            ngx_free_connection(connection);
        }

        ac->ev.data = NULL;
    }
}


int
redis_nginx_event_attach(redisAsyncContext *ac)
{
    ngx_connection_t *connection;
    redisContext *c = &(ac->c);

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: context already attached");
        return REDIS_ERR;
    }

    connection = ngx_get_connection(c->fd, ngx_cycle->log);
    if (connection == NULL) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "redis_nginx_adapter: could not get a connection for fd #%d", c->fd);
        return REDIS_ERR;
    }


    /* Register functions to start/stop listening for events */
    ac->ev.addRead = redis_nginx_add_read;
    ac->ev.delRead = redis_nginx_del_read;
    ac->ev.addWrite = redis_nginx_add_write;
    ac->ev.delWrite = redis_nginx_del_write;
    ac->ev.cleanup = redis_nginx_cleanup;
    ac->ev.data = connection;
    connection->data = ac;

    return REDIS_OK;
}
