#!/bin/sh
export LC_ALL=C.UTF-8
VERSION=$1
[ ! "$VERSION" ] && exit 1
# The build only uses DynASM and minilua from LuaJIT
luajit_files=$(cd 3rdparty/luajit/ && git ls-files --error-unmatch COPYRIGHT dynasm src/host/minilua.c) || {
    echo "3rdparty/luajit is missing release files, run 'git submodule update --init 3rdparty/luajit'" >&2
    exit 1
}
{
    echo MANIFEST
    git ls-files | perl -ne "print unless /^3rdparty\/\w+$/"
    for submod in 3rdparty/libatomicops/ 3rdparty/dyncall/ 3rdparty/libuv/ 3rdparty/libtommath/ 3rdparty/cmp/ 3rdparty/zmij/ 3rdparty/mimalloc/ 3rdparty/rapidhash/; do
        cd $submod
        git ls-files | perl -pe "s{^}{$submod}"
        cd ../..;
    done
    echo "$luajit_files" | perl -pe "s{^}{3rdparty/luajit/}"
} | sort > MANIFEST
[ -d MoarVM-$VERSION ] || ln -s . MoarVM-$VERSION
tag_timestamp=$(git log -1 --format='%at' $VERSION)
perl -pe "s{^}{MoarVM-$VERSION/}" MANIFEST | tar c -H gnu --mode=go=rX,u+rw,a-s -I 'gzip -9n' --mtime=@$tag_timestamp --owner=0 --group=0 --numeric-owner -T - -f MoarVM-$VERSION.tar.gz
rm MoarVM-$VERSION
