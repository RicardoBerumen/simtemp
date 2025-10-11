#!/usr/bin/env bash
set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

MODULE="nxp_simtemp"
DEV="/dev/simtemp"

echo -e "${BLUE}=== Loading Kernel Module ===${NC}"
sudo insmod ../kernel/${MODULE}.ko
sleep 0.5

if ! lsmod | grep -q "$MODULE"; then
    echo -e "${RED}Error: module failed to load${NC}"
    exit 1
fi

echo -e "${GREEN}Module loaded successfully${NC}"

echo -e "${BLUE}Setting defaults: sampling=200ms threshold=45000mC mode=normal${NC}"
echo 200 | sudo tee /sys/class/misc/simtemp/sampling_ms >/dev/null
echo 45000 | sudo tee /sys/class/misc/simtemp/threshold_mC >/dev/null
echo "normal" | sudo tee /sys/class/misc/simtemp/mode >/dev/null

echo -e "${BLUE}=== Running CLI test ===${NC}"
start=$(date +%s)
if ! sudo python3 ../user/cli/main.py --test; then
    echo -e "${RED}CLI test failed!${NC}"
    sudo rmmod $MODULE
    exit 1
fi
end=$(date +%s)

echo -e "${GREEN}CLI test passed in $((end-start))s.${NC}"

echo -e "${BLUE}=== Unloading kernel module ===${NC}"
sudo rmmod $MODULE
echo -e "${GREEN}Module unloaded successfully.${NC}"