#!/bin/bash

writefile=$1
writestr=$2
#echo "writefile: $1"
#echo "writestr: $2"

# Return error if incorrect number of parameters specified
if [ $# != 2 ]; then
	echo "Incorrect number of parameters"
	exit 1
fi

# Create new file at writefile path with content of writestr
if [ ! -e $writefile ]; then
	mkdir -p $(dirname $writefile)
	touch $writefile
fi

echo $writestr > $writefile

# Check if file was successfully created
if [ ! -f $writefile ]; then
	echo "$writefile was not created successfully"
	exit 1
fi
