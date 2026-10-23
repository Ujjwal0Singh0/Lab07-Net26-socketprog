#include <iostream>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <csignal>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

const int MAX_NODES = 26;
const int ON_PORT = 5000;
const uint8_t MSG_CONNECT = 1;
const uint8_t MSG_LINKSTATE = 2;

bool parseConfig(const char *filename, int cost[MAX_NODES][MAX_NODES], int &n) {
    ifstream fin(filename);
    if (!fin.is_open()) {
        return false;
    }

    vector<vector<int>> rows;
    string line;
    while (getline(fin, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == string::npos || line[first] == '#') {
            continue;
        }

        istringstream iss(line);
        vector<int> values;
        int value;
        while (iss >> value) {
            values.push_back(value);
        }

        if (values.empty()) {
            continue;
        }
        rows.push_back(values);
    }

    if (rows.empty()) {
        return false;
    }

    int computed_n = static_cast<int>(rows.size()) + 1;
    if (computed_n > MAX_NODES) {
        return false;
    }

    for (int i = 0; i < MAX_NODES; ++i) {
        for (int j = 0; j < MAX_NODES; ++j) {
            cost[i][j] = -1;
        }
        cost[i][i] = 0;
    }

    for (int i = 0; i < computed_n - 1; ++i) {
        int expected = computed_n - i - 1;
        if (static_cast<int>(rows[i].size()) != expected) {
            return false;
        }
        for (int offset = 0; offset < expected; ++offset) {
            int j = i + offset + 1;
            int value = rows[i][offset];
            cost[i][j] = value;
            cost[j][i] = value;
        }
    }

    n = computed_n;
    return true;
}

struct VNInfo {
    int fd;
    in_addr ip;
    uint16_t port;
};

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

bool send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t r = send(fd, p, len, MSG_NOSIGNAL);
        if (r <= 0) return false;
        p += r;
        len -= r;
    }
    return true;
}

bool handleConnect(int fd, VNInfo &info) {
    unsigned char hdr[3];
    if (!recv_all(fd, hdr, 3)) {
        return false;
    }
    uint8_t type = hdr[0];
    uint16_t len;
    memcpy(&len, hdr + 1, 2);
    len = ntohs(len);

    unsigned char body[6];
    if (type != MSG_CONNECT || len != 6 || !recv_all(fd, body, 6)) {
        return false;
    }

    info.fd = fd;
    memcpy(&info.ip, body, 4);
    uint16_t p;
    memcpy(&p, body + 4, 2);
    info.port = ntohs(p);
    return true;
}

