/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <glog/logging.h>
#include <thread>
#define log(severity, msg) \
  LOG(severity) << msg;    \
  google::FlushLogFiles(google::severity);

#include "coordinator.grpc.pb.h"
#include "sns.grpc.pb.h"

using csce438::Confirmation;
using csce438::CoordService;
using csce438::ListReply;
using csce438::Message;
using csce438::ForwardRequest;
using csce438::Reply;
using csce438::Request;
using csce438::ServerInfo;
using csce438::SNSService;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

struct Client
{
  std::string username;
  bool connected = true;
  int following_file_size = 0;
  std::vector<Client *> client_followers;
  std::vector<Client *> client_following;
  ServerReaderWriter<Message, Message> *stream = 0;
  bool operator==(const Client &c1) const
  {
    return (username == c1.username);
  }
};

std::string worker_hostname = "";
std::string worker_port = "";
std::string type = "";
int server_id;
int cluster_id;

// Vector that stores every client that has been created
std::vector<Client *> client_db;

// Helper function used to find a Client object given its username
int find_user(std::string username)
{
  int index = 0;
  for (Client *c : client_db)
  {
    if (c->username == username)
      return index;
    index++;
  }
  return -1;
}
std::unique_ptr<SNSService::Stub> sns_stub_;

class SNSServiceImpl final : public SNSService::Service
{
  void initialize_client_db()
  {
    // Read all users from file into client database
    std::string line;
    std::ifstream in("./" + type + std::to_string(cluster_id) + "/" + "all_users");
    std::cout << "Reading all users from file" << std::endl;
    while (getline(in, line))
    {
      if(find_user(line) < 0){
        Client *c = new Client();
        c->username = line;
        c->connected = false;
        client_db.push_back(c);
      }
    }
    in.close();
    // Read all following from file into client database
    for (Client *c : client_db)
    {
      std::ifstream in2("./" + type + std::to_string(cluster_id) + "/" + c->username + "_follow_list");
      while (getline(in2, line))
      {

        // if not already in the following list, add it
        if (std::find(c->client_following.begin(), c->client_following.end(), client_db[find_user(line)]) == c->client_following.end()){
          Client *other_user = client_db[find_user(line)];
          c->client_following.push_back(other_user);
          other_user->client_followers.push_back(c);
        }
      }
      in2.close();
    }

  }
  Status List(ServerContext *context, const Request *request, ListReply *list_reply) override
  {
    log(INFO, "Serving List Request from: " + request->username() + "\n");

    initialize_client_db();
    Client *user = client_db[find_user(request->username())];
    for (Client *c : client_db)
    {
      list_reply->add_all_users(c->username);
    }
    std::vector<Client *>::const_iterator it;
    for (it = user->client_followers.begin(); it != user->client_followers.end(); it++)
    {
      list_reply->add_followers((*it)->username);
    }
    return Status::OK;
  }

  Status Follow(ServerContext *context, const Request *request, Reply *reply) override
  {
    initialize_client_db();
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    log(INFO, "Serving Follow Request from: " + username1 + " for: " + username2 + "\n");

    int join_index = find_user(username2);
    if (join_index < 0 || username1 == username2)
      reply->set_msg("Join Failed -- Invalid Username");
    else
    {
      Client *user1 = client_db[find_user(username1)];
      Client *user2 = client_db[join_index];
      if (std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end())
      {
        reply->set_msg("Join Failed -- Already Following User");
        return Status::OK;
      }
      user1->client_following.push_back(user2);
      user2->client_followers.push_back(user1);
      // Create a new file to store all the followers of current user
      std::ofstream following_file("./" + type + std::to_string(cluster_id) + "/" + username1+"_follow_list", std::ios::app | std::ios::out | std::ios::in);
      following_file << username2 << "\n";
      following_file.close();
      reply->set_msg("Follow Successful");
    }
    forward_to_worker("follow", username1, username2);
    return Status::OK;
  }

