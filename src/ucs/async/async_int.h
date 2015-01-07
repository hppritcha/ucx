/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
*
* $COPYRIGHT$
* $HEADER$
*/

#ifndef UCS_ASYNC_INT_H
#define UCS_ASYNC_INT_H

#include "async.h"

#include <ucs/datastruct/queue.h>


/**
 * Dispatch event coming from async context.
 *
 * @param id         Handler to dispatch.
 * @param from_async Whether the handler is called from async context or main context.
 */
ucs_status_t ucs_async_dispatch_handler(int id, int from_async);


/**
 * Run a callback on the async context of an event handler.
 */
typedef ucs_status_t (*ucs_async_check_handler_cb_t)(int id, ucs_async_context_t *async);
ucs_status_t ucs_async_check_handler(int id, ucs_async_check_handler_cb_t cb);


/**
 * Operation for specific async event delivery method.
 */
typedef struct ucs_async_ops {
    void         (*init)();
    void         (*cleanup)();

    ucs_status_t (*context_init)(ucs_async_context_t *async);
    int          (*context_try_block)(ucs_async_context_t *async, int from_async);
    void         (*context_unblock)(ucs_async_context_t *async);

    ucs_status_t (*add_event_fd)(ucs_async_context_t *async, int event_fd,
                                 int events);
    ucs_status_t (*remove_event_fd)(ucs_async_context_t *async, int event_fd);

    ucs_status_t (*add_timer)(ucs_async_context_t *async, int timer_id,
                              ucs_time_t interval);
    ucs_status_t (*remove_timer)(ucs_async_context_t *async, int timer_id);
} ucs_async_ops_t;


extern ucs_async_ops_t ucs_async_thread_ops;
extern ucs_async_ops_t ucs_async_signal_ops;

#endif