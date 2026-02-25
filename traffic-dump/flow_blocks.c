#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <rte_flow.h>

#define MAX_PATTERN_NUM		4
#define MAX_ACTION_NUM		2


/**
 * create a flow rule that sends packets with matching src and dest ip
 * to selected queue.
 *
 * @param port_id
 *   The selected port.
 * @param rx_q
 *   The selected target queue.
 * @param src_ip
 *   The src ip value to match the input packet.
 * @param src_mask
 *   The mask to apply to the src ip.
 * @param dest_ip
 *   The dest ip value to match the input packet.
 * @param dest_mask
 *   The mask to apply to the dest ip.
 * @param dest_port
 * @param src_port
 * @param[out] error
 *   Perform verbose error reporting if not NULL.
 *
 * @return
 *   A flow if the rule could be created else return NULL.
 */

/* Function responsible for creating the flow rule. 8< */
struct rte_flow *
generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
		uint32_t src_ip, uint32_t src_mask,
		uint32_t dest_ip, uint32_t dest_mask,
		uint16_t dest_port, uint16_t dest_port_mask,
		uint16_t src_port, uint16_t src_port_mask,
		bool is_rx,
		struct rte_flow_error *error)
{
	/* Declaring structs being used. 8< */
	struct rte_flow_attr attr;
	struct rte_flow_item pattern[MAX_PATTERN_NUM];
	struct rte_flow_action action[MAX_ACTION_NUM];
	struct rte_flow *flow = NULL;
	struct rte_flow_action_queue queue = { .index = rx_q };
	struct rte_flow_item_ipv4 ip_spec;
	struct rte_flow_item_ipv4 ip_mask;
	struct rte_flow_item_tcp tcp_spec;
	struct rte_flow_item_tcp tcp_mask;
	/* >8 End of declaring structs being used. */
	int res;

	memset(pattern, 0, sizeof(pattern));
	memset(action, 0, sizeof(action));

	/* Set the rule attribute, only ingress packets will be checked. 8< */
	memset(&attr, 0, sizeof(struct rte_flow_attr));
	attr.ingress = 1;
	/* >8 End of setting the rule attribute. */

	/*
	 * create the action sequence.
	 * one action only,  move packet to queue
	 */
	action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
	action[0].conf = &queue;
	// action[1].type = RTE_FLOW_ACTION_TYPE_PASSTHRU;
	// action[1].conf = NULL;
	action[1].type = RTE_FLOW_ACTION_TYPE_END;
	action[1].conf = NULL;

	/*
	 * set the first level of the pattern (ETH).
	 * since in this example we just want to get the
	 * ipv4 we set this level to allow all.
	 */

	/* Set this level to allow all. 8< */
	pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
	/* >8 End of setting the first level of the pattern. */

	/*
	 * setting the second level of the pattern (IP).
	 * in this example this is the level we care about
	 * so we set it according to the parameters.
	 */

	/* Setting the second level of the pattern. 8< */
	memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
	memset(&tcp_mask, 0, sizeof(struct rte_flow_item_ipv4));
	ip_spec.hdr.dst_addr = rte_cpu_to_be_16(dest_ip);
	ip_mask.hdr.dst_addr = dest_mask;
	ip_spec.hdr.src_addr = rte_cpu_to_be_16(src_ip);
	ip_mask.hdr.src_addr = src_mask;
	pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
	pattern[1].spec = &ip_spec;
	pattern[1].mask = &ip_mask;
	/* >8 End of setting the second level of the pattern. */

	/* Setting the third level of the pattern. 8< */
	memset(&ip_spec, 0, sizeof(struct rte_flow_item_tcp));
	memset(&ip_mask, 0, sizeof(struct rte_flow_item_tcp));
	if (is_rx) {
		tcp_spec.hdr.dst_port = rte_cpu_to_be_16(dest_port);
		tcp_mask.hdr.dst_port = dest_port_mask;

	} else {
		tcp_spec.hdr.src_port = rte_cpu_to_be_16(src_port);
		tcp_mask.hdr.src_port = src_port_mask;
	}
	pattern[2].type = RTE_FLOW_ITEM_TYPE_TCP;
	pattern[2].spec = &tcp_spec;
	pattern[2].mask = &tcp_mask;
	/* >8 End of setting the third level of the pattern. */

	/* The final level must be always type end. 8< */
	pattern[3].type = RTE_FLOW_ITEM_TYPE_END;
	/* >8 End of final level must be always type end. */

	/* Validate the rule and create it. 8< */
	res = rte_flow_validate(port_id, &attr, pattern, action, error);
	if (!res)
		flow = rte_flow_create(port_id, &attr, pattern, action, error);
	/* >8 End of validation the rule and create it. */

