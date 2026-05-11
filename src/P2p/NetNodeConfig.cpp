// Copyright (c) 2017-2025 Elderfire Privacy Council
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

#include "NetNodeConfig.h"

#include <boost/utility/value_init.hpp>

#include <Common/Util.h>
#include "Common/CommandLine.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "CryptoNoteConfig.h"

namespace CryptoNote {
namespace {

const command_line::arg_descriptor<std::string> arg_p2p_bind_ip        = {"p2p-bind-ip", "Interface for p2p network protocol", "0.0.0.0"};
const command_line::arg_descriptor<uint16_t>    arg_p2p_bind_port      = {"p2p-bind-port", "Port for p2p network protocol", P2P_DEFAULT_PORT};
const command_line::arg_descriptor<uint16_t>    arg_p2p_external_port = { "p2p-external-port", "External port for p2p network protocol (if port forwarding used with NAT)", 0 };
const command_line::arg_descriptor<bool>        arg_p2p_allow_local_ip = {"allow-local-ip", "Allow local ip add to peer list, mostly in debug purposes"};
const command_line::arg_descriptor<std::vector<std::string> > arg_p2p_add_peer   = {"add-peer", "Manually add peer to local peerlist"};
const command_line::arg_descriptor<std::vector<std::string> > arg_p2p_add_priority_node   = {"add-priority-node", "Specify list of peers to connect to and attempt to keep the connection open"};
const command_line::arg_descriptor<std::vector<std::string> > arg_p2p_add_exclusive_node   = {"add-exclusive-node", "Specify list of peers to connect to only."
      " If this option is given the options add-priority-node and seed-node are ignored"};
const command_line::arg_descriptor<std::vector<std::string> > arg_p2p_seed_node   = {"seed-node", "Connect to a node to retrieve peer addresses, and disconnect"};
const command_line::arg_descriptor<bool> arg_p2p_hide_my_port   =    {"hide-my-port", "Do not announce yourself as peerlist candidate", false, true};

const command_line::arg_descriptor<bool>        arg_p2p_use_i2p        = {"p2p-use-i2p", "Route P2P connections through I2P SOCKS5 proxy", false};
const command_line::arg_descriptor<std::string> arg_i2p_socks_host     = {"i2p-socks-host", "I2P SOCKS5 proxy host", "127.0.0.1"};
const command_line::arg_descriptor<uint16_t>    arg_i2p_socks_port     = {"i2p-socks-port", "I2P SOCKS5 proxy port (i2pd default: 4447)", 4447};
const command_line::arg_descriptor<bool>        arg_p2p_use_tor        = {"p2p-use-tor", "Route P2P connections through Tor SOCKS5 proxy", false};
const command_line::arg_descriptor<std::string> arg_tor_socks_host     = {"tor-socks-host", "Tor SOCKS5 proxy host", "127.0.0.1"};
const command_line::arg_descriptor<uint16_t>    arg_tor_socks_port     = {"tor-socks-port", "Tor SOCKS5 proxy port", 9050};
const command_line::arg_descriptor<bool>        arg_p2p_restrict_privacy = {"p2p-restrict-to-privacy-net", "Disable clearnet P2P; only use I2P/Tor", false};

bool parsePeerFromString(NetworkAddress& pe, const std::string& node_addr) {
  return Common::parseIpAddressAndPort(pe.ip, pe.port, node_addr);
}

bool parsePeersAndAddToContainer(const boost::program_options::variables_map& vm,
    const command_line::arg_descriptor<std::vector<std::string>>& arg, std::vector<NetworkAddress>& container)
{
  std::vector<std::string> peers = command_line::get_arg(vm, arg);

  for(const std::string& str: peers) {
    NetworkAddress na = boost::value_initialized<NetworkAddress>();
    if (!parsePeerFromString(na, str)) {
      return false;
    }
    container.push_back(na);
  }

  return true;
}

} //namespace

void NetNodeConfig::initOptions(boost::program_options::options_description& desc) {
  command_line::add_arg(desc, arg_p2p_bind_ip);
  command_line::add_arg(desc, arg_p2p_bind_port);
  command_line::add_arg(desc, arg_p2p_external_port);
  command_line::add_arg(desc, arg_p2p_allow_local_ip);
  command_line::add_arg(desc, arg_p2p_add_peer);
  command_line::add_arg(desc, arg_p2p_add_priority_node);
  command_line::add_arg(desc, arg_p2p_add_exclusive_node);
  command_line::add_arg(desc, arg_p2p_seed_node);
  command_line::add_arg(desc, arg_p2p_hide_my_port);
  command_line::add_arg(desc, arg_p2p_use_i2p);
  command_line::add_arg(desc, arg_i2p_socks_host);
  command_line::add_arg(desc, arg_i2p_socks_port);
  command_line::add_arg(desc, arg_p2p_use_tor);
  command_line::add_arg(desc, arg_tor_socks_host);
  command_line::add_arg(desc, arg_tor_socks_port);
  command_line::add_arg(desc, arg_p2p_restrict_privacy);
}

NetNodeConfig::NetNodeConfig() {
  bindIp = "";
  bindPort = 0;
  externalPort = 0;
  allowLocalIp = false;
  hideMyPort = false;
  configFolder = Tools::getDefaultDataDirectory();
  testnet = false;
}

bool NetNodeConfig::init(const boost::program_options::variables_map& vm)
{
  if (vm.count(arg_p2p_bind_ip.name) != 0 && (!vm[arg_p2p_bind_ip.name].defaulted() || bindIp.empty())) {
    bindIp = command_line::get_arg(vm, arg_p2p_bind_ip);
  }

  if (vm.count(arg_p2p_bind_port.name) != 0 && (!vm[arg_p2p_bind_port.name].defaulted() || bindPort == 0)) {
    bindPort = command_line::get_arg(vm, arg_p2p_bind_port);
  }

  if (vm.count(arg_p2p_external_port.name) != 0 && (!vm[arg_p2p_external_port.name].defaulted() || externalPort == 0)) {
    externalPort = command_line::get_arg(vm, arg_p2p_external_port);
  }

  if (vm.count(arg_p2p_allow_local_ip.name) != 0 && (!vm[arg_p2p_allow_local_ip.name].defaulted() || !allowLocalIp)) {
    allowLocalIp = command_line::get_arg(vm, arg_p2p_allow_local_ip);
  }

  if (vm.count(command_line::arg_data_dir.name) != 0 && (!vm[command_line::arg_data_dir.name].defaulted() || configFolder == Tools::getDefaultDataDirectory())) {
    configFolder = command_line::get_arg(vm, command_line::arg_data_dir);
  }

  p2pStateFilename = CryptoNote::parameters::P2P_NET_DATA_FILENAME;

  if (command_line::has_arg(vm, arg_p2p_add_peer)) {
    std::vector<std::string> perrs = command_line::get_arg(vm, arg_p2p_add_peer);
    for(const std::string& pr_str: perrs) {
      PeerlistEntry pe = boost::value_initialized<PeerlistEntry>();
      pe.id = Crypto::rand<uint64_t>();
      if (!parsePeerFromString(pe.adr, pr_str)) {
        return false;
      }

      peers.push_back(pe);
    }
  }

  if (command_line::has_arg(vm,arg_p2p_add_exclusive_node)) {
    if (!parsePeersAndAddToContainer(vm, arg_p2p_add_exclusive_node, exclusiveNodes))
      return false;
  }

  if (command_line::has_arg(vm, arg_p2p_add_priority_node)) {
    if (!parsePeersAndAddToContainer(vm, arg_p2p_add_priority_node, priorityNodes))
      return false;
  }

  if (command_line::has_arg(vm, arg_p2p_seed_node)) {
    if (!parsePeersAndAddToContainer(vm, arg_p2p_seed_node, seedNodes))
      return false;
  }

  if (command_line::has_arg(vm, arg_p2p_hide_my_port)) {
    hideMyPort = true;
  }

  privacyNetConfig.useI2P = command_line::get_arg(vm, arg_p2p_use_i2p);
  if (privacyNetConfig.useI2P) {
    privacyNetConfig.i2pSocksHost = command_line::get_arg(vm, arg_i2p_socks_host);
    privacyNetConfig.i2pSocksPort = command_line::get_arg(vm, arg_i2p_socks_port);
  }

  privacyNetConfig.useTor = command_line::get_arg(vm, arg_p2p_use_tor);
  if (privacyNetConfig.useTor) {
    privacyNetConfig.torSocksHost = command_line::get_arg(vm, arg_tor_socks_host);
    privacyNetConfig.torSocksPort = command_line::get_arg(vm, arg_tor_socks_port);
  }

  privacyNetConfig.restrictToPrivacyNet = command_line::get_arg(vm, arg_p2p_restrict_privacy);

  return true;
}

void NetNodeConfig::setTestnet(bool isTestnet) {
  testnet = isTestnet;
}

std::string NetNodeConfig::getP2pStateFilename() const {
  if (testnet) {
    return "testnet_" + p2pStateFilename;
  }

  return p2pStateFilename;
}

bool NetNodeConfig::getTestnet() const {
  return testnet;
}

std::string NetNodeConfig::getBindIp() const {
  return bindIp;
}

uint16_t NetNodeConfig::getBindPort() const {
  return bindPort;
}

uint16_t NetNodeConfig::getExternalPort() const {
  return externalPort;
}

bool NetNodeConfig::getAllowLocalIp() const {
  return allowLocalIp;
}

std::vector<PeerlistEntry> NetNodeConfig::getPeers() const {
  return peers;
}

std::vector<NetworkAddress> NetNodeConfig::getPriorityNodes() const {
  return priorityNodes;
}

std::vector<NetworkAddress> NetNodeConfig::getExclusiveNodes() const {
  return exclusiveNodes;
}

std::vector<NetworkAddress> NetNodeConfig::getSeedNodes() const {
  return seedNodes;
}

bool NetNodeConfig::getHideMyPort() const {
  return hideMyPort;
}

std::string NetNodeConfig::getConfigFolder() const {
  return configFolder;
}

void NetNodeConfig::setP2pStateFilename(const std::string& filename) {
  p2pStateFilename = filename;
}

void NetNodeConfig::setBindIp(const std::string& ip) {
  bindIp = ip;
}

void NetNodeConfig::setBindPort(uint16_t port) {
  bindPort = port;
}

void NetNodeConfig::setExternalPort(uint16_t port) {
  externalPort = port;
}

void NetNodeConfig::setAllowLocalIp(bool allow) {
  allowLocalIp = allow;
}

void NetNodeConfig::setPeers(const std::vector<PeerlistEntry>& peerList) {
  peers = peerList;
}

void NetNodeConfig::setPriorityNodes(const std::vector<NetworkAddress>& addresses) {
  priorityNodes = addresses;
}

void NetNodeConfig::setExclusiveNodes(const std::vector<NetworkAddress>& addresses) {
  exclusiveNodes = addresses;
}

void NetNodeConfig::setSeedNodes(const std::vector<NetworkAddress>& addresses) {
  seedNodes = addresses;
}

void NetNodeConfig::setHideMyPort(bool hide) {
  hideMyPort = hide;
}

void NetNodeConfig::setConfigFolder(const std::string& folder) {
  configFolder = folder;
}

const PrivacyNetConfig& NetNodeConfig::getPrivacyNetConfig() const { return privacyNetConfig; }
bool NetNodeConfig::getUseI2P() const { return privacyNetConfig.useI2P; }
std::string NetNodeConfig::getI2PSocksHost() const { return privacyNetConfig.i2pSocksHost; }
uint16_t NetNodeConfig::getI2PSocksPort() const { return privacyNetConfig.i2pSocksPort; }
bool NetNodeConfig::getUseTor() const { return privacyNetConfig.useTor; }
std::string NetNodeConfig::getTorSocksHost() const { return privacyNetConfig.torSocksHost; }
uint16_t NetNodeConfig::getTorSocksPort() const { return privacyNetConfig.torSocksPort; }
bool NetNodeConfig::getRestrictToPrivacyNet() const { return privacyNetConfig.restrictToPrivacyNet; }

void NetNodeConfig::setUseI2P(bool use) { privacyNetConfig.useI2P = use; }
void NetNodeConfig::setI2PSocksHost(const std::string& host) { privacyNetConfig.i2pSocksHost = host; }
void NetNodeConfig::setI2PSocksPort(uint16_t port) { privacyNetConfig.i2pSocksPort = port; }
void NetNodeConfig::setUseTor(bool use) { privacyNetConfig.useTor = use; }
void NetNodeConfig::setTorSocksHost(const std::string& host) { privacyNetConfig.torSocksHost = host; }
void NetNodeConfig::setTorSocksPort(uint16_t port) { privacyNetConfig.torSocksPort = port; }
void NetNodeConfig::setRestrictToPrivacyNet(bool restrict) { privacyNetConfig.restrictToPrivacyNet = restrict; }


} //namespace nodetool
