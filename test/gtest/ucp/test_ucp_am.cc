#include <list>
#include <numeric>
#include <set>
#include <vector>

#include "ucp_datatype.h"
#include "ucp_test.h"



class test_ucp_am_base : public ucp_test {
public:
    int sent_ams;
    int recv_ams;
    static ucp_params_t get_ctx_params() {
        ucp_params_t params = ucp_test::get_ctx_params();
        params.field_mask |= UCP_PARAM_FIELD_FEATURES;
        params.features    = UCP_FEATURE_AM;
        return params;
    }

    static void ucp_put_am_cb(void *request, ucs_status_t status);
    static ucs_status_t ucp_process_am_cb(void *arg, void *data, 
                                          size_t length, unsigned flags);

    ucs_status_t am_handler(test_ucp_am_base *me, void *data, 
                            size_t  length, unsigned flags);
};

void test_ucp_am_base::ucp_put_am_cb(void *request, ucs_status_t status){
    return;
}

ucs_status_t test_ucp_am_base::ucp_process_am_cb(void *arg, void *data,
                                                 size_t length, unsigned flags){
    test_ucp_am_base *self = reinterpret_cast<test_ucp_am_base*>(arg);
    return self->am_handler(self, data, length, flags);
}

ucs_status_t test_ucp_am_base::am_handler(test_ucp_am_base *me, void *data, 
                                          size_t length, unsigned flags){
    std::vector<char> cmp(length, (char)length);
    std::vector<char> databuf(length, 'r');
    memcpy(&databuf[0], data, length);
    EXPECT_EQ(cmp, databuf);
    me->recv_ams++;
    return UCS_OK;
}

class test_ucp_am : public test_ucp_am_base
{
public:
    ucp_ep_params_t get_ep_params() {
        ucp_ep_params_t params = test_ucp_am_base::get_ep_params();
        params.field_mask |= UCP_EP_PARAM_FIELD_FLAGS;
        params.flags      |= UCP_EP_PARAMS_FLAGS_NO_LOOPBACK;
        return params;
    }
    virtual void init(){
        ucp_test::init();
        sender().connect(&receiver(), get_ep_params());
        receiver().connect(&sender(), get_ep_params());
    }

protected:
    void do_put_process_data_test();
};

void test_ucp_am::do_put_process_data_test()
{
    size_t buf_size = 512;
    char buf[buf_size];
    ucs_status_ptr_t sstatus;
    recv_ams = 0;
    sent_ams = 0;
    ucp_ep_set_am_handler(sender().ep(), 0, ucp_process_am_cb, this, UCP_CB_FLAG_ASYNC);
    ucp_ep_set_am_handler(receiver().ep(), 0, ucp_process_am_cb, this, UCP_CB_FLAG_ASYNC);
    
    for (size_t i = 8; i < buf_size + 1; i *= 2) {
        for(size_t j = 0; j < i; j++){
            buf[j] = (char)i;
        }
        sstatus = ucp_ep_am_put(receiver().ep(), 0, 
                                buf, 1, ucp_dt_make_contig(i), 
                                test_ucp_am_base::ucp_put_am_cb, 0);
        
        EXPECT_FALSE(UCS_PTR_IS_ERR(sstatus));
        
        wait(sstatus);
        sent_ams++;
    }
    
    while(sent_ams != recv_ams){
        progress();
    }
}


UCS_TEST_P(test_ucp_am, put_process_data_async) {
    do_put_process_data_test();
}

UCP_INSTANTIATE_TEST_CASE(test_ucp_am)

