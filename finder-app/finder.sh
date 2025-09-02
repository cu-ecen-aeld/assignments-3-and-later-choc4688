#!/bin/sh

filesdir=$1
searchstr=$2

if [ $# != 2 ]
then
    echo "Expected 2 arguments: filesdir and searchdir"
    exit 1
elif [ -d $1 ]
then
    : #Reference: https://stackoverflow.com/questions/12404661/what-is-the-use-case-of-noop-in-bash
else
    echo "$1 does not represent a directory on the filesystem"
    exit 1
fi   

filesdir="${filesdir}/*"


#Formatting of "$()": https://stackoverflow.com/questions/4651437/how-do-i-set-a-variable-to-the-output-of-a-command-in-bash
#wc counts lines, words, bytes
#-l used to just output number of counted lines
numFiles="$(grep -c $2 ${filesdir} | wc -l)" 

#grep -o outputs each match on a separate line according to the grep man page
numLines="$(grep -o $2 ${filesdir} | wc -l)"


echo "The number of files are ${numFiles} and the number of matching lines are ${numLines}"



