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
  rpcc *cl;
 public:
  demo_client(const char * port);
  virtual ~demo_client() {};
  virtual demo_protocol::status stat(demo_protocol::demoVar);
  virtual demo_protocol::status pass_string(demo_protocol::demoString);
};

demo_client::demo_client(const char* port)
{
  cl = new rpcc("127.0.0.1", port);
  if (cl->bind() < 0) {
    printf("demo_client: call bind\n");
  }
}

int
demo_client::stat(demo_protocol::demoVar var)
{
  int r = 0;
  demo_protocol::status ret = cl->call(demo_protocol::stat, cl->id(), var, r, MAX_TIMEOUT);
  VERIFY (ret == demo_protocol::OK);
  return r;
}

int
demo_client::pass_string(demo_protocol::demoString str) {
  int r = 0;
  demo_protocol::status ret = cl->call(demo_protocol::pass_string, cl->id(), (demo_protocol::demoVar)timer::get_usec(), str, r, MAX_TIMEOUT);
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

  demo_client *dc = new demo_client(argv[1]);
  r = dc->stat(1);
  printf ("stat returned %d\n", r);

  // pass string
  ifstream fin("input.txt", ios::in);
	if (!fin.is_open()) {
		cout << "cannot open file!" << endl;
		return 0;
	}
	string temp, res;
  while(getline(fin,temp)){
      res+=temp;
      res+='\n';
  }
	fin.close();

  uint64_t start, end;
  start = timer::get_usec();
  r = dc->pass_string(res);
  end = timer::get_usec();
  printf ("pass %lu buff, returned %d\n", res.size(), r);
  printf("RPC (RTT) latency: %lu usec\n", (end - start));
}