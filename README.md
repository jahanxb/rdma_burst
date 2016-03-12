### xfer_test network throughput tool

##### RDMA example usage:


##### Server
```
$ ./xfer_test -s -r
Using a SLaBS buffer of size 1048576 with 1 partitions of size 1048576
Waiting for RDMA control conn...[connection from: 192.168.3.2]
11091: | port=18515 | ib_port=1 | tx_depth=16 | sl=0 | duplex=0 | cma=1 |
Created SLAB buffer with SIZE: 1048576 PARTITIONS: 1
Metadata exchange complete
[0.0-10.0 sec]	      12.20 GB	       9.77 Gb/s	bytes: 12201230336
```

##### Client
```
$ ./xfer_test -c 192.168.3.3 -r -i 2
Using a SLaBS buffer of size 1048576 with 1 partitions of size 1048576
23113: | port=18515 | ib_port=1 | tx_depth=16 | sl=0 | duplex=0 | cma=1 |
Created SLAB buffer with SIZE: 1048576 PARTITIONS: 1
raddr: 0x7f1c575b4000, laddr: 0x7fb8e6cd7000, size: 1048576
Metadata exchange complete
[0.0-2.0 sec]	       2.44 GB	       9.77 Gb/s
[2.0-4.0 sec]	       2.44 GB	       9.77 Gb/s
[4.0-6.0 sec]	       2.44 GB	       9.77 Gb/s
[6.0-8.0 sec]	       2.44 GB	       9.77 Gb/s
[0.0-10.0 sec]	      12.20 GB	       9.77 Gb/s	bytes: 12201230336
```

