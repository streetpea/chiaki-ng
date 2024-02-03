#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <chiaki/log.h>
#include <chiaki/sock.h>
#include <chiaki/random.h>

#define STUN_REPLY_TIMEOUT_SEC 1

#define STUN_HEADER_SIZE 20
#define STUN_MSG_TYPE_BINDING_REQUEST 0x0001
#define STUN_MSG_TYPE_BINDING_RESPONSE 0x0101
#define STUN_MAGIC_COOKIE 0x2112A442
#define STUN_TRANSACTION_ID_LENGTH 12
#define STUN_ATTRIB_MAPPED_ADDRESS 0x0001
#define STUN_ATTRIB_XOR_MAPPED_ADDRESS 0x0020
#define STUN_MAPPED_ADDR_FAMILY_IPV4 0x01
#define STUN_MAPPED_ADDR_FAMILY_IPV6 0x02


typedef struct stun_server_t {
    char* host;
    uint16_t port;
} StunServer;

StunServer STUN_SERVERS[] = {
    {"stun.moonlight-stream.org", 3478},
    {"stun.l.google.com", 19302},
    {"stun.l.google.com", 19305},
    {"stun1.l.google.com", 19302},
    {"stun1.l.google.com", 19305},
    {"stun2.l.google.com", 19302},
    {"stun2.l.google.com", 19305},
    {"stun3.l.google.com", 19302},
    {"stun3.l.google.com", 19305},
    {"stun4.l.google.com", 19302},
    {"stun4.l.google.com", 19305}
};

static bool stun_get_external_address_from_server(ChiakiLog *log, StunServer *server, char *address, uint16_t *port);

/**
 * Get external address and port using STUN.
 *
 * This will try multiple STUN servers, preferring the STUN server of the Moonlight
 * project, and if that fails, trying other STUN servers in random order.
 *
 * @param log Log context
 * @param[out] address Buffer to store address in
 * @param[out] port Buffer to store port in
 * @return true if successful, false otherwise
 */
static bool stun_get_external_address(ChiakiLog *log, char *address, uint16_t *port)
{
    // Try moonlight server first
    if (stun_get_external_address_from_server(log, &STUN_SERVERS[0], address, port)) {
        return true;
    }
    CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[0].host, STUN_SERVERS[0].port);

    // Shuffle order of other servers
    size_t num_servers = sizeof(STUN_SERVERS) / sizeof(StunServer);
    for (int i = num_servers - 1; i > 1; i--) {
        int j = 1 + chiaki_random_32() % (i - 1);
        StunServer temp = STUN_SERVERS[i];
        STUN_SERVERS[i] = STUN_SERVERS[j];
        STUN_SERVERS[j] = temp;
    }

    // Try other servers
    for (int i = 1; i < num_servers; i++) {
        if (stun_get_external_address_from_server(log, &STUN_SERVERS[i], address, port)) {
            return true;
        }
        CHIAKI_LOGW(log, "Failed to get external address from %s:%d, retrying with another STUN server...", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
    }

    CHIAKI_LOGE(log, "Failed to get external address from any STUN server.");
    return false;
}

