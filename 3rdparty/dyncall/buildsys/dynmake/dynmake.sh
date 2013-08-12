#!/bin/sh
#cpp -D MAKE_CMD_$2 -P Makefile.M | sed "s/^  */	/" > Makefile.dynmake
gcc -D MAKE_CMD_$2 -E -P -x c Makefile.M | sed "s/^  */	/" > Makefile.dynmake
$2 -f Makefile.dynmake $1
