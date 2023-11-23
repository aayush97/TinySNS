#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <glog/logging.h>

#define log(severity, msg) \
  LOG(severity) << msg;    \
  google::FlushLogFiles(google::severity);

#include "coordinator.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::CoordService;
using csce438::ServerInfo;
using csce438::Confirmation;
using csce438::ID;
// using csce438::ServerList;
// using csce438::SynchService;

std::time_t getTimeNow();

struct zNode
{
  int serverID;
  std::string hostname;
  std::string port;
  std::string type;
  std::time_t last_heartbeat;
  bool missed_heartbeat;
  bool isActive();
};

class Cluster{
  private:
    zNode master;
    zNode worker;
  public:
    Cluster(){
      master = {
        .serverID = -1,
        .hostname = "",
        .port = "",
        .type = "master",
        .last_heartbeat = getTimeNow(),
        .missed_heartbeat = false,
      };
      worker = {
        .serverID = -1,
        .hostname = "",
        .port = "",
        .type = "slave",
        .last_heartbeat = getTimeNow(),
        .missed_heartbeat = false,
      };
    }
    zNode& getMaster();
    zNode& getWorker();
    void receiveHeartBeat(int server_id);
    void addServer(int server_id, std::string hostname, std::string port);
    void removeServer(int server_id);
    void switchRoles();
    bool findServer(int server_id);
    zNode& getServer(int server_id);
};

zNode& Cluster::getMaster(){
  return master;
}

zNode& Cluster::getWorker(){
  return worker;
}

void Cluster::receiveHeartBeat(int server_id){
  if (master.serverID == server_id){
    master.last_heartbeat = getTimeNow();
    master.missed_heartbeat = false;
  }else if (worker.serverID == server_id){
    worker.last_heartbeat = getTimeNow();
    worker.missed_heartbeat = false;
  }
}

void Cluster::addServer(int server_id, std::string hostname, std::string port){
  if (master.serverID == -1){
    master.serverID = server_id;
    master.hostname = hostname;
    master.port = port;
    master.last_heartbeat = getTimeNow();
    master.missed_heartbeat = false;
  }else if (worker.serverID == -1){
    worker.serverID = server_id;
    worker.hostname = hostname;
    worker.port = port;
    worker.last_heartbeat = getTimeNow();
    worker.missed_heartbeat = false;
  }else{
    std::cout << "Cluster is full" << std::endl;
  }
}

void Cluster::removeServer(int server_id){
  if (master.serverID == server_id){
    master.serverID = -1;
    master.hostname = "";
    master.port = "";
    master.last_heartbeat = getTimeNow();
    master.missed_heartbeat = false;
  }else if (worker.serverID == server_id){
    worker.serverID = -1;
    worker.hostname = "";
    worker.port = "";
    worker.last_heartbeat = getTimeNow();
    worker.missed_heartbeat = false;
  }else{
    std::cout << "Server not found" << std::endl;
  }
}

void Cluster::switchRoles(){
  if (master.serverID != -1 && worker.serverID != -1){
    std::string temp_hostname = master.hostname;
    std::string temp_port = master.port;
    master.hostname = worker.hostname;
    master.port = worker.port;
    worker.hostname = temp_hostname;
    worker.port = temp_port;
  }else{
    std::cout << "Cluster is not full" << std::endl;
  }
}

bool Cluster::findServer(int server_id){
  if (master.serverID == server_id || worker.serverID == server_id){
    return true;
  }else{
    return false;
  }
}

zNode& Cluster::getServer(int server_id){
  if (master.serverID == server_id){
    return master;
  }else if (worker.serverID == server_id){
    return worker;
  }else{
    std::cout << "Server not found" << std::endl;
  }
}

//potentially thread safe
std::mutex v_mutex;
Cluster cluster1;
Cluster cluster2;
Cluster cluster3;

// use a map to store clusters
std::map<int, Cluster*> clusters;

//func declarations
int findServer(std::vector<zNode> v, int id);
std::time_t getTimeNow();
void checkHeartbeat();


