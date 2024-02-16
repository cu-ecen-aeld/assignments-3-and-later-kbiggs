#!/bin/sh

mode=$1

# Start aesdsocket in daemon mode
if [ "$mode" = "start" ]; then
    echo Starting aesdsocket as daemon
    start-stop-daemon --start -n aesdsocket --startas /usr/bin/aesdsocket -- -d
# Stop aesdsocket and send SIGTERM
elif [ "$mode" = "stop" ]; then
    echo Stopping aesdsocket
    start-stop-daemon --stop -n aesdsocket
else
    echo Need to specify either start or stop
fi