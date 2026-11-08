/* fuego_node.cpp — Minimal C API implementation for the static node.
 *
 * This is scaffolding. It currently provides a no-op / status-only surface
 * so the static library can be built and linked on iOS without pulling in
 * the full node startup path yet. Wire the real InProcessNode + core
 * initialization in a follow-up once the iOS toolchain and dependencies
 * (Boost, OpenSSL) are confirmed.
 */
#include "fuego_node.h"

#include <cstdlib>
#include <cstring>

struct FuegoNode {
  FuegoNodeConfig cfg;
  int running;
  uint64_t height;
  uint32_t peers;
};

static void copy_config(FuegoNodeConfig *dst, const FuegoNodeConfig *src) {
  if (!src) return;
  *dst = *src;
  if (src->data_dir) dst->data_dir = strdup(src->data_dir);
  if (src->p2p_bind) dst->p2p_bind = strdup(src->p2p_bind);
  if (src->rpc_bind) dst->rpc_bind = strdup(src->rpc_bind);
}

extern "C" {

FuegoNode *fuego_node_start(const FuegoNodeConfig *cfg, FuegoNodeStatus *out_status) {
  if (!cfg || !cfg->data_dir) {
    if (out_status) *out_status = FUEGO_NODE_ERR_INVALID;
    return nullptr;
  }

  FuegoNode *node = static_cast<FuegoNode *>(std::calloc(1, sizeof(FuegoNode)));
  if (!node) {
    if (out_status) *out_status = FUEGO_NODE_ERR_INIT;
    return nullptr;
  }

  copy_config(&node->cfg, cfg);
  node->running = 1;
  node->height = 0;
  node->peers = 0;

  if (out_status) *out_status = FUEGO_NODE_OK;
  return node;
}

void fuego_node_stop(FuegoNode *node) {
  if (!node) return;
  if (node->cfg.data_dir) std::free(const_cast<char *>(node->cfg.data_dir));
  if (node->cfg.p2p_bind) std::free(const_cast<char *>(node->cfg.p2p_bind));
  if (node->cfg.rpc_bind) std::free(const_cast<char *>(node->cfg.rpc_bind));
  std::free(node);
}

int fuego_node_is_running(const FuegoNode *node) {
  return node && node->running;
}

uint64_t fuego_node_height(const FuegoNode *node) {
  return node ? node->height : 0;
}

uint32_t fuego_node_peer_count(const FuegoNode *node) {
  return node ? node->peers : 0;
}

} /* extern "C" */
