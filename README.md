# **Distributed Key-Value Store (DKVS) - Human written code**

## IMPORTANT NOTE
The project and code was written by me. The purpose of this project was to learn, so I had no incentive to let AI do the coding. When it tried, I told it to stop. I started every conversation with something like, "I would like you to be my teacher and a guide for my distributed key-value store project. Please don't write code for me. Challenge me to think." I then used AI as a support tool - for example, asking things like "Explain Lamport timestamps," "What are different ways to ensure consistency across nodes?" or "What else can I add to my current implementation?" For C++ questions, I referred to cppreference.com first.

## **Introduction**

The Distributed Key-Value Store (DKVS) is an in-memory storage system designed for high availability, scalability, and consistency across a cluster of independent server nodes. Built from scratch in C++, this project explores fundamental concepts in distributed systems, including custom networking protocols, concurrent programming, advanced data sharding, and robust replication strategies.

Traditional centralized data stores face limitations in scalability and fault tolerance. DKVS addresses these challenges by distributing data and processing across multiple machines, ensuring that the system remains operational and responsive even if individual nodes fail.

## **Features**

* **Core Key-Value Operations:** Supports PUT (store) and GET (retrieve) operations for string key-value pairs.  
* **Custom TCP/IP Networking:** Implements a custom length-prefixed binary protocol over raw Linux sockets (socket, send, recv, arpa/inet.h) for reliable client-server and inter-server communication.  
* **Multi-threaded Concurrency:**  
  * Server uses a custom thread pool (std::jthread, std::mutex, std::condition\_variable) to handle hundreds of concurrent client connections and asynchronous internal tasks.  
  * Has exception handling within the thread pool prevents individual task failures from crashing worker threads.  
* **Advanced Data Sharding (Consistent Hashing with Virtual Nodes):**  
  * Uses Consistent Hashing to distribute keys across physical server nodes.  
  * Has **Virtual Nodes** (VIRTUAL\_NODES\_PER\_PHYSICAL\_NODE configurable) to achieve a more uniform distribution of data and load, minimizing hotspots.  
  * Dynamically handles node additions and removals with minimal data remapping, this enables graceful scaling.  
* **Quorum-Based Replication:**  
  * Implements a **write-through replication** strategy with a configurable REPLICATION\_FACTOR (e.g., 3 copies total: primary \+ 2 replicas).  
  * Enforces **Write Quorum (W)**: A PUT operation is only considered successful after a majority (N/2 \+ 1\) of the relevant replica nodes (including the primary) acknowledge the write.  
* **Consistency Model (Last-Writer-Wins with Lamport Timestamps):**  
  * Uses **Lamport Timestamps** associated with each key-value pair to establish a causal ordering of events across the distributed system.  
  * During GET operations, clients query multiple replicas and resolve conflicts by selecting the value with the highest Lamport timestamp.  
* **Asynchronous Read Repair:**  
  * After a GET operation, if stale data is detected on any queried replica (i.e., its timestamp is older than the chosen latest version), an asynchronous "read repair" PUT operation is initiated to update that replica to the latest state.  
* **Protocol Buffers (Protobuf):**  
  * All client-server and inter-server messages are serialized and deserialized using Google Protocol Buffers. This is an efficient way to send structured data and makes serializing and deserializing messages over the wire easier.
* **Robust Error Handling:** Comprehensive error handling for network operations, system calls, and data parsing, ensuring system stability and informative error messages.

## **Architecture Overview**

The DKVS operates as a cluster of independent server processes, each managing a portion of the overall key-value space.

* **Client:**  
  * Uses a HashRing instance to determine the primary server for PUT requests and the full set of relevant servers (primary \+ replicas) for GET requests.  
  * Communicates with servers using Protobuf-serialized messages over TCP sockets.  
  * For GET requests, it queries multiple servers concurrently and applies quorum logic to determine the latest consistent value.  
  * Initiates asynchronous read repair for stale replicas.  
* **Server:**  
  * Listens for incoming client and replication requests.  
  * Uses a ThreadPool to handle each incoming connection concurrently.  
  * Maintains an in-memory std::unordered\_map for its portion of the data, associating each value with a Lamport timestamp.  
  * For PUT requests it's responsible for (as determined by its HashRing):  
    * Stores the data locally (updating its Lamport clock).  
    * Asynchronously forwards the PUT (with the new timestamp) to replica nodes.  
    * Waits for a write quorum of acknowledgments from replicas before responding to the client.  
  * For GET requests, it returns the value and its timestamp from its local store.  
