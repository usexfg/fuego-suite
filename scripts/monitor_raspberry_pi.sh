#!/bin/bash

# Raspberry Pi Workflow Monitor
# Monitors the Raspberry Pi ARM64 workflow specifically

REPO="ColinRitman/fuego"
WORKFLOW_FILE="raspberry-pi.yml"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🍓 Raspberry Pi ARM64 Workflow Monitor${NC}"
echo -e "${BLUE}Repository: $REPO${NC}"
echo -e "${BLUE}Workflow: $WORKFLOW_FILE${NC}"
echo "=================================================="

# Function to check workflow status
check_workflow() {
    echo -e "\n${YELLOW}🔍 Checking Raspberry Pi workflow...${NC}"
    
    # Get workflow runs
    local runs=$(curl -s "https://api.github.com/repos/$REPO/actions/workflows/$WORKFLOW_FILE/runs?per_page=5")
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}❌ Failed to fetch workflow data${NC}"
        return 1
    fi
    
    # Parse latest run
    local status=$(echo "$runs" | jq -r '.workflow_runs[0].status // "unknown"')
    local conclusion=$(echo "$runs" | jq -r '.workflow_runs[0].conclusion // "unknown"')
    local html_url=$(echo "$runs" | jq -r '.workflow_runs[0].html_url // "unknown"')
    local created_at=$(echo "$runs" | jq -r '.workflow_runs[0].created_at // "unknown"')
    local run_id=$(echo "$runs" | jq -r '.workflow_runs[0].id // "unknown"')
    
    echo -e "📊 Status: ${status}"
    echo -e "🎯 Conclusion: ${conclusion}"
    echo -e "🕐 Created: ${created_at}"
    echo -e "🔗 URL: ${html_url}"
    
    # Check individual job statuses if run exists
    if [ "$run_id" != "unknown" ] && [ "$run_id" != "null" ]; then
        echo -e "📋 Job Details:"
        local jobs=$(curl -s "https://api.github.com/repos/$REPO/actions/runs/$run_id/jobs")
        if [ $? -eq 0 ]; then
            echo "$jobs" | jq -r '.jobs[] | "  \(.name): \(.status) - \(.conclusion // "in_progress")"' 2>/dev/null || echo "  Unable to fetch job details"
        else
            echo "  Unable to fetch job details"
        fi
    fi
    
    # Status indicators
    case "$status" in
        "completed")
            case "$conclusion" in
                "success")
                    echo -e "${GREEN}✅ Raspberry Pi workflow successful!${NC}"
                    return 0
                    ;;
                "failure")
                    echo -e "${RED}❌ Raspberry Pi workflow failed!${NC}"
                    echo -e "${YELLOW}🔧 Check logs: $html_url${NC}"
                    return 1
                    ;;
                "cancelled")
                    echo -e "${YELLOW}⏹️ Raspberry Pi workflow was cancelled${NC}"
                    return 1
                    ;;
                *)
                    echo -e "${YELLOW}⚠️ Raspberry Pi workflow completed with status: $conclusion${NC}"
                    return 1
                    ;;
            esac
            ;;
        "in_progress")
            echo -e "${BLUE}🔄 Raspberry Pi workflow in progress...${NC}"
            return 2
            ;;
        "queued")
            echo -e "${YELLOW}⏳ Raspberry Pi workflow queued...${NC}"
            return 2
            ;;
        *)
            echo -e "${RED}❓ Raspberry Pi workflow unknown status: $status${NC}"
            return 1
            ;;
    esac
}

# Function to trigger Raspberry Pi workflow
trigger_workflow() {
    echo -e "${BLUE}🚀 Triggering Raspberry Pi workflow...${NC}"
    
    if [ -z "$GITHUB_TOKEN" ]; then
        echo -e "${YELLOW}⚠️ No GitHub token provided. Manual trigger required.${NC}"
        echo -e "${BLUE}Manual trigger URL: https://github.com/$REPO/actions/workflows/$WORKFLOW_FILE${NC}"
        return 1
    fi
    
    # Trigger workflow via API
    local response=$(curl -s -X POST \
        -H "Authorization: token $GITHUB_TOKEN" \
        -H "Accept: application/vnd.github.v3+json" \
        "https://api.github.com/repos/$REPO/actions/workflows/$WORKFLOW_FILE/dispatches" \
        -d '{"ref":"master"}')
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✅ Raspberry Pi workflow triggered successfully${NC}"
        return 0
    else
        echo -e "${RED}❌ Failed to trigger Raspberry Pi workflow${NC}"
        echo -e "${YELLOW}Response: $response${NC}"
        return 1
    fi
}

