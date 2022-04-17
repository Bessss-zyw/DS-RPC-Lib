#include <list>
#include <netdb.h>

#include "common.h"
#include "connection.h"

#define MAX_TIMEOUT rpc_const::to_max

//manages per rpc info
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

class rpcc {
private:
    sockaddr_in dst_;
	pthread_t poll_th_;
    unsigned int rid_;		// next request id
    unsigned int cid_;		// client id
    unsigned int sid_;		// server id
    bool bind_done_;
    connection *ch;
    std::map<int, caller *> calls_;
    // std::list<unsigned int> rid_rep_window_;

	// mutexs
	pthread_mutex_t m_; 		// protect meta info(calls_)
	pthread_mutex_t chan_m_;	// protect channel

    int call1(unsigned int proc, marshall &req, unmarshall &rep, TO to);
	void process_msg(connection *c, char *buf, size_t sz);

public:
    rpcc(const char *host, const char *port);
    ~rpcc();
    int bind(TO to = rpc_const::to_max);   // a sample RPC call to bind with server
	unsigned int id() { return cid_; }
	void poll_and_push();			// constantly do poll and push

	// -----------rpc calls-----------
    template<class R>
        int call_m(unsigned int proc, marshall &req, R & r, TO to);

    template<class R>
        int call(unsigned int proc, R & r, TO to); 
    template<class R, class A1>
        int call(unsigned int proc, const A1 & a1, R & r, TO to); 
    template<class R, class A1, class A2>
        int call(unsigned int proc, const A1 & a1, const A2 & a2, R & r, 
                TO to); 
    template<class R, class A1, class A2, class A3>
        int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
                R & r, TO to); 
    template<class R, class A1, class A2, class A3, class A4>
        int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
                const A4 & a4, R & r, TO to);
    template<class R, class A1, class A2, class A3, class A4, class A5>
        int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
                const A4 & a4, const A5 & a5, R & r, TO to); 
    template<class R, class A1, class A2, class A3, class A4, class A5,
        class A6>
            int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
                    const A4 & a4, const A5 & a5, const A6 & a6,
                    R & r, TO to); 
    template<class R, class A1, class A2, class A3, class A4, class A5, 
        class A6, class A7>
            int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
                    const A4 & a4, const A5 & a5, const A6 &a6, const A7 &a7,
                    R & r, TO to); 
};


template<class R> int 
rpcc::call_m(unsigned int proc, marshall &req, R & r, TO to) 
{
	unmarshall u;
	int intret = call1(proc, req, u, to);
	if (intret < 0) return intret;
	u >> r;
	if(u.okdone() != true) {
                fprintf(stderr, "rpcc::call_m: failed to unmarshall the reply."
                       "You are probably calling RPC 0x%x with wrong return "
                       "type.\n", proc);
                VERIFY(0);
		return rpc_const::unmarshal_reply_failure;
        }
	return intret;
}

template<class R> int
rpcc::call(unsigned int proc, R & r, TO to) 
{
	marshall m;
	return call_m(proc, m, r, to);
}

template<class R, class A1> int
rpcc::call(unsigned int proc, const A1 & a1, R & r, TO to) 
{
	marshall m;
	m << a1;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4, class A5> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, const A5 & a5, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	m << a5;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4, class A5,
	class A6> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, const A5 & a5, 
		const A6 & a6, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	m << a5;
	m << a6;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4, class A5,
	class A6, class A7> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, const A5 & a5, 
		const A6 & a6, const A7 & a7,
		R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	m << a5;
	m << a6;
	m << a7;
	return call_m(proc, m, r, to);
}

int cmp_timespec(const struct timespec &a, const struct timespec &b);
void add_timespec(const struct timespec &a, int b, struct timespec *result);
int diff_timespec(const struct timespec &a, const struct timespec &b);