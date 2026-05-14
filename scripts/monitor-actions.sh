#!/bin/bash
# GitHub Actions Monitor
# Monitors GitHub Actions workflows in real-time

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    echo -e "${RED}Error: GitHub CLI (gh) is not installed${NC}"
    echo "Install: brew install gh"
    echo "Then: gh auth login"
    exit 1
fi

# Get repository info
REPO_URL=$(git remote get-url origin)
if [[ $REPO_URL == *"github.com"* ]]; then
    REPO_NAME=$(echo $REPO_URL | sed 's/.*github\.com[:/]\([^/]*\/[^/]*\).*/\1/' | sed 's/\.git$//')
    echo -e "${BLUE}📊 Monitoring: ${CYAN}$REPO_NAME${NC}"
else
    echo -e "${RED}Error: Not a GitHub repository${NC}"
    exit 1
fi

# Get branch name
BRANCH=${1:-$(git branch --show-current)}
echo -e "${BLUE}🌿 Branch: ${CYAN}$BRANCH${NC}"
echo ""

# Function to display run status
display_run() {
    local status=$1
    local name=$2
    local workflow=$3
    local duration=$4
    local run_id=$5
    
    local status_icon=""
    local status_color=""
    
    case $status in
        "completed")
            status_icon="✅"
            status_color="${GREEN}"
            ;;
        "success")
            status_icon="✅"
            status_color="${GREEN}"
            ;;
        "failure")
            status_icon="❌"
            status_color="${RED}"
            ;;
        "in_progress"|"queued")
            status_icon="🔄"
            status_color="${YELLOW}"
            ;;
        "cancelled")
            status_icon="⏹️ "
            status_color="${PURPLE}"
            ;;
        *)
            status_icon="❓"
            status_color="${NC}"
            ;;
    esac
    
    echo -e "${status_color}${status_icon} ${workflow}${NC} - ${name} (${duration})"
    echo -e "   ${CYAN}https://github.com/${REPO_NAME}/actions/runs/${run_id}${NC}"
}

# Monitor mode
if [[ "${2}" == "--watch" ]] || [[ "${2}" == "-w" ]]; then
    echo -e "${YELLOW}👁️  Watch mode: Refreshing every 10 seconds (Ctrl+C to exit)${NC}"
    echo ""
    
    while true; do
        clear
        echo -e "${BLUE}📊 GitHub Actions Monitor - $REPO_NAME${NC}"
        echo -e "${BLUE}🌿 Branch: ${CYAN}$BRANCH${NC}"
        echo -e "${BLUE}🕐 $(date '+%Y-%m-%d %H:%M:%S')${NC}"
        echo "════════════════════════════════════════════════════════════"
        echo ""
        
        # Fetch latest runs
        gh run list --branch "$BRANCH" --limit 10 --json status,name,workflowName,startedAt,updatedAt,databaseId,conclusion \
            --jq '.[] | "\(.conclusion // .status)|\(.name)|\(.workflowName)|\(.startedAt)|\(.updatedAt)|\(.databaseId)"' | \
        while IFS='|' read -r status name workflow started updated run_id; do
            if [ -n "$started" ] && [ -n "$updated" ]; then
                start_sec=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$started" "+%s" 2>/dev/null || echo "0")
                update_sec=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$updated" "+%s" 2>/dev/null || echo "0")
                duration=$((update_sec - start_sec))
            else
                duration=0
            fi
            
            if [ $duration -eq 0 ]; then
                duration_str="<1s"
            elif [ $duration -lt 60 ]; then
                duration_str="${duration}s"
            else
                duration_str="$((duration / 60))m$((duration % 60))s"
            fi
            display_run "$status" "$name" "$workflow" "$duration_str" "$run_id"
        done
        
        echo ""
        echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"
        echo -e "${YELLOW}Refreshing in 10 seconds...${NC}"
        sleep 10
    done
else
    # One-shot mode
    echo -e "${YELLOW}📋 Recent workflow runs:${NC}"
    echo "════════════════════════════════════════════════════════════"
    echo ""
    
    gh run list --branch "$BRANCH" --limit 10 --json status,name,workflowName,startedAt,updatedAt,databaseId,conclusion \
        --jq '.[] | "\(.conclusion // .status)|\(.name)|\(.workflowName)|\(.startedAt)|\(.updatedAt)|\(.databaseId)"' | \
    while IFS='|' read -r status name workflow started updated run_id; do
        if [ -n "$started" ] && [ -n "$updated" ]; then
            start_sec=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$started" "+%s" 2>/dev/null || echo "0")
            update_sec=$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$updated" "+%s" 2>/dev/null || echo "0")
            duration=$((update_sec - start_sec))
        else
            duration=0
        fi
        
        if [ $duration -eq 0 ]; then
            duration_str="<1s"
        elif [ $duration -lt 60 ]; then
            duration_str="${duration}s"
        else
            duration_str="$((duration / 60))m$((duration % 60))s"
        fi
        display_run "$status" "$name" "$workflow" "$duration_str" "$run_id"
    done
    
    echo ""
    echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}Usage:${NC}"
    echo -e "  ${YELLOW}$0 [branch] [--watch|-w]${NC}"
    echo -e "  ${YELLOW}$0${NC}                    # Monitor current branch"
    echo -e "  ${YELLOW}$0 main${NC}               # Monitor specific branch"
    echo -e "  ${YELLOW}$0 main --watch${NC}       # Watch mode (auto-refresh)"
fi
