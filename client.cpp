#include "threadpool.h"
#include "utilities.h"
#include "hashring.h"
#include "nodes.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <format>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "./protobufs/generated/dkvs.pb.h"



class Client {
    HashRing m_hashRing;
    ThreadPool m_threadPool;
    
    dkvs::ServerMessage getFromServer(const Node& server, const dkvs::GetRequest& getRequest) {
        sockaddr_in socketAddress = getSocketAddress(server);

        int socketfd = getSocketFd();
        if (connect(socketfd, (const sockaddr*)&socketAddress, sizeof(struct sockaddr_in)) == -1) {
            cleanup(socketfd);
            throw std::runtime_error(std::format("Couldn't connect client socket to server {}:{} -- {}", server.ip, server.port, std::string(strerror(errno))));
        }

        try {
            dkvs::ClientMessage clientMessage;
            *clientMessage.mutable_get() = getRequest;
            sendMessage(socketfd, clientMessage.SerializeAsString());
        } catch (std::runtime_error& e) {
            cleanup(socketfd);
            throw std::runtime_error(std::format("Failed to send message: {}", e.what()));
        }

        dkvs::ServerMessage serverMessage;
        try {
            std::string message = getMessage(socketfd);
            if (!serverMessage.ParseFromString(message)) {
                throw std::runtime_error(std::format("Failed to parse server message"));
            }
            std::cout << std::format("Server responded \"{}\"", serverMessage.DebugString()) << std::endl;
        } catch (std::runtime_error& e) {
            cleanup(socketfd);
            throw std::runtime_error(std::format("Failed to get reponse: {}", e.what()));
        }

        cleanup(socketfd);
        return serverMessage;
    }

    void putAtServer(const Node& server, const dkvs::PutRequest& putRequest) {
        sockaddr_in socketAddress = getSocketAddress(server);
        
        int socketfd = getSocketFd();
        if (connect(socketfd, (const sockaddr*)&socketAddress, sizeof(struct sockaddr_in)) == -1) {
            cleanup(socketfd);
            throw std::runtime_error(std::format("Couldn't connect client socket to server {}:{} -- {}", server.ip, server.port, std::string(strerror(errno))));
        }

        try {
            dkvs::ClientMessage clientMessage;
            *clientMessage.mutable_put() = putRequest;
            sendMessage(socketfd, clientMessage.SerializeAsString());
        } catch (std::runtime_error& e) {
            cleanup(socketfd);
            throw std::runtime_error(std::format("Failed to send message: {}", e.what()));
        }

        try {
            std::string response = getMessage(socketfd);
        } catch (std::runtime_error& e) {
            cleanup(socketfd);
            throw std::runtime_error(std::format("Failed to get reponse: {}", e.what()));
        }

        cleanup(socketfd);
    }

    void tryReadRepair(const std::vector<Node>& servers, const std::vector<dkvs::GetResponse>& responses, const std::string& key, const dkvs::GetResponse& chosenResponse) {
        for (size_t i = 0; i < servers.size(); i++) {
            m_threadPool.addTask([this, server = servers[i], response = responses[i], key, chosenResponse](){
                if (response.found() && response.timestamp() == chosenResponse.timestamp() && response.value() == chosenResponse.value())
                    return;
                dkvs::PutRequest putRequest;
                putRequest.set_key(key);
                putRequest.set_value(chosenResponse.value());
                putRequest.set_value(chosenResponse.value());
                putAtServer(server, putRequest);
                std::cout << std::format("Updating server at port {} with fresh data because {} {} != {} {}", server.port, response.value(), response.timestamp(), chosenResponse.value(), chosenResponse.timestamp()) << std::endl;
            });
        }
    }
public:
    Client() :
    m_hashRing{nodes} {
        signal(SIGPIPE, SIG_IGN);
    }

    ~Client() {
        m_threadPool.stop();
    }

    void put(const std::string& key, const std::string& value) {
        Node server = m_hashRing.getNodeForKey(key);
        dkvs::PutRequest putRequest;
        putRequest.set_key(key);
        putRequest.set_value(value);
        putAtServer(server, putRequest);
    }

    void get(const std::string& key) {
        std::vector<Node> serversToAsk = m_hashRing.getNodesForKey(key);
        size_t timeout{30};
        size_t numServersToAsk = serversToAsk.size();
        size_t thresholdForCompletion = m_hashRing.getNumNodes()/2 + 1;

        dkvs::GetRequest getRequest;
        getRequest.set_key(key);

        std::vector<std::future<dkvs::ServerMessage>> futureVals;
        futureVals.reserve(numServersToAsk);
        std::atomic<size_t> responses{0};
        std::mutex mtx;
        std::condition_variable cv;
        for (size_t i = 0; i < numServersToAsk; i++) {
            futureVals.push_back(std::async(std::launch::async, [this, &getRequest, &serversToAsk, i, &responses, &cv](){
                dkvs::ServerMessage obj = getFromServer(serversToAsk[i], getRequest);
                responses.fetch_add(1, std::memory_order_relaxed);
                cv.notify_one();
                return obj;
            }));
        }
        std::unique_lock<std::mutex> lock(mtx);
        bool noTimeout = cv.wait_for(lock, std::chrono::seconds(timeout), [&responses, &thresholdForCompletion](){
            return responses.load(std::memory_order::relaxed) >= thresholdForCompletion;
        });

        if (noTimeout){
            std::vector<dkvs::GetResponse> serverResponses;
            serverResponses.reserve(numServersToAsk);

            dkvs::GetResponse chosenResult;
            for(auto& future : futureVals) {
                dkvs::ServerMessage message = future.valid() ? future.get() : dkvs::ServerMessage{};
                dkvs::GetResponse response;
                response.set_found(false);
                if (message.has_get() && message.status() == dkvs::Status::OK)
                    response = message.get();
                serverResponses.push_back(response);
                if (response.found() && response.timestamp() > chosenResult.timestamp()) chosenResult = response;
            }
            if (chosenResult.found())
                tryReadRepair(serversToAsk, serverResponses, key, chosenResult);
            std::cout << std::format("Got {} from server", chosenResult.DebugString()) << std::endl;
        }else
            std::cerr << std::format("Failed to get {} responses with the {} seconds, only got {}", thresholdForCompletion, timeout, responses.load(std::memory_order_acquire)) << std::endl;

    }
};

int main(int argc, char* args[]) {
    try {
        Client client{};
        if (argc == 4 && std::string(args[1]) == "PUT") {
            std::string key{args[2]};
            std::string message{args[3]};
            client.put(key, message);
        } else if (argc == 3 && std::string(args[1]) == "GET") {
            std::string key{args[2]};
            client.get(key);
        } else {
            throw std::invalid_argument("Usage: ./program PUT key message or GET key");
        }
    } catch (std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}