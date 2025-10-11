#!/usr/bin/env bash
set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

KERNEL_HEADERS=$(uname -r)
MODULE_DIR="../kernel"
USER_APP_DIR="../user"

echo -e "${BLUE}=== Building SimTemp kernel module ===${NC}"
start=$(date +%s)

if [ ! -d "/lib/modules/$KERNEL_HEADERS/build" ]; then
    echo -e "{RED}Error: Kernel headers not found for $(uname -r)${NC}"
    echo -e "{YELLOW}Install headers: sudo apt install linux-headers-$(uname -r)${NC}"
    exit 1
fi

echo -e "${GREEN}Building kernel module...${NC}"
make -C /lib/modules/$KERNEL_HEADERS/build M="$PWD/../kernel" modules

echo -e "${BLUE}Checking Python dependencies...${NC}"
if ! python3 -c "import PyQt5, pyqtgraph" %>/dev/null; then
    echo -e "${YELLOW}Warning: pyqt5/pyqtgraph not installed. GUI may fail ${NC}"
fi

end=$(date +%s)
echo -e "${GREEN}Build completed succesfully in $((end-start))s${NC}"
