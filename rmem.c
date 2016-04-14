/*
 * A sample, extra-simple block driver. Updated for kernel 2.6.31.
 *
 * (C) 2003 Eklektix, Inc.
 * (C) 2010 Pat Patterson <pat at superpat dot com>
 * Redistributable under the terms of the GNU GPL.
 * Modified by Sangjin Han (sangjin@eecs.berkeley.edu) and Peter Gao (petergao@berkeley.edu)
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/random.h>
#include <linux/un.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/delay.h>

#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rdma_library.h"
#include "log.h"
MODULE_LICENSE("Dual BSD/GPL");

static int npages = 2048 * 1024; 
module_param(npages, int, 0); 

static char* servers = "localhost:8888";
module_param(servers, charp, 0);

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE   512
#define SECTORS_PER_PAGE  (PAGE_SIZE / KERNEL_SECTOR_SIZE)
#define DEVICE_BOUND 100
#define MAX_REQ 1024
#define MERGE_REQ true
/*
 * The internal representation of our device.
 */
struct rmem_device {
  unsigned long size;
  spinlock_t lock;
  spinlock_t rdma_lock;
  struct gendisk *gd;
  int major_num;
  rdma_ctx_t rdma_ctx;
  rdma_request rdma_req[MAX_REQ];
};

struct rmem_device* devices[DEVICE_BOUND];

int initd = 0;

static void rmem_request(struct request_queue *q) 
{
  struct request *req;
  rdma_req_t rdma_req_p;
  rdma_req_t last_rdma_req_p = NULL;
  int i, count = 0;
  unsigned long flags;


  LOG_KERN(LOG_INFO, "======New rmem request======", 0);
  //spin_lock_irqsave(&devices[q->id]->rdma_lock, flags);
  req = blk_fetch_request(q);
  while (req != NULL) {
    if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
      printk (KERN_NOTICE "Skip non-CMD request\n");
      __blk_end_request_all(req, -EIO);
      continue;
    }
//    rmem_transfer(devices[q->id], blk_rq_pos(req), blk_rq_cur_sectors(req),
//        bio_data(req->bio), rq_data_dir(req));
    rdma_req_p = devices[q->id]->rdma_req + count;
    
    rdma_req_p->rw = rq_data_dir(req)?RDMA_WRITE:RDMA_READ;
    rdma_req_p->length = PAGE_SIZE * blk_rq_cur_sectors(req) / SECTORS_PER_PAGE;
    rdma_req_p->dma_addr = rdma_map_address(bio_data(req->bio), rdma_req_p->length);
    rdma_req_p->remote_offset = blk_rq_pos(req) / SECTORS_PER_PAGE * PAGE_SIZE;
    if(MERGE_REQ && count > 0){
      last_rdma_req_p = devices[q->id]->rdma_req + count - 1;
      if(rdma_req_p->rw == last_rdma_req_p->rw && 
            last_rdma_req_p->dma_addr + last_rdma_req_p->length == rdma_req_p->dma_addr &&
            last_rdma_req_p->remote_offset + last_rdma_req_p->length == rdma_req_p->remote_offset)
        last_rdma_req_p->length += rdma_req_p->length;
      else{
        LOG_KERN(LOG_INFO, "Constructing RDMA req w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", last_rdma_req_p->rw == RDMA_WRITE, last_rdma_req_p->dma_addr, bio_data(req->bio), last_rdma_req_p->remote_offset, last_rdma_req_p->length);
        count++;
      }
    }else{
      LOG_KERN(LOG_INFO, "Constructing RDMA req w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", rdma_req_p->rw == RDMA_WRITE, rdma_req_p->dma_addr, bio_data(req->bio), rdma_req_p->remote_offset, rdma_req_p->length);
      count++;
    }
    if(count >= MAX_REQ){
      LOG_KERN(LOG_INFO, "Sending %d rdma reqs", count);
      rdma_op(devices[q->id]->rdma_ctx, devices[q->id]->rdma_req, count);
      LOG_KERN(LOG_INFO, "Finished %d rdma reqs", count);
      for(i = 0; i < count; i++)
        rdma_unmap_address(devices[q->id]->rdma_req[i].dma_addr, devices[q->id]->rdma_req[i].length);
      count = 0;
    }
    //LOG_KERN(LOG_INFO, ("Done.\n"));
    if ( ! __blk_end_request_cur(req, 0) ) {
      req = blk_fetch_request(q);
    }
  }
  if(count) {
    LOG_KERN(LOG_INFO, "Sending %d rdma reqs", count);
    rdma_op(devices[q->id]->rdma_ctx, devices[q->id]->rdma_req, count);
    LOG_KERN(LOG_INFO, "Finished %d rdma reqs", count);
    for(i = 0; i < count; i++)
      rdma_unmap_address(devices[q->id]->rdma_req[i].dma_addr, devices[q->id]->rdma_req[i].length);
  }
  LOG_KERN(LOG_INFO, "======End of rmem request======\n", 0);
  //spin_unlock_irqrestore(&devices[q->id]->rdma_lock, flags);
}

