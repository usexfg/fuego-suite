#!/bin/bash
# Fuego Node Setup for Oracle Cloud Always Free Tier
# Supports both mainnet and testnet deployment

set -e

echo "🔥 Fuego Node Oracle Cloud Setup"
echo "================================="

# Configuration
FUEGO_VERSION="latest"
NODE_TYPE=${1:-mainnet}  # mainnet or testnet
WEB_PORT=${2:-8080}

if [[ "$NODE_TYPE" != "mainnet" && "$NODE_TYPE" != "testnet" ]]; then
    echo "Usage: $0 [mainnet|testnet] [web_port]"
    echo "Example: $0 testnet 8080"
    exit 1
fi

echo "Setting up $NODE_TYPE node with web interface on port $WEB_PORT"

# Update system
echo "📦 Updating system packages..."
sudo apt update && sudo apt upgrade -y

# Install Docker
echo "🐳 Installing Docker..."
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER
rm get-docker.sh

# Install Docker Compose
echo "🔧 Installing Docker Compose..."
sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose

# Create Fuego directory
echo "📁 Creating Fuego directories..."
mkdir -p ~/fuego/{data,web}
cd ~/fuego

# Create docker-compose.yml
echo "📝 Creating Docker Compose configuration..."
cat > docker-compose.yml << EOF
version: '3.8'

services:
  fuego-node:
    image: ghcr.io/usexfg/fuego:latest
    container_name: fuego-node
    restart: unless-stopped
    ports:
      - "20808:20808"  # P2P port
      - "28180:28180"  # RPC port
    volumes:
      - ./data:/home/fuego/.fuego
    command: >
      fuegod
      --data-dir=/home/fuego/.fuego
      --rpc-bind-ip=0.0.0.0
      --rpc-bind-port=28180
      --p2p-bind-ip=0.0.0.0
      --p2p-bind-port=20808
      --enable-cors="*"
      --enable-blockchain-indexes
      $(if [ "$NODE_TYPE" = "testnet" ]; then echo "--testnet"; fi)
    networks:
      - fuego-net

  fuego-web:
    image: nginx:alpine
    container_name: fuego-web
    restart: unless-stopped
    ports:
      - "$WEB_PORT:80"
    volumes:
      - ./web:/usr/share/nginx/html:ro
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
    depends_on:
      - fuego-node
    networks:
      - fuego-net

networks:
  fuego-net:
    driver: bridge
EOF

# Create nginx configuration
echo "🌐 Creating web server configuration..."
cat > nginx.conf << EOF
events {
    worker_connections 1024;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;
    
    upstream fuego_rpc {
        server fuego-node:28180;
    }
    
    server {
        listen 80;
        server_name _;
        
        # Serve static files
        location / {
            root /usr/share/nginx/html;
            index index.html;
            try_files \$uri \$uri/ /index.html;
        }
        
        # Proxy RPC calls
        location /json_rpc {
            proxy_pass http://fuego_rpc/json_rpc;
            proxy_set_header Host \$host;
            proxy_set_header X-Real-IP \$remote_addr;
            proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
            
            # CORS headers
            add_header Access-Control-Allow-Origin "*" always;
            add_header Access-Control-Allow-Methods "GET, POST, OPTIONS" always;
            add_header Access-Control-Allow-Headers "Content-Type, Authorization" always;
            
            if (\$request_method = OPTIONS) {
                return 204;
            }
        }
    }
}
EOF

