#include "utilities.h"
#include "nodes.h"
#include "hashring.h"
#include "threadpool.h"
#include <type_traits>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <future>
#include <format>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "./protobufs/generated/dkvs.pb.h"

static const int MAX_SERVER_CONNECTION_QUEUE = 100;
class Server {
    struct StoreObject {
        std::string value;
        uint64_t timestamp;
    };
    HashRing m_hashRing;
    ThreadPool m_threadPool;
    std::unordered_map<std::string, StoreObject> m_store;
    std::mutex m_storeAndTimestampMtx;
    uint64_t m_timestamp{1};
    int m_serverSocketfd;
    short m_serverPort;

    dkvs::GetResponse get(const dkvs::GetRequest& request) {
        const std::string& key = request.key();
        std::unique_lock<std::mutex> lock(m_storeAndTimestampMtx);
        dkvs::GetResponse response;
        if(!m_store.count(key)) response.set_found(false);
        else {
            response.set_found(true);
            response.set_value(m_store[key].value);
            response.set_timestamp(m_store[key].timestamp);
        }
        std::cout<<std::format("server is responding with {}", response.DebugString())<<std::endl;
        return response;
    }

    dkvs::PutResponse tryReplicatePut(const dkvs::PutRequest& request) {
        std::vector<Node> replicas = m_hashRing.getNodesForKey(request.key());
        // first replica is the primary node that is responible for replicating
        if (replicas[0].port != m_serverPort) {
            dkvs::PutResponse response;
            response.set_success(true);
            return response;
        }
        size_t timeout{30};
        size_t numServersReplicateTo = replicas.size();
        size_t thresholdForCompletion = m_hashRing.getNumNodes()/2 + 1;

        std::vector<std::future<void>> futureVals;
        futureVals.reserve(numServersReplicateTo-1);

        dkvs::ClientMessage message;
        *message.mutable_put() = request;  

        std::atomic<size_t> responses{1}; // includes primary
        std::mutex mtx;
        std::condition_variable cv;
        for (size_t i = 1; i < numServersReplicateTo; i++) {
            futureVals.emplace_back(std::async(std::launch::async, [this, &message, &replicas, i, &responses, &cv](){
                Node& server = replicas[i];
                sockaddr_in socketAddress = getSocketAddress(server);
                int socketfd = getSocketFd();
                if (connect(socketfd, (const sockaddr*)&socketAddress, sizeof(struct sockaddr_in)) == -1) {
                    cleanup(socketfd);
                    throw std::runtime_error(std::format("Couldn't connect client socket to server {}:{} -- {}", server.ip, server.port, std::string(strerror(errno))));
                }        
                try {
                    sendMessage(socketfd, message.SerializeAsString());
                } catch (std::runtime_error& e) {
                    cleanup(socketfd);
                    throw std::runtime_error(std::format("Failed to send message: {}", e.what()));
                }
                dkvs::PutResponse putResponse; 
                std::string serverResponse = getMessage(socketfd);
                dkvs::ServerMessage serverMessage;
                if (!serverMessage.ParseFromString(serverResponse))
                    throw std::runtime_error("Failed to parse server message inside of replicate put");
                if (!serverMessage.has_put())
                    throw std::runtime_error("Server response does not have a put response");
                if (!serverMessage.put().success())
                    return;
                std::cout << std::format("server on port {} replicated to server on port {} successfully", m_serverPort, server.port) << std::endl;
                responses.fetch_add(1, std::memory_order_relaxed);
                cv.notify_one();
            }));
        }
        std::unique_lock<std::mutex> lock(mtx);
        bool noTimeout = cv.wait_for(lock, std::chrono::seconds(timeout), [&responses, &thresholdForCompletion](){
            return responses.load(std::memory_order::relaxed) >= thresholdForCompletion;
        });

        std::cout << std::format("Replicated to {} nodes and timeout was {}", responses.load(std::memory_order_relaxed), timeout?"true":"false") << std::endl;
        dkvs::PutResponse response;
        response.set_success(noTimeout);
        return response;
    }

