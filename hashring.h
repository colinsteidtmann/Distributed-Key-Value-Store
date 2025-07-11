#ifndef HASHRING_H
#define HASHRING_H
#include <cstdint>
#include <map>
#include <vector>
#include <string>
struct Node;


inline const uint32_t REPLICATION_FACTOR = 3;
inline const uint32_t VIRTUAL_NODES_PER_PHYSICAL_NODE = 100; 

class HashRing {
    std::vector<Node> m_nodes;
    std::map<size_t, size_t> m_hashRing; // maps hash values to server node indexes

    size_t getHash(const std::string& str) const;

    std::map<size_t, size_t>::const_iterator getNodeIteratorForKey(const std::string& key) const;

public:
    HashRing(const std::vector<Node>& nodes);

    Node getNodeForKey(const std::string& key) const;
    std::vector<Node> getNodesForKey(const std::string& key) const;
    size_t getNumNodes() const {return m_nodes.size();};
    void addNode(const Node& node);
    void removeNode(const Node& node);
};
#endif // HASHRING_H