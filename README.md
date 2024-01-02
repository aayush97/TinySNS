# Tiny Social Network System (TinySNS)

## Overview
The TinySNS project is an assignment from CSCE 662: Distributed Processing System, focused on building a scalable, fault-tolerant social networking service. This project extends the SNS service built in MP2.1 by incorporating high availability and fault tolerance features.

### Key Features
- **Fault Tolerance and High Availability:** The system is designed to handle failures transparently, ensuring continued service availability.
- **Master-Slave Architecture:** Each cluster has a Master-Slave pair for redundancy and fault tolerance.
- **Follower Synchronization:** Dedicated processes synchronize follower information across clusters.
- **Centralized Coordination:** A coordinator manages load balancing and server synchronization.
- **Resilience Testing:** The system includes specific test cases to check its resilience against failures.

## System Architecture
The architecture of TinySNS consists of several key components:
- **Coordinator:** Manages load balancing and server process synchronization.
- **Servers:** Handle core SNS functionalities like FOLLOW, UNFOLLOW, TIMELINE, and LIST.
- **Clients:** Users interact with the SNS through these client processes.
- **Follower Synchronizers:** Ensure consistent view of user data across clusters.

## Limitations
Currently, the system has certain limitations:
- UNFOLLOW command works only within the same cluster.
- TIMELINE mode requires manual updates for cross-cluster user posts. An input of `UPDATE` in timeline mode refreshes the timeline with all available posts from clients followed by the user.

## Testing
The document provides detailed test cases to validate the functionalities and resilience of the system. These include sanity checks within clusters, cross-cluster functionality checks, and system resilience tests.

## Getting Started
Instructions to run the coordinator, servers, synchronizers, and clients are detailed in the design document. Please refer to the test cases section for specific commands and procedures.

---
### Usage
You might need to change the location of gRPC in the Makefile. Then running a simple make builds all the necessary executables and object files.
```
make
```
Then to run the coordinator, servers, and synchronizer processes, execute the following commands.
```
# Run the coordinator
./coordinator -p 9090

# Run the servers
./tsd -h localhost -p 3010 -s 1 -k 9090 # cluster 1
./tsd -h localhost -p 3011 -s 4 -k 9090

./tsd -h localhost -p 3012 -s 2 -k 9090 # cluster 2
./tsd -h localhost -p 3013 -s 5 -k 9090

./tsd -h localhost -p 3014 -s 3 -k 9090 # cluster 3
./tsd -h localhost -p 3015 -s 6 -k 9090

# Run the synchronizers
./synchronizer -n 2 -p 3029
./synchronizer -n 3 -p 3030
./synchronizer -n 1 -p 3031

# Run the clients
./tsc -u 1 -k 9090
./tsc -u 2 -k 9090
./tsc -u 3 -k 9090
./tsc -u 5 -k 9090
```
