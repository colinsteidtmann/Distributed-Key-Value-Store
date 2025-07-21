#include "hashring.h"
#include "nodes.h"
#include <stdexcept>
#include <format>
#include <unordered_set>

HashRing::HashRing(const std::vector<Node>& nodes) {
    size_t numNodes{nodes.size()};
    if (!numNodes)
        throw std::runtime_error("Nodes cannot be empty");
    m_nodes.reserve(numNodes);
    for (const auto& node : nodes)
        addNode(node);
};

void HashRing::addNode(const Node& node) {
    if (std::find(m_nodes.begin(), m_nodes.end(), node) != m_nodes.end())
        throw std::runtime_error("Node already exists");

    m_nodes.push_back(node);
    size_t nodeIdx{m_nodes.size()-1};
    const auto& ip = m_nodes[nodeIdx].ip;
    const auto& port = m_nodes[nodeIdx].port;
    for (size_t i{0}; i < VIRTUAL_NODES_PER_PHYSICAL_NODE; i++) {
        std::string key = std::format("{}:{}:{}", ip, port, std::to_string(i));
        m_hashRing[getHash(key)] = nodeIdx;
    }
}

void HashRing::removeNode(const Node& node) {
    size_t numNodes{getNumNodes()};
    size_t nodeIdx{0};
    while (nodeIdx < numNodes && m_nodes[nodeIdx] != node)nodeIdx++;
    if (nodeIdx == numNodes)
        throw std::runtime_error("Node not found");
    
    size_t lastNodeIdx{m_nodes.size()-1};
    std::swap(m_nodes[nodeIdx], m_nodes[lastNodeIdx]);
    m_nodes.pop_back();

    auto it = m_hashRing.begin();
    while (it != m_hashRing.end()) {
        if (it->second == nodeIdx)
            it = m_hashRing.erase(it);
        else {
            if (it->second == lastNodeIdx)
                it->second = nodeIdx;
            ++it;
        } 
    }
}

Node HashRing::getNodeForKey(const std::string& key) const {
    auto it = getNodeIteratorForKey(key);
    return m_nodes[it->second];
};

std::vector<Node> HashRing::getNodesForKey(const std::string& key) const {
    if (getNumNodes() < REPLICATION_FACTOR)
        throw std::runtime_error("Not enough nodes to do replication");
    
    std::vector<Node> result;
    result.reserve(REPLICATION_FACTOR);
    std::unordered_set<size_t> foundNodeIdxs;
    auto it = getNodeIteratorForKey(key);
    while (result.size() < REPLICATION_FACTOR) {
        if (foundNodeIdxs.find(it->second) == foundNodeIdxs.end()) {
            foundNodeIdxs.insert(it->second);
            result.push_back(m_nodes[it->second]);
        }
        ++it;
        if (it == m_hashRing.end())
            it = m_hashRing.begin();
    }

    return result;
};

size_t HashRing::getHash(const std::string& str) const {
    return std::hash<std::string>{}(str);
}

std::map<size_t, size_t>::const_iterator HashRing::getNodeIteratorForKey(const std::string& key) const {
        if (m_hashRing.empty())
        throw std::runtime_error("Hash ring should not be empty");
        auto it = m_hashRing.upper_bound(getHash(key));
        if (it == m_hashRing.end()) it = m_hashRing.begin();
        return it;
    }