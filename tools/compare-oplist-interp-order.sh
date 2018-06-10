#!/usr/bin/env sh
# This tool compares the order of things in oplist-order with interp-order
# Shows diff output in less as `diff oplist-order interp-order`
get_oplist_order () {
    grep -vE '(^#)|(^\s*$)' src/core/oplist | sed -E 's/^(\S+)\s*.*/\1/'
}
get_interp_order () {
    grep -F 'OP(' src/core/interp.c | grep -v '^#' | sed -E 's/^\s*OP\(([^)]+)\).*/\1/'
}
if [ "$1" == 'get_oplist_order' ]; then
    get_oplist_order
    exit 0
fi
get_oplist_order > /tmp/oplist-order.txt
get_interp_order > /tmp/interp-order.txt
diff -aur /tmp/oplist-order.txt /tmp/interp-order.txt | less
rm /tmp/oplist-order.txt
rm /tmp/interp-order.txt
