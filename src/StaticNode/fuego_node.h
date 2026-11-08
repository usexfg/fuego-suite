/* fuego_node.h — Thin C API for embedding the Fuego node as a static library.
 *
 * Intended for iOS / mobile use cases where a separate fuegod process is not
 * allowed. Wraps the existing InProcessNode + CryptoNoteCore path.
 *
 * This is a drop-in static library surface. Mining is intentionally not
 * exposed here (disabled on iOS). The caller owns the data directory and
 * must call fuego_node_stop before process exit.
 */
#ifndef FUEGO_NODE_H
#define FUEGO_NODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FuegoNode FuegoNode;

typedef enum {
  FUEGO_NODE_OK = 0,
  FUEGO_NODE_ERR_INIT = 1,
  FUEGO_NODE_ERR_ALREADY_RUNNING = 2,
  FUEGO_NODE_ERR_NOT_RUNNING = 3,
  FUEGO_NODE_ERR_IO = 4,
  FUEGO_NODE_ERR_INVALID = 5
} FuegoNodeStatus;

typedef struct {
  const char *data_dir;       /* required: blockchain + wallet storage root */
  const char *p2p_bind;       /* optional, default "0.0.0.0" */
  uint16_t p2p_port;          /* optional, default 11898 */
  const char *rpc_bind;       /* optional, default "127.0.0.1" */
  uint16_t rpc_port;          /* optional, default 18180 (internal) */
  int light_mode;             /* 1 = prefer light/pruned sync when available */
  int allow_local_ip;         /* 1 = allow local-only peers */
} FuegoNodeConfig;

/* Create and start an in-process node. Returns NULL on failure. */
FuegoNode *fuego_node_start(const FuegoNodeConfig *cfg, FuegoNodeStatus *out_status);

/* Stop and free the node. Safe to call with NULL. */
void fuego_node_stop(FuegoNode *node);

/* 1 if the node is running and has at least one peer or is synced. */
int fuego_node_is_running(const FuegoNode *node);

/* Current blockchain height (0 if not yet synced). */
uint64_t fuego_node_height(const FuegoNode *node);

/* Peer count. */
uint32_t fuego_node_peer_count(const FuegoNode *node);

#ifdef __cplusplus
}
#endif

#endif /* FUEGO_NODE_H */
