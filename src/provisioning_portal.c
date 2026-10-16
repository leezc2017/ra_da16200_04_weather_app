#include <string.h>
#include <stdio.h>

#include "provisioning_portal.h"

static int hex_value(char c);

static const char s_portal_html[] =
    "<!doctype html><html lang='en'><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WiFi Configure</title><style>body{font:16px Arial;background:#f4f4f4;"
    "margin:0;min-height:100vh;display:grid;place-items:center}.box{background:white;"
    "padding:24px;border-radius:10px;width:min(300px,85vw);box-shadow:0 0 10px #ccc}"
    "input,button{box-sizing:border-box;width:100%;padding:10px;margin:6px 0}"
    "button{background:#5cadb8;color:white;border:0;border-radius:5px}</style>"
    "<div class='box'><h2>WiFi Configure</h2><form method='post' action='/cfg'>"
    "<input name='ssid' placeholder='SSID' required>"
    "<input type='password' name='password' placeholder='Password' minlength='8' maxlength='63' required>"
    "<input name='city' placeholder='City pinyin, blank uses IP location'>"
    "<button type='submit'>Save</button></form></div></html>";

static const char s_saved_html[] =
    "<!doctype html><html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
    "<title>WiFi Configure</title><body style='font:18px Arial;text-align:center;margin-top:20vh'>"
    "<h2>WiFi Configure</h2><p>Saved. Reconnecting to WiFi...</p></body></html>";

static const char s_pending_html[] =
    "<!doctype html><html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
    "<title>WiFi Configure</title><body style='font:18px Arial;text-align:center;margin-top:20vh'>"
    "<h2>Configuration received</h2><p>Connecting to WiFi. Please wait...</p></body></html>";

static const char s_error_html[] =
    "<!doctype html><html><meta charset='utf-8'><title>WiFi Configure</title>"
    "<body style='font:18px Arial;text-align:center'><h2>Invalid WiFi settings</h2>"
    "<p>Use an SSID and a WPA2 password with 8-63 characters.</p><a href='/'>Back</a></body></html>";

const char *provisioning_portal_html(void)
{
    return s_portal_html;
}

bool provisioning_portal_request_parse(const char *request, size_t length,
                                       char *path, size_t path_size,
                                       size_t *body_offset, size_t *content_length)
{
    size_t line_end = 0U;
    size_t header_end = 0U;
    size_t i;
    size_t path_len = 0U;
    bool post;
    if ((NULL == request) || (NULL == path) || (0U == path_size) ||
        (NULL == body_offset) || (NULL == content_length)) return false;
    while ((line_end + 1U < length) &&
           !((request[line_end] == '\r') && (request[line_end + 1U] == '\n'))) line_end++;
    if (line_end + 1U >= length) return false;
    post = ((line_end >= 5U) && (0 == memcmp(request, "POST ", 5U)));
    if (!post && !((line_end >= 4U) && (0 == memcmp(request, "GET ", 4U)))) return false;
    i = post ? 5U : 4U;
    while ((i < line_end) && (request[i] != ' ') && (path_len + 1U < path_size))
        path[path_len++] = request[i++];
    if ((i >= line_end) || (request[i] != ' ') || (0U == path_len)) return false;
    path[path_len] = '\0';
    for (i = line_end + 2U; i + 3U < length; i++)
    {
        if ((request[i] == '\r') && (request[i + 1U] == '\n') &&
            (request[i + 2U] == '\r') && (request[i + 3U] == '\n'))
        {
            header_end = i + 4U;
            break;
        }
    }
    if (0U == header_end) return false;
    *content_length = 0U;
    if (post)
    {
        for (i = line_end + 2U; i + 15U < header_end; i++)
        {
            static const char header_name[] = "Content-Length:";
            size_t h;
            for (h = 0U; (h < sizeof(header_name) - 1U) &&
                 ((request[i + h] == header_name[h]) ||
                  ((request[i + h] >= 'a') && (request[i + h] <= 'z') &&
                   (char)(request[i + h] - 'a' + 'A') == header_name[h])); h++)
            {
            }
            if (h == sizeof(header_name) - 1U)
            {
                i += 15U;
                while ((i < header_end) && ((request[i] == ' ') || (request[i] == '\t'))) i++;
                while ((i < header_end) && (request[i] >= '0') && (request[i] <= '9'))
                {
                    *content_length = (*content_length * 10U) + (size_t)(request[i] - '0');
                    i++;
                }
                break;
            }
        }
        if (0U == *content_length) return false;
    }
    *body_offset = header_end;
    return true;
}

