#!/bin/bash

qemu -nographic -m 32 -hda obj/kern/bochs.img -hdb obj/fs/fs.img -serial stdio \
     -net user -net nic,model=i82559er,macaddr=52:54:00:12:34:56 -debug-e100\
     -redir tcp:8080::80 -redir tcp:4242::10000 -pcap slirp.cap "$@"
