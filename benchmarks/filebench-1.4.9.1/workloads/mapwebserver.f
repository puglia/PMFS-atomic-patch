#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

set $dir=/mnt/pmfs
set $nfiles=1000
set $meandirwidth=20
set $meanfilesize=16k
set $nthreads=50
set $iosize=1m
set $meanappendsize=16k

define fileset name=bigfileset,path=$dir,size=$meanfilesize,entries=$nfiles,dirwidth=$meandirwidth,prealloc=100
define fileset name=logfiles,path=$dir,size=$meanfilesize,entries=1,dirwidth=$meandirwidth,prealloc

define process name=filereader,instances=1
{
  thread name=filereaderthread,memsize=10m,instances=$nthreads
  {
    flowop openfile name=openfile1,filesetname=bigfileset,fd=1
    	flowop mmap name=mmap1,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile1,fd=1,iosize=$iosize
    	flowop munmap name=munmap1,fd=1
    flowop closefile name=closefile1,fd=1
    flowop openfile name=openfile2,filesetname=bigfileset,fd=1
     	flowop mmap name=mmap2,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile2,fd=1,iosize=$iosize
    	flowop munmap name=munmap2,fd=1
    flowop closefile name=closefile2,fd=1
    flowop openfile name=openfile3,filesetname=bigfileset,fd=1
    	flowop mmap name=mmap3,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile3,fd=1,iosize=$iosize
    	flowop munmap name=munmap3,fd=1
    flowop closefile name=closefile3,fd=1
    flowop openfile name=openfile4,filesetname=bigfileset,fd=1
    	flowop mmap name=mmap4,filesetname=bigfileset,fd=1
   	 flowop mmap_readwholefile name=mapreadwholefile4,fd=1,iosize=$iosize
   	 flowop munmap name=munmap4,fd=1
    flowop closefile name=closefile4,fd=1
    flowop openfile name=openfile5,filesetname=bigfileset,fd=1
    	 flowop mmap name=mmap5,filesetname=bigfileset,fd=1
   	 flowop mmap_readwholefile name=mapreadwholefile5,fd=1,iosize=$iosize
   	 flowop munmap name=munmap5,fd=1
    flowop closefile name=closefile5,fd=1
    flowop openfile name=openfile6,filesetname=bigfileset,fd=1
     	flowop mmap name=mmap6,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile6,fd=1,iosize=$iosize
    	flowop munmap name=munmap6,fd=1
    flowop closefile name=closefile6,fd=1
    flowop openfile name=openfile7,filesetname=bigfileset,fd=1
    	flowop mmap name=mmap7,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile7,fd=1,iosize=$iosize
    	flowop munmap name=munmap7,fd=1
    flowop closefile name=closefile7,fd=1
    flowop openfile name=openfile8,filesetname=bigfileset,fd=1
    	flowop mmap name=mmap8,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile8,fd=1,iosize=$iosize
    	flowop munmap name=munmap8,fd=1
    flowop closefile name=closefile8,fd=1
    flowop openfile name=openfile9,filesetname=bigfileset,fd=1
     	flowop mmap name=mmap9,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile9,fd=1,iosize=$iosize
    	flowop munmap name=munmap9,fd=1
    flowop closefile name=closefile9,fd=1
    flowop openfile name=openfile10,filesetname=bigfileset,fd=1
     	flowop mmap name=mmap10,filesetname=bigfileset,fd=1
    	flowop mmap_readwholefile name=mapreadwholefile10,fd=1,iosize=$iosize
    	flowop munmap name=munmap10,fd=1
    flowop closefile name=closefile10,fd=1
	#flowop appendfilerand name=appendlog,filesetname=logfiles,iosize=$meanappendsize,fd=2
    	flowop mmap name=mmap11,filesetname=logfiles,iosize=$meanappendsize,fd=2
    	flowop mmap_appendfilerand name=appendlog,filesetname=logfiles,iosize=$meanappendsize,fd=2
    	flowop msync name=msync1,fd=2,iosize=$iosize
    	flowop munmap name=munmap11,fd=2, iosize=$meanappendsize
  }
}

echo  "Web-server Version 3.0 personality successfully loaded"
usage "Usage: set \$dir=<dir>"
usage "       set \$meanfilesize=<size>   defaults to $meanfilesize"
usage "       set \$nfiles=<value>    defaults to $nfiles"
usage "       set \$meandirwidth=<value>  defaults to $meandirwidth"
usage "       set \$nthreads=<value>  defaults to $nthreads"
usage "       set \$iosize=<size>     defaults to $iosize"
usage "       run runtime (e.g. run 60)"