bool provisioning_portal_parse_headers(const char *request, size_t length,
                                       char *path, size_t path_size,
                                       size_t *body_offset, size_t *content_length,
                                       size_t *header_bytes)
{
    size_t i;
    if ((NULL == request) || (NULL == header_bytes)) return false;
    for (i = 0U; i + 3U < length; i++)
    {
        if ((request[i] == '\r') && (request[i + 1U] == '\n') &&
            (request[i + 2U] == '\r') && (request[i + 3U] == '\n'))
        {
            *header_bytes = i + 4U;
            return provisioning_portal_request_parse(request, *header_bytes, path, path_size,
                                                     body_offset, content_length);
        }
    }
    return false;
}

provisioning_http_status_t provisioning_portal_request_state(const char *request,
                                                              size_t length,
                                                              char *path,
                                                              size_t path_size,
                                                              size_t *body_offset,
                                                              size_t *content_length,
                                                              size_t *request_length)
{
    size_t header_bytes = 0U;
    if ((NULL == request) || (NULL == path) || (NULL == body_offset) ||
        (NULL == content_length) || (NULL == request_length))
        return PROVISIONING_HTTP_INVALID;
    if (!provisioning_portal_parse_headers(request, length, path, path_size,
                                           body_offset, content_length, &header_bytes))
        return (length < PROVISIONING_REQUEST_MAX) ?
               PROVISIONING_HTTP_INCOMPLETE : PROVISIONING_HTTP_TOO_LARGE;
    if ((*content_length > (PROVISIONING_REQUEST_MAX - header_bytes)) ||
        (header_bytes > PROVISIONING_REQUEST_MAX))
        return PROVISIONING_HTTP_TOO_LARGE;
    *request_length = header_bytes + *content_length;
    if (length < *request_length) return PROVISIONING_HTTP_INCOMPLETE;
    return PROVISIONING_HTTP_READY;
}

size_t provisioning_portal_response(bool success, char *out, size_t capacity)
{
    const char *page = success ? s_saved_html : s_error_html;
    int n;
    size_t page_len = strlen(page);
    if ((NULL == out) || (capacity == 0U)) return 0U;
    n = snprintf(out, capacity,
                 "HTTP/1.0 %s\r\nContent-Type: text/html; charset=utf-8\r\n"
                 "Content-Length: %u\r\nConnection: close\r\n\r\n%s",
                 success ? "200 OK" : "400 Bad Request", (unsigned)page_len, page);
    if ((n < 0) || ((size_t)n >= capacity)) return 0U;
    return (size_t)n;
}

size_t provisioning_portal_pending_response(char *out, size_t capacity)
{
    int n;
    size_t page_len = strlen(s_pending_html);
    if ((NULL == out) || (capacity == 0U)) return 0U;
    n = snprintf(out, capacity,
                 "HTTP/1.0 202 Accepted\r\nContent-Type: text/html; charset=utf-8\r\n"
                 "Content-Length: %u\r\nConnection: close\r\n\r\n%s",
                 (unsigned)page_len, s_pending_html);
    if ((n < 0) || ((size_t)n >= capacity)) return 0U;
    return (size_t)n;
}

size_t provisioning_portal_get_response(const char *path, char *out, size_t capacity)
{
    const char *html;
    const char *status;
    const char *type;
    int n;
    size_t body_len;

    if ((NULL == path) || (NULL == out) || (0U == capacity)) return 0U;
    if ((0 == strcmp(path, "/")) || (0 == strcmp(path, "/index.html")) ||
        (0 == strcmp(path, "/generate_204")) ||
        (0 == strcmp(path, "/hotspot-detect.html")))
    {
        html = s_portal_html;
        status = "200 OK";
        type = "text/html; charset=utf-8";
        body_len = strlen(html);
        n = snprintf(out, capacity,
                     "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Connection: close\r\n\r\n%s",
                     status, type, (unsigned)body_len, html);
    }
    else
    {
        static const char not_found[] = "Not found";
        status = "404 Not Found";
        type = "text/plain";
        body_len = sizeof(not_found) - 1U;
        n = snprintf(out, capacity,
                     "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                     "Connection: close\r\n\r\n%s",
                     status, type, (unsigned)body_len, not_found);
    }
    if ((n < 0) || ((size_t)n >= capacity)) return 0U;
    return (size_t)n;
}

