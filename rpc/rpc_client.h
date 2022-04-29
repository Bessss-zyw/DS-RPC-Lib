#include <list>
#include <netdb.h>

#include "common.h"
#include "connection.hpp"

#define MAX_TIMEOUT rpc_const::to_max

// manages per RPC info
struct caller {
    caller(unsigned int id, unmarshall *un);
    ~caller();

    unsigned int rid;		// request id
    unmarshall *un;
    int result;
    bool done;
    pthread_mutex_t m;
    pthread_cond_t c;
};

// RPC client endpoint
class RPCC {
private:
    sockaddr_in dst_;       // server address
	pthread_t poll_th_;     // polling thread
    unsigned int rid_;		// next request id
    unsigned int cid_;		// client id
    unsigned int sid_;		// server id
    bool bind_done_;        // if already bind with server
    Connection *ch;         // connection with server
    std::map<int, caller *> calls_;     // RPC requests

	// mutexs
	pthread_mutex_t m_; 		// protect meta info(calls_)
	pthread_mutex_t chan_m_;	// protect channel

    int call1(unsigned int proc, marshall &req, unmarshall &rep, TO to);
	void process_msg(Connection *c, char *buf, size_t sz);  // process single msg from server

public:
    RPCC(const char *host, unsigned int port);
    ~RPCC();
	unsigned int id() { return cid_; }
    int bind(TO to = rpc_const::to_max);    // a sample RPC call to bind with server
	void poll_and_push();			        // constantly do poll and push

	// -----------rpc calls-----------
    template<class R>
        int call_m(unsigned int proc, marshall &req, R & r, TO to);

    template<class R, class... Args>
        int call(unsigned int proc, R & r, TO to, const Args&... args);
};


template<class R> int 
RPCC::call_m(unsigned int proc, marshall &req, R & r, TO to) 
{
	unmarshall u;
	int intret = call1(proc, req, u, to);
	if (intret < 0) return intret;
	u >> r;
	if(u.okdone() != true) {
                fprintf(stderr, "RPCC::call_m: failed to unmarshall the reply."
                       "You are probably calling RPC 0x%x with wrong return "
                       "type.\n", proc);
                VERIFY(0);
		return rpc_const::unmarshal_reply_failure;
        }
	return intret;
}

template<class R, class... Args> int
RPCC::call(unsigned int proc, R & r, TO to, const Args&... args) 
{
	marshall m;
	(m << ... << args);
	return call_m(proc, m, r, to);
}

int cmp_timespec(const struct timespec &a, const struct timespec &b);
void add_timespec(const struct timespec &a, int b, struct timespec *result);
int diff_timespec(const struct timespec &a, const struct timespec &b);