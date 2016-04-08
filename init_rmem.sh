#!/bin/sh

if [ -n "$(cat /proc/swaps | grep /mnt/swap)" ]
then
  swapoff /mnt/swap
fi
insmod rmem_rdma.ko npages=498073 servers=10.10.49.84:18515

for s in $(ls /dev/rmem_rdma*);
do
  mkswap -f $s
  swapon $s
done
            
