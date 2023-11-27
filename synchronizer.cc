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
#include <algorithm>
#include <assert.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"
#include "coordinator.grpc.pb.h"

namespace fs = std::filesystem;

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
using csce438::ServerList;
using csce438::SynchService;
using csce438::AllUsers;
using csce438::TLFL;

int synchID = 1;
std::vector<std::string> get_lines_from_file(std::string);
void run_synchronizer(std::string,std::string,std::string,int);
std::vector<std::string> get_all_users_func(int);
std::vector<std::string> get_tl_or_fl(int, int, bool);

class SynchServiceImpl final : public SynchService::Service {
    Status GetAllUsers(ServerContext* context, const Confirmation* confirmation, AllUsers* allusers) override{
        std::cout<<"Got GetAllUsers"<<std::endl;
        std::vector<std::string> list = get_all_users_func(synchID);
        //package list
        for(auto s:list){
            allusers->add_users(s);
        }

        //return list
        return Status::OK;
    }

    Status GetTLFL(ServerContext* context, const ID* id, TLFL* tlfl){
        std::cout<<"Got GetTLFL"<<std::endl;
        int clientID = id->id();
        assert(clientID%3+1==synchID);
        std::vector<std::string> tl = get_tl_or_fl(synchID, clientID, true);
        std::vector<std::string> fl = get_tl_or_fl(synchID, clientID, false);

        //now populate TLFL tl and fl for return
        for(auto s:tl){
            tlfl->add_tl(s);
        }
        for(auto s:fl){
            tlfl->add_fl(s);
        }
        tlfl->set_status(true); 

        return Status::OK;
    }

    Status ResynchServer(ServerContext* context, const ServerInfo* serverinfo, Confirmation* c){
        std::cout<<serverinfo->type()<<"("<<serverinfo->serverid()<<") just restarted and needs to be resynched with counterpart"<<std::endl;
        std::string backupServerType;

        // YOUR CODE HERE


        return Status::OK;
    }
};

void RunServer(std::string coordIP, std::string coordPort, std::string port_no, int synchID){
  //localhost = 127.0.0.1
  std::string server_address("127.0.0.1:"+port_no);
  SynchServiceImpl service;
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

  std::thread t1(run_synchronizer,coordIP, coordPort, port_no, synchID);
  /*
  TODO List:
    -Implement service calls
    -Set up initial single heartbeat to coordinator
    -Set up thread to run synchronizer algorithm
  */

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}



int main(int argc, char** argv) {
  
  int opt = 0;
  std::string coordIP = "127.0.0.1";
  std::string coordPort = "9090";
  std::string port = "3029";

  while ((opt = getopt(argc, argv, "h:j:p:n:")) != -1){
    switch(opt) {
      case 'h':
          coordIP = optarg;
          break;
      case 'j':
          coordPort = optarg;
          break;
      case 'p':
          port = optarg;
          break;
      case 'n':
          synchID = std::stoi(optarg);
          break;
      default:
	         std::cerr << "Invalid Command Line Argument\n";
    }
  }

  RunServer(coordIP, coordPort, port, synchID);
  return 0;
}

