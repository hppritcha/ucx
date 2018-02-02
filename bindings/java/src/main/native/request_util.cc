/*
 * Copyright (C) Mellanox Technologies Ltd. 2001-2017.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */
#include "request_util.h"
#include "worker.h"

void request_util::request_handler::request_init(void *request) {
    jucx_request* req   = (jucx_request*) request;

    req->request_worker = NULL;
}
