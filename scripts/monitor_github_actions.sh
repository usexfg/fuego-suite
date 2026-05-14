#!/bin/bash

# Enhanced GitHub Actions Monitor Script
# Monitors all workflow builds and provides detailed status

REPO="ColinRitman/fuego"
WORKFLOWS=("build.yml" "appimage.yml" "docker.yml" "raspberry-pi.yml" "test-dynamic-supply.yml" "testnet.yml" "termux.yml" "release.yml")
CHECK_INTERVAL=120  # 2 minutes

echo "🔍 Enhanced GitHub Actions Monitor for $REPO"
echo "📋 Monitoring workflows: ${WORKFLOWS[*]}"
echo "⏰ Checking every $(($CHECK_INTERVAL / 60)) minutes..."
echo "=================================="

# Function to check workflow status
check_workflow() {
    local workflow=$1
    local workflow_name=$(echo $workflow | sed 's/.yml//')
    
    echo ""
    echo "🔧 Checking $workflow_name..."
    

    # Get the latest workflow run
    local response=$(curl -s "https://api.github.com/repos/$REPO/actions/workflows/$workflow/runs?per_page=1")
    local status=$(echo "$response" | jq -r '.workflow_runs[0].status // "no_runs"')
    local conclusion=$(echo "$response" | jq -r '.workflow_runs[0].conclusion // "unknown"')
    local html_url=$(echo "$response" | jq -r '.workflow_runs[0].html_url // "unknown"')
    local run_number=$(echo "$response" | jq -r '.workflow_runs[0].run_number // "unknown"')
    local created_at=$(echo "$response" | jq -r '.workflow_runs[0].created_at // "unknown"')
    
    if [ "$status" = "no_runs" ]; then
        echo "  ℹ️  No recent runs found"
        return 0
    fi
    
    echo "  📊 Run #$run_number - Status: $status"
    echo "  🎯 Conclusion: $conclusion"
    echo "  🕐 Created: $created_at"
    echo "  🔗 URL: $html_url"
    
    # Get job details if available
    local run_id=$(echo "$response" | jq -r '.workflow_runs[0].id // "unknown"')
    if [ "$run_id" != "unknown" ] && [ "$run_id" != "null" ]; then
        local jobs_response=$(curl -s "https://api.github.com/repos/$REPO/actions/runs/$run_id/jobs")
        local job_count=$(echo "$jobs_response" | jq -r '.jobs | length')
        
        if [ "$job_count" -gt 0 ]; then
            echo "  📋 Jobs ($job_count):"
            echo "$jobs_response" | jq -r '.jobs[] | "    \(.name): \(.status) - \(.conclusion // "in_progress")"' 2>/dev/null || echo "    Unable to fetch job details"
        fi
    fi
    return 0
}

# Function to get workflow status
get_workflow_status() {
    local workflow_file=$1
    local workflow_info=$(curl -s "https://api.github.com/repos/$REPO/actions/workflows/$workflow_file/runs?per_page=5" 2>/dev/null)
    echo "$workflow_info"
}

# Function to display workflow status
display_workflow_status() {
    local workflow_file=$1
    local workflow_name=$2
    local workflow_data=$(get_workflow_status "$workflow_file")

    if [ $? -ne 0 ] || [ "$workflow_data" = "" ]; then
        echo -e "${YELLOW}⚠️ Could not fetch status for $workflow_name${NC}"
        return 1
    fi

    local status=$(echo "$workflow_data" | jq -r '.workflow_runs[0].status // "unknown"')
    local conclusion=$(echo "$workflow_data" | jq -r '.workflow_runs[0].conclusion // "unknown"')
    local html_url=$(echo "$workflow_data" | jq -r '.workflow_runs[0].html_url // "unknown"')
    local run_id=$(echo "$workflow_data" | jq -r '.workflow_runs[0].id // "unknown"')

    echo -e "${PURPLE}📋 $workflow_name:${NC}"
    echo -e "   📊 Status: $status"
    echo -e "   🎯 Conclusion: $conclusion"
    echo -e "   🔗 URL: $html_url"

    # Show job details if available
    if [ "$run_id" != "unknown" ] && [ "$run_id" != "null" ] && [ "$status" = "in_progress" ]; then
        local job_data=$(curl -s "https://api.github.com/repos/$REPO/actions/runs/$run_id/jobs" 2>/dev/null)
        if [ $? -eq 0 ] && [ "$job_data" != "" ]; then
            echo "   📋 Job Details:"
            echo "$job_data" | jq -r '.jobs[] | "      \(.name): \(.status) - \(.conclusion // "in_progress")"' 2>/dev/null || echo "      Unable to fetch job details"
        fi
    fi

    # Status indicators
    case "$status" in
        "completed")
            case "$conclusion" in
                "success")
                    echo "  ✅ Build successful!"
                    return 0
                    ;;
                "failure")
                    echo "  ❌ Build failed!"
                    return 1
                    ;;
                "cancelled")
                    echo "  ⏹️ Build was cancelled"
                    return 1
                    ;;
                *)
                    echo "  ⚠️ Build completed with status: $conclusion"
                    return 1
                    ;;
            esac
            ;;
        "in_progress")
            echo "  🔄 Build in progress..."
            return 2
            ;;
        "queued")
            echo "  ⏳ Build queued..."
            return 2
            ;;
        *)
            echo "  ❓ Unknown status: $status"
            return 1
            ;;
    esac
}

