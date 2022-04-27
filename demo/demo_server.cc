// demo server

#include <string>
#include <map>
#include <pthread.h>
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "demo_protocol.h"

class demo_server {
 public:
  demo_server() {};
  ~demo_server() {};
  demo_protocol::status stat(int clt, demo_protocol::demoVar a, int &);
  demo_protocol::status process_string(int clt, demo_protocol::demoVar start, demo_protocol::demoString str, demo_protocol::demoString &);
  // for illustration only
      // demo_protocol::status rpcA(int clt, demo_protocol::demoVar a, int &);
      // demo_protocol::status rpcB(int clt, demo_protocol::demoVar a, int &);
};

demo_protocol::status
demo_server::stat(int clt, demo_protocol::demoVar var, int &r)
// demo_server::stat(int &r, int clt, demo_protocol::demoVar var)
{
  printf("[Server] receive \"stat\" request from clt %d.\n", clt);
  demo_protocol::status ret = demo_protocol::OK;
  r = 12345;
  return ret;
}

demo_protocol::status
demo_server::process_string(int clt, demo_protocol::demoVar start, demo_protocol::demoString var, demo_protocol::demoString &r)
// demo_server::process_string(demo_protocol::demoString &r, int clt, demo_protocol::demoVar start, demo_protocol::demoString var)
{
  printf("[Server] receive \"process_string\" request from clt %d.\n", clt);
  printf("RPC(client->server) latency: %lu usec.\n", (timer::get_usec() - (uint64_t)start));
  demo_protocol::status ret = demo_protocol::OK;
  size_t sz = (1 << 15);    // 32k
  r.assign(sz, 0);
  return ret;
}

int main(int argc, char const *argv[])
{
    int count = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    srandom(getpid());

    if(argc != 2){
      fprintf(stderr, "Usage: %s port\n", argv[0]);
      exit(1);
    }

    char *count_env = getenv("RPC_COUNT");
    if(count_env != NULL){
      count = atoi(count_env);
    }

    demo_server ds;  
    RPCServer server(atoi(argv[1]), count);
    server.reg(demo_protocol::stat, &ds, &demo_server::stat);
    server.reg(demo_protocol::pass_string, &ds, &demo_server::process_string);

    server.start();

    while(1)
      sleep(1000);
}