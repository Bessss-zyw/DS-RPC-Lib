// demo protocol
#include "rpc/rpc_server.h"
#include "utils/timer.h"

class demo_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long demoVar;
  typedef std::string demoString;
  enum rpc_numbers {
    stat = 0x7001,
    pass_string,
    rpcA,
    rpcB
  };
};

