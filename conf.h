#ifndef _CONF_H_
#define _CONF_H_

#define CUSTOM_MAKE_REQ_FN false
#define KERNEL_SECTOR_SIZE   512
#define SECTORS_PER_PAGE  (PAGE_SIZE / KERNEL_SECTOR_SIZE)
#define DEVICE_BOUND 100
#define REQ_ARR_SIZE 10
#define MAX_REQ 1024
#define MERGE_REQ true
#define REQ_POOL_SIZE 1024

#endif