bool provisioning_portal_trdts_decode(const char *urc, size_t length,
                                      char *ip, size_t ip_capacity, uint16_t *port,
                                      char *data, size_t data_capacity, size_t *data_length)
{
    size_t field_start = 7U;
    size_t comma[4];
    size_t count = 0U;
    size_t i;
    unsigned long parsed_port = 0UL;
    unsigned long payload_size = 0UL;
    size_t ip_len;
    if ((NULL == urc) || (length < 8U) || (0 != memcmp(urc, "+TRDTS:", 7U)) ||
        (NULL == ip) || (0U == ip_capacity) || (NULL == port) ||
        (NULL == data) || (NULL == data_length)) return false;
    /* Format documented by UM-WI-003: +TRDTS:<cid>,<src_ip>,<src_port>,<len>,<data>
     * Stop at comma #4; TCP data itself may contain arbitrary commas. */
    for (i = field_start; (i < length) && (count < 4U); i++)
        if (urc[i] == ',') comma[count++] = i;
    if (count != 4U) return false;
    if ((comma[0] != field_start + 1U) || (urc[field_start] != '0')) return false;
    ip_len = comma[1] - comma[0] - 1U;
    if ((ip_len == 0U) || (ip_len >= ip_capacity)) return false;
    memcpy(ip, urc + comma[0] + 1U, ip_len);
    ip[ip_len] = '\0';
    for (i = comma[1] + 1U; i < comma[2]; i++)
    {
        if ((urc[i] < '0') || (urc[i] > '9')) return false;
        parsed_port = parsed_port * 10UL + (unsigned long)(urc[i] - '0');
    }
    for (i = comma[2] + 1U; i < comma[3]; i++)
    {
        if ((urc[i] < '0') || (urc[i] > '9')) return false;
        payload_size = payload_size * 10UL + (unsigned long)(urc[i] - '0');
    }
    if ((parsed_port == 0UL) || (parsed_port > 65535UL) ||
        (payload_size > data_capacity) || (comma[3] + 1U > length)) return false;
    *port = (uint16_t)parsed_port;
    /* Provisioning explicitly sets AT+TCPDATAMODE=1, so <data> is an ASCII
     * hex string and <length> remains the decoded byte count. Do not guess
     * text-vs-hex based on length (ASCII payloads can be ambiguous). */
    if ((length - comma[3] - 1U) != payload_size * 2UL) return false;
    if (!provisioning_portal_hex_decode(urc + comma[3] + 1U, (size_t)payload_size * 2U,
                                        data, data_capacity, data_length)) return false;
    return true;
}

bool provisioning_portal_hex_decode(const char *hex, size_t length,
                                    char *data, size_t capacity, size_t *data_length)
{
    size_t i;
    if ((NULL == hex) || (NULL == data) || (NULL == data_length) ||
        ((length & 1U) != 0U) || (length / 2U > capacity)) return false;
    for (i = 0U; i < length; i += 2U)
    {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i + 1U]);
        if ((hi < 0) || (lo < 0)) return false;
        data[i / 2U] = (char)((hi << 4) | lo);
    }
    *data_length = length / 2U;
    return true;
}

static bool decode_key(const char *src, size_t len, char *dst, size_t cap)
{
    size_t i = 0U;
    size_t o = 0U;
    while ((i < len) && (o + 1U < cap))
    {
        unsigned char c = (unsigned char)src[i++];
        if ('+' == c) c = ' ';
        else if ('%' == c)
        {
            int hi;
            int lo;
            if ((i + 1U >= len) || ((hi = hex_value(src[i])) < 0) || ((lo = hex_value(src[i + 1U])) < 0)) return false;
            c = (unsigned char)((hi << 4) | lo);
            i += 2U;
        }
        if ((c < 0x20U) || (c > 0x7EU)) return false;
        dst[o++] = (char)c;
    }
    if (i != len) return false;
    dst[o] = '\0';
    return true;
}

