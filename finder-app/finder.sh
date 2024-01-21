#!/bin/bash

filesdir=$1
searchstr=$2
#echo "filesdir: $filesdir"
#echo "searchstr: $searchstr"

# Return error if incorrect number of parameters specified
if [ $# != 2 ]; then
	echo "Incorrect number of parameters"
	exit 1
fi

# Return error if filesdir isn't actually a directory
if [ ! -d $filesdir ]; then
	echo "$filesdir is not a directory"
	exit 1
fi

# Find matching number of files and lines for searchstr
# Echo out
echo "The number of files are $(ls $filesdir | wc -l) and the number of matching lines are $(grep -r $searchstr $filesdir | wc -l)"
