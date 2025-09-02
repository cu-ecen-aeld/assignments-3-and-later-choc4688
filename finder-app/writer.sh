#!/bin/sh

writefile=$1 

writestr=$2

if [ $# != 2 ]
then
    echo "Expected 2 arguments: writefile and writestr"
    exit 1
fi


#Reference: https://stackoverflow.com/questions/3362920/get-just-the-filename-from-a-path-in-a-bash-script
fileName="$(basename ${writefile})"
direcName="$(dirname ${writefile})"


if [ -d "${direcName}" ]
then
    : #Reference: https://stackoverflow.com/questions/12404661/what-is-the-use-case-of-noop-in-bash
else
    #According to the mkdir man page, -p creates the intermediate / parent directories if multiple levels
    mkdir -p "${direcName}"

    #Reference: https://stackoverflow.com/questions/16633066/bash-how-to-test-for-failure-of-mkdir-command
    if [ $? -ne 0 ]
    then
        echo "File could not be created. Error creating directory"
        exit 1
    fi
fi


currDir="$(pwd)"
cd "${direcName}"

#Creating file + check
touch ${fileName}
if [ $? -ne 0 ]
then
    echo "File could not be created."
    exit 1
fi

#Storing arg2 string into the file
echo ${writestr} > ${fileName}

#Return to original directory
cd ${currDir}




