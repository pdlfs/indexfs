**Scalable file system metadata partitioning over multiple machines.**

[![License](https://img.shields.io/badge/license-New%20BSD-blue.svg)](LICENSE.txt)

IndexFS by DeltaFS
=======

This is the DeltaFS re-implementation of the [IndexFS](https://dl.acm.org/citation.cfm?id=2683620) paper of CMU published by Kai, Qing, and Garth at [IEEE/ACM SC 2014](http://sc14.supercomputing.org/). THIS IS *NOT* THE ORIGINAL INDEXFS CODE. The original IndexFS code is written by Kai and Qing and is available at [https://github.com/zhengqmark/indexfs-0.4](https://github.com/zhengqmark/indexfs-0.4). Internally, DeltaFS uses this re-implementation of IndexFS to evaluate different distributed filesystem metadata design options and to assist DeltaFS development. This re-implementation reuses some of the DeltaFS code. Again, this is not the original IndexFS code.

```
XXXXXXXXX
XX      XX                 XX                  XXXXXXXXXXX
XX       XX                XX                  XX
XX        XX               XX                  XX
XX         XX              XX   XX             XX
XX          XX             XX   XX             XXXXXXXXX
XX           XX  XXXXXXX   XX XXXXXXXXXXXXXXX  XX         XX
XX          XX  XX     XX  XX   XX       XX XX XX      XX
XX         XX  XX       XX XX   XX      XX  XX XX    XX
XX        XX   XXXXXXXXXX  XX   XX     XX   XX XX    XXXXXXXX
XX       XX    XX          XX   XX    XX    XX XX           XX
XX      XX      XX      XX XX   XX X    XX  XX XX         XX
XXXXXXXXX        XXXXXXX   XX    XX        XX  XX      XX
```

DeltaFS was developed, in part, under U.S. Government contract 89233218CNA000001 for Los Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC for the U.S. Department of Energy/National Nuclear Security Administration. Please see the accompanying [LICENSE.txt](LICENSE.txt) for further information.
