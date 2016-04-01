#!/bin/sh

if [ -n "$(cat /proc/swaps | grep /mnt/swap)" ]
then
  swapoff /mnt/swap
fi
insmod rmem_rdma.ko npages=10000 servers=10.10.49.98:18515

for s in $(ls /dev/rmem*);
do
  mkswap -f $s
  swapon $s
done
            
