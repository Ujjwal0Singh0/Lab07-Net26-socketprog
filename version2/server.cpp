#include <iostream>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "parse_config.h"

using namespace std;

const int ON_PORT = 5000;
const uint8_t MSG_CONNECT = 1;
const uint8_t MSG_LINKSTATE = 2;

bool recv_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t r = recv(fd, p, len, 0);
        if (r <= 0) return false;
        p += r;
        len -= r;
    }
    return true;
}

struct VNInfo {
    int fd;
    in_addr ip;
    uint16_t port;
};

bool send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t r = send(fd, p, len, 0);
        if (r <= 0) return false;
        p += r;
        len -= r;
    }
    return true;
}

bool sendLinkState(int i, int n, int cost[MAX_NODES][MAX_NODES], VNInfo vn[]) {
    unsigned char msg[3 + MAX_NODES * 9];
    size_t off = 3;  

    for (int j = 0; j < n; ++j) {
        if (cost[i][j] < 0) continue;

        msg[off++] = 'A' + j;
        memcpy(msg + off, &vn[j].ip, 4);
        off += 4;
        uint16_t p = htons(vn[j].port);
        memcpy(msg + off, &p, 2);
        off += 2;
        uint16_t c = htons((uint16_t)cost[i][j]);
        memcpy(msg + off, &c, 2);
        off += 2;
    }

    msg[0] = MSG_LINKSTATE;
    uint16_t len = htons((uint16_t)(off - 3));
    memcpy(msg + 1, &len, 2);

    return send_all(vn[i].fd, msg, off);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    setvbuf(stdout, nullptr, _IOLBF, 0);

    int cost[MAX_NODES][MAX_NODES];
    int n = 0;
    if (!parseConfig(argv[1], cost, n)) return 1;

    cout << "Number of nodes: " << n << "\n";
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (j > 0) cout << ' ';
            cout << cost[i][j];
        }
        cout << '\n';
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ON_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    cout << "Oracle Node listening on port " << ON_PORT << "...\n";

    VNInfo vn[MAX_NODES];
    for (int i = 0; i < n; ++i) {
        int fd = accept(server_fd, nullptr, nullptr);
        if (fd < 0) {
            perror("accept");
            return 1;
        }

        unsigned char hdr[3];
        if (!recv_all(fd, hdr, 3)) {
            cerr << "Connection " << i << " closed before sending header\n";
            close(fd);
            --i;
            continue;
        }
        uint8_t type = hdr[0];
        uint16_t len;
        memcpy(&len, hdr + 1, 2);
        len = ntohs(len);

        unsigned char body[6];
        if (type != MSG_CONNECT || len != 6 || !recv_all(fd, body, 6)) {
            cerr << "Bad CONNECT message on connection " << i << "\n";
            close(fd);
            --i;
            continue;
        }

        vn[i].fd = fd;
        memcpy(&vn[i].ip, body, 4);
        uint16_t p;
        memcpy(&p, body + 4, 2);
        vn[i].port = ntohs(p);

        printf("Registered %c = %s:%d\n", 'A' + i, inet_ntoa(vn[i].ip), vn[i].port);
    }

    cout << "All " << n << " virtual nodes registered.\n";

    for (int i = 0; i < n; ++i) {
        if (!sendLinkState(i, n, cost, vn)) {
            cerr << "Failed to send LINK-STATE to " << (char)('A' + i) << "\n";
        } else {
            cout << "Sent LINK-STATE to " << (char)('A' + i) << "\n";
        }
    }

    while (true) sleep(1);

    for (int i = 0; i < n; ++i) close(vn[i].fd);
    close(server_fd);
    return 0;
}