bool sendLinkState(int i, int n, int cost[MAX_NODES][MAX_NODES], VNInfo vn[]) {
    if (vn[i].fd < 0) return false;

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

    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    int cost[MAX_NODES][MAX_NODES];
    for (int i = 0; i < MAX_NODES; ++i) {
        for (int j = 0; j < MAX_NODES; ++j) {
            cost[i][j] = -1;
        }
        cost[i][i] = 0;
    }

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
#ifdef SO_REUSEPORT
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

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
    for (int i = 0; i < MAX_NODES; ++i) {
        vn[i].fd = -1;
    }

    int total_registered = 0;
    while (total_registered < n) {
        int fd = accept(server_fd, nullptr, nullptr);
        if (fd < 0) {
            perror("accept");
            close(server_fd);
            return 1;
        }

        if (!handleConnect(fd, vn[total_registered])) {
            cerr << "Connection " << total_registered << " sent bad CONNECT message\n";
            close(fd);
            continue;
        }

        printf("Registered %c = %s:%d\n", 'A' + total_registered,
               inet_ntoa(vn[total_registered].ip), vn[total_registered].port);
        total_registered++;
    }

    cout << "All " << n << " virtual nodes registered.\n";

    for (int i = 0; i < n; ++i) {
        if (!sendLinkState(i, n, cost, vn)) {
            cerr << "Failed to send LINK-STATE to " << (char)('A' + i) << "\n";
        } else {
            cout << "Sent LINK-STATE to " << (char)('A' + i) << "\n";
        }
    }

    // Monitoring loop for extra connections and config file changes
    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for (int i = 0; i < total_registered; ++i) {
            if (vn[i].fd >= 0) {
                FD_SET(vn[i].fd, &read_fds);
                if (vn[i].fd > max_fd) {
                    max_fd = vn[i].fd;
                }
            }
        }

        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Handle new connections (extra nodes beyond configured n)
        if (FD_ISSET(server_fd, &read_fds)) {
            int fd = accept(server_fd, nullptr, nullptr);
            if (fd >= 0) {
                if (total_registered >= MAX_NODES) {
                    cerr << "Max virtual nodes (" << MAX_NODES << ") reached. Rejecting extra connection.\n";
                    close(fd);
                } else {
                    VNInfo extra_vn{};
                    if (!handleConnect(fd, extra_vn)) {
                        cerr << "Bad CONNECT message on extra connection\n";
                        close(fd);
                    } else {
                        int idx = total_registered;
                        vn[idx] = extra_vn;
                        // Extra nodes are disconnected islands: cost[idx][j] = -1, cost[idx][idx] = 0
                        cost[idx][idx] = 0;
                        for (int j = 0; j < MAX_NODES; ++j) {
                            if (j != idx) cost[idx][j] = cost[j][idx] = -1;
                        }
                        printf("Registered extra node %c (disconnected island) = %s:%d\n",
                               'A' + idx, inet_ntoa(vn[idx].ip), vn[idx].port);
                        total_registered++;

                        // Send isolated LINK-STATE to the newly connected island node
                        if (!sendLinkState(idx, total_registered, cost, vn)) {
                            cerr << "Failed to send LINK-STATE to extra node " << (char)('A' + idx) << "\n";
                        } else {
                            cout << "Sent LINK-STATE to extra node " << (char)('A' + idx) << "\n";
                        }
                    }
                }
            }
        }

        // Handle disconnects / unexpected data on existing connections
        for (int i = 0; i < total_registered; ++i) {
            if (vn[i].fd >= 0 && FD_ISSET(vn[i].fd, &read_fds)) {
                char dummy[64];
                ssize_t r = recv(vn[i].fd, dummy, sizeof(dummy), 0);
                if (r <= 0) {
                    cout << "Virtual node " << (char)('A' + i) << " disconnected.\n";
                    close(vn[i].fd);
                    vn[i].fd = -1;
                }
            }
        }

        // Monitor config file for changes
        int new_cost[MAX_NODES][MAX_NODES];
        int new_n = 0;
        if (parseConfig(argv[1], new_cost, new_n) && new_n == n) {
            bool changed = false;
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    if (cost[i][j] != new_cost[i][j]) {
                        changed = true;
                        break;
                    }
                }
                if (changed) break;
            }

            if (changed) {
                cout << "\n[Config Change Detected] Link costs changed:\n";
                for (int i = 0; i < n; ++i) {
                    for (int j = 0; j < n; ++j) {
                        cost[i][j] = new_cost[i][j];
                        if (j > 0) cout << ' ';
                        cout << cost[i][j];
                    }
                    cout << '\n';
                }

                // Resend LINK-STATE to all connected nodes
                for (int i = 0; i < total_registered; ++i) {
                    if (vn[i].fd >= 0) {
                        if (!sendLinkState(i, total_registered, cost, vn)) {
                            cerr << "Failed to resend LINK-STATE to " << (char)('A' + i) << "\n";
                        } else {
                            cout << "Resent LINK-STATE to " << (char)('A' + i) << "\n";
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < total_registered; ++i) {
        if (vn[i].fd >= 0) close(vn[i].fd);
    }
    close(server_fd);
    return 0;
}