/**
* Copyright (C) Los Alamos National Security, LLC. 2018 ALL RIGHTS RESERVED.
* 
* See file LICENSE for terms.
*/

#include <ucp/core/ucp_ep.h>
#include <ucp/core/ucp_ep.inl>
#include <ucp/core/ucp_worker.h>
#include <ucp/core/ucp_context.h>
#include <ucp/proto/proto.h>
#include <ucp/proto/proto_am.inl>
#include <ucp/dt/dt.h>
#include <ucp/dt/dt.inl>

ucs_status_t ucp_worker_set_am_handler(ucp_worker_h worker, uint8_t id, 
                                       ucp_am_callback_t cb, void *arg, 
                                       uint32_t flags)
{
    uct_am_callback_t am_cb = cb;
    ucp_worker_iface_t *w_iface = worker->ifaces;
    ucp_context_h      context = worker->context;
    uint8_t am_id;
    ucs_status_t ret;
    uint32_t uct_flags = 0;
    int iface_index;

    ucs_assert(id <= (UCT_AM_ID_MAX - 1)-UCP_AM_ID_LAST);
    
    am_id = id + UCP_AM_ID_LAST;
    
    if(flags & UCP_CB_FLAG_ASYNC){
        uct_flags |= UCT_CB_FLAG_ASYNC;
    } else if(flags & UCP_CB_FLAG_SYNC){
        uct_flags |= UCT_CB_FLAG_SYNC;
    }

    UCP_THREAD_CS_ENTER_CONDITIONAL(&worker->mt_lock);
    UCS_ASYNC_BLOCK(&worker->async);
 
    ret = UCS_OK;
    for(iface_index = 0; iface_index < context->num_tls; iface_index++){
        ret = uct_iface_set_am_handler(w_iface[iface_index].iface, 
                                       am_id, am_cb, 
                                       arg, uct_flags);
    }

    UCS_ASYNC_UNBLOCK(&worker->async);
    UCP_THREAD_CS_EXIT_CONDITIONAL(&worker->mt_lock);
    
    return ret;
}

size_t bcopy_pack_args(void *dest, void *arg)
{
    ucp_request_t *args = arg;
    memcpy(dest, args->send.buffer, args->send.length);
    return args->send.length;
}

ucs_status_t ucp_ep_am_put_short(ucp_ep_h ep, uint8_t id, 
                                 const void *payload, size_t length)
{
    uct_ep_h am_ep = ucp_ep_get_am_uct_ep(ep);
    uint64_t header = *(uint64_t *)payload;
    void *buf;
    unsigned short_length;
    
    if(length > sizeof(header)){
        buf = (char *)payload + sizeof(header);
        short_length = length - sizeof(header);
    } else{
        buf = NULL;
        short_length = 0;
    }

    return uct_ep_am_short(am_ep, id, header, buf, short_length);
}

static ucs_status_t ucp_am_contig_short(uct_pending_req_t *self)
{
    ucp_request_t *req   = ucs_container_of(self, ucp_request_t, send.uct);
    ucs_status_t  status = ucp_ep_am_put_short(req->send.ep, 
                                               req->send.am.am_id, 
                                               req->send.buffer, 
                                               req->send.length);

    if(ucs_likely(status == UCS_OK)){
        ucp_request_complete_send(req, UCS_OK);
    }
    return status;
}

static ucs_status_t ucp_am_bcopy_single(uct_pending_req_t *self)
{
    ucs_status_t status;
    ucp_request_t *req = ucs_container_of(self, ucp_request_t, send.uct);
    status = ucp_do_am_bcopy_single(self, req->send.am.am_id, 
                                    bcopy_pack_args);

    if(status == UCS_OK){
        ucp_request_send_generic_dt_finish(req);
        ucp_request_complete_send(req, UCS_OK);
    }

    return status;
}

static ucs_status_t ucp_am_bcopy_multi(uct_pending_req_t *self)
{
    ucs_warn("no multiple active message support, reduce message size");
    return UCS_ERR_NOT_IMPLEMENTED;
}

static ucs_status_t ucp_am_zcopy_single(uct_pending_req_t *self)
{
    ucp_request_t *req = ucs_container_of(self, ucp_request_t, send.uct);
    return ucp_do_am_zcopy_single(self, req->send.am.am_id, NULL,
                                  0, ucp_proto_am_zcopy_req_complete);
}

static ucs_status_t ucp_am_zcopy_multi(uct_pending_req_t *self)
{
    ucs_warn("no multiple active message support, reduce message size");
    return UCS_ERR_NOT_IMPLEMENTED;
}

