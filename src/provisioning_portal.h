#ifndef PROVISIONING_PORTAL_H
#define PROVISIONING_PORTAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROVISIONING_PORTAL_SSID "leeWethWifi"
#define PROVISIONING_PORTAL_PASSWORD "12345678"
#define PROVISIONING_PORTAL_IP "192.168.1.100"
#define PROVISIONING_HTTP_RX_MAX 800U
#define PROVISIONING_TX_MAX 1400U

typedef struct
{
    char ssid[33];
    char password[64];
    char city[33];
} provisioning_config_t;

/* Decodes a URL-encoded application/x-www-form-urlencoded POST body. */
bool provisioning_portal_parse(const char *body, size_t length, provisioning_config_t *out);
const char *provisioning_portal_html(void);
bool provisioning_portal_request_parse(const char *request, size_t length,
                                       char *path, size_t path_size,
                                       size_t *body_offset, size_t *content_length);
size_t provisioning_portal_response(bool success, char *out, size_t capacity);
bool provisioning_portal_trdts_decode(const char *urc, size_t length,
                                      char *ip, size_t ip_capacity, uint16_t *port,
                                      char *data, size_t data_capacity, size_t *data_length);
bool provisioning_portal_hex_decode(const char *hex, size_t length,
                                    char *data, size_t capacity, size_t *data_length);

#endif