void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID){
    //setup coordinator stub
    //std::cout<<"synchronizer stub"<<std::endl;
    std::string target_str = coordIP + ":" + coordPort;
    std::unique_ptr<CoordService::Stub> coord_stub_;
    coord_stub_ = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials())));
    //std::cout<<"MADE STUB"<<std::endl;
    ID msg;
    Confirmation c;
    grpc::ClientContext context_init;
    ServerInfo serverinfo;
    serverinfo.set_type("follower");
    serverinfo.set_serverid(synchID);
    serverinfo.set_hostname("127.0.0.1");
    serverinfo.set_port(port);
    coord_stub_->Init(&context_init, serverinfo, &c);

    msg.set_id(synchID);
    //TODO: begin synchronization process
    while(true){
        //change this to 30 eventually
        sleep(20);
        grpc::ClientContext context;

        // send init heartbeat
        ServerList serverlist;
        coord_stub_->GetAllFollowerServers(&context, msg, &serverlist);
        AllUsers allusers;
        //synch all users file 
        //get list of all followers
        
        // YOUR CODE HERE
        //set up stub
        std::unique_ptr<SynchService::Stub> sync_stub_;
        // send each a GetAllUsers request
        // aggregate users into a list
        std::vector<std::string> aggregated_users;
        std::cout << serverlist.serverid_size() << std::endl;
        for (int i = 0; i < serverlist.serverid_size(); i++){
            if (serverlist.serverid(i) == synchID)
                continue;
            std::string synchIP = serverlist.hostname(i);
            std::string synchPort = serverlist.port(i);
            std::string target_str = synchIP + ":" + synchPort;
            std::cout<<"target_str: "<<target_str<<std::endl;
            sync_stub_ = std::unique_ptr<SynchService::Stub>(SynchService::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials())));
            std::cout<<"Sending GetAllUsers to "<<serverlist.serverid(i)<<std::endl;
            grpc::ClientContext context;
            sync_stub_->GetAllUsers(&context, c, &allusers);
            std::cout<<"Got AllUsers from "<<serverlist.port(i)<<std::endl;
            //write to file
            for(int j = 0; j<allusers.users_size(); j++){
                aggregated_users.push_back(allusers.users(j));
            }
        }
        // get managed users from master directory and add to aggregated users
        std::string filename = "./master"+std::to_string(synchID)+"/managed_users.txt";
        std::vector<std::string> managed_users = get_lines_from_file(filename);
        // std::cout << "Managed users:" << managed_users.size()<< std::endl;
        for(int i = 0; i<managed_users.size(); i++){
            aggregated_users.push_back(managed_users[i]);
        }
        //sort list and remove duplicates
        std::sort(aggregated_users.begin(), aggregated_users.end());
        aggregated_users.erase(std::unique(aggregated_users.begin(), aggregated_users.end()), aggregated_users.end());
        std::cout<<"Aggregated users:"<<std::endl;
        for(int i = 0; i<aggregated_users.size(); i++){
            std::cout<<aggregated_users[i]<<std::endl;
            // write to all user file in the master and slave directories
            std::string filename = "./master"+std::to_string(synchID)+"/all_users";
            std::ofstream file;
            file.open(filename, std::ios::ios_base::out);
            for(int i = 0; i<aggregated_users.size(); i++){
                file<<aggregated_users[i]<<std::endl;
            }
            file.close();
            filename = "./slave"+std::to_string(synchID)+"/all_users";
            file.open(filename, std::ios::ios_base::out);
            for(int i = 0; i<aggregated_users.size(); i++){
                file<<aggregated_users[i]<<std::endl;
            }
            file.close();
        }

        //for all users
        for(auto i : aggregated_users){
            // sync their following lists
            std::vector<std::string> managed_users = get_all_users_func(synchID);
            // if i not in managed_users then create new following list
            
            if(std::find(managed_users.begin(), managed_users.end(), i) == managed_users.end()){
                // get the client cluster id
                int cluster_id = (std::stoi(i)%3) + 1;
                // get follower server info from coordinator
                ID msg;
                msg.set_id(cluster_id);
                grpc::ClientContext context;
                ServerInfo serverinfo;
                coord_stub_->GetFollowerServer(&context, msg, &serverinfo);
                // set up stub
                std::string synchIP = serverinfo.hostname();
                std::string synchPort = serverinfo.port();
                std::string target_str = synchIP + ":" + synchPort;
                std::unique_ptr<SynchService::Stub> sync_stub_;
                sync_stub_ = std::unique_ptr<SynchService::Stub>(SynchService::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials())));
                // send GetTLFL request
                TLFL tlfl;
                grpc::ClientContext context2;
                msg.set_id(std::stoi(i));
                sync_stub_->GetTLFL(&context2, msg, &tlfl);
                // write to file
                std::string filename = "./master"+std::to_string(synchID)+"/"+i+"_follow_list";
                std::ofstream file;
                file.open(filename, std::ios::ios_base::out);
                for(int i = 0; i<tlfl.fl_size(); i++){
                    file<<tlfl.fl(i)<<std::endl;
                }
                file.close();
                filename = "./slave"+std::to_string(synchID)+"/"+i+"_follow_list";
                file.open(filename, std::ios::ios_base::out);
                for(int i = 0; i<tlfl.fl_size(); i++){
                    file<<tlfl.fl(i)<<std::endl;
                }
                file.close();
            }
        }
    }
}

std::vector<std::string> get_lines_from_file(std::string filename){
  std::vector<std::string> users;
  std::string user;
  std::ifstream file; 
  file.open(filename);
  if(file.peek() == std::ifstream::traits_type::eof()){
    //return empty vector if empty file
    //std::cout<<"returned empty vector bc empty file"<<std::endl;
    file.close();
    return users;
  }
  while(file){
    getline(file,user);

    if(!user.empty())
      users.push_back(user);
  } 

  file.close();

  //std::cout<<"File: "<<filename<<" has users:"<<std::endl;
  /*for(int i = 0; i<users.size(); i++){
    std::cout<<users[i]<<std::endl;
  }*/ 

  return users;
}

bool file_contains_user(std::string filename, std::string user){
    std::vector<std::string> users;
    //check username is valid
    users = get_lines_from_file(filename);
    for(int i = 0; i<users.size(); i++){
      //std::cout<<"Checking if "<<user<<" = "<<users[i]<<std::endl;
      if(user == users[i]){
        //std::cout<<"found"<<std::endl;
        return true;
      }
    }
    //std::cout<<"not found"<<std::endl;
    return false;
}

std::vector<std::string> get_all_users_func(int synchID){
    //read all_users file master and client for correct serverID
    std::string master_users_file = "./master"+std::to_string(synchID)+"/managed_users.txt";
    std::string slave_users_file = "./slave"+std::to_string(synchID)+"/managed_users.txt";
    //take longest list and package into AllUsers message
    std::vector<std::string> master_user_list = get_lines_from_file(master_users_file);
    std::vector<std::string> slave_user_list = get_lines_from_file(slave_users_file);

    if(master_user_list.size() >= slave_user_list.size())
        return master_user_list;
    else
        return slave_user_list;
}

std::vector<std::string> get_tl_or_fl(int synchID, int clientID, bool tl){
    std::string master_fn = "./master"+std::to_string(synchID)+"/"+std::to_string(clientID);
    std::string slave_fn = "./slave"+std::to_string(synchID)+"/" + std::to_string(clientID);
    if(tl){
        master_fn.append("_timeline");
        slave_fn.append("_timeline");
    }else{
        master_fn.append("_follow_list");
        slave_fn.append("_follow_list");
    }

    std::vector<std::string> m = get_lines_from_file(master_fn);
    std::vector<std::string> s = get_lines_from_file(slave_fn);

    if(m.size()>=s.size()){
        return m;
    }else{
        return s;
    }

}
