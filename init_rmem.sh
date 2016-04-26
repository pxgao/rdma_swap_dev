#!/bin/sh

if [ -n "$(cat /proc/swaps | grep /mnt/swap)" ]
then
  swapoff /mnt/swap
fi
insmod rmem_rdma.ko npages=786432 servers=10.10.49.85:18515

for s in $(ls /dev/rmem_rdma*);
do
  sleep 1
  mkswap -f $s
  sleep 1
  swapon $s
done
            
