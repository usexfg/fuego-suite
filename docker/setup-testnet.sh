#!/bin/bash

# Fuego Testnet Docker Setup Script
# This script sets up a Fuego testnet using Docker

set -e

echo "🔥 Setting up Fuego Testnet with Docker 🔥"
echo "=========================================="

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

# Check if Docker is installed
check_docker() {
    print_status "Checking Docker installation..."
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed. Please install Docker first."
        exit 1
    fi

    if ! command -v docker-compose &> /dev/null; then
        print_error "Docker Compose is not installed. Please install Docker Compose first."
        exit 1
    fi

    print_success "Docker and Docker Compose are installed"
}

# Check if we're in the right directory
check_directory() {
    print_status "Checking current directory..."
    if [ ! -f "docker-compose.testnet.yml" ]; then
        print_error "Please run this script from the docker directory"
        print_error "Current directory: $(pwd)"
        exit 1
    fi
    print_success "Running from correct directory"
}

# Create necessary directories
create_directories() {
    print_status "Creating necessary directories..."

    # Create config directory if it doesn't exist
    mkdir -p config

    # Create nginx config directory
    mkdir -p nginx/ssl

    print_success "Directories created"
}

# Build the Docker images
build_images() {
    print_status "Building Docker images..."

    # Build the Fuego image
    docker-compose -f docker-compose.testnet.yml build

    print_success "Docker images built successfully"
}

# Start the testnet
start_testnet() {
    print_status "Starting Fuego testnet..."

    # Start the services
    docker-compose -f docker-compose.testnet.yml up -d

    print_success "Fuego testnet started successfully"
}

# Wait for services to be ready
wait_for_services() {
    print_status "Waiting for services to be ready..."

    # Wait for the node to be ready
    print_status "Waiting for Fuego node to be ready..."
    timeout=300
    counter=0

    while [ $counter -lt $timeout ]; do
        if curl -s http://localhost:28280/getinfo > /dev/null 2>&1; then
            print_success "Fuego node is ready"
            break
        fi

        counter=$((counter + 10))
        sleep 10
        print_status "Waiting... ($counter/$timeout seconds)"
    done

    if [ $counter -eq $timeout ]; then
        print_warning "Timeout waiting for Fuego node. Check logs with: docker logs fuego-testnet-node"
    fi

    # Wait for wallet service
    print_status "Waiting for wallet service to be ready..."
    timeout=120
    counter=0

    while [ $counter -lt $timeout ]; do
        if curl -s http://localhost:8070/getinfo > /dev/null 2>&1; then
            print_success "Wallet service is ready"
            break
        fi

        counter=$((counter + 5))
        sleep 5
        print_status "Waiting... ($counter/$timeout seconds)"
    done

    if [ $counter -eq $timeout ]; then
        print_warning "Timeout waiting for wallet service. Check logs with: docker logs fuego-testnet-wallet"
    fi
}

# Show status and information
show_status() {
    echo ""
    echo "🔥 Fuego Testnet Status 🔥"
    echo "=========================="

    # Show running containers
    print_status "Running containers:"
    docker ps --filter "name=fuego-testnet" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

    echo ""
    print_status "Service endpoints:"
    echo "  - Fuego Node RPC: http://localhost:28280"
    echo "  - Wallet Service: http://localhost:8070"
    echo "  - P2P Port: 20808"

    echo ""
    print_status "Useful commands:"
    echo "  - View node logs: docker logs fuego-testnet-node"
    echo "  - View wallet logs: docker logs fuego-testnet-wallet"
    echo "  - Stop testnet: docker-compose -f docker-compose.testnet.yml down"
    echo "  - Restart testnet: docker-compose -f docker-compose.testnet.yml restart"
    echo "  - View blockchain data: docker volume ls | grep fuego-testnet"

    echo ""
    print_status "Test the RPC API:"
    echo "  curl -X POST http://localhost:28280/getinfo"
    echo "  curl -X POST http://localhost:28280/getblockcount"
    echo "  curl -X POST http://localhost:8070/getinfo"
}

# Main execution
main() {
    echo "Starting Fuego testnet setup..."

    check_docker
    check_directory
    create_directories
    build_images
    start_testnet
    wait_for_services
    show_status

    echo ""
    print_success "Fuego testnet setup complete! 🎉"
    echo ""
    print_status "The testnet is now running with:"
    print_status "- Testnet mode enabled"
    print_status "- Dynamic supply tracking active"
    print_status "- All RPC endpoints available"
}

# Run main function
main "$@"
