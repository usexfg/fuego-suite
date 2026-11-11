#!/bin/bash

# Trigger Fuego Build Script
# This script helps trigger GitHub Actions builds and check their status

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if gh CLI is installed
check_gh_cli() {
    if ! command -v gh &> /dev/null; then
        print_error "GitHub CLI (gh) is not installed. Please install it first:"
        echo "  brew install gh"
        echo "  gh auth login"
        exit 1
    fi
}

# Get repository info
get_repo_info() {
    REPO_URL=$(git remote get-url origin)
    if [[ $REPO_URL == *"github.com"* ]]; then
        REPO_NAME=$(echo $REPO_URL | sed 's/.*github\.com[:/]\([^/]*\/[^/]*\).*/\1/' | sed 's/\.git$//')
        print_status "Repository: $REPO_NAME"
    else
        print_error "Not a GitHub repository"
        exit 1
    fi
}

# Trigger workflow
trigger_workflow() {
    local workflow=$1
    print_status "Triggering $workflow workflow..."
    
    gh workflow run $workflow.yml --repo $REPO_NAME
    
    if [ $? -eq 0 ]; then
        print_success "Workflow triggered successfully!"
        print_status "You can monitor the build at: https://github.com/$REPO_NAME/actions"
    else
        print_error "Failed to trigger workflow"
        exit 1
    fi
}

# Check workflow status
check_status() {
    print_status "Checking workflow status..."
    gh run list --repo $REPO_NAME --limit 5
}

# Main script
main() {
    echo "🔥 Fuego Build Trigger Script 🔥"
    echo "================================"
    
    check_gh_cli
    get_repo_info
    
    case "${1:-build}" in
        "build")
            trigger_workflow "build"
            ;;
        "test-dynamic-supply")
            trigger_workflow "test-dynamic-supply"
            ;;
        "testnet")
            trigger_workflow "testnet"
            ;;
        "status")
            check_status
            ;;
        "all")
            print_status "Triggering all workflows..."
            trigger_workflow "build"
            sleep 2
            trigger_workflow "test-dynamic-supply"
            sleep 2
            trigger_workflow "testnet"
            ;;
        *)
            echo "Usage: $0 [build|test-dynamic-supply|testnet|status|all]"
            echo ""
            echo "Options:"
            echo "  build              - Trigger main build workflow"
            echo "  test-dynamic-supply - Trigger dynamic supply tests"
            echo "  testnet           - Trigger testnet build and test"
            echo "  status            - Check workflow status"
            echo "  all               - Trigger all workflows"
            exit 1
            ;;
    esac
}

main "$@"
