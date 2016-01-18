#!/bin/bash

if [ ! -d kern ]; then
    echo "go to the top level os161 directory" 1>&2;
    exit 1;
fi

for i in $(find . -name .svnignore); do
    DIR=$(dirname $i)
    echo $DIR
    pushd $DIR
    svn propset svn:ignore -F .svnignore .
    popd
done
