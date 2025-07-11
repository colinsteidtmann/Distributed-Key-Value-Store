#ifndef UTILITIES_H
#define UTILITIES_H

#include "nodes.h"
#include <string>
#include <vector>
#include <iostream>
#include <cstring>
#include <format>
#include <cstdint>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

inline void cleanup(int socketFd) {
    if (close(socketFd) == -1)
        perror("Couldn't close socket");
}

inline void sendMessage(int socketFd, const std::string& message) {
    uint32_t messageSize = htonl(static_cast<uint32_t>(message.size()));
    ssize_t sent = send(socketFd, &messageSize, sizeof(messageSize), 0);
    if (sent <= 0) {
        throw std::runtime_error(std::format("Failed to send message size: {}", std::string(strerror(errno))));
    }

    size_t totBytesSent = 0;
    while (totBytesSent < message.size()) {
        ssize_t bytesSent = send(socketFd, message.data() + totBytesSent, message.size() - totBytesSent, 0);
        if (bytesSent < 0) {
            throw std::runtime_error(std::format("Failed to send message body: {}", std::string(strerror(errno))));
        }
        if (bytesSent == 0) {
            throw std::runtime_error("Connection closed during send");
        }
        totBytesSent += static_cast<size_t>(bytesSent);
    }
}

inline std::string getMessage(int socketFd) {
    uint32_t len_n;
    ssize_t ret = recv(socketFd, &len_n, sizeof(len_n), MSG_WAITALL);
    if (ret != sizeof(len_n)) {
        if (ret == 0) {
            throw std::runtime_error("Connection closed while reading message length");
        } else {
            throw std::runtime_error(std::format("Failed to read message length: {}", strerror(errno)));
        }
    }

    uint32_t len = ntohl(len_n);

    std::vector<char> buffer(len);
    ret = recv(socketFd, buffer.data(), len, MSG_WAITALL);
    if (ret != static_cast<ssize_t>(len)) {
        if (ret == 0) {
            throw std::runtime_error("Connection closed while reading message body");
        } else {
            throw std::runtime_error(std::format("Failed to read message body: {}", strerror(errno)));
        }
    }

    return std::string(buffer.begin(), buffer.end());
}

inline sockaddr_in getSocketAddress(const Node& node) {
    sockaddr_in socketAddress{};
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(node.port);
    if (inet_pton(AF_INET, node.ip.c_str(), &socketAddress.sin_addr) == 0) {
        throw std::runtime_error(std::format("Failed to get socket address: {}", std::string(strerror(errno))));
    }
    return socketAddress;
}

inline int getSocketFd() {
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        throw std::runtime_error(std::format("Couldn't create client socket: {}", std::string(strerror(errno))));
    }
    return socketfd;
}

template <typename T>
inline T stringToVal(const std::string& val) {
    size_t idx{0};
    int64_t result = stoll(val, &idx);
    if (idx != val.size())
        throw std::runtime_error("Failed to convert entire value to integer");
    if ((result < 0 && (std::is_signed_v<T> || result < std::numeric_limits<T>::min())) || result > std::numeric_limits<T>::max())
        throw std::runtime_error("Value is outside of input type range");
    return static_cast<T>(result);
}


#endif // UTILITIES_H