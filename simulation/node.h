#ifndef NODE_H
#define NODE_H

#include <stdint.h>

#include "../library/base.h"

struct numachip_s {
	ht_t ht;
};

struct config_s {
	sci_t id;
};

struct Node {
	uint64_t mmio32_base, mmio32_limit;
	uint64_t mmio64_base, mmio64_limit;

	struct numachip_s *numachip;
	struct config_s *config;
};

extern Node *local_node;
extern Node **nodes;
extern unsigned nnodes;

#endif