# Create web interface
echo "🎨 Creating web interface..."
cat > web/index.html << 'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fuego Node Dashboard</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #1a1a1a; color: #fff; }
        .container { max-width: 1200px; margin: 0 auto; padding: 20px; }
        .header { text-align: center; margin-bottom: 40px; }
        .header h1 { color: #ff6b35; font-size: 2.5em; margin-bottom: 10px; }
        .stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin-bottom: 40px; }
        .stat-card { background: #2d2d2d; padding: 20px; border-radius: 10px; border-left: 4px solid #ff6b35; }
        .stat-card h3 { color: #ff6b35; margin-bottom: 10px; }
        .stat-card .value { font-size: 1.5em; font-weight: bold; }
        .status { padding: 4px 12px; border-radius: 20px; font-size: 0.8em; }
        .status.online { background: #00ff88; color: #000; }
        .status.offline { background: #ff4757; color: #fff; }
        .loading { text-align: center; padding: 40px; }
        .footer { text-align: center; margin-top: 40px; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔥 Fuego Node</h1>
            <p>Blockchain Explorer & Node Status</p>
        </div>
        
        <div id="loading" class="loading">
            <p>Loading node information...</p>
        </div>
        
        <div id="content" style="display: none;">
            <div class="stats">
                <div class="stat-card">
                    <h3>Node Status</h3>
                    <div class="value">
                        <span id="status" class="status offline">Offline</span>
                    </div>
                </div>
                <div class="stat-card">
                    <h3>Block Height</h3>
                    <div class="value" id="height">-</div>
                </div>
                <div class="stat-card">
                    <h3>Network Hash Rate</h3>
                    <div class="value" id="hashrate">-</div>
                </div>
                <div class="stat-card">
                    <h3>Difficulty</h3>
                    <div class="value" id="difficulty">-</div>
                </div>
                <div class="stat-card">
                    <h3>Connected Peers</h3>
                    <div class="value" id="peers">-</div>
                </div>
                <div class="stat-card">
                    <h3>Network Type</h3>
                    <div class="value" id="network">-</div>
                </div>
            </div>
        </div>
        
        <div class="footer">
            <p>Fuego Node Dashboard - Free Oracle Cloud Hosting</p>
        </div>
    </div>

    <script>
        async function fetchNodeInfo() {
            try {
                const response = await fetch('/json_rpc', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        jsonrpc: '2.0',
                        id: '1',
                        method: 'getinfo'
                    })
                });
                
                const data = await response.json();
                if (data.result) {
                    updateUI(data.result);
                }
            } catch (error) {
                console.error('Failed to fetch node info:', error);
            }
        }
        
        function updateUI(info) {
            document.getElementById('loading').style.display = 'none';
            document.getElementById('content').style.display = 'block';
            
            const status = document.getElementById('status');
            status.textContent = 'Online';
            status.className = 'status online';
            
            document.getElementById('height').textContent = info.height?.toLocaleString() || '-';
            document.getElementById('hashrate').textContent = formatHashRate(info.hashrate) || '-';
            document.getElementById('difficulty').textContent = info.difficulty?.toLocaleString() || '-';
            document.getElementById('peers').textContent = info.outgoing_connections_count + info.incoming_connections_count || '-';
            document.getElementById('network').textContent = info.testnet ? 'Testnet' : 'Mainnet';
        }
        
        function formatHashRate(hashrate) {
            if (!hashrate) return '-';
            const units = ['H/s', 'KH/s', 'MH/s', 'GH/s', 'TH/s'];
            let i = 0;
            while (hashrate >= 1000 && i < units.length - 1) {
                hashrate /= 1000;
                i++;
            }
            return hashrate.toFixed(2) + ' ' + units[i];
        }
        
        // Update every 30 seconds
        fetchNodeInfo();
        setInterval(fetchNodeInfo, 30000);
    </script>
</body>
</html>
EOF

# Configure firewall
echo "🔒 Configuring firewall..."
sudo ufw allow ssh
sudo ufw allow $WEB_PORT
sudo ufw allow 20808  # P2P port
sudo ufw --force enable

# Start services
echo "🚀 Starting Fuego node..."
docker-compose up -d

echo ""
echo "✅ Setup complete!"
echo "================================="
echo "🌐 Web Interface: http://$(curl -s ifconfig.me):$WEB_PORT"
echo "🔗 RPC Endpoint: http://$(curl -s ifconfig.me):28180/json_rpc"
echo "📊 Node Type: $NODE_TYPE"
echo ""
echo "📋 Useful commands:"
echo "  docker-compose logs -f fuego-node  # View logs"
echo "  docker-compose stop               # Stop services"
echo "  docker-compose start              # Start services"
echo "  docker-compose down               # Remove containers"
echo ""
echo "🔥 Your Fuego node is now running!"
EOF 