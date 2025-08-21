// Ported plain C version from https://github.com/rohanverma2007/navigator/blob/main/status-api/server.js
// Minimal footprint (~1MB) C port of the navigator tool API server.js shared in r/homelab by u/SmokerrYT
// @faustinoaq

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#define PORT 11080
#define TIMEOUT 3
#define CACHE_TTL 20
#define CACHE_MAX 100
#define BUF_SIZE 4096

struct CacheEntry {
    char url[256];
    int online;
    int code;
    int time;
    time_t ts;
};
struct CacheEntry cache[CACHE_MAX];
int cache_size = 0;

// Find cache entry
int find_cache(const char *url) {
    for (int i = 0; i < cache_size; ++i) {
        if (strcmp(cache[i].url, url) == 0) return i;
    }
    return -1;
}

// Add/update cache
void set_cache(const char *url, int online, int code, int elapsed) {
    int idx = find_cache(url);
    if (idx < 0 && cache_size < CACHE_MAX) idx = cache_size++;
    if (idx >= 0) {
        strncpy(cache[idx].url, url, 255);
        cache[idx].online = online;
        cache[idx].code = code;
        cache[idx].time = elapsed;
        cache[idx].ts = time(NULL);
    }
}

// Clean old cache
void clean_cache() {
    time_t now = time(NULL);
    for (int i = 0; i < cache_size;) {
        if (now - cache[i].ts > CACHE_TTL) {
            cache[i] = cache[--cache_size];
        } else {
            ++i;
        }
    }
}

// URL decode function
void url_decode(char *dst, const char *src) {
    char *p = dst;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *p++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *p++ = ' ';
            src++;
        } else {
            *p++ = *src++;
        }
    }
    *p = 0;
}

// Minimal HTTP check using curl
void check_http(const char *url, int *online, int *code, int *ms) {
    char cmd[512], buf[16];
    char full_url[512];
    
    // Add https:// if not present
    if (strstr(url, "http://") == url || strstr(url, "https://") == url) {
        strcpy(full_url, url);
    } else {
        snprintf(full_url, sizeof(full_url), "https://%s", url);
    }
    
    snprintf(cmd, sizeof(cmd), "curl -s -o /dev/null -w %%{http_code} --connect-timeout 2 --max-time 3 --insecure '%s'", full_url);
    clock_t start = clock();
    FILE *fp = popen(cmd, "r");
    if (!fp) { *online = 0; *code = 0; *ms = TIMEOUT * 1000; return; }
    if (fgets(buf, sizeof(buf), fp) == NULL) buf[0] = '\0';
    pclose(fp);
    *ms = (int)(1000.0 * (clock() - start) / CLOCKS_PER_SEC);
    *code = atoi(buf);
    // Consider auth prompts (401/403) as online since service is responding
    *online = (*code >= 200 && *code < 500 && *code != 404);
}

// Get content type based on file extension
const char* get_content_type(const char *path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".jpg") || strstr(path, ".jpeg")) return "image/jpeg";
    return "text/plain";
}

// Serve static file
void serve_file(int fd, const char *path) {
    int f = open(path, O_RDONLY);
    if (f < 0) {
        dprintf(fd, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found");
        return;
    }
    struct stat st;
    fstat(f, &st);
    const char *content_type = get_content_type(path);
    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nCache-Control: public, max-age=86400\r\n\r\n", content_type, st.st_size);
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(f, buf, BUF_SIZE)) > 0) {
        ssize_t w = write(fd, buf, n);
        (void)w;
    }
    close(f);
}

// Minimal JSON response
void send_json(int fd, const char *json) {
    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json);
}

// Parse URL param with decoding
void get_query(const char *req, const char *key, char *out, int outlen) {
    char *p = strstr(req, key);
    if (!p) { out[0] = 0; return; }
    p += strlen(key);
    char *e = strchr(p, '&');
    if (!e) e = strchr(p, ' ');
    int len = e ? e - p : strlen(p);
    if (len > outlen-1) len = outlen-1;
    
    char temp[256];
    strncpy(temp, p, len); 
    temp[len] = 0;
    url_decode(out, temp);
}

// Main server loop
int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return 1;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    listen(server_fd, 10);
    printf("Listening on port %d...\n", PORT);
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        char req[BUF_SIZE] = {0};
        ssize_t r = read(client_fd, req, BUF_SIZE-1);
        (void)r;
        clean_cache();
        // API routes
        if (strstr(req, "GET /api/health")) {
            char json[128];
            snprintf(json, sizeof(json), "{\"ok\":1,\"up\":%ld}", time(NULL));
            send_json(client_fd, json);
        } else if (strstr(req, "GET /api/check?url=")) {
            char url[256];
            get_query(req, "url=", url, sizeof(url));
            int idx = find_cache(url);
            int online, code, ms;
            if (idx >= 0 && time(NULL)-cache[idx].ts < CACHE_TTL) {
                online = cache[idx].online; code = cache[idx].code; ms = cache[idx].time;
            } else {
                check_http(url, &online, &code, &ms);
                set_cache(url, online, code, ms);
            }
            char json[256];
            snprintf(json, sizeof(json), "{\"url\":\"%s\",\"online\":%s,\"code\":%d,\"time\":%d}", url, online ? "true" : "false", code, ms);
            send_json(client_fd, json);
        } else if (strstr(req, "GET /api/cache")) {
            char json[BUF_SIZE];
            int n = snprintf(json, sizeof(json), "{\"size\":%d,\"ttl\":%d,\"entries\":[", cache_size, CACHE_TTL);
            for (int i = 0; i < cache_size; ++i) {
                n += snprintf(json+n, sizeof(json)-n, "{\"url\":\"%s\",\"online\":%s,\"age\":%ld}%s", cache[i].url, cache[i].online ? "true" : "false", time(NULL)-cache[i].ts, i+1<cache_size?",":"");
            }
            snprintf(json+n, sizeof(json)-n, "]}");
            send_json(client_fd, json);
        } else if (strstr(req, "DELETE /api/cache")) {
            cache_size = 0;
            send_json(client_fd, "{\"cleared\":1}");
        } else if (strstr(req, "GET / ") || strstr(req, "GET /\r")) {
            serve_file(client_fd, "index.html");
        } else if (strstr(req, "GET /services.json")) {
            serve_file(client_fd, "services.json");
        } else if (strstr(req, "GET /")) {
            // Serve static file
            char path[128] = {0};
            char *p = strstr(req, "GET /") + 4;
            char *e = strchr(p, ' ');
            int len = e ? e-p : 0;
            if (len > 0 && len < 100) {
                if (*p == '/') p++;  // Skip leading slash
                strncpy(path, p, len-1);
                path[len-1] = 0;
                serve_file(client_fd, path);
            } else {
                dprintf(client_fd, "HTTP/1.1 400 Bad Request\r\n\r\n");
            }
        } else {
            dprintf(client_fd, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found");
        }
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
