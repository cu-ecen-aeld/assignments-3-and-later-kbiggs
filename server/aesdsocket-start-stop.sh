#!/bin/sh

mode=$1

case "$1" in
    # Start aesdsocket in daemon mode
    start)
        echo "Starting aesdsocket as daemon"
        start-stop-daemon --start -n aesdsocket --startas /usr/bin/aesdsocket -- -d
        ;;
    # Stop aesdsocket and send SIGTERM
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon --stop -n aesdsocket
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac