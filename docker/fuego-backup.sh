#!/bin/bash

# 🔥 Fuego Backup & Restore Script
# Backup and restore Fuego data and configuration

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BACKUP_DIR="${PROJECT_DIR}/backups"
DATA_DIR="${PROJECT_DIR}/data"
CONFIG_DIR="${PROJECT_DIR}/config"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
BACKUP_NAME="fuego_backup_${TIMESTAMP}"

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

# Function to check if services are running
check_services_running() {
    if docker ps --format "table {{.Names}}" | grep -q "fuego-node"; then
        return 0
    else
        return 1
    fi
}

# Function to stop services
stop_services() {
    print_status "Stopping Fuego services..."
    docker-compose -f "${PROJECT_DIR}/docker-compose.fuego-docker.yml" down
    print_success "Services stopped"
}

# Function to start services
start_services() {
    print_status "Starting Fuego services..."
    docker-compose -f "${PROJECT_DIR}/docker-compose.fuego-docker.yml" up -d
    print_success "Services started"
}

# Function to create backup
create_backup() {
    local backup_path="${BACKUP_DIR}/${BACKUP_NAME}"
    
    print_status "Creating backup: ${BACKUP_NAME}"
    
    # Create backup directory
    mkdir -p "${backup_path}"
    
    # Backup data directories
    if [[ -d "${DATA_DIR}" ]]; then
        print_status "Backing up data directories..."
        cp -r "${DATA_DIR}" "${backup_path}/"
        print_success "Data directories backed up"
    else
        print_warning "Data directory not found: ${DATA_DIR}"
    fi
    
    # Backup configuration files
    if [[ -d "${CONFIG_DIR}" ]]; then
        print_status "Backing up configuration files..."
        cp -r "${CONFIG_DIR}" "${backup_path}/"
        print_success "Configuration files backed up"
    else
        print_warning "Config directory not found: ${CONFIG_DIR}"
    fi
    
    # Backup environment file
    if [[ -f "${PROJECT_DIR}/.env" ]]; then
        print_status "Backing up environment file..."
        cp "${PROJECT_DIR}/.env" "${backup_path}/"
        print_success "Environment file backed up"
    fi
    
    # Backup docker-compose file
    if [[ -f "${PROJECT_DIR}/docker-compose.fuego-docker.yml" ]]; then
        print_status "Backing up docker-compose file..."
        cp "${PROJECT_DIR}/docker-compose.fuego-docker.yml" "${backup_path}/"
        print_success "Docker-compose file backed up"
    fi
    
    # Create backup info file
    cat > "${backup_path}/backup_info.txt" << EOF
Fuego Backup Information
========================
Backup Date: $(date)
Backup Name: ${BACKUP_NAME}
Fuego Version: $(git describe --tags 2>/dev/null || echo "Unknown")
System: $(uname -a)
Docker Version: $(docker --version)

Backup Contents:
- Data directories (blockchain, wallet, logs)
- Configuration files
- Environment variables
- Docker-compose configuration

Restore Command:
./scripts/fuego-backup.sh restore ${BACKUP_NAME}
EOF
    
    # Create compressed archive
    print_status "Creating compressed archive..."
    cd "${BACKUP_DIR}"
    tar -czf "${BACKUP_NAME}.tar.gz" "${BACKUP_NAME}"
    rm -rf "${BACKUP_NAME}"
    
    print_success "Backup created: ${BACKUP_DIR}/${BACKUP_NAME}.tar.gz"
    
    # Show backup size
    local backup_size=$(du -h "${BACKUP_DIR}/${BACKUP_NAME}.tar.gz" | cut -f1)
    print_status "Backup size: ${backup_size}"
}

