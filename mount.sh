insmod rmem_rdma.ko npages=598073 servers=10.10.49.85:18515;
sleep 1
mkfs.ext4 `ls /dev/rmem_rdma*` 
sleep 1
mount `ls /dev/rmem_rdma*` ~/temp