# Function to show summary
show_summary() {
    local successful=0
    local failed=0
    local in_progress=0
    
    echo ""
    echo "📈 WORKFLOW SUMMARY"
    echo "==================="
    
    for workflow in "${WORKFLOWS[@]}"; do
        check_workflow "$workflow"
        case $? in
            0) ((successful++)) ;;
            1) ((failed++)) ;;
            2) ((in_progress++)) ;;
        esac
    done
    
    echo ""
    echo "📊 TOTALS:"
    echo "  ✅ Successful: $successful"
    echo "  ❌ Failed: $failed"
    echo "  🔄 In Progress: $in_progress"
    echo "  📝 Total: ${#WORKFLOWS[@]}"
    
    if [ $failed -eq 0 ] && [ $in_progress -eq 0 ]; then
        echo ""
        echo "🎉 ALL WORKFLOWS GREEN! 🎉"
        return 0
    elif [ $in_progress -gt 0 ]; then
        echo ""
        echo "⏳ Workflows still running..."
        return 1
    else
        echo ""
        echo "❌ Some workflows failed"
        return 1
    fi
}

# Main monitoring loop
while true; do
    echo ""
    echo "🕐 $(date '+%Y-%m-%d %H:%M:%S') - Checking all workflows..."
    echo "================================================="
    
    if show_summary; then
        echo ""
        echo "🎊 SUCCESS! All workflows are green! 🎊"
        echo "Monitoring complete."
        break
    fi
    
    echo ""
    echo "⏰ Waiting $(($CHECK_INTERVAL / 60)) minutes before next check..."
    sleep $CHECK_INTERVAL
done
    if [ "$status" = "completed" ]; then
        case "$conclusion" in
            "success")
                echo -e "   ${GREEN}✅ Build successful!${NC}"
                return 0
                ;;
            "failure")
                echo -e "   ${RED}❌ Build failed!${NC}"
                return 1
                ;;
            "cancelled")
                echo -e "   ${YELLOW}⏹️ Build was cancelled${NC}"
                return 2
                ;;
            *)
                echo -e "   ${YELLOW}⚠️ Build completed with status: $conclusion${NC}"
                return 3
                ;;
        esac
    elif [ "$status" = "in_progress" ]; then
        echo -e "   ${BLUE}🔄 Build in progress...${NC}"
        return 4
    elif [ "$status" = "queued" ]; then
        echo -e "   ${YELLOW}⏳ Build queued...${NC}"
        return 5
    else
        echo -e "   ${YELLOW}❓ Unknown status: $status${NC}"
        return 6
    fi
}

# Main monitoring loop
main_loop() {
    local all_successful=true
    local failed_workflows=()
    local successful_workflows=()
    local in_progress_workflows=()

    while true; do
        echo ""
        echo -e "${BLUE}🕐 $(date '+%Y-%m-%d %H:%M:%S') - Checking build status...${NC}"

        # Check rate limit
        if ! check_rate_limit; then
            echo -e "${RED}⏸️ Pausing due to API rate limit...${NC}"
            sleep 300  # Wait 5 minutes
            continue
        fi

        # Reset status
        all_successful=true
        failed_workflows=()
        successful_workflows=()
        in_progress_workflows=()

        # Monitor specific workflows
        workflows=(
            "build.yml:Main Build"
            "testnet.yml:Testnet Build"
            "test-dynamic-supply.yml:Dynamic Supply Test"
            "docker.yml:Docker Images"
        )

        for workflow_info in "${workflows[@]}"; do
            IFS=':' read -r workflow_file workflow_name <<< "$workflow_info"
            local result=$(display_workflow_status "$workflow_file" "$workflow_name")

            case $? in
                0)  # Success
                    successful_workflows+=("$workflow_name")
                    ;;
                1)  # Failure
                    all_successful=false
                    failed_workflows+=("$workflow_name")
                    ;;
                4)  # In progress
                    in_progress_workflows+=("$workflow_name")
                    all_successful=false
                    ;;
                5)  # Queued
                    in_progress_workflows+=("$workflow_name")
                    all_successful=false
                    ;;
            esac
        done

        # Display summary
        echo ""
        echo -e "${BLUE}📊 Summary:${NC}"

        if [ ${#successful_workflows[@]} -gt 0 ]; then
            echo -e "${GREEN}✅ Successful: ${successful_workflows[*]}${NC}"
        fi

        if [ ${#in_progress_workflows[@]} -gt 0 ]; then
            echo -e "${BLUE}🔄 In Progress: ${in_progress_workflows[*]}${NC}"
        fi

        if [ ${#failed_workflows[@]} -gt 0 ]; then
            echo -e "${RED}❌ Failed: ${failed_workflows[*]}${NC}"
        fi

        # Check if all builds are successful
        if [ "$all_successful" = true ] && [ ${#successful_workflows[@]} -gt 0 ]; then
            echo ""
            echo -e "${GREEN}🎉 All monitored workflows are successful!${NC}"
            echo -e "${GREEN}🎊 Build monitoring complete.${NC}"
            break
        fi

        echo ""
        echo -e "${BLUE}⏰ Waiting $CHECK_INTERVAL minutes before next check...${NC}"
        sleep $((CHECK_INTERVAL * 60))
    done
}

# Run main loop
main_loop
