#!/bin/sh
# Finds a text string in a directory
# Author: Juan I. Giorgetti

if [ $# -ne 2 ] 
then
    echo "error, script usage: writer.sh filesdir searchstr"
    exit 1
else
    filename=$(basename "$1")
    path=$(dirname "$1")
    if [ -d $path ]
    then
        echo $2 > $1
    else
        mkdir -p $path
        echo $2 > $1
    fi
    if [ ! -f $1 ]; then
        echo "error, not possible to create the file"
        exit 1
    fi
fi
