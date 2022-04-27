// demo client

#include <string>
#include <vector>
#include <arpa/inet.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <stdio.h>

#include "demo_protocol.h"
#include "rpc/rpc_client.h"

using namespace std;

// Client interface to the demo server
class demo_client {
 protected:
  RPCClient *cl;
 public:
  demo_client(const char * port);
  virtual ~demo_client() {};
  virtual int stat(demo_protocol::demoVar);
  virtual demo_protocol::demoString pass_string(demo_protocol::demoString);
};

demo_client::demo_client(const char* port)
{
  cl = new RPCClient("127.0.0.1", port);
  if (cl->bind() < 0) {
    printf("demo_client: call bind\n");
  }
}

int
demo_client::stat(demo_protocol::demoVar var)
{
  int r = 0;
  demo_protocol::status ret = cl->call(demo_protocol::stat, r, MAX_TIMEOUT, cl->id(), var);
  VERIFY (ret == demo_protocol::OK);
  return r;
}

demo_protocol::demoString
demo_client::pass_string(demo_protocol::demoString str) {
  demo_protocol::demoString r;
  demo_protocol::status ret = cl->call(demo_protocol::pass_string, r, MAX_TIMEOUT, cl->id(), (demo_protocol::demoVar)timer::get_usec(), str);
  VERIFY (ret == demo_protocol::OK);
  return r;
}

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 2){
    fprintf(stderr, "Usage: %s [host:]port\n", argv[0]);
    exit(1);
  }

  // stat RPC
  demo_client *dc = new demo_client(argv[1]);
  r = dc->stat(1);
  printf ("[Client] receive \"stat\" result %d.\n", r);

  // read file
  ifstream fin("demo/input.txt", ios::in);
	if (!fin.is_open()) {
		cout << "cannot open file!" << endl;
		return 0;
	}
	string temp, arg;
  while(getline(fin,temp)){
      arg+=temp;
      arg+='\n';
  }
	fin.close();

  // pass_string RPC
  uint64_t start, end;
  start = timer::get_usec();
  std::string res = dc->pass_string(arg);
  end = timer::get_usec();
  printf ("[Client] receive \"pass_string\" result %lu bytes.\n", res.size());
  printf("RPC (RTT) latency: %lu usec.\n", (end - start));
}