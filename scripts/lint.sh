#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== Linting dependencies check ===${NC}"

missing_tools=()
for tool in checkpatch.pl clang-format; do
    if ! command -v $tool &>/dev/null; then
        missing_tools+=($tool)
    fi
done

if [ ${#missing_tools[@]} -ne 0 ]; then
    echo -e "${YELLOW}Warning: Missing tools: ${missing_tools[*]}${NC}"
    echo -e "${YELLOW}Some lint checks may be skipped.${NC}"
fi

# Run linting
if command -v checkpatch.pl &>/dev/null; then
    echo -e "${GREEN}Running checkpatch.pl...${NC}"
    checkpatch.pl ../kernel/nxp_simtemp.c --strict
fi

if command -v clang-format &>/dev/null; then
    echo -e "${GREEN}Running clang-format...${NC}"
    clang-format -i ../kernel/nxp_simtemp.c ../user/simtemp_cli.py
fi

echo -e "${GREEN}Linting completed.${NC}"