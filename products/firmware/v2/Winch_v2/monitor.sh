#!/bin/bash
# Quick serial monitor - kills existing processes and starts immediately
PORT="/dev/cu.usbmodem2101"
BAUD=115200
LINES=${1:-50}
TIMEOUT=${2:-30}

# Kill any existing serial connections
lsof "$PORT" 2>/dev/null | awk 'NR>1 {print $2}' | sort -u | xargs kill -9 2>/dev/null
sleep 1

# Configure and read
stty -f "$PORT" $BAUD raw -echo
timeout $TIMEOUT head -$LINES < "$PORT" 2>&1

echo ""
echo "--- Monitor ended ---"
