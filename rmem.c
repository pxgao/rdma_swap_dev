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
#include <linux/bio.h>

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
#define REQ_POOL_SIZE 1024
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
  //struct request *blk_req[MAX_REQ];
};

struct rmem_device* devices[DEVICE_BOUND];

int initd = 0;

static struct proc_dir_entry* proc_entry;
/*
static void rmem_request2(struct request_queue *q) 
{
  struct request *req;
  rdma_req_t rdma_req_p;
  rdma_req_t last_rdma_req_p = NULL;
  int ret, i, count = 0;
  unsigned long flags;
  struct batch_request* batch_req, *last_batch_req = NULL;

  LOG_KERN(LOG_INFO, "======New rmem request======", 0);
  req = blk_fetch_request(q);
  if(req)
  {
    LOG_KERN(LOG_INFO, "new request %p", req);
    debug_pool_insert(devices[q->id]->rdma_ctx->pool, req); 
    batch_req = get_batch_request(devices[q->id]->rdma_ctx->pool);
    batch_req->req = req;
    batch_req->outstanding_reqs = 0;
    batch_req->next = NULL;
  }

  while (req != NULL) 
  {
    if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) 
    {
      printk (KERN_NOTICE "Skip non-CMD request\n");
      __blk_end_request_all(req, -EIO);
      return_batch_request(devices[q->id]->rdma_ctx->pool, batch_req);
      BUG();
    }

    rdma_req_p = devices[q->id]->rdma_req + count;    

    rdma_req_p->rw = rq_data_dir(req)?RDMA_WRITE:RDMA_READ;
    rdma_req_p->length = PAGE_SIZE * blk_rq_cur_sectors(req) / SECTORS_PER_PAGE;
    rdma_req_p->dma_addr = rdma_map_address(bio_data(req->bio), rdma_req_p->length);
    rdma_req_p->remote_offset = blk_rq_pos(req) / SECTORS_PER_PAGE * PAGE_SIZE;
    rdma_req_p->batch_req = batch_req;

    if(MERGE_REQ && count > 0)
    {
      last_rdma_req_p = devices[q->id]->rdma_req + count - 1;
      if(rdma_req_p->rw == last_rdma_req_p->rw && 
            last_rdma_req_p->dma_addr + last_rdma_req_p->length == rdma_req_p->dma_addr &&
            last_rdma_req_p->remote_offset + last_rdma_req_p->length == rdma_req_p->remote_offset)
      {
        last_rdma_req_p->length += rdma_req_p->length;
        //LOG_KERN(LOG_INFO, "Merging RDMA req %p w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", req, last_rdma_req_p->rw == RDMA_WRITE, last_rdma_req_p->dma_addr, bio_data(req->bio), last_rdma_req_p->remote_offset, last_rdma_req_p->length);
      }
      else
      {
        //LOG_KERN(LOG_INFO, "Constructing RDMA req %p w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", req, rdma_req_p->rw == RDMA_WRITE, rdma_req_p->dma_addr, bio_data(req->bio), rdma_req_p->remote_offset, rdma_req_p->length);
        count++;
        batch_req->outstanding_reqs++;
      }
    }
    else
    {
      //LOG_KERN(LOG_INFO, "Constructing RDMA req %p w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", req, rdma_req_p->rw == RDMA_WRITE, rdma_req_p->dma_addr, bio_data(req->bio), rdma_req_p->remote_offset, rdma_req_p->length);
      count++;
      batch_req->outstanding_reqs++;
    }
    if(count >= MAX_REQ)
    {
      LOG_KERN(LOG_INFO, "Sending %d rdma reqs", count);
      rdma_op(devices[q->id]->rdma_ctx, devices[q->id]->rdma_req, count);
      //LOG_KERN(LOG_INFO, "Finished %d rdma reqs", count);
      //for(i = 0; i < count; i++){
      //  rdma_unmap_address(devices[q->id]->rdma_req[i].dma_addr, devices[q->id]->rdma_req[i].length);
      //  LOG_KERN(LOG_INFO, "end req %p ret %d", devices[q->id]->rdma_req[i].blk_req, ret);
      //}
      count = 0;
    }
    //ret = __blk_end_request_cur(req, 0);
    ret = blk_update_request(req, 0, blk_rq_cur_bytes(req)) || (unlikely(blk_bidi_rq(req)) && blk_update_request(req->next_rq, 0, 0));
    if (!ret)
    {
      //devices[q->id]->blk_req[blk_req_count++] = req;
      //__blk_end_request_all(req, 0);
      LOG_KERN(LOG_INFO, "-----end of req----- batch id %d sz %d", batch_req->id, batch_req->outstanding_reqs);
      if(batch_req->outstanding_reqs == 0)
      {
        last_batch_req->next = batch_req;
        LOG_KERN(LOG_INFO, "batch req %d -> %d", last_batch_req->id, batch_req->id);
      }

      req = blk_fetch_request(q);
      if(req)
      {
        LOG_KERN(LOG_INFO, "new request %p", req);
        debug_pool_insert(devices[q->id]->rdma_ctx->pool, req); 
        last_batch_req = batch_req;
        batch_req = get_batch_request(devices[q->id]->rdma_ctx->pool);
        batch_req->req = req;
        batch_req->outstanding_reqs = 0;
        batch_req->next = NULL;
      }
    }
  }
  if(count)
  {
    LOG_KERN(LOG_INFO, "Sending %d rdma reqs", count);
    rdma_op(devices[q->id]->rdma_ctx, devices[q->id]->rdma_req, count);
    LOG_KERN(LOG_INFO, "Finished %d rdma reqs", count);
    //for(i = 0; i < count; i++){
    //  rdma_unmap_address(devices[q->id]->rdma_req[i].dma_addr, devices[q->id]->rdma_req[i].length);
    //  LOG_KERN(LOG_INFO, "end req %p ret %d", devices[q->id]->rdma_req[i].blk_req, ret);
    //}
  }

  //if(blk_req_count){
  //  for(i = 0; i < blk_req_count; i++)
  //    __blk_end_request_all(devices[q->id]->blk_req[i], 0);
  //}
  LOG_KERN(LOG_INFO, "======End of rmem request======\n", 0);
}
*/

static void rmem_request(struct request_queue *q)
{
  struct request *req;
  struct bio *bio;
  struct bio_vec *bvec;
  sector_t sector;
  rdma_req_t last_rdma_req, cur_rdma_req = NULL;
  int i, rdma_req_count = 0;
  struct rmem_device *dev = q->queuedata;
  char* buffer;
  struct batch_request* batch_req = NULL, *last_batch_req;

  LOG_KERN(LOG_INFO, "=======Start of rmem request======");

  while ((req = blk_fetch_request(q)) != NULL) 
  {
    if (req->cmd_type != REQ_TYPE_FS) 
    {
      printk (KERN_NOTICE "Skip non-fs request\n");
      __blk_end_request(req, -EIO, blk_rq_cur_bytes(req));
      continue;
    }
    spin_unlock_irq(q->queue_lock); 

    last_batch_req = batch_req;
    batch_req = get_batch_request(dev->rdma_ctx->pool);
    batch_req->req = req;
    batch_req->outstanding_reqs = 0;
    batch_req->next = NULL;
    batch_req->nsec = 0;

    __rq_for_each_bio(bio, req) 
    {
      sector = bio->bi_sector;
      bio_for_each_segment(bvec, bio, i) 
      {
        buffer = __bio_kmap_atomic(bio, i);
        //sbull_transfer(dev, sector, bio_cur_bytes(bio) >> 9, buffer, bio_data_dir(bio) == WRITE);
        cur_rdma_req = dev->rdma_req + rdma_req_count;
        cur_rdma_req->rw = bio_data_dir(bio)?RDMA_WRITE:RDMA_READ;
        cur_rdma_req->length = (bio_cur_bytes(bio) >> 9) * KERNEL_SECTOR_SIZE;
        cur_rdma_req->dma_addr = rdma_map_address(buffer, cur_rdma_req->length);
        cur_rdma_req->remote_offset = sector * KERNEL_SECTOR_SIZE;
        cur_rdma_req->batch_req = batch_req;

        last_rdma_req = dev->rdma_req + rdma_req_count - 1;
        if(MERGE_REQ && rdma_req_count > 0 && cur_rdma_req->rw == last_rdma_req->rw && 
            last_rdma_req->dma_addr + last_rdma_req->length == cur_rdma_req->dma_addr &&
            last_rdma_req->remote_offset + last_rdma_req->length == cur_rdma_req->remote_offset)
        {
          last_rdma_req->length += cur_rdma_req->length;
          //LOG_KERN(LOG_INFO, "Merging RDMA req %p w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", req, last_rdma_req_p->rw == RDMA_WRITE, last_rdma_req_p->dma_addr, bio_data(req->bio), last_rdma_req_p->remote_offset, last_rdma_req_p->length);
        }
        else
        {
          LOG_KERN(LOG_INFO, "Constructing RDMA req %p w: %d  addr: %llu (ptr: %p)  offset: %u  len: %d", req, cur_rdma_req->rw == RDMA_WRITE, cur_rdma_req->dma_addr, buffer, cur_rdma_req->remote_offset, cur_rdma_req->length);
          batch_req->outstanding_reqs++;
          rdma_req_count++;
        }
        BUG_ON(rdma_req_count > MAX_REQ);

        sector += bio_cur_bytes(bio) >> 9;
        __bio_kunmap_atomic(buffer);
      }
      batch_req->nsec += bio->bi_size/KERNEL_SECTOR_SIZE;
    }

    if(batch_req->outstanding_reqs == 0)
    {
      last_batch_req->next = batch_req;
      LOG_KERN(LOG_INFO, "batch req %d -> %d", last_batch_req->id, batch_req->id);
    }

    spin_lock_irq(q->queue_lock);
  }

  if(rdma_req_count)
  {
    rdma_op(dev->rdma_ctx, dev->rdma_req, rdma_req_count);
  }  
  LOG_KERN(LOG_INFO, "======End of rmem request======");
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


static void rdma_transfer(struct rmem_device
*dev, unsigned long sector,
    unsigned long nsect, char *buffer, int write, struct batch_request* batch_req, rdma_request* rdma_reqs)
{
  rdma_request* req = rdma_reqs + batch_req->outstanding_reqs;
  unsigned long offset = sector*KERNEL_SECTOR_SIZE;
  unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;

  if ((offset + nbytes) > dev->size) {
    printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
    return;
  }
  req->rw = write?RDMA_WRITE:RDMA_READ;
  req->length = nbytes;
  req->dma_addr = rdma_map_address(buffer, nbytes);
  req->remote_offset = offset;
  req->batch_req = batch_req;
  batch_req->outstanding_reqs++;
  BUG_ON(batch_req->outstanding_reqs > MAX_REQ);
}

static int rdma_xfer_bio(struct rmem_device *dev, struct bio *bio, struct batch_request* batch_req, rdma_request* rdma_reqs)
{
  int i;
  struct bio_vec *bvec;
  sector_t sector = bio->bi_sector;

  /* Do each segment independently. */
  bio_for_each_segment(bvec, bio, i) {
    char *buffer = __bio_kmap_atomic(bio, i);
    rdma_transfer(dev, sector, bio_cur_bytes(bio) >> 9, buffer, bio_data_dir(bio) == WRITE, batch_req, rdma_reqs);
    sector += bio_cur_bytes(bio) >> 9;
    __bio_kunmap_atomic(buffer);
  }
  return 0; /* Always "succeed" */
}

static void rdma_make_request(struct request_queue *q, struct bio *bio)
{
  struct rmem_device *dev;
  int status;
  struct batch_request* batch_req;

  LOG_KERN(LOG_INFO, "======New rmem request======");
  dev = q->queuedata;

  batch_req = get_batch_request(dev->rdma_ctx->pool);
  LOG_KERN(LOG_INFO, "batch req %d", batch_req->id);
  batch_req->outstanding_reqs = 0;
  batch_req->next = NULL;
  batch_req->bio = bio;
  status = rdma_xfer_bio(dev, bio, batch_req, dev->rdma_req);

  rdma_op(dev->rdma_ctx, dev->rdma_req, batch_req->outstanding_reqs);
  LOG_KERN(LOG_INFO, "======End of rmem request======");

}



static int debug_show(struct seq_file *m, void *v)
{
  int i,j;
  struct batch_request** reqs;
  for(i = 0; i < DEVICE_BOUND; i++)
  {
    if(devices[i]){
      seq_printf(m, "-----Found device %d-----\n", i);
      spin_lock_irq(&devices[i]->rdma_ctx->pool->lock);
      reqs = vmalloc(sizeof(batch_request*) * devices[i]->rdma_ctx->pool->size);
      memset(reqs, 0, sizeof(batch_request*) * devices[i]->rdma_ctx->pool->size);
      for(j = 0; j < devices[i]->rdma_ctx->pool->size; j++)
      {
        if(devices[i]->rdma_ctx->pool->data[j])
        {
          reqs[devices[i]->rdma_ctx->pool->data[j]->id] = devices[i]->rdma_ctx->pool->data[j];
        }
      }
      for(j = 0; j < devices[i]->rdma_ctx->pool->size; j++)
      {
        if(reqs[j] == NULL)
          seq_printf(m, "%d\n", j);
      }
      vfree(reqs);
      for(j = 0; j < 1024; j++)
      {
          if(devices[i]->rdma_ctx->pool->io_req[i])
              seq_printf(m, "%p\n", devices[i]->rdma_ctx->pool->io_req[i]);
      }
      spin_unlock_irq(&devices[i]->rdma_ctx->pool->lock);
    }
  }
  return 0;
}

static int debug_open(struct inode * sp_inode, struct file *sp_file)
{
  return single_open(sp_file, debug_show, NULL);
}

static ssize_t debug_write(struct file *sp_file, const char __user *buf, size_t size, loff_t *offset)
{
  LOG_KERN(LOG_INFO, "debug");
  return 0;
}

static struct file_operations debug_fops = {
  .open = debug_open,
  .read = seq_read,
  .write = debug_write,
  .llseek = seq_lseek,
  .release = single_release
};

static int __init rmem_init(void) {
  int c,major_num,i;
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
      

      device->size = npages * PAGE_SIZE;
      spin_lock_init(&(device->lock));
      spin_lock_init(&(device->rdma_lock));
    
      device->rdma_ctx = rdma_init(npages, tmp_srv, port, REQ_POOL_SIZE);
      if(device->rdma_ctx == NULL){
        pr_info("rdma_init() failed\n");
        goto out;
      }
      /*
       * Get a request queue.
       */
      if(CUSTOM_MAKE_REQ_FN)
      {
        queue = blk_alloc_queue(GFP_KERNEL);
        if (queue == NULL)
          goto out_rdma_exit;
        blk_queue_make_request(queue, rdma_make_request);
      }
      else
      {
        queue = blk_init_queue(rmem_request, &device->lock);
        if (queue == NULL)
          goto out_rdma_exit;
      }
      queue->queuedata = device;
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

      
    }
    else
      break;
  }
  initd = 1;

  proc_entry = proc_create("rmem_debug", 0666, NULL, &debug_fops);


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
  int c, i;
  for(c = 0; c < DEVICE_BOUND; c++) {
    if(devices[c] != NULL){
      del_gendisk(devices[c]->gd);
      put_disk(devices[c]->gd);
      pr_info("Unregistering blkdev %s major_num %d\n", devices[c]->gd->disk_name, devices[c]->major_num);
      unregister_blkdev(devices[c]->major_num, devices[c]->gd->disk_name);
      blk_cleanup_queue(devices[c]->gd->queue);

      rdma_exit(devices[c]->rdma_ctx);
      
      vfree(devices[c]);
      devices[c] = NULL;
    }
  }
  rdma_library_exit();
  
  remove_proc_entry("rmem_debug", NULL);

  pr_info("rmem: bye!\n");
}

module_init(rmem_init);
module_exit(rmem_exit);
