#ifndef NODES
#define NODES
#include <string>
#include <vector>

struct Node {
    std::string ip;
    short port;
    auto operator<=>(const Node&) const = default; 
};

inline const std::vector<Node> nodes {
    {"127.0.0.1", 8081},
    {"127.0.0.1", 8082},
    {"127.0.0.1", 8083},
    {"127.0.0.1", 8084},
};
#endif // NODES_H