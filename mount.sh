insmod rmem_rdma.ko npages=598073 servers=10.10.49.85:18515;
sleep 1
mkfs.ext4 /dev/rmem_rdma33 
sleep 1
mount /dev/rmem_rdma33 ~/temp
