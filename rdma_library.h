/*
 * RDMA Client Library (Kernel Space)
 * Not concurrent
 */

#ifndef _RDMA_LIB_H_
#define _RDMA_LIB_H_

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/blkdev.h>

typedef struct rdma_ctx* rdma_ctx_t;
typedef struct rdma_request* rdma_req_t;

typedef enum {RDMA_READ,RDMA_WRITE} RDMA_OP;

typedef struct rdma_request
{
    RDMA_OP rw;
    u64 dma_addr;
    uint32_t remote_offset;
    int length;
    struct batch_request* batch_req;
} rdma_request;

typedef struct batch_request
{
    int id;
    struct request * req;
    volatile int outstanding_reqs;
    struct batch_request* next;
} batch_request;

typedef struct batch_request_pool
{
    struct batch_request** data;
    int size;
    int head;
    int tail;
    spinlock_t lock;
} batch_request_pool;

struct rdma_ctx {
    struct socket *sock;
   
    struct ib_cq* send_cq, *recv_cq;
    struct ib_pd* pd;
    struct ib_qp* qp;
    struct ib_qp_init_attr qp_attr;
    struct ib_mr *mr;
    int rkey;

    int lid;
    int qpn;
    int psn;

    char* rdma_recv_buffer;
    u64 dma_addr;
    unsigned long long int rem_mem_size;

    int rem_qpn;
    int rem_psn;
    int rem_lid;
   
    unsigned long long int rem_vaddr;
    uint32_t rem_rkey;

    atomic64_t wr_count;

    volatile unsigned long outstanding_requests;
    //atomic_t outstanding_requests;
    wait_queue_head_t queue;
    atomic_t operation_count;
    wait_queue_head_t queue2;
    atomic_t comp_handler_count;

    struct batch_request_pool* pool;
};

batch_request_pool* get_batch_request_pool(int size);
void destroy_batch_request_pool(batch_request_pool* pool);
batch_request* get_batch_request(batch_request_pool* pool);
void return_batch_request(batch_request_pool* pool, batch_request* req);

u64 rdma_map_address(void* addr, int length);
void rdma_unmap_address(u64 addr, int length);
int rdma_library_ready(void);

int rdma_library_init(void);
int rdma_library_exit(void);

rdma_ctx_t rdma_init(int npages, char* ip_addr, int port, int mem_pool_size);
int rdma_exit(rdma_ctx_t);

int rdma_op(rdma_ctx_t ctx, rdma_req_t req, int n_requests);
#endif // _RDMA_LIB_H_


