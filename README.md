<<<<<<< HEAD
### xfer_test network throughput tool

##### RDMA example usage:


##### Server
```
$ ./xfer_test -s -f /data/1T -r
Using a SLaBS buffer of size 16777216 with 2 partitions of size 8388608
Waiting for RDMA control conn...[connection from: 10.10.2.1]
245811: | port=18515 | ib_port=1 | tx_depth=16 | sl=0 | duplex=0 | cma=1 |
Created SLAB buffer with SIZE: 67108864 PARTITIONS: 4
Metadata exchange complete
[0.0-95.6 sec]	    1048.58 GB	      87.77 Gb/s	bytes: 1048576000000
```

##### Client
```
$ ./xfer_test -c 10.10.2.2 -t 120 -i 20 -f /data/1T -o 26 -a 4 -r
Using a SLaBS buffer of size 67108864 with 4 partitions of size 16777216
file size: 1048576000000 (to transfer: 1048576000000)
107139: | port=18515 | ib_port=1 | tx_depth=16 | sl=0 | duplex=0 | cma=1 |
Created SLAB buffer with SIZE: 67108864 PARTITIONS: 4
raddr: 0x7fd56a180000, laddr: 0x7fb9ff12b000, size: 16777216
raddr: 0x7fd56917e000, laddr: 0x7fb9fe129000, size: 16777216
raddr: 0x7fd56817c000, laddr: 0x7fb9fd127000, size: 16777216
raddr: 0x7fd562fff000, laddr: 0x7fb9fc125000, size: 16777216
Metadata exchange complete
[0.0-20.0 sec]	     219.18 GB	      87.67 Gb/s
[20.0-40.0 sec]	     219.63 GB	      87.85 Gb/s
[40.0-60.0 sec]	     219.82 GB	      87.93 Gb/s
[60.0-80.0 sec]	     219.63 GB	      87.85 Gb/s
[0.0-95.6 sec]	    1048.58 GB	      87.77 Gb/s	bytes: 1048576000000
```

=======
# rdma_burst
rdma private repo (code modified from https://github.com/disprosium8/xfer_test)
>>>>>>> 03a85d9f25728a5bc50e1cca49caa454b9cf3dbd
