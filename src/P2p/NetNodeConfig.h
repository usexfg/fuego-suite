// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include <boost/program_options.hpp>
#include "P2pProtocolTypes.h"
#include "NetworkAddressTypes.h"

#ifdef ENABLE_FUEGOMESH
#include "FuegoMeshtastic/MeshtasticIntegration.h"
#endif

namespace CryptoNote {

struct AnonymousInbound {
  std::string our_address;  // .b32.i2p or .onion address
  std::string bind_address; // local loopback bind (e.g. 127.0.0.1:18083)
  uint32_t max_connections;
  NetworkZone zone;
};

struct TxProxy {
  NetworkZone zone;
  std::string proxy_host;
  uint16_t proxy_port;
  uint32_t max_connections;
};

class NetNodeConfig {
public:
  NetNodeConfig();
  static void initOptions(boost::program_options::options_description& desc);
  bool init(const boost::program_options::variables_map& vm);

  std::string getP2pStateFilename() const;
  bool getTestnet() const;
  std::string getBindIp() const;
  uint16_t getBindPort() const;
  uint16_t getExternalPort() const;
  bool getAllowLocalIp() const;
  std::vector<PeerlistEntry> getPeers() const;
  std::vector<NetworkAddress> getPriorityNodes() const;
  std::vector<NetworkAddress> getExclusiveNodes() const;
  std::vector<NetworkAddress> getSeedNodes() const;
  bool getHideMyPort() const;
  std::string getConfigFolder() const;

  // I2P configuration
  bool getI2PEnabled() const { return i2pEnabled; }
  std::string getI2PSocksHost() const { return i2pSocksHost; }
  uint16_t getI2PSocksPort() const { return i2pSocksPort; }

  // Tor configuration
  bool getTorEnabled() const { return torEnabled; }
  std::string getTorSocksHost() const { return torSocksHost; }
  uint16_t getTorSocksPort() const { return torSocksPort; }

  // Privacy network options
  bool getRestrictToPrivacyNet() const { return restrictToPrivacyNet; }
  std::vector<AnonymousInbound> getAnonymousInbound() const { return anonymousInbound; }
  std::vector<TxProxy> getTxProxies() const { return txProxies; }

#ifdef ENABLE_FUEGOMESH
  bool getMeshtasticEnabled() const;
  std::string getMeshtasticHost() const;
  uint16_t getMeshtasticPort() const;
  std::string getMeshtasticDevice() const;
  MeshtasticConfig getMeshtasticConfig() const;
#endif

  void setP2pStateFilename(const std::string& filename);
  void setTestnet(bool isTestnet);
  void setBindIp(const std::string& ip);
  void setBindPort(uint16_t port);
  void setExternalPort(uint16_t port);
  void setAllowLocalIp(bool allow);
  void setPeers(const std::vector<PeerlistEntry>& peerList);
  void setPriorityNodes(const std::vector<NetworkAddress>& addresses);
  void setExclusiveNodes(const std::vector<NetworkAddress>& addresses);
  void setSeedNodes(const std::vector<NetworkAddress>& addresses);
  void setHideMyPort(bool hide);
  void setConfigFolder(const std::string& folder);

#ifdef ENABLE_FUEGOMESH
  void setMeshtasticConfig(const MeshtasticConfig& config);
#endif

private:
  std::string bindIp;
  uint16_t bindPort;
  uint16_t externalPort;
  bool allowLocalIp;
  std::vector<PeerlistEntry> peers;
  std::vector<NetworkAddress> priorityNodes;
  std::vector<NetworkAddress> exclusiveNodes;
  std::vector<NetworkAddress> seedNodes;
  bool hideMyPort;
  std::string configFolder;
  std::string p2pStateFilename;
  bool testnet;

  // I2P
  bool i2pEnabled;
  std::string i2pSocksHost;
  uint16_t i2pSocksPort;

  // Tor
  bool torEnabled;
  std::string torSocksHost;
  uint16_t torSocksPort;

  // Privacy options
  bool restrictToPrivacyNet;
  std::vector<AnonymousInbound> anonymousInbound;
  std::vector<TxProxy> txProxies;

#ifdef ENABLE_FUEGOMESH
  bool meshtasticEnabled;
  std::string meshtasticHost;
  uint16_t meshtasticPort;
  std::string meshtasticDevice;
  MeshtasticConfig meshtasticConfig;
#endif
};

} //namespace nodetool