  Status UnFollow(ServerContext *context, const Request *request, Reply *reply) override
  {
    initialize_client_db();
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    log(INFO, "Serving Unfollow Request from: " + username1 + " for: " + username2);

    int leave_index = find_user(username2);
    if (leave_index < 0 || username1 == username2)
    {
      reply->set_msg("Unknown follower");
    }
    else
    {
      Client *user1 = client_db[find_user(username1)];
      Client *user2 = client_db[leave_index];
      if (std::find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end())
      {
        reply->set_msg("You are not a follower");
        return Status::OK;
      }

      user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2));
      user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
      reply->set_msg("UnFollow Successful");
    }
    return Status::OK;
  }

  // RPC Login
  Status Login(ServerContext *context, const Request *request, Reply *reply) override
  {
    initialize_client_db();
    Client *c = new Client();
    std::string username = request->username();
    log(INFO, "Serving Login Request: " + username + "\n");

    int user_index = find_user(username);
    if (user_index < 0)
    {
      c->username = username;
      if (type == "master"){
        c->connected = true;
      }
      else{
        c->connected = false;
      }
      client_db.push_back(c);
      // Create a new file to store all the users managed by the server
      int this_cluster_id = (server_id%3)+1;
      std::ofstream all_users_file("./" + type + std::to_string(this_cluster_id) + "/" + "managed_users.txt", std::ios::app | std::ios::out | std::ios::in);
      all_users_file << username << "\n";
      // close the file
      all_users_file.close();
      reply->set_msg("Login Successful!");
    }
    else
    {
      Client *user = client_db[user_index];
      if (user->connected)
      {
        log(WARNING, "User already logged on");
        reply->set_msg("you have already joined");
      }
      else if(type == "master")
      {
        std::string msg = "Welcome Back " + user->username;
        reply->set_msg(msg);
        user->connected = true;
      }
    }
    forward_to_worker("login", username, "");
    return Status::OK;
  }

  Status Timeline(ServerContext *context,
                  ServerReaderWriter<Message, Message> *stream) override
  {
    log(INFO, "Serving Timeline Request");
    Message message;
    Client *c;
    while (stream->Read(&message))
    {
      std::string username = message.username();
      int user_index = find_user(username);
      c = client_db[user_index];
      // Write the current message to "username.txt"
      std::string filename = "./" + type + std::to_string(cluster_id) + "/" + username + "_timeline";
      std::ofstream user_file(filename, std::ios::app | std::ios::out | std::ios::in);
      google::protobuf::Timestamp temptime = message.timestamp();
      std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
      std::string fileinput = time + " :: " + message.username() + ":" + message.msg() + "\n";
      //"Set Stream" is the default message from the client to initialize the stream
      if ((message.msg() != "Set Stream") && (message.msg() != "Update\n") && (message.msg() != "Set Stream\n")){
        user_file << fileinput;
        forward_to_worker("timeline", username, fileinput);
      }
        // also update the stream from file
      // If message = "Set Stream", print the first 20 chats from the people you follow
      else
      {
        user_file.close();
        if (c->stream == 0)
          c->stream = stream;
        std::string line;
        std::vector<std::string> newest_twenty;
        std::ifstream in("./" + type + std::to_string(cluster_id) + "/" + username + "_following.txt");
        int count = 0;
        // Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
        while (getline(in, line))
        {
          //          count++;
          //          if(c->following_file_size > 20){
          //	    if(count < c->following_file_size-20){
          //	      continue;
          //            }
          //          }
          newest_twenty.push_back(line);
        }
        Message new_msg;
        // Send the newest messages to the client to be displayed
        if (newest_twenty.size() >= 20)
        {
          for (int i = newest_twenty.size() - 20; i < newest_twenty.size(); i += 1)
          {
            new_msg.set_msg(newest_twenty[i]);
            stream->Write(new_msg);
          }
        }
        else
        {
          for (int i = 0; i < newest_twenty.size(); i += 1)
          {
            new_msg.set_msg(newest_twenty[i]);
            stream->Write(new_msg);
          }
        }
        // std::cout << "newest_twenty.size() " << newest_twenty.size() << std::endl;
        continue;
      }
      // Send the message to each follower's stream
      std::vector<Client *>::const_iterator it;
      for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
      {
        Client *temp_client = *it;
        if (temp_client->stream != 0 && temp_client->connected)
          temp_client->stream->Write(message);
        // For each of the current user's followers, put the message in their following.txt file
        std::string temp_username = temp_client->username;
        std::string temp_file = temp_username + "_following.txt";
        std::ofstream timeline_file("./" + type + std::to_string(cluster_id) + "/" + temp_file, std::ios::app | std::ios::out | std::ios::in);
        timeline_file << fileinput;
        std::cout<<fileinput<<std::endl;
        temp_client->following_file_size++;
        std::ofstream user_file(temp_username + "_timeline", std::ios::app | std::ios::out | std::ios::in);
        user_file << fileinput;
      }
    }
    // If the client disconnected from Chat Mode, set connected to false
    c->connected = false;
    return Status::OK;
  }

  Status forward(ServerContext *context, const ForwardRequest *forward_request, Reply *reply){
    std::string command = forward_request->command();
    Request request;
    request.set_username(forward_request->username());
    for (int i = 0; i < forward_request->arguments_size(); i++){
      request.add_arguments(forward_request->arguments(i));
    }
    if(command == "follow"){
      Follow(context, &request, reply);
    }
    else if(command == "unfollow"){
      UnFollow(context, &request, reply);
    } else if (command == "login"){
      Login(context, &request, reply);
    }else if (command=="timeline"){
      std::string username = request.username();
      std::string filename = "./" + type + std::to_string(cluster_id) + "/" + username + "_timeline";
      std::ofstream user_file(filename, std::ios::app | std::ios::out | std::ios::in);
      std::string fileinput = request.arguments(0);
      user_file << fileinput;
      user_file.close();
    }
    return Status::OK;
  }

  void forward_to_worker(std::string command, std::string username, std::string argument){
    if (type != "master" || worker_hostname == "" || worker_port == ""){
      return;
    }
    std::string server_login_info;
    server_login_info = worker_hostname + ":" + worker_port;

    sns_stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
        grpc::CreateChannel(
            server_login_info, grpc::InsecureChannelCredentials())));
    ClientContext context;
    ForwardRequest forward_request;
    forward_request.set_command(command);
    forward_request.set_username(username);
    forward_request.add_arguments(argument);
    Reply reply;
    Status status = sns_stub_->forward(&context, forward_request, &reply);
    if(!status.ok()){
      std::cout << "Forwarding failed!" << std::endl;
    }
  }
};

