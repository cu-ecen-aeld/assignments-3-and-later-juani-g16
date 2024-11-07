#!/bin/sh
# Finds a text string in a directory
# Author: Juan I. Giorgetti

if [ $# -ne 2 ] 
then
    echo "error, script usage: finder.sh filesdir searchstr"
    exit 1
else
    if [ -d $1 ]
    then
        filesNum=$(find $1 -type f | wc -l)
        wordsNum=$(find $1 -type f -exec grep -E "\b$2" {} + | wc -l)
        echo "The number of files are $filesNum and the number of matching lines are $wordsNum"
        exit 0
    else
        echo "error, the first parameter is not a valid d"
        exit 1
    fi
fi
