#!/bin/sh
VERSION=$1
{
    echo MANIFEST
    git ls-files | perl -ne "print unless /^3rdparty\/\w+$/"
    for submod in 3rdparty/libatomicops/ 3rdparty/dyncall/ 3rdparty/libuv/ 3rdparty/dynasm/ 3rdparty/libtommath/ 3rdparty/cmp/; do
        cd $submod
        git ls-files | perl -pe "s{^}{$submod}"
        cd ../..;
    done
} > MANIFEST
[ -d MoarVM-$VERSION ] || ln -s . MoarVM-$VERSION
perl -pe "s{^}{MoarVM-$VERSION/}" MANIFEST | tar zc -H gnu --owner=0 --group=0 --numeric-owner -T - -f MoarVM-$VERSION.tar.gz
rm MoarVM-$VERSION
