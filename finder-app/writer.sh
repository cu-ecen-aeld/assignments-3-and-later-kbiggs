#!/bin/bash

# CU AESD Assignment 1
# Katie Biggs
# Jan 21, 2024

# Store off arguments for easier readability
writefile=$1
writestr=$2

#echo "writefile: $1"
#echo "writestr: $2"

# Return error if incorrect number of parameters specified
if [ $# != 2 ]; then
	echo "Incorrect number of parameters. Expecting 2."
	exit 1
fi

# If writefile doesn't exist, create new file at writefile path
# This code block was based off of content at https://stackoverflow.com/questions/125281/how-do-i-remove-the-file-suffix-and-path-portion-from-a-path-string-in-bash. I utilized this to identify how to separate the directory path from the full string provided.
if [ ! -e $writefile ]; then
	# Create parent directories if needed
	mkdir -p $(dirname $writefile)
	# Create file
	touch $writefile
fi

# Write contents of string out to the desired file
# Using > will overwrite the existing content
echo $writestr > $writefile

# Check if file was successfully created
if [ ! -f $writefile ]; then
	echo "$writefile was not created successfully"
	exit 1
fi