static void ucp_am_send_req_init(ucp_request_t *req, ucp_ep_h ep,
                                 void *buffer, uintptr_t datatype,
                                 size_t count, uint16_t flags, 
                                 uint8_t am_id)
{
    req->flags = flags;
    req->send.ep = ep;
    req->send.am.am_id = am_id;
    req->send.buffer = buffer;
    req->send.datatype = datatype;
    req->send.mem_type = UCT_MD_MEM_TYPE_HOST;
    req->send.lane = ep->am_lane;
    ucp_request_send_state_init(req, datatype, count);
    req->send.length = ucp_dt_length(req->send.datatype, count,
                                     req->send.buffer,
                                     &req->send.state.dt);
}

static UCS_F_ALWAYS_INLINE ucs_status_ptr_t
ucp_am_send_req(ucp_request_t *req, size_t count,
                const ucp_ep_msg_config_t * msg_config,
                ucp_send_callback_t cb, const ucp_proto_t *proto)
{
    size_t zcopy_thresh = ucp_proto_get_zcopy_threshold(req, msg_config,
                                                       count, SIZE_MAX);
    ssize_t max_short   = ucp_proto_get_short_max(req, msg_config);

    ucs_status_t status = ucp_request_send_start(req, max_short, 
                                                 zcopy_thresh, SIZE_MAX,
                                                 count, msg_config,
                                                 proto);
    if(status != UCS_OK){
      return UCS_STATUS_PTR(status);
    }

    /*Start the request.
     * If it is completed immediately, release the request and return the status.
     * Otherwise, return the request.
     */
    status = ucp_request_send(req);
    if(req->flags & UCP_REQUEST_FLAG_COMPLETED){
        ucs_trace_req("releasing send request %p, returning status %s", req,
                      ucs_status_string(status));
        ucp_request_put(req);
        return UCS_STATUS_PTR(status);
    }

    ucp_request_set_callback(req, send.cb, cb);
    ucs_trace_req("returning send request %p", req);
    
    return req + 1;
}

ucs_status_ptr_t ucp_ep_am_put(ucp_ep_h ep, uint8_t id,
                           void *payload, size_t count,
                           uintptr_t datatype, ucp_send_callback_t cb,
                           unsigned flags)
{
    uint8_t am_id = id + UCP_AM_ID_LAST;
    ucs_status_t status;
    ucs_status_ptr_t ret;
    size_t length;
    ucp_request_t *req;

    UCP_THREAD_CS_ENTER_CONDITIONAL(&ep->worker->mt_lock);
    
    if(ucs_unlikely(flags != 0)){
        ret = UCS_STATUS_PTR(UCS_ERR_NOT_IMPLEMENTED);
        goto out;
    }
    if(ucs_likely(UCP_DT_IS_CONTIG(datatype))){
        length = ucp_contig_dt_length(datatype, count);
        if(ucs_likely((ssize_t)length <= ucp_ep_config(ep)->am.max_short)){
            status = ucp_ep_am_put_short(ep, am_id, payload, length);
            if(ucs_likely(status != UCS_ERR_NO_RESOURCE)){
                UCP_EP_STAT_TAG_OP(ep, EAGER);
                ret = UCS_STATUS_PTR(status);
                goto out;
            }   
        }
    }

    req = ucp_request_get(ep->worker);
    if(ucs_unlikely(req == NULL)){
        ret = UCS_STATUS_PTR(UCS_ERR_NO_MEMORY);
        goto out;
    }

    ucp_am_send_req_init(req, ep, payload, datatype, count, flags, am_id);
  
    ret = ucp_am_send_req(req, count, &ucp_ep_config(ep)->am, cb,
                          ucp_ep_config(ep)->am_u.proto);

out:
    UCP_THREAD_CS_EXIT_CONDITIONAL(&ep->worker->mt_lock);
    return ret;
}

const ucp_proto_t ucp_am_proto = {
    .contig_short           = ucp_am_contig_short,
    .bcopy_single           = ucp_am_bcopy_single,
    .bcopy_multi            = ucp_am_bcopy_multi,
    .zcopy_single           = ucp_am_zcopy_single,
    .zcopy_multi            = ucp_am_zcopy_multi,
    .zcopy_completion       = ucp_proto_am_zcopy_completion,
    .only_hdr_size          = 0,
    .first_hdr_size         = 0,
    .mid_hdr_size           = 0
};
