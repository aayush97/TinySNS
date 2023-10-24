#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <csignal>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
#include "coordinator.grpc.pb.h"

using csce438::Confirmation;
using csce438::CoordService;
using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::ServerInfo;
using csce438::SNSService;
using csce438::ID;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

void sig_ignore(int sig)
{
  std::cout << "Signal caught " + sig;
}

Message MakeMessage(const std::string &username, const std::string &msg)
{
  Message m;
  m.set_username(username);
  m.set_msg(msg);
  google::protobuf::Timestamp *timestamp = new google::protobuf::Timestamp();
  timestamp->set_seconds(time(NULL));
  timestamp->set_nanos(0);
  m.set_allocated_timestamp(timestamp);
  return m;
}

class Client : public IClient
{
public:
  Client(const std::string &hname,
         const std::string &uname,
         const std::string &p)
      : hostname(hname), username(uname), port(p) {}

protected:
  virtual int connectTo();
  virtual IReply processCommand(std::string &input);
  virtual void processTimeline();

private:
  std::string hostname;
  std::string username;
  std::string port;

  // You can have an instance of the client stub
  // as a member variable.
  std::unique_ptr<SNSService::Stub> stub_;
  std::unique_ptr<CoordService::Stub> coord_stub_;

  IReply Login();
  IReply List();
  IReply Follow(const std::string &username);
  IReply UnFollow(const std::string &username);
  void Timeline(const std::string &username);
};

///////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////
int Client::connectTo()
{
  // ------------------------------------------------------------
  // In this function, you are supposed to create a stub so that
  // you call service methods in the processCommand/porcessTimeline
  // functions. That is, the stub should be accessible when you want
  // to call any service methods in those functions.
  // I recommend you to have the stub as
  // a member variable in your own Client class.
  // Please refer to gRpc tutorial how to create a stub.
  // ------------------------------------------------------------

  // Connect to coordinator stub
  std::string login_info = hostname + ":" + port;
  coord_stub_ = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(
      grpc::CreateChannel(
          login_info, grpc::InsecureChannelCredentials())));
  
  ServerInfo serverinfo;
  ClientContext context;
  ID id;
  // set id to be equal to the last digit in the username
  id.set_id(std::stoi(std::string(1,username[username.size()-1])));

  Status status;
  status = coord_stub_->GetServer(&context, id, &serverinfo);

  // server node in desired cluster is not available
  if (!status.ok() || serverinfo.hostname() == "not available")
  {
    return -1;
  }

  // Connect to server stub
  std::string server_login_info;
  server_login_info = serverinfo.hostname() + ":" + serverinfo.port();


  
  stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
      grpc::CreateChannel(
          server_login_info, grpc::InsecureChannelCredentials())));

  IReply ire = Login();
  if (!ire.grpc_status.ok() || (ire.comm_status == FAILURE_ALREADY_EXISTS))
  {
    return -1;
  }

  return 1;
}

IReply Client::processCommand(std::string &input)
{
  // ------------------------------------------------------------
  // GUIDE 1:
  // In this function, you are supposed to parse the given input
  // command and create your own message so that you call an
  // appropriate service method. The input command will be one
  // of the followings:
  //
  // FOLLOW <username>
  // UNFOLLOW <username>
  // LIST
  // TIMELINE
  //
  // - JOIN/LEAVE and "<username>" are separated by one space.
  // ------------------------------------------------------------

  // ------------------------------------------------------------
  // GUIDE 2:
  // Then, you should create a variable of IReply structure
  // provided by the client.h and initialize it according to
  // the result. Finally you can finish this function by returning
  // the IReply.
  // ------------------------------------------------------------

  // ------------------------------------------------------------
  // HINT: How to set the IReply?
  // Suppose you have "Join" service method for JOIN command,
  // IReply can be set as follow:
  //
  //     // some codes for creating/initializing parameters for
  //     // service method
  //     IReply ire;
  //     grpc::Status status = stub_->Join(&context, /* some parameters */);
  //     ire.grpc_status = status;
  //     if (status.ok()) {
  //         ire.comm_status = SUCCESS;
  //     } else {
  //         ire.comm_status = FAILURE_NOT_EXISTS;
  //     }
  //
  //      return ire;
  //
  // IMPORTANT:
  // For the command "LIST", you should set both "all_users" and
  // "following_users" member variable of IReply.
  // ------------------------------------------------------------

  IReply ire;
  std::size_t index = input.find_first_of(" ");
  std::cout << "Processing " + input + ". ";
  // check is server is up by trying to login
  Request request;
  request.set_username(username);
  Reply reply;
  ClientContext context;
  Status status = stub_->Login(&context, request, &reply);
  if(!status.ok()){
    ire.comm_status = FAILURE_UNKNOWN;
    return ire;
  }
  if (index != std::string::npos)
  {
    std::string cmd = input.substr(0, index);

    /*
      if (input.length() == index + 1) {
      std::cout << "Invalid Input -- No Arguments Given\n";
      }
    */

    std::string argument = input.substr(index + 1, (input.length() - index));

    if (cmd == "FOLLOW")
    {
      return Follow(argument);
    }
    else if (cmd == "UNFOLLOW")
    {
      return UnFollow(argument);
    }
  }
  else
  {
    if (input == "LIST")
    {
      return List();
    }
    else if (input == "TIMELINE")
    {
      ire.comm_status = SUCCESS;
      return ire;
    }
  }

  ire.comm_status = FAILURE_INVALID;
  return ire;
}

