#!/bin/sh

# CU AESD Assignment 1
# Katie Biggs
# Jan 21, 2024

# Store off arguments for easier readability
filesdir=$1
searchstr=$2

#echo "filesdir: $filesdir"
#echo "searchstr: $searchstr"

# Return error if incorrect number of parameters specified
if [ $# != 2 ]; then
	echo "Incorrect number of parameters. Expecting 2."
	exit 1
fi

# Return error if filesdir isn't actually a directory
if [ ! -d $filesdir ]; then
	echo "$filesdir is not a directory"
	exit 1
fi

# Find number of files and number of matching lines for searchstr and echo out results to user
# This code was based off of content from https://www.cyberciti.biz/faq/grep-count-lines-if-a-string-word-matches/. I used this content to help understand how to use grep to count the instances of a string in a file. I modified it to look recursively through directories.
echo "The number of files are $(ls -r $filesdir | wc -l) and the number of matching lines are $(grep -r $searchstr $filesdir | wc -l)"