bool zNode::isActive(){
    bool status = false;
    if(!missed_heartbeat){
        status = true;
    }else if(difftime(getTimeNow(),last_heartbeat) < 10){
        status = true;
    }
    if(!status){
      type="slave";
    }
    return status;
}

class CoordServiceImpl final : public CoordService::Service {
  static const std::string MASTER;

  Status Heartbeat(ServerContext* context, const ServerInfo* serverinfo, Confirmation* confirmation) override {
    std::cout<<"Got Heartbeat! "<<serverinfo->type()<<"("<<serverinfo->serverid()<<")"<<std::endl;
    log(INFO, "Got Heartbeat! " + serverinfo->type() + "(" + std::to_string(serverinfo->serverid()) + ")") int server_id = serverinfo->serverid();
    int cluster_id = (server_id % 3) + 1;
    Cluster* cluster = clusters[cluster_id];
    if (cluster->findServer(server_id)){
      cluster->receiveHeartBeat(server_id);
    }else{
      cluster->addServer(server_id, serverinfo->hostname(), serverinfo->port());
    }
    zNode& cur_server = cluster->getServer(server_id);
    zNode& master = cluster->getMaster();
    zNode& worker = cluster->getWorker();
    if (!master.isActive() && worker.isActive()){
      cluster->switchRoles();
    }
    confirmation->set_status(true);
    confirmation->set_designation(cur_server.type);
    confirmation->set_worker_hostname(worker.hostname);
    confirmation->set_worker_port(worker.port);
    return Status::OK;
  }
  
  //function returns the server information for requested client id
  //this function assumes there are always 3 clusters and has math
  //hardcoded to represent this.
  Status GetServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
    std::cout<<"Got GetServer for clientID: "<<id->id()<<std::endl;
    log(INFO, "Got GetServer for clientID: " + std::to_string(id->id()));

    int cluster_id = (id->id() % 3) + 1;
    zNode& server_node = clusters[cluster_id]->getMaster();
    if (server_node.isActive()){
      serverinfo->set_serverid(server_node.serverID);
      serverinfo->set_hostname(server_node.hostname);
      serverinfo->set_port(server_node.port);
      serverinfo->set_type(server_node.type);
      // confirmation->set_status(true)
    }else{
      // confirmation->set_status(false);
      serverinfo->set_hostname("not available");
      std::cout << "No server available in cluster: " << cluster_id << std::endl;
      log(INFO, "No server available in cluster " + std::to_string(cluster_id));
    }
    return Status::OK;
  }
  

};
const std::string CoordServiceImpl::MASTER = "master";
void RunServer(std::string port_no){
  //start thread to check heartbeats
  std::thread hb(checkHeartbeat);
  //localhost = 127.0.0.1
  std::string server_address("127.0.0.1:"+port_no);
  CoordServiceImpl service;
  //grpc::EnableDefaultHealthCheckService(true);
  //grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  
  std::string port = "9000";
  int opt = 0;
  clusters[1] = &cluster1;
  clusters[2] = &cluster2;
  clusters[3] = &cluster3;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
          break;
      default:
	std::cerr << "Invalid Command Line Argument\n";
    }
  }
  std::string log_file_name = std::string("coordinator-") + port;
  google::InitGoogleLogging(log_file_name.c_str());
  log(INFO, "Logging Initialized. Coordinator starting...");
  RunServer(port);
  return 0;
}



void checkHeartbeat(){
    while(true){
      //check servers for heartbeat > 10
      //if true turn missed heartbeat = true
      // Your code below
      for(auto& cluster: clusters){
        zNode& s1 = cluster.second->getMaster();

        if(difftime(getTimeNow(),s1.last_heartbeat)>10)
        {
          s1.missed_heartbeat = true;
        }
        zNode& s2 = cluster.second->getWorker();
        if (difftime(getTimeNow(), s2.last_heartbeat) > 10)
        {
          s2.missed_heartbeat = true;
        }
      }
        sleep(3);
    }
}


std::time_t getTimeNow(){
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