void Client::processTimeline()
{
  Timeline(username);
  // ------------------------------------------------------------
  // In this function, you are supposed to get into timeline mode.
  // You may need to call a service method to communicate with
  // the server. Use getPostMessage/displayPostMessage functions
  // for both getting and displaying messages in timeline mode.
  // You should use them as you did in hw1.
  // ------------------------------------------------------------

  // ------------------------------------------------------------
  // IMPORTANT NOTICE:
  //
  // Once a user enter to timeline mode , there is no way
  // to command mode. You don't have to worry about this situation,
  // and you can terminate the client program by pressing
  // CTRL-C (SIGINT)
  // ------------------------------------------------------------
}

///////////////////////////////////////////
// List Command
//////////////////////////////////////////
IReply Client::List()
{
  // Data being sent to the server
  Request request;
  request.set_username(username);

  // Container for the data from the server
  ListReply list_reply;

  // Context for the client
  ClientContext context;

  Status status = stub_->List(&context, request, &list_reply);
  IReply ire;
  ire.grpc_status = status;

  // Loop through list_reply.all_users and list_reply.following_users
  // Print out the name of each room
  if (status.ok())
  {
    ire.comm_status = SUCCESS;
    std::string all_users;
    std::string following_users;
    for (std::string s : list_reply.all_users())
    {
      ire.all_users.push_back(s);
    }
    for (std::string s : list_reply.followers())
    {
      ire.followers.push_back(s);
    }
  }
  return ire;
}

IReply Client::Follow(const std::string &username2)
{
  Request request;
  request.set_username(username);
  request.add_arguments(username2);

  Reply reply;
  ClientContext context;

  Status status = stub_->Follow(&context, request, &reply);
  IReply ire;
  ire.grpc_status = status;
  if (reply.msg() == "unkown user name")
  {
    ire.comm_status = FAILURE_INVALID_USERNAME;
  }
  else if (reply.msg() == "unknown follower username")
  {
    ire.comm_status = FAILURE_INVALID_USERNAME;
  }
  else if (reply.msg() == "you have already joined")
  {
    ire.comm_status = FAILURE_ALREADY_EXISTS;
  }
  else if (reply.msg() == "Follow Successful")
  {
    ire.comm_status = SUCCESS;
  }
  else
  {
    ire.comm_status = FAILURE_UNKNOWN;
  }
  return ire;
}

IReply Client::UnFollow(const std::string &username2)
{
  Request request;

  request.set_username(username);
  request.add_arguments(username2);

  Reply reply;

  ClientContext context;

  Status status = stub_->UnFollow(&context, request, &reply);
  IReply ire;
  ire.grpc_status = status;
  if (reply.msg() == "Unknown follower")
  {
    ire.comm_status = FAILURE_INVALID_USERNAME;
  }
  else if (reply.msg() == "You are not a follower")
  {
    ire.comm_status = FAILURE_NOT_A_FOLLOWER;
  }
  else if (reply.msg() == "UnFollow Successful")
  {
    ire.comm_status = SUCCESS;
  }
  else
  {
    ire.comm_status = FAILURE_UNKNOWN;
  }

  return ire;
}

IReply Client::Login()
{
  Request request;
  request.set_username(username);
  Reply reply;
  ClientContext context;

  Status status = stub_->Login(&context, request, &reply);

  IReply ire;
  ire.grpc_status = status;
  std::cout << "REPLY MESSAGE: " + reply.msg();
  if (reply.msg() == "you have already joined")
  {
    ire.comm_status = FAILURE_ALREADY_EXISTS;
  }
  else
  {
    ire.comm_status = SUCCESS;
  }
  return ire;
}

void Client::Timeline(const std::string &username)
{
  ClientContext context;

  std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
      stub_->Timeline(&context));

  // Thread used to read chat messages and send them to the server
  std::thread writer([username, stream]()
                     {
    std::string input = "Set Stream";
    Message m = MakeMessage(username, input);
    stream->Write(m);
    while (1) {
      input = getPostMessage();
      m = MakeMessage(username, input);
      stream->Write(m);
    }
    stream->WritesDone(); });

  std::thread reader([username, stream]()
                     {
    Message m;
    while(stream->Read(&m)){
      google::protobuf::Timestamp temptime = m.timestamp();
      std::time_t time = temptime.seconds();
      displayPostMessage(m.username(), m.msg(), time);
    } });

  // Wait for the threads to finish
  writer.join();
  reader.join();
}

//////////////////////////////////////////////
// Main Function
/////////////////////////////////////////////

int main(int argc, char **argv)
{

  std::string hostname = "localhost";
  std::string username = "default";
  std::string port = "9000";

  int opt = 0;
  while ((opt = getopt(argc, argv, "h:u:k:")) != -1)
  {
    switch (opt)
    {
    case 'h':
      hostname = optarg;
      break;
    case 'u':
      username = optarg;
      break;
    case 'k':
      port = optarg;
      break;
    default:
      std::cout << "Invalid Command Line Argument\n";
    }
  }

  std::cout << "Logging Initialized. Client starting...";

  Client myc(hostname, username, port);

  // for(int i = 1; i <= 31; i++)
  //   signal(i, sig_ignore);

  myc.run();

  return 0;
}