# Function to list backups
list_backups() {
    print_status "Available backups:"
    echo ""
    
    if [[ ! -d "${BACKUP_DIR}" ]] || [[ -z "$(ls -A "${BACKUP_DIR}" 2>/dev/null)" ]]; then
        print_warning "No backups found"
        return
    fi
    
    echo "Backup Name                    | Size      | Date"
    echo "--------------------------------|-----------|-------------------"
    
    for backup in "${BACKUP_DIR}"/*.tar.gz; do
        if [[ -f "$backup" ]]; then
            local filename=$(basename "$backup")
            local size=$(du -h "$backup" | cut -f1)
            local date=$(stat -c %y "$backup" | cut -d' ' -f1)
            printf "%-30s | %-9s | %s\n" "$filename" "$size" "$date"
        fi
    done
}

# Function to restore backup
restore_backup() {
    local backup_name=$1
    
    if [[ -z "$backup_name" ]]; then
        print_error "Please specify a backup name to restore"
        echo "Usage: $0 restore <backup_name>"
        echo ""
        echo "Available backups:"
        list_backups
        exit 1
    fi
    
    local backup_file="${BACKUP_DIR}/${backup_name}.tar.gz"
    
    if [[ ! -f "$backup_file" ]]; then
        print_error "Backup not found: $backup_file"
        echo ""
        echo "Available backups:"
        list_backups
        exit 1
    fi
    
    print_warning "This will overwrite existing data. Are you sure? (y/N)"
    read -r response
    if [[ ! "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        print_status "Restore cancelled"
        exit 0
    fi
    
    # Stop services if running
    if check_services_running; then
        stop_services
    fi
    
    print_status "Restoring backup: ${backup_name}"
    
    # Create temporary directory
    local temp_dir=$(mktemp -d)
    
    # Extract backup
    print_status "Extracting backup..."
    tar -xzf "$backup_file" -C "$temp_dir"
    
    # Find the extracted directory
    local extracted_dir=$(find "$temp_dir" -maxdepth 1 -type d -name "${backup_name}" | head -1)
    
    if [[ -z "$extracted_dir" ]]; then
        print_error "Failed to extract backup"
        rm -rf "$temp_dir"
        exit 1
    fi
    
    # Restore data directories
    if [[ -d "${extracted_dir}/data" ]]; then
        print_status "Restoring data directories..."
        rm -rf "${DATA_DIR}"
        cp -r "${extracted_dir}/data" "${PROJECT_DIR}/"
        print_success "Data directories restored"
    fi
    
    # Restore configuration files
    if [[ -d "${extracted_dir}/config" ]]; then
        print_status "Restoring configuration files..."
        rm -rf "${CONFIG_DIR}"
        cp -r "${extracted_dir}/config" "${PROJECT_DIR}/"
        print_success "Configuration files restored"
    fi
    
    # Restore environment file
    if [[ -f "${extracted_dir}/.env" ]]; then
        print_status "Restoring environment file..."
        cp "${extracted_dir}/.env" "${PROJECT_DIR}/"
        print_success "Environment file restored"
    fi
    
    # Restore docker-compose file
    if [[ -f "${extracted_dir}/docker-compose.fuego-docker.yml" ]]; then
        print_status "Restoring docker-compose file..."
        cp "${extracted_dir}/docker-compose.fuego-docker.yml" "${PROJECT_DIR}/"
        print_success "Docker-compose file restored"
    fi
    
    # Clean up
    rm -rf "$temp_dir"
    
    # Set proper permissions
    if [[ -d "${DATA_DIR}" ]]; then
        chmod -R 755 "${DATA_DIR}"
        if [[ $EUID -eq 0 ]]; then
            chown -R 1000:1000 "${DATA_DIR}"
        fi
    fi
    
    print_success "Backup restored successfully"
    
    # Ask if user wants to start services
    print_status "Do you want to start the services now? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        start_services
    fi
}

# Function to clean old backups
clean_backups() {
    local days=${1:-30}
    
    print_status "Cleaning backups older than ${days} days..."
    
    if [[ ! -d "${BACKUP_DIR}" ]]; then
        print_warning "Backup directory not found"
        return
    fi
    
    local deleted_count=0
    for backup in "${BACKUP_DIR}"/*.tar.gz; do
        if [[ -f "$backup" ]]; then
            local file_age=$(( ($(date +%s) - $(stat -c %Y "$backup")) / 86400 ))
            if [[ $file_age -gt $days ]]; then
                print_status "Deleting old backup: $(basename "$backup")"
                rm "$backup"
                ((deleted_count++))
            fi
        fi
    done
    
    if [[ $deleted_count -eq 0 ]]; then
        print_status "No old backups found to delete"
    else
        print_success "Deleted ${deleted_count} old backup(s)"
    fi
}

# Function to show usage
show_usage() {
    echo "🔥 Fuego Backup & Restore Script"
    echo ""
    echo "Usage: $0 [COMMAND] [ARGS...]"
    echo ""
    echo "Commands:"
    echo "  backup [name]           Create backup (optional custom name)"
    echo "  restore <name>          Restore backup by name"
    echo "  list                    List available backups"
    echo "  clean [days]           Clean backups older than N days (default: 30)"
    echo "  help                    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 backup"
    echo "  $0 backup my_custom_backup"
    echo "  $0 restore fuego_backup_20231201_143022"
    echo "  $0 list"
    echo "  $0 clean 7"
    echo ""
    echo "Backup Location: ${BACKUP_DIR}"
}

# Function to check dependencies
check_dependencies() {
    if ! command_exists docker; then
        print_error "Docker is required but not installed"
        exit 1
    fi
    
    if ! command_exists docker-compose; then
        print_error "Docker Compose is required but not installed"
        exit 1
    fi
    
    if ! command_exists tar; then
        print_error "tar is required but not installed"
        exit 1
    fi
}

# Main script logic
main() {
    local command=${1:-"help"}
    
    check_dependencies
    
    # Create backup directory if it doesn't exist
    mkdir -p "${BACKUP_DIR}"
    
    case $command in
        "backup")
            local backup_name=${2:-""}
            if [[ -n "$backup_name" ]]; then
                BACKUP_NAME="${backup_name}_${TIMESTAMP}"
            fi
            create_backup
            ;;
        "restore")
            restore_backup "$2"
            ;;
        "list")
            list_backups
            ;;
        "clean")
            clean_backups "$2"
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