#!/bin/bash

# 🐳 Fuego Docker Setup Script
# Super easy setup for Fuego cryptocurrency node

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="fuego-docker"
DEFAULT_DATA_PATH="./data"
DEFAULT_CONFIG_PATH="./config"

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

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check Docker installation
check_docker() {
    if ! command_exists docker; then
        print_error "Docker is not installed. Please install Docker first."
        echo "Visit: https://docs.docker.com/get-docker/"
        exit 1
    fi

    if ! docker info >/dev/null 2>&1; then
        print_error "Docker is not running. Please start Docker first."
        exit 1
    fi

    print_success "Docker is installed and running"
}

# Function to check Docker Compose
check_docker_compose() {
    if ! command_exists docker-compose; then
        print_error "Docker Compose is not installed. Please install Docker Compose first."
        echo "Visit: https://docs.docker.com/compose/install/"
        exit 1
    fi

    print_success "Docker Compose is installed"
}

# Function to create directory structure
create_directories() {
    print_status "Creating directory structure..."
    
    mkdir -p "${DEFAULT_DATA_PATH}/fuego"
    mkdir -p "${DEFAULT_DATA_PATH}/wallet"
    mkdir -p "${DEFAULT_DATA_PATH}/logs"
    mkdir -p "${DEFAULT_CONFIG_PATH}"
    mkdir -p "./nginx"
    mkdir -p "./monitoring/grafana/dashboards"
    mkdir -p "./monitoring/grafana/datasources"
    mkdir -p "./scripts"
    
    print_success "Directory structure created"
}

