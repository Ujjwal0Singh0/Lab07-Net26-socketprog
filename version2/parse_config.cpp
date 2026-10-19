#include "parse_config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

bool parseConfig(const char *filename, int cost[MAX_NODES][MAX_NODES], int &n) {
    ifstream fin(filename);

    for (int i = 0; i < MAX_NODES; ++i) {
        for (int j = 0; j < MAX_NODES; ++j) {
            cost[i][j] = -1;
        }
        cost[i][i] = 0;
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

    n = static_cast<int>(rows.size()) + 1;

    for (int i = 0; i < n - 1; ++i) {
        int expected = n - i - 1;
        for (int offset = 0; offset < expected; ++offset) {
            int j = i + offset + 1;
            int value = rows[i][offset];
            cost[i][j] = value;
            cost[j][i] = value;
        }
    }

    return true;
}