/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), which
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
int rmem_getgeo(struct block_device * block_device, struct hd_geometry * geo) {
  long size;

  /* We have no real geometry, of course, so make something up. */
  //size = device.size * (PAGE_SIZE / KERNEL_SECTOR_SIZE);
  size = npages * PAGE_SIZE * (PAGE_SIZE / KERNEL_SECTOR_SIZE);
  geo->cylinders = (size & ~0x3f) >> 6;
  geo->heads = 4;
  geo->sectors = 16;
  geo->start = 0;
  return 0;
}

/*
 * The device operations structure.
 */
static struct block_device_operations rmem_ops = {
  .owner  = THIS_MODULE,
  .getgeo = rmem_getgeo
};


static int __init rmem_init(void) {
  int c,major_num;
  struct rmem_device* device;
  struct request_queue *queue;
  char dev_name[20];
  char *servers_str_p = servers;
  char delim[] = ",:";
  char* tmp_srv;
  char* tmp_port;
  char* tmp_port_end_p;
  int port;

  
  for(c = 0; c < DEVICE_BOUND; c++) {
    devices[c] = NULL;
  }

  pr_info("Start rmem_rdma. rdma_library_init()\n");
  pr_info("init success? %d\n", rdma_library_init());

  while(!rdma_library_ready());
  pr_info("init done\n");


  while(1){
    queue = NULL;
    tmp_srv = strsep(&servers_str_p, delim);
    tmp_port = strsep(&servers_str_p, delim);
    if (tmp_srv && tmp_port){
      port = simple_strtol(tmp_port, &tmp_port_end_p, 10);
      if(tmp_port_end_p == NULL){
        pr_info("Incorrect port %s\n", tmp_port);
        goto out;
      }
      pr_info("Connecting to server %s port %d \n", tmp_srv, port);

      device = vmalloc(sizeof(*device));
      /*
       * Set up our internal device.
       */
      device->size = npages * PAGE_SIZE;
      spin_lock_init(&(device->lock));
      spin_lock_init(&(device->rdma_lock));
    
      device->rdma_ctx = rdma_init(npages, tmp_srv, port);
      if(device->rdma_ctx == NULL){
        pr_info("rdma_init() failed\n");
        goto out;
      }
      /*
       * Get a request queue.
       */
      queue = blk_init_queue(rmem_request, &device->lock);
      if (queue == NULL)
        goto out_rdma_exit;
      pr_info("init queue id %d\n", queue->id);
      if (queue->id >= DEVICE_BOUND) 
        goto out_rdma_exit;
      scnprintf(dev_name, 20, "rmem_rdma%d", queue->id);
      devices[queue->id] = device;
      blk_queue_physical_block_size(queue, PAGE_SIZE);
      blk_queue_logical_block_size(queue, PAGE_SIZE);
      blk_queue_io_min(queue, PAGE_SIZE);
      blk_queue_io_opt(queue, PAGE_SIZE * 4);
      /*
       * Get registered.
       */
      major_num = register_blkdev(0, dev_name);
      device->major_num = major_num;
      pr_info("Registering blkdev %s major_num %d\n", dev_name, major_num);
      if (major_num < 0) {
        printk(KERN_WARNING "rmem: unable to get major number\n");
        goto out_rdma_exit;
      }
      /*
       * And the gendisk structure.
       */
      device->gd = alloc_disk(16);
      if (!device->gd)
        goto out_unregister;
      device->gd->major = major_num;
      device->gd->first_minor = 0;
      device->gd->fops = &rmem_ops;
      device->gd->private_data = device;
      strcpy(device->gd->disk_name, dev_name);
      set_capacity(device->gd, 0);
      device->gd->queue = queue;
      add_disk(device->gd);
      set_capacity(device->gd, npages * SECTORS_PER_PAGE);

      //test(device->rdma_ctx);  
      
    }
    else
      break;
  }
  initd = 1;
  pr_info("rmem_rdma successfully loaded!\n");
  return 0;

out_unregister:
  unregister_blkdev(major_num, dev_name);
out_rdma_exit:
  rdma_exit(device->rdma_ctx);
out:
  if(queue && devices[queue->id])
    devices[queue->id] = NULL;
  for(c = 0; c < DEVICE_BOUND; c++) {
    if(devices[c]){
      unregister_blkdev(devices[c]->gd->major, devices[c]->gd->disk_name);
      rdma_exit(devices[c]->rdma_ctx);
    }
  }
  rdma_library_exit();
  return -ENOMEM;
}

static void __exit rmem_exit(void)
{
  int c;
  for(c = 0; c < DEVICE_BOUND; c++) {
    if(devices[c] != NULL){
      del_gendisk(devices[c]->gd);
      put_disk(devices[c]->gd);
      pr_info("Unregistering blkdev %s major_num %d\n", devices[c]->gd->disk_name, devices[c]->major_num);
      unregister_blkdev(devices[c]->major_num, devices[c]->gd->disk_name);
      blk_cleanup_queue(devices[c]->gd->queue);

      rdma_exit(devices[c]->rdma_ctx);
      devices[c] = NULL;
    }
  }
  rdma_library_exit();
  pr_info("rmem: bye!\n");
}

module_init(rmem_init);
module_exit(rmem_exit);