# Function to analyze Raspberry Pi specific issues
analyze_raspberry_pi_issues() {
    echo -e "\n${BLUE}🔍 Analyzing Raspberry Pi specific issues...${NC}"
    
    # Check if jq is installed
    if ! command -v jq &> /dev/null; then
        echo -e "${RED}❌ jq is not installed. Install with: sudo apt-get install jq${NC}"
        return 1
    fi
    
    # Check if curl is working
    if ! curl -s "https://api.github.com" > /dev/null; then
        echo -e "${RED}❌ Cannot reach GitHub API. Check internet connection.${NC}"
        return 1
    fi
    
    echo -e "${GREEN}✅ Basic checks passed${NC}"
    
    # Check for common Raspberry Pi build issues
    echo -e "${YELLOW}🔧 Common Raspberry Pi build issues to check:${NC}"
    echo -e "  - ARM64 cross-compilation toolchain setup"
    echo -e "  - Boost library compilation for ARM64"
    echo -e "  - ICU library compilation for ARM64"
    echo -e "  - CMake toolchain file configuration"
    echo -e "  - Missing dependencies for cross-compilation"
    echo -e "  - PTHREAD_STACK_MIN preprocessor issues"
}

# Function to provide Raspberry Pi troubleshooting suggestions
troubleshoot_raspberry_pi() {
    echo -e "\n${YELLOW}🔧 Raspberry Pi troubleshooting suggestions:${NC}"
    echo -e "${BLUE}  Cross-compilation issues:${NC}"
    echo -e "    - Verify ARM64 toolchain is properly installed"
    echo -e "    - Check CMake toolchain file configuration"
    echo -e "    - Ensure all dependencies are available for ARM64"
    echo -e ""
    echo -e "${BLUE}  Boost compilation issues:${NC}"
    echo -e "    - Check Boost version compatibility"
    echo -e "    - Verify PTHREAD_STACK_MIN patch is applied"
    echo -e "    - Ensure proper ARM64 architecture flags"
    echo -e ""
    echo -e "${BLUE}  ICU compilation issues:${NC}"
    echo -e "    - Check ICU version compatibility"
    echo -e "    - Verify cross-compilation configuration"
    echo -e "    - Ensure proper host/target architecture"
    echo -e ""
    echo -e "${BLUE}  Build system issues:${NC}"
    echo -e "    - Check CMake configuration"
    echo -e "    - Verify Ninja build system"
    echo -e "    - Check for missing source files"
}

# Main monitoring loop
main() {
    local action=${1:-"monitor"}
    
    case "$action" in
        "trigger")
            echo -e "${BLUE}🚀 Triggering Raspberry Pi workflow...${NC}"
            trigger_workflow
            ;;
        "monitor")
            echo -e "${BLUE}🔍 Monitoring Raspberry Pi workflow...${NC}"
            check_workflow
            ;;
        "analyze")
            echo -e "${BLUE}🔍 Analyzing Raspberry Pi workflow...${NC}"
            analyze_raspberry_pi_issues
            ;;
        "troubleshoot")
            echo -e "${BLUE}🔧 Troubleshooting Raspberry Pi workflow...${NC}"
            troubleshoot_raspberry_pi
            ;;
        "continuous")
            echo -e "${BLUE}🔄 Starting continuous monitoring...${NC}"
            while true; do
                echo ""
                echo -e "${BLUE}🕐 $(date '+%Y-%m-%d %H:%M:%S') - Checking Raspberry Pi workflow...${NC}"
                
                if ! analyze_raspberry_pi_issues; then
                    echo -e "${RED}❌ Basic checks failed. Fix issues and retry.${NC}"
                    sleep 60
                    continue
                fi
                
                check_workflow
                local result=$?
                
                if [ $result -eq 0 ]; then
                    echo -e "${GREEN}✅ Raspberry Pi workflow is successful!${NC}"
                    echo -e "${GREEN}🎉 Monitoring complete.${NC}"
                    break
                elif [ $result -eq 2 ]; then
                    echo -e "${BLUE}🔄 Raspberry Pi workflow in progress, waiting...${NC}"
                else
                    echo -e "${RED}❌ Raspberry Pi workflow failed or has issues${NC}"
                    troubleshoot_raspberry_pi
                fi
                
                echo -e "${BLUE}⏰ Waiting 2 minutes before next check...${NC}"
                sleep 120
            done
            ;;
        *)
            echo -e "${YELLOW}Usage: $0 [trigger|monitor|analyze|troubleshoot|continuous]${NC}"
            echo -e "${BLUE}  trigger:     Trigger a Raspberry Pi workflow run${NC}"
            echo -e "${BLUE}  monitor:     Check current Raspberry Pi workflow status${NC}"
            echo -e "${BLUE}  analyze:     Analyze potential Raspberry Pi issues${NC}"
            echo -e "${BLUE}  troubleshoot: Show troubleshooting suggestions${NC}"
            echo -e "${BLUE}  continuous:  Continuously monitor Raspberry Pi workflow${NC}"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"