class ServerClass{
public:
  ServerClass(const std::string &hname,
         const std::string &p, const std::string &c_hname, const std::string &c_p, int s, int k)
      : hostname(hname), port(p), coord_hostname(c_hname), coord_port(c_p) {
        server_id = s;
        cluster_id = (server_id%3) + 1;
        is_master = false;
        is_active = false;
    connect();
      }
  void sendHeartBeats();

protected:
  int connect ();
  int HeartBeat();

private:
  static const std::string MASTER;
  static const std::string SLAVE;
  std::string hostname;
  std::string port;
  std::string coord_hostname;
  std::string coord_port;
  bool is_master;
  bool is_active;

  // You can have an instance of the client stub
  // as a member variable.
  std::unique_ptr<CoordService::Stub> stub_;
};

int ServerClass::connect()
{
  std::string login_info = coord_hostname + ":" + coord_port;
  stub_ = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(
      grpc::CreateChannel(
          login_info, grpc::InsecureChannelCredentials())));

  return 1;
}

int ServerClass::HeartBeat(){
  ClientContext context;
  Confirmation confirmation;
  ServerInfo server_info;
  server_info.set_serverid(server_id);
  server_info.set_hostname(hostname);
  server_info.set_port(port);
  server_info.set_type(type);
  Status status = stub_->Heartbeat(&context, server_info, &confirmation);
  if(!status.ok() or !confirmation.status()){
    std::cout << "Sending heartbeat failed!" << std::endl;
    return -1;
  }
  type = confirmation.designation();
  worker_hostname = confirmation.worker_hostname();
  worker_port = confirmation.worker_port();
  std::cout << "I am a " << type << std::endl;
  if(type==MASTER){
    std::cout << "Slave is " << worker_hostname << ":" << worker_port << std::endl;
  }
  return 0;
}

void ServerClass::sendHeartBeats(){
  while(true){
    std::cout << "Sending heartbeat!" << std::endl;
    if (HeartBeat()==0){
      sleep(3);
    }
    
    // HeartBeat();
  }
}


void RunServer(std::string port_no)
{
  std::string server_address = "0.0.0.0:" + port_no;
  SNSServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  log(INFO, "Server listening on " + server_address);

  server->Wait();
}
const std::string ServerClass::MASTER = "master";
const std::string ServerClass::SLAVE = "slave";
int main(int argc, char **argv)
{
  std::string hostname = "0.0.0.0";
  std::string port = "3010";
  std::string coord_port = "9000";
  int server_id = 1;
  int cluster_id = 1;
  int opt = 0;
  while ((opt = getopt(argc, argv, "h:p:k:s:c:")) != -1)
  {
    switch (opt)
    {
    case 'p':
      port = optarg;
      break;
    case 'k':
      coord_port = optarg;
      break;
    case 's':
      server_id = atoi(optarg);
      break;
    case 'c':
      cluster_id = atoi(optarg);
      break;
    case 'h':
      hostname = optarg;
      break;
    default:
      std::cerr << "Invalid Command Line Argument\n";
    }
  }  
  std::string log_file_name = std::string("server-") + port;
  google::InitGoogleLogging(log_file_name.c_str());
  log(INFO, "Logging Initialized. Server starting...");
  ServerClass server_object("0.0.0.0", port, "0.0.0.0", coord_port, server_id, cluster_id);
  std::thread hb(&ServerClass::sendHeartBeats, &server_object);
  RunServer(port);

  return 0;
}