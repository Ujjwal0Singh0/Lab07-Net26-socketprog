#ifndef PARSE_CONFIG_H
#define PARSE_CONFIG_H

const int MAX_NODES = 26;

// Reads the upper-triangular config file into a symmetric cost matrix.
// cost[i][j] = -1 means no link, cost[i][i] = 0. Sets n = number of nodes.
// Returns false (with a message on stderr) if the file is unusable.
bool parseConfig(const char *filename, int cost[MAX_NODES][MAX_NODES], int &n);

#endif