static int hex_value(char c)
{
    if ((c >= '0') && (c <= '9')) return c - '0';
    if ((c >= 'a') && (c <= 'f')) return c - 'a' + 10;
    if ((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
    return -1;
}

static bool decode_value(const char *src, size_t len, char *dst, size_t cap)
{
    size_t i = 0U;
    size_t o = 0U;
    while ((i < len) && (o + 1U < cap))
    {
        unsigned char c = (unsigned char)src[i++];
        if ('+' == c)
        {
            c = ' ';
        }
        else if ('%' == c)
        {
            int hi;
            int lo;
            if ((i + 1U >= len) || ((hi = hex_value(src[i])) < 0) || ((lo = hex_value(src[i + 1U])) < 0))
                return false;
            c = (unsigned char)((hi << 4) | lo);
            i += 2U;
        }
        /* Delimiters are parsed before decoding. Percent-encoded '&'/'='
         * are ordinary credential characters and must remain valid. */
        if ((c < 0x20U) || (c > 0x7EU))
            return false;
        dst[o++] = (char)c;
    }
    if (i != len) return false;
    dst[o] = '\0';
    return true;
}

bool provisioning_portal_parse(const char *body, size_t length, provisioning_config_t *out)
{
    size_t pos = 0U;
    bool got_ssid = false;
    bool got_password = false;
    bool got_city = false;
    if ((NULL == body) || (NULL == out) || (0U == length)) return false;
    memset(out, 0, sizeof(*out));
    while (pos < length)
    {
        size_t end = pos;
        size_t eq;
        while ((end < length) && ('&' != body[end])) end++;
        char key[12];
        eq = pos;
        while ((eq < end) && ('=' != body[eq])) eq++;
        if ((eq == end) || !decode_key(body + pos, eq - pos, key, sizeof(key))) return false;
        if (0 == strcmp(key, "ssid"))
        {
            if (got_ssid || !decode_value(body + eq + 1U, end - eq - 1U, out->ssid, sizeof(out->ssid))) return false;
            got_ssid = true;
        }
        else if (0 == strcmp(key, "password"))
        {
            if (got_password || !decode_value(body + eq + 1U, end - eq - 1U, out->password, sizeof(out->password))) return false;
            got_password = true;
        }
        else if (0 == strcmp(key, "city"))
        {
            if (got_city || !decode_value(body + eq + 1U, end - eq - 1U, out->city, sizeof(out->city))) return false;
            got_city = true;
        }
        else return false;
        pos = end + 1U;
    }
    if (!got_ssid || !got_password || !got_city) return false;
    if ((out->ssid[0] != '\0') &&
        ((strlen(out->password) < 8U) || (strlen(out->password) > 63U))) return false;
    /* AT+WFJAPA uses comma-delimited parameters. Quoting values containing a
     * comma/single quote is ambiguous in the host parser, so reject them. */
    if ((NULL != strchr(out->ssid, ',')) || (NULL != strchr(out->ssid, '\'')) ||
        (NULL != strchr(out->password, ',')) || (NULL != strchr(out->password, '\'')))
        return false;
    return true;
}

bool provisioning_portal_selftest(char *out, size_t out_size)
{
    static const char post[] =
        "POST /cfg HTTP/1.1\r\nHost: 192.168.1.100\r\nContent-Length: 48\r\n\r\n"
        "ssid=HomeNet&password=p%26%3D12345&city=Shanghai";
    static const char tcp_hex[] = "+TRDTS:0,192.168.1.2,54321,4,74657374";
    provisioning_config_t cfg;
    char path[32];
    char peer_ip[16];
    char payload[8];
    uint16_t peer_port = 0U;
    size_t body_offset = 0U;
    size_t content_length = 0U;
    size_t request_length = 0U;
    size_t payload_length = 0U;
    provisioning_http_status_t state;
    int n;
    if ((NULL == out) || (out_size == 0U)) return false;
    out[0] = '\0';

    state = provisioning_portal_request_state(post, sizeof(post) - 1U,
                                               path, sizeof(path), &body_offset,
                                               &content_length, &request_length);
    if ((state != PROVISIONING_HTTP_READY) || (strcmp(path, "/cfg") != 0) ||
        (body_offset + content_length != sizeof(post) - 1U) ||
        !provisioning_portal_parse(post + body_offset, content_length, &cfg) ||
        (strcmp(cfg.ssid, "HomeNet") != 0) || (strcmp(cfg.password, "p&=12345") != 0) ||
        (strcmp(cfg.city, "Shanghai") != 0))
    {
        (void)snprintf(out, out_size, "HTTP form parse/Content-Length failed");
        return false;
    }
    if (!provisioning_portal_trdts_decode(tcp_hex, sizeof(tcp_hex) - 1U,
                                          peer_ip, sizeof(peer_ip), &peer_port,
                                          payload, sizeof(payload), &payload_length) ||
        (strcmp(peer_ip, "192.168.1.2") != 0) || (peer_port != 54321U) ||
        (payload_length != 4U) || (memcmp(payload, "test", 4U) != 0))
    {
        (void)snprintf(out, out_size, "+TRDTS hex decode failed");
        return false;
    }
    n = snprintf(out, out_size, "HTTP POST=%u bytes, +TRDTS hex=%u bytes",
                 (unsigned)request_length, (unsigned)payload_length);
    return (n > 0) && ((size_t)n < out_size);
}