	return flow;
}
/* >8 End of function responsible for creating the flow rule. */

struct rte_flow *
generate_tcp_syn_flow(uint16_t port_id, uint16_t rx_q,
		uint32_t src_ip, uint32_t src_mask,
		uint32_t dest_ip, uint32_t dest_mask,
		uint16_t dest_port, uint16_t dest_port_mask,
		uint16_t src_port, uint16_t src_port_mask,
		bool is_rx,
		struct rte_flow_error *error)
{
	/* Declaring structs being used. 8< */
	struct rte_flow_attr attr;
	struct rte_flow_item pattern[MAX_PATTERN_NUM];
	struct rte_flow_action action[MAX_ACTION_NUM];
	struct rte_flow *flow = NULL;
	struct rte_flow_action_queue queue = { .index = rx_q };
	struct rte_flow_item_ipv4 ip_spec;
	struct rte_flow_item_ipv4 ip_mask;
	struct rte_flow_item_tcp tcp_spec;
	struct rte_flow_item_tcp tcp_mask;
	/* >8 End of declaring structs being used. */
	int res;

	memset(pattern, 0, sizeof(pattern));
	memset(action, 0, sizeof(action));

	/* Set the rule attribute, only ingress packets will be checked. 8< */
	memset(&attr, 0, sizeof(struct rte_flow_attr));
	attr.ingress = 1;
	/* >8 End of setting the rule attribute. */

	/*
	 * create the action sequence.
	 * one action only,  move packet to queue
	 */
	action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
	action[0].conf = &queue;
	// action[1].type = RTE_FLOW_ACTION_TYPE_PASSTHRU;
	// action[1].conf = NULL;
	action[1].type = RTE_FLOW_ACTION_TYPE_END;
	action[1].conf = NULL;

	/*
	 * set the first level of the pattern (ETH).
	 * since in this example we just want to get the
	 * ipv4 we set this level to allow all.
	 */

	/* Set this level to allow all. 8< */
	pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
	/* >8 End of setting the first level of the pattern. */

	/*
	 * setting the second level of the pattern (IP).
	 * in this example this is the level we care about
	 * so we set it according to the parameters.
	 */

	/* Setting the second level of the pattern. 8< */
	memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
	memset(&tcp_mask, 0, sizeof(struct rte_flow_item_ipv4));
	ip_spec.hdr.dst_addr = rte_cpu_to_be_16(dest_ip);
	ip_mask.hdr.dst_addr = dest_mask;
	ip_spec.hdr.src_addr = rte_cpu_to_be_16(src_ip);
	ip_mask.hdr.src_addr = src_mask;
	pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
	pattern[1].spec = &ip_spec;
	pattern[1].mask = &ip_mask;
	/* >8 End of setting the second level of the pattern. */

	/* Setting the third level of the pattern. 8< */
	memset(&ip_spec, 0, sizeof(struct rte_flow_item_tcp));
	memset(&ip_mask, 0, sizeof(struct rte_flow_item_tcp));
	if (is_rx) {
		tcp_spec.hdr.dst_port = rte_cpu_to_be_16(dest_port);
		tcp_mask.hdr.dst_port = dest_port_mask;
		tcp_spec.hdr.tcp_flags = RTE_TCP_SYN_FLAG;
		tcp_mask.hdr.tcp_flags = RTE_TCP_SYN_FLAG | RTE_TCP_ACK_FLAG | RTE_TCP_RST_FLAG | RTE_TCP_FIN_FLAG;
	} else {
		tcp_spec.hdr.src_port = rte_cpu_to_be_16(src_port);
		tcp_mask.hdr.src_port = src_port_mask;
		tcp_spec.hdr.tcp_flags = RTE_TCP_SYN_FLAG;
		tcp_mask.hdr.tcp_flags = RTE_TCP_SYN_FLAG | RTE_TCP_ACK_FLAG | RTE_TCP_RST_FLAG | RTE_TCP_FIN_FLAG;
	}
	pattern[2].type = RTE_FLOW_ITEM_TYPE_TCP;
	pattern[2].spec = &tcp_spec;
	pattern[2].mask = &tcp_mask;
	/* >8 End of setting the third level of the pattern. */

	/* The final level must be always type end. 8< */
	pattern[3].type = RTE_FLOW_ITEM_TYPE_END;
	/* >8 End of final level must be always type end. */

	/* Validate the rule and create it. 8< */
	res = rte_flow_validate(port_id, &attr, pattern, action, error);
	if (!res)
		flow = rte_flow_create(port_id, &attr, pattern, action, error);
	/* >8 End of validation the rule and create it. */

	return flow;
}
