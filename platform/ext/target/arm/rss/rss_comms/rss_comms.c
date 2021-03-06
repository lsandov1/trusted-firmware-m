/*
 * Copyright (c) 2022, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "rss_comms.h"

#include <stddef.h>
#include <stdint.h>

#include "rss_comms_hal.h"
#include "rss_comms_queue.h"
#include "tfm_rpc.h"
#include "tfm_multi_core.h"
#include "tfm_psa_call_pack.h"
#include "tfm_spm_log.h"

static struct client_request_t *req_to_process;

static psa_status_t message_dispatch(struct client_request_t *req)
{
    /* Create the call parameters */
    struct client_call_params_t spm_params = {
        .handle = req->msg.handle,
        .type = PARAM_UNPACK_TYPE(req->msg.ctrl_param),
        .in_vec = req->in_vec,
        .in_len = PARAM_UNPACK_IN_LEN(req->msg.ctrl_param),
        .out_vec = req->out_vec,
        .out_len = PARAM_UNPACK_OUT_LEN(req->msg.ctrl_param),
    };

    SPMLOG_DBGMSG("[RSS-COMMS] Dispatching message\r\n");
    SPMLOG_DBGMSGVAL("handle=", spm_params.handle);
    SPMLOG_DBGMSGVAL("type=", spm_params.type);
    SPMLOG_DBGMSGVAL("in_len=", spm_params.in_len);
    SPMLOG_DBGMSGVAL("out_len=", spm_params.out_len);
    if (spm_params.in_len > 0) {
        SPMLOG_DBGMSGVAL("in_vec[0].len=", spm_params.in_vec[0].len);
    }
    if (spm_params.in_len > 1) {
        SPMLOG_DBGMSGVAL("in_vec[1].len=", spm_params.in_vec[1].len);
    }
    if (spm_params.in_len > 2) {
        SPMLOG_DBGMSGVAL("in_vec[2].len=", spm_params.in_vec[2].len);
    }
    if (spm_params.in_len > 3) {
        SPMLOG_DBGMSGVAL("in_vec[3].len=", spm_params.in_vec[3].len);
    }
    if (spm_params.out_len > 0) {
        SPMLOG_DBGMSGVAL("out_vec[0].len=", spm_params.out_vec[0].len);
    }
    if (spm_params.out_len > 1) {
        SPMLOG_DBGMSGVAL("out_vec[1].len=", spm_params.out_vec[1].len);
    }
    if (spm_params.out_len > 2) {
        SPMLOG_DBGMSGVAL("out_vec[2].len=", spm_params.out_vec[2].len);
    }
    if (spm_params.out_len > 3) {
        SPMLOG_DBGMSGVAL("out_vec[3].len=", spm_params.out_vec[3].len);
    }

    return tfm_rpc_psa_call(&spm_params);
}

static void rss_comms_reply(const void *owner, int32_t ret)
{
    uint32_t i;
    struct client_request_t *req = (struct client_request_t *)owner;

    /* Create reply message */
    req->reply.protocol_ver = req->msg.protocol_ver;
    req->reply.seq_num = req->msg.seq_num;
    req->reply.client_id = req->msg.client_id;
    req->reply.return_val = ret;

    /* Populate outvec sizes with values returned from service */
    for (i = 0; i < PARAM_UNPACK_OUT_LEN(req->msg.ctrl_param); ++i) {
        req->reply.out_size[i] = req->out_vec[i].len;
    }

    SPMLOG_DBGMSG("[RSS-COMMS] Sending reply\r\n");
    SPMLOG_DBGMSGVAL("protocol_ver=", req->reply.protocol_ver);
    SPMLOG_DBGMSGVAL("seq_num=", req->reply.seq_num);
    SPMLOG_DBGMSGVAL("client_id=", req->reply.client_id);
    SPMLOG_DBGMSGVAL("return_val=", req->reply.return_val);
    SPMLOG_DBGMSGVAL("out_size[0]=", req->reply.out_size[0]);
    SPMLOG_DBGMSGVAL("out_size[1]=", req->reply.out_size[1]);
    SPMLOG_DBGMSGVAL("out_size[2]=", req->reply.out_size[2]);
    SPMLOG_DBGMSGVAL("out_size[3]=", req->reply.out_size[3]);

    if (tfm_multi_core_hal_reply(req) != TFM_PLAT_ERR_SUCCESS) {
        SPMLOG_DBGMSG("[RSS-COMMS] Sending reply failed!\r\n");
    }
}

static void rss_comms_handle_req(void)
{
    psa_status_t status;
    void *queue_entry;

    /* FIXME: consider memory limitations that may prevent dispatching all
     * messages in one go.
     */
    while (queue_dequeue(&queue_entry) == 0) {
        /* Deliver PSA Client call request to handler in SPM. */
        req_to_process = queue_entry;
        status = message_dispatch(req_to_process);
        if (status != PSA_SUCCESS) {
            SPMLOG_DBGMSGVAL("[RSS-COMMS] Message dispatch failed: ", status);
            rss_comms_reply(req_to_process, status);
        }
    }
}

static const void *rss_comms_get_caller_data(int32_t client_id)
{
    (void)client_id;

    return req_to_process;
}

static struct tfm_rpc_ops_t rpc_ops = {
    .handle_req = rss_comms_handle_req,
    .reply = rss_comms_reply,
    .get_caller_data = rss_comms_get_caller_data,
};

int32_t tfm_inter_core_comm_init(void)
{
    int32_t ret;

    /* Register RPC callbacks */
    ret = tfm_rpc_register_ops(&rpc_ops);
    if (ret != TFM_RPC_SUCCESS) {
        return ret;
    }

    /* Platform specific initialization */
    ret = tfm_multi_core_hal_init();
    if (ret != TFM_PLAT_ERR_SUCCESS) {
        tfm_rpc_unregister_ops();
        return ret;
    }

    return TFM_RPC_SUCCESS;
}