* **HashRing:**  
  * A core component used by both clients and servers.  
  * Manages the consistent hash ring, mapping keys to physical nodes via virtual nodes.  
  * Provides methods to find the primary node for a key (getNodeForKey) and the full set of nodes responsible for a key's replication group (getNodesForKey).  
  * Supports dynamic addNode and removeNode operations to update the cluster topology.  
* **Utilities:** Provides common networking helper functions (sendMessage, getMessage, getSocketFd, cleanup).

## **Getting Started**

### **Prerequisites**

* A C++20 compatible compiler (e.g., GCC 10+ or Clang 10+)
* Gcc and G++ version 13+
* CMake (version 3.10 or higher)  
* Google Protocol Buffers compiler (protoc) and development libraries.  
* *Note* everything should be handled by the docker container setup.

### **Building the Project**

1. **Clone the repository:**  
   git clone https://github.com/your-username/Distributed-Key-Value-Store.git  
   cd Distributed-Key-Value-Store

2. **Open in VS Code Remote Container**
   I haven't experimented opening this project any other way.

2. **Create a build directory (automatically created by docker script) and configure CMake:**
   mkdir build *(if needed)*
   cd build  
   cmake ..

3. **Build the executables:**  
   make
   This will compile DKVSServer and DKVSClient executables in the build directory.

### **Running the DKVS Cluster**

The nodes.h file defines the initial physical nodes for your cluster.

1. **Ensure your startServers.sh script is executable:**  
   chmod \+x startServers.sh

2. **Start all servers using the script:**  
   ./startServers.sh

   This script will typically launch each DKVSServer instance in a separate process, as defined in your nodes.h file. You should see output indicating each server is listening on its respective port.

### **Usage**

Once all servers are running, you can use the DKVSClient to interact with the cluster.

#### **PUT Operation**

To store a key-value pair:

./build/DKVSClient PUT \<key\> \<value\>

**Example:**

./build/DKVSClient PUT mykey "Hello Distributed World"

Observe the server logs: The primary server for mykey will store it locally and then initiate replication to its replicas, waiting for the write quorum.

#### **GET Operation**

To retrieve a value by key:

./build/DKVSClient GET \<key\>

**Example:**

./build/DKVSClient GET mykey

The client will query multiple relevant servers, resolve conflicts based on Lamport timestamps, and perform read repair if necessary.

### **Testing Fault Tolerance (Manual)**

1. **Start all servers** (e.g., using ./startServers.sh).  
2. **PUT a key:** ./build/DKVSClient PUT mydata "important value"  
3. **Kill a server process:** Go to one of your server terminals (or find its PID and use kill \<PID\>).  
4. **Try GETting the key:** ./build/DKVSClient GET mydata  
   * If your read quorum (R) is met by the remaining active servers, the GET should still succeed.  
   * You might see read repair attempts if the killed server comes back online later and is queried.  
5. **Try PUTting a new key (or updating an existing one):** ./build/DKVSClient PUT newkey "new value"  
   * If your write quorum (W) requires the killed server, the PUT might fail or time out, indicating the quorum wasn't met. This demonstrates the consistency guarantee.  
6. **Restart the killed server:** Manually restart the specific server process (e.g., ./build/DKVSServer 8002).  
7. **GET the key again:** Observe if read repair happens in the logs, bringing the restarted server up-to-date.

## **Future Work**

* **Testing:** My number one priority. I should add this soon.
* **Persistent Storage:** Integrate with a disk-based storage engine (e.g., RocksDB, SQLite) to persist data beyond in-memory storage.  
* **Leader Election/Cluster Membership:** Implement a more robust mechanism for dynamic node discovery and failure detection (e.g., using a gossip protocol or integrating with Apache ZooKeeper/etcd/Consul).  
* **Advanced Consistency Models:** Explore stronger consistency models (e.g., linearizability) or tunable consistency (e.g., N, R, W parameters at client request).  
* **Anti-Entropy/Background Reconciliation:** Implement background processes to periodically compare data across replicas and resolve inconsistencies without waiting for a read repair.  
* **Command-Line Interface (CLI):** A more interactive CLI for client operations and server management.  
* **Monitoring and Metrics:** Expose metrics (e.g., request latency, error rates, data size) for better operational visibility.  
* **Authentication and Authorization:** Secure communication and access control.