# Function to create configuration files
create_config_files() {
    print_status "Creating configuration files..."
    
    # Create .env file
    cat > .env << EOF
# Fuego Docker Environment Variables
FUEGO_DATA_PATH=${DEFAULT_DATA_PATH}/fuego
FUEGO_WALLET_PATH=${DEFAULT_DATA_PATH}/wallet
FUEGO_LOGS_PATH=${DEFAULT_DATA_PATH}/logs

# Build Configuration
BUILD_TYPE=Release
ENABLE_OPTIMIZATIONS=ON
BUILD_TESTS=OFF
STATIC_BUILD=ON
PARALLEL_BUILD=4

# Network Configuration
P2P_PORT=20808
RPC_PORT=28180
WALLET_PORT=8070

# Logging
FUEGO_LOG_LEVEL=2
EOF

    # Create nginx configuration
    cat > nginx/nginx.conf << EOF
events {
    worker_connections 1024;
}

http {
    upstream fuego_node {
        server fuego-node:28180;
    }
    
    upstream fuego_wallet {
        server fuego-wallet:8070;
    }
    
    server {
        listen 80;
        server_name localhost;
        
        location / {
            root /usr/share/nginx/html;
            index index.html;
        }
        
        location /api/ {
            proxy_pass http://fuego_node/;
            proxy_set_header Host \$host;
            proxy_set_header X-Real-IP \$remote_addr;
            proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto \$scheme;
        }
        
        location /wallet/ {
            proxy_pass http://fuego_wallet/;
            proxy_set_header Host \$host;
            proxy_set_header X-Real-IP \$remote_addr;
            proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto \$scheme;
        }
    }
}
EOF

    # Create basic HTML page
    cat > nginx/html/index.html << EOF
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fuego Docker Node</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .container { max-width: 800px; margin: 0 auto; }
        .status { padding: 10px; margin: 10px 0; border-radius: 5px; }
        .online { background-color: #d4edda; color: #155724; }
        .offline { background-color: #f8d7da; color: #721c24; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔥 Fuego Docker Node</h1>
        <p>Your Fuego cryptocurrency node is running successfully!</p>
        
        <h2>Services</h2>
        <div class="status online">
            <strong>Node Status:</strong> Online
        </div>
        <div class="status online">
            <strong>Wallet Service:</strong> Available
        </div>
        
        <h2>Endpoints</h2>
        <ul>
            <li><strong>Node RPC:</strong> <a href="/api/getinfo">http://localhost/api/</a></li>
            <li><strong>Wallet RPC:</strong> <a href="/wallet/">http://localhost/wallet/</a></li>
            <li><strong>Prometheus:</strong> <a href="http://localhost:9090">http://localhost:9090</a></li>
            <li><strong>Grafana:</strong> <a href="http://localhost:3000">http://localhost:3000</a></li>
        </ul>
        
        <h2>Documentation</h2>
        <p>For more information, visit: <a href="https://github.com/usexfg/fuego">Fuego GitHub</a></p>
    </div>
</body>
</html>
EOF

    # Create Prometheus configuration
    cat > monitoring/prometheus.yml << EOF
global:
  scrape_interval: 15s
  evaluation_interval: 15s

rule_files:
  # - "first_rules.yml"
  # - "second_rules.yml"

scrape_configs:
  - job_name: 'prometheus'
    static_configs:
      - targets: ['localhost:9090']

  - job_name: 'fuego-node'
    static_configs:
      - targets: ['fuego-node:28180']
    metrics_path: '/metrics'
    scrape_interval: 30s

  - job_name: 'fuego-wallet'
    static_configs:
      - targets: ['fuego-wallet:8070']
    metrics_path: '/metrics'
    scrape_interval: 30s
EOF

    # Create Grafana datasource
    mkdir -p monitoring/grafana/datasources
    cat > monitoring/grafana/datasources/prometheus.yml << EOF
apiVersion: 1

datasources:
  - name: Prometheus
    type: prometheus
    access: proxy
    url: http://prometheus:9090
    isDefault: true
EOF

    print_success "Configuration files created"
}

# Function to set proper permissions
set_permissions() {
    print_status "Setting proper permissions..."
    
    chmod +x "${SCRIPT_DIR}/fuego-docker-setup.sh"
    chmod 755 "${DEFAULT_DATA_PATH}"
    chmod 755 "${DEFAULT_CONFIG_PATH}"
    
    # Set ownership for data directories (if running as root)
    if [[ $EUID -eq 0 ]]; then
        chown -R 1000:1000 "${DEFAULT_DATA_PATH}"
    fi
    
    print_success "Permissions set"
}

# Function to build and start services
start_services() {
    local profile=${1:-"node"}
    
    print_status "Starting Fuego Docker services with profile: ${profile}"
    
    # Build the image first
    print_status "Building Fuego Docker image..."
    docker-compose -f docker-compose.fuego-docker.yml build
    
    # Start services
    print_status "Starting services..."
    docker-compose -f docker-compose.fuego-docker.yml --profile "${profile}" up -d
    
    print_success "Services started successfully!"
}

# Function to show status
show_status() {
    print_status "Checking service status..."
    
    echo ""
    echo "=== Docker Containers ==="
    docker-compose -f docker-compose.fuego-docker.yml ps
    
    echo ""
    echo "=== Service Health ==="
    docker-compose -f docker-compose.fuego-docker.yml exec fuego-node curl -f http://localhost:28180/getinfo 2>/dev/null && \
        print_success "Node is healthy" || print_warning "Node health check failed"
    
    echo ""
    echo "=== Port Status ==="
    echo "P2P Port (20808): $(netstat -tuln | grep :20808 >/dev/null && echo "LISTENING" || echo "NOT LISTENING")"
    echo "RPC Port (28180): $(netstat -tuln | grep :28180 >/dev/null && echo "LISTENING" || echo "NOT LISTENING")"
    echo "Wallet Port (8070): $(netstat -tuln | grep :8070 >/dev/null && echo "LISTENING" || echo "NOT LISTENING")"
}

# Function to show usage
show_usage() {
    echo "🐳 Fuego Docker Setup Script"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  setup           Complete setup (default)"
    echo "  start [profile] Start services with specified profile"
    echo "  stop            Stop all services"
    echo "  restart         Restart all services"
    echo "  status          Show service status"
    echo "  logs [service]  Show logs for specific service"
    echo "  clean           Clean up containers and volumes"
    echo "  help            Show this help message"
    echo ""
    echo "Profiles:"
    echo "  node            Node only (default)"
    echo "  wallet          Node + Wallet"
    echo "  web             Node + Wallet + Nginx"
    echo "  monitoring      Node + Wallet + Monitoring stack"
    echo "  full            All services"
    echo "  development     Development environment"
    echo ""
    echo "Examples:"
    echo "  $0 setup"
    echo "  $0 start wallet"
    echo "  $0 start full"
    echo "  $0 status"
}

# Function to handle logs
show_logs() {
    local service=${1:-"fuego-node"}
    print_status "Showing logs for ${service}..."
    docker-compose -f docker-compose.fuego-docker.yml logs -f "${service}"
}

# Function to clean up
clean_up() {
    print_warning "This will remove all containers and volumes. Are you sure? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        print_status "Cleaning up..."
        docker-compose -f docker-compose.fuego-docker.yml down -v
        docker system prune -f
        print_success "Cleanup completed"
    else
        print_status "Cleanup cancelled"
    fi
}

# Main script logic
main() {
    local command=${1:-"setup"}
    
    case $command in
        "setup")
            print_status "Starting Fuego Docker setup..."
            check_docker
            check_docker_compose
            create_directories
            create_config_files
            set_permissions
            start_services "node"
            show_status
            print_success "Setup completed successfully!"
            echo ""
            echo "🎉 Your Fuego Docker node is ready!"
            echo "📊 Check status: $0 status"
            echo "📝 View logs: $0 logs fuego-node"
            echo "🌐 Web interface: http://localhost (if using web profile)"
            ;;
        "start")
            local profile=${2:-"node"}
            start_services "$profile"
            show_status
            ;;
        "stop")
            print_status "Stopping services..."
            docker-compose -f docker-compose.fuego-docker.yml down
            print_success "Services stopped"
            ;;
        "restart")
            print_status "Restarting services..."
            docker-compose -f docker-compose.fuego-docker.yml restart
            print_success "Services restarted"
            ;;
        "status")
            show_status
            ;;
        "logs")
            show_logs "$2"
            ;;
        "clean")
            clean_up
            ;;
        "help"|"-h"|"--help")
            show_usage
            ;;
        *)
            print_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"