static bool stun_get_external_address_from_server(ChiakiLog *log, StunServer *server, char *address, uint16_t *port)
{
    chiaki_socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to create socket, error was '%s'", strerror(errno));
        return false;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(0);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to bind socket to local address, error was '%s'", strerror(errno));
        close(sock);
        return false;
    }

    struct addrinfo* resolved;
    struct addrinfo hints;
    struct sockaddr_in *server_addr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(server->host, NULL, &hints, &resolved) != 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to resolve STUN server '%s', error was '%s'", server->host, strerror(errno));
        close(sock);
        return false;
    }

    server_addr = (struct sockaddr_in*)resolved->ai_addr;
    server_addr->sin_port = htons(server->port);

    uint8_t binding_req[STUN_HEADER_SIZE];
    memset(binding_req, 0, sizeof(binding_req));
    *(uint16_t*)(&binding_req[0]) = htons(STUN_MSG_TYPE_BINDING_REQUEST);
    *(uint16_t*)(&binding_req[2]) = htons(0);  // Length
    *(int*)(&binding_req[4]) = htonl(STUN_MAGIC_COOKIE);
    chiaki_random_bytes_crypt(&binding_req[8], STUN_TRANSACTION_ID_LENGTH);

    uint8_t* transaction_id = &binding_req[8];

    ssize_t sent = sendto(sock, binding_req, sizeof(binding_req), 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in));
    if (sent != sizeof(binding_req)) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to send STUN request, error was '%s'", strerror(errno));
        close(sock);
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = STUN_REPLY_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to set socket timeout, error was '%s'", strerror(errno));
        close(sock);
        return false;
    }

    uint8_t binding_resp[256];
    ssize_t received = recvfrom(sock, binding_resp, sizeof(binding_resp), 0, NULL, NULL);
    close(sock);
    if (received < 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Failed to receive STUN response, error was '%s'", strerror(errno));
        return false;
    }


    if (*(uint16_t*)(&binding_resp[0]) != htons(STUN_MSG_TYPE_BINDING_RESPONSE)) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid message type");
        return false;
    }

    // Verify length stored in binding_resp[2] is correct
    size_t expected_size = ntohs(*(uint16_t*)(&binding_resp[2])) + STUN_HEADER_SIZE;
    if (received != ntohs(*(uint16_t*)(&binding_resp[2])) + STUN_HEADER_SIZE) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid length: %d received, %d expected", received, expected_size);
        return false;
    }

    if (*(int*)(&binding_resp[4]) != htonl(STUN_MAGIC_COOKIE)) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid magic cookie");
        return false;
    }

    if (memcmp(&binding_resp[8], &binding_req[8], STUN_TRANSACTION_ID_LENGTH) != 0) {
        CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid transaction ID");
        return false;
    }

    uint16_t response_attrs_length = ntohs(*(uint16_t*)(&binding_resp[2]));
    uint16_t response_pos = STUN_HEADER_SIZE;
    while (response_pos < received)
    {
        uint16_t attr_type = ntohs(*(uint16_t*)(&binding_resp[response_pos]));
        uint16_t attr_length = ntohs(*(uint16_t*)(&binding_resp[response_pos + 2]));

        if (attr_type != STUN_ATTRIB_MAPPED_ADDRESS && attr_type != STUN_ATTRIB_XOR_MAPPED_ADDRESS)
        {
            response_pos += sizeof(attr_type) + sizeof(attr_length) + attr_length;
            continue;
        }

        bool xored = attr_type == STUN_ATTRIB_XOR_MAPPED_ADDRESS;
        uint8_t family = binding_resp[response_pos + 4];
        if (family == STUN_MAPPED_ADDR_FAMILY_IPV4) {
            if (xored) {
                uint16_t xored_port = *(uint16_t*)(&binding_resp[response_pos + 6]) ^ (STUN_MAGIC_COOKIE >> 16);
                *port = ntohs(xored_port);
                uint32_t xored_addr = *(uint32_t*)(&binding_resp[response_pos + 8]) ^ STUN_MAGIC_COOKIE;
                uint32_t addr = ntohl(xored_addr);
                inet_ntop(AF_INET, &addr, address, INET_ADDRSTRLEN);
            } else {
                *port = *(uint16_t*)(&binding_resp[response_pos + 6]);
                uint32_t addr = ntohl(*(uint32_t*)(&binding_resp[response_pos + 8]));
                inet_ntop(AF_INET, &addr, address, INET_ADDRSTRLEN);
            }
        } else if (family == STUN_MAPPED_ADDR_FAMILY_IPV6) {
            if (xored) {
                uint16_t xored_port = *(uint16_t*)(&binding_resp[response_pos + 6]) ^ (STUN_MAGIC_COOKIE >> 16);
                *port = ntohs(xored_port);

                // XOR address with concat(STUN_MAGIC_COOKIE, transaction_id)
                // NOTE: RFC5389 says we need to convert from network to host
                // endianness here, but this seems to be misleading, see
                // https://stackoverflow.com/a/40325004
                uint8_t xored_addr[16];
                for (int i = 0; i < 16; i++) {
                    xored_addr[i] = binding_resp[response_pos + 8 + i] ^ binding_req[4 + i];
                }
                inet_ntop(AF_INET6, &xored_addr, address, INET6_ADDRSTRLEN);
            } else {
                *port = *(uint16_t*)(&binding_resp[response_pos + 6]);
                inet_ntop(AF_INET6, &binding_resp[response_pos + 8], address, INET6_ADDRSTRLEN);
            }
        } else {
            CHIAKI_LOGE(log, "remote/stun.h: Received STUN response with invalid address family: %d", family);
            return false;
        }

        return true;
    }

    return false;
}