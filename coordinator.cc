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

struct zNode{
    int serverID;
    std::string hostname;
    std::string port;
    std::string type;
    std::time_t last_heartbeat;
    bool missed_heartbeat;
    bool isActive();

};

//potentially thread safe
std::mutex v_mutex;
std::vector<zNode> cluster1;
std::vector<zNode> cluster2;
std::vector<zNode> cluster3;


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
    return status;
}

class CoordServiceImpl final : public CoordService::Service {

  
  Status Heartbeat(ServerContext* context, const ServerInfo* serverinfo, Confirmation* confirmation) override {
    std::cout<<"Got Heartbeat! "<<serverinfo->type()<<"("<<serverinfo->serverid()<<")"<<std::endl;
    log(INFO, "Got Heartbeat! " + serverinfo->type() + "(" + std::to_string(serverinfo->serverid()) + ")") int server_id = serverinfo->serverid();
    int cluster_id = (server_id % 3) + 1;
    std::vector<zNode>* cluster;
    switch (cluster_id)
    {
    case 1:
      cluster = &cluster1;
      break;
    case 2:
      cluster = &cluster2;
      break;
    case 3:
      cluster = &cluster3;
      break;
    }
    bool found = false;
    for(auto node: *cluster){
      if (node.serverID == server_id){
        node.last_heartbeat = getTimeNow(); 
        found = true;
        break;
      }
    }
    if (!found){
      zNode new_node = {.serverID = server_id,
                        .hostname = serverinfo->hostname(),
                        .port = serverinfo->port(),
                        .type = serverinfo->type(),
                        .last_heartbeat = getTimeNow(),
                        .missed_heartbeat = false,
                        };
      std::cout << "New server added to cluster: " << cluster_id << std::endl;
      log(INFO, "New server added to cluster: " + std::to_string(cluster_id));
          cluster->push_back(new_node);
    }

    confirmation->set_status(true);
    return Status::OK;
  }
  
  //function returns the server information for requested client id
  //this function assumes there are always 3 clusters and has math
  //hardcoded to represent this.
  Status GetServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
    std::cout<<"Got GetServer for clientID: "<<id->id()<<std::endl;
    log(INFO, "Got GetServer for clientID: " + std::to_string(id->id()));

    int cluster_id = (id->id() % 3) + 1;
    zNode* server_node = NULL;
    switch(cluster_id){
      case 1:
        if (cluster1.size()>0){
          server_node = &cluster1[0];
        }
        break;
      case 2:
        if(cluster2.size()>0){
          server_node = &cluster2[0];
        }
        break;
      case 3:
        if(cluster3.size()>0){
          server_node = &cluster3[0];
        }
        break;
    }
    if (server_node!=NULL && server_node->isActive()){
      serverinfo->set_serverid(server_node->serverID);
      serverinfo->set_hostname(server_node->hostname);
      serverinfo->set_port(server_node->port);
      serverinfo->set_type(server_node->type);
      // confirmation->set_status(true)
    }else{
      // confirmation->set_status(false);
      serverinfo->set_hostname("not available");
      std::cout << "Server from cluster: " << cluster_id << "with id: " << server_node->serverID << "is dead!"<< std::endl;
      log(INFO, "No server available in cluster " + std::to_string(cluster_id));
    }
    return Status::OK;
  }
  

};

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
      for(auto& s : cluster1){
	if(difftime(getTimeNow(),s.last_heartbeat)>10){
	  if(!s.missed_heartbeat){
	    s.missed_heartbeat = true;
	    s.last_heartbeat = getTimeNow();
	  }else{
	    
	  }
	}
      }
      
      sleep(3);
    }
}


std::time_t getTimeNow(){
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

