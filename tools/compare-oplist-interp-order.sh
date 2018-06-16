#!/usr/bin/env sh
# This tool compares the order of things in oplist-order with interp-order
# Shows diff output in less as `diff oplist-order interp-order`
OPT_OP_ORDER='--get-oplist-order'
OPT_SHOW_DEP='--show-deprecated'
TEMP_FOLDER='/tmp'
PAGER="${PAGER:-less}"
if [ ${#OPT_OP_ORDER} > ${#OPT_SHOW_DEP} ]; then
    MAX_OPT_LEN="${#OPT_OP_ORDER}"
else
    MAX_OPT_LEN="${#OPT_SHOW_DEP}"
fi
get_oplist_order () {
    grep -vE '(^#)|(^\s*$)' src/core/oplist | sed -E 's/^(\S+)\s*.*/\1/'
}
get_interp_order () {
    grep -F 'OP(' src/core/interp.c | grep -v '^#' | sed -E 's/^\s*OP\(([^)]+)\).*/\1/'
}
filter_funct () {
    grep -v DEPRECATED
}
show_help () {
    printf "Usage: %s [OPTION]
 Compares the order of ops from the oplist with the order in interp.c
 Shows diff output in less as 'diff -aur oplist-order interp-order | %s'
 The PAGER env var allows you to set a different pager. Set it to 'cat'
 if you don't want any pager.

    %s
      Gets a list of all the ops in order
    %s
      Shows DEPRECATED_XX ops instead of hiding them\n" \
      "$0" "$PAGER" "$OPT_OP_ORDER" "$OPT_SHOW_DEP"
    exit $1
}
filter=filter_funct
for i in $@; do
    if [ "$i" == "$OPT_OP_ORDER" ]; then ENABLE_OPLIST=1;
    elif [ "$i" == "$OPT_SHOW_DEP" ]; then filter=cat
    elif [ "$i" == '-h' ] || [ "$1" == '--help' ]; then
        show_help 0
    else
        printf "Error: %s is not a recognized command\n\n" "$i"
        show_help 1
    fi
done
if [ "$1" == "$OPT_OP_ORDER" ]; then
    get_oplist_order | $filter
    exit 0
fi

get_oplist_order | $filter > "$TEMP_FOLDER/oplist-order.txt"
get_interp_order | $filter > "$TEMP_FOLDER/interp-order.txt"
diff -aur "$TEMP_FOLDER/oplist-order.txt" "$TEMP_FOLDER/interp-order.txt" | $PAGER
rm /tmp/oplist-order.txt
rm /tmp/interp-order.txt
