RPC=./rpc
CXX = g++
CXXFLAGS = -std=c++17 -O0 -g -MMD -Wall -I. -I$(RPC) -D_FILE_OFFSET_BITS=64 -no-pie
RPCLIB=librpc.a
LDFLAGS = -L. -L/usr/local/lib
LDLIBS = -lpthread 

all: demo_server demo_client

demo_server:
	$(CXX) $(CXXFLAGS) demo/demo_server.cc $(LDFLAGS) $(LDLIBS) -o build/demo_server

demo_client:
	$(CXX) $(CXXFLAGS) demo/demo_client.cc $(LDFLAGS) $(LDLIBS) -o build/demo_client


clean_files=rpc/*.o rpc/*.d *.o *.d demo_client demo_server
clean: 
	rm $(clean_files)


# use lib
# all: rpc/librpc.a demo_client demo_server

# rpclib=rpc/rpc.cc rpc/connection.hpp rpc/pollmgr.cc rpc/thr_pool.cc rpc/jsl_log.cc rpc/gettime.cc
# rpc/librpc.a: $(patsubst %.cc,%.o,$(rpclib))
# 	rm -f $@
# 	ar cq $@ $^
# 	ranlib rpc/librpc.a

# demo_client:
# 	$(CXX) $(CXXFLAGS) demo_client.cc rpc/$(RPCLIB) $(LDFLAGS) $(LDLIBS) -o demo_client

# demo_server:
# 	$(CXX) $(CXXFLAGS) demo_server.cc rpc/$(RPCLIB) $(LDFLAGS) $(LDLIBS) -o demo_server