    dkvs::PutResponse put(const dkvs::PutRequest& request) {
        dkvs::PutRequest forwardedRequest(request);
        std::unique_lock<std::mutex> lock(m_storeAndTimestampMtx);
        if (request.has_timestamp())m_timestamp = std::max(m_timestamp, request.timestamp());
        else m_timestamp++;
        forwardedRequest.set_timestamp(m_timestamp);
        m_store[request.key()] = StoreObject{.value{request.value()}, .timestamp{m_timestamp}};
        lock.unlock();
        dkvs::PutResponse response = tryReplicatePut(forwardedRequest);
        return response;
    }

    void handleConnection(int connectedfd) {
        std::string message;
        try {
            message = getMessage(connectedfd);
        } catch (std::runtime_error& e) {
            std::cerr << std::format("Failed to get message: {}", e.what()) << std::endl;
            cleanup(connectedfd);
            return;
        }
        
        dkvs::ServerMessage serverMessage;
        serverMessage.set_status(dkvs::Status::OK);
        dkvs::ClientMessage clientMessage;
        if (!clientMessage.ParseFromString(message)) {
            serverMessage.set_status(dkvs::Status::INVALID);
            serverMessage.set_error_message(std::format("Failed to parse message as ClientMessage {}", message));
        } else {
            std::cout << std::format("Recieved \"{}\" from client", clientMessage.DebugString()) << std::endl;
            if (clientMessage.has_get())
                *serverMessage.mutable_get() = get(clientMessage.get());
            else if (clientMessage.has_put())
                *serverMessage.mutable_put() = put(clientMessage.put());
            else {
                serverMessage.set_status(dkvs::Status::INVALID);
                serverMessage.set_error_message(std::format("Message does not have a get or put request {}",message));
            }
        }

        try {
            sendMessage(connectedfd, serverMessage.SerializeAsString());
        } catch (std::runtime_error& e) {
            std::cerr << std::format("Failed to send response: {}", e.what()) << std::endl;
            cleanup(connectedfd);
            return;
        }

        cleanup(connectedfd);
    }

public:
    Server(short port) : 
    m_serverPort{port},
    m_serverSocketfd{socket(AF_INET, SOCK_STREAM, 0)},
    m_hashRing{nodes}
    {
        if (m_serverSocketfd == -1) {
            throw std::runtime_error(std::format("Couldn't create server socket: {}", std::string(strerror(errno))));
        }

        int opt = 1;
        if (setsockopt(m_serverSocketfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
            cleanup(m_serverSocketfd);
            throw std::runtime_error(std::format("setsockopt failed: {}", std::string(strerror(errno))));
        }

        sockaddr_in socketAddress{};
        socketAddress.sin_family = AF_INET;
        socketAddress.sin_port = htons(port);
        socketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(m_serverSocketfd, (const sockaddr*)&socketAddress, sizeof(struct sockaddr_in)) == -1) {
            cleanup(m_serverSocketfd);
            throw std::runtime_error(std::format("Couldn't bind server socket: {}", std::string(strerror(errno))));
        }
        
        if (listen(m_serverSocketfd, MAX_SERVER_CONNECTION_QUEUE) == -1) {
            cleanup(m_serverSocketfd);
            throw std::runtime_error(std::format("Couldn't listen server socket: {}", std::string(strerror(errno))));
        }

        std::cout << std::format("Server is listening on port {}", port) << std::endl;
    }

    ~Server() {
        m_threadPool.stop();
        if (m_serverSocketfd != -1) cleanup(m_serverSocketfd);
    }

    void start() {
        sockaddr_in connectedAddress{};
        socklen_t connectedAddressLength;
        int connectedfd;
        while ((connectedfd = accept(m_serverSocketfd, (sockaddr*)&connectedAddress, &connectedAddressLength)) != -1) {
            char clientIp[1024];
            inet_ntop(AF_INET, (const void*)&connectedAddress.sin_addr, clientIp, sizeof(clientIp));
            std::cout << "Accepted connection from " 
                    << clientIp
                    << ":" << ntohs(connectedAddress.sin_port) 
                    << std::endl;
            m_threadPool.addTask([this, connectedfd] { 
                handleConnection(connectedfd);
             });
        }
    }

    void stop() {
        m_threadPool.stop();
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2)
            throw std::invalid_argument("You must pass in one argument for the port the server runs on");
        
        short port = std::stoi(argv[1]);
        Server server{port};
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}