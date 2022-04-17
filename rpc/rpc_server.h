#include <sys/socket.h>
#include <netinet/in.h>
#include <list>
#include <map>
#include <set>
#include <stdio.h>
#include <unistd.h>
#include <mutex>
#include <shared_mutex>

#include "common.h"
#include "connection.h"
#include "utils/verify.h"
#include "utils/slock.h"

// rpc server endpoint.
class rpcs {
	typedef enum {
		NEW,  // new RPC, not a duplicate
		INPROGRESS, // duplicate of an RPC we're still processing
		DONE, // duplicate of an RPC we already replied to (have reply)
		FORGOTTEN,  // duplicate of an old RPC whose reply we've forgotten
	} rpcstate_t;

	int port_;		// the port to listen on
	int tcp_; 		// file desciptor for accepting connection
	unsigned int sid_;						// server id
	std::map<int, handler *> procs_;		// handlers
	std::map<int, connection *> conns_;		// connections

	// for select
	fd_set fds;
	int max_fd = 0;

	// provide at most once semantics by maintaining a window of replies
	// per client that that client hasn't acknowledged receiving yet.
	// indexed by client nonce.
	// std::map<unsigned int, std::list<reply_t> > reply_window_;

	// pthread_mutex_t procs_m_; 	// protect insert/delete to procs[]

	bool tcp_conn(int port);	// create tcp socket
	void connect();				// start a new connection for a client
	void disconnect(int fd);	// end a connection
	void poll_and_push();		// loop to accept && send msg from each socket
	void process();				// process all msgs in read buffer
	void sweep();				// remove all dead connections
	void process_msg(connection *c, char *buf, size_t sz);		// porcess a single msg
	void reg1(unsigned int proc, handler *h);

public:
	rpcs(unsigned int port, int counts=0);
	~rpcs();

	// RPC handler for clients binding
	int rpcbind(int a, int &r);

	// begin to monitor port
	void start();

	// -----------register a handler-----------
	template<class S, class A1, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1 a1, R & r));
	template<class S, class A1, class A2, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1 a1, const A2, 
					R & r));
	template<class S, class A1, class A2, class A3, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
					const A3, R & r));
	template<class S, class A1, class A2, class A3, class A4, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
					const A3, const A4, R & r));
	template<class S, class A1, class A2, class A3, class A4, class A5, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
					const A3, const A4, const A5, 
					R & r));
	template<class S, class A1, class A2, class A3, class A4, class A5, class A6,
		class R>
			void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
						const A3, const A4, const A5, 
						const A6, R & r));
	template<class S, class A1, class A2, class A3, class A4, class A5, class A6,
		class A7, class R>
			void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
						const A3, const A4, const A5, 
						const A6, const A7,
						R & r));
};


// -----------register a handler-----------
template<class S, class A1, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				R r;
				args >> a1;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				R r;
				args >> a1;
				args >> a2;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, R & r))
				: sob(xsob), meth(xmeth)  { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				A4 a4;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				args >> a4;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			const A5 a5, R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
					const A5 a5, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, const A5 a5, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				A4 a4;
				A5 a5;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				args >> a4;
				args >> a5;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, a5, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, class A6, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			const A5 a5, const A6 a6, 
			R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
					const A5 a5, const A6 a6, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, const A5 a5, const A6 a6, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				A4 a4;
				A5 a5;
				A6 a6;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				args >> a4;
				args >> a5;
				args >> a6;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, a5, a6, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, 
	class A6, class A7, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			const A5 a5, const A6 a6,
			const A7 a7, R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
					const A5 a5, const A6 a6, const A7 a7, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, const A5 a5, const A6 a6,
						const A7 a7, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				A4 a4;
				A5 a5;
				A6 a6;
				A7 a7;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				args >> a4;
				args >> a5;
				args >> a6;
				args >> a7;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, a5, a6, a7, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}
