#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <list>
#include <map>
#include <set>
#include <stdio.h>
#include <unistd.h>
#include <mutex>
#include <shared_mutex>

#include "common.hpp"
#include "connection.hpp"
#include "utils/verify.h"
#include "utils/slock.h"

// RPC server endpoint
class RPCS {
	int port_;		// the port to listen on
	int tcp_; 		// file desciptor for accepting connection
	unsigned int sid_;						// server id
	std::map<int, handler *> procs_;		// handlers
	std::map<int, Connection *> conns_;		// connections

	// for select
	fd_set fds;
	int max_fd = 0;

	// create tcp socket
	bool tcp_conn(int port) {
		struct sockaddr_in sin;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(port);

		tcp_ = socket(AF_INET, SOCK_STREAM, 0);
		if(tcp_ < 0){
			perror("tcpsconn::tcpsconn accept_loop socket:");
			return false;
		}

		int yes = 1;
		setsockopt(tcp_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		setsockopt(tcp_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

		if(bind(tcp_, (sockaddr *)&sin, sizeof(sin)) < 0){
			perror("accept_loop tcp bind:");
			return false;
		}

		if(listen(tcp_, 1000) < 0) {
			perror("tcpsconn::tcpsconn listen:");
			return false;
		}

		// printf("RPCS::tcpsconn listen on %d %d\n", port, sin.sin_port);
		return true;

		// if (pipe(pipe_) < 0) {
		// 	perror("accept_loop pipe:");
		// 	return false;
		// }
		// int flags = fcntl(pipe_[0], F_GETFL, NULL);
		// flags |= O_NONBLOCK;
		// fcntl(pipe_[0], F_SETFL, flags);
		// VERIFY((th_ = method_thread(this, false, &tcpsconn::accept_conn)) != 0); 
	}

	// start a new connection for a client
	void connect() {
		// printf("---RPCS::connect---\n");
		sockaddr_in sin;
		socklen_t slen = sizeof(sin);
		// int s1 = accept4(tcp_, (sockaddr *)&sin, &slen, SOCK_NONBLOCK); 
		int s1 = accept(tcp_, (sockaddr *)&sin, &slen); 
		if (s1 < 0) {
			printf("RPCS::connect failure, errno = %d\n", errno);
			// pthread_exit(NULL);
			return;
		}

		// printf("RPCS::connect got connection fd=%d %s:%d\n", 
		// 		s1, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

		// add to meta
		conns_[s1] = new Connection(s1);
		FD_SET(s1, &fds);
		if (max_fd < s1) max_fd = s1;
	}

	// end a connection
	void disconnect(int fd) {
		// printf("---RPCS::disconnect(fd = %d)---\n", fd);
		// fd should be in conns
		auto res = conns_.find(fd);
		VERIFY(res != conns_.end());
		// shutdown fd
		delete res->second;
		// erase fd from meta
		conns_.erase(res);
		FD_CLR(fd, &fds);
		// find max fd
		max_fd = -1;
		for (auto &&iter : conns_)
			if (iter.first > max_fd) max_fd = iter.first;
	}

	// loop to accept && send msg from each socket
	void poll_and_push() {
		// printf("---RPCS::poll_and_push--- on fd_set: (%d) ", tcp_);
		// for (auto &&conn : conns_) printf("%d ", conn.first);
		// printf("\n");

		int max = tcp_ < max_fd? max_fd: tcp_;
		fd_set rfds, wfds;
		rfds = wfds = fds;
		FD_SET(tcp_, &rfds);
		for (auto &&conn : conns_)
			if (conn.second->empty_wbuf()) 
				FD_CLR(conn.first, &wfds);

		int ret = select(max + 1, &rfds, &wfds, NULL, NULL);
		// printf("RPCS::poll_and_push %d socket ready...\n", ret);

		if (ret < 0) {
			if (errno == EINTR) {
				return;
			} else {
				printf("RPCS::poll_and_push select failure, errno %d\n",errno);
				VERIFY(0);
			}
		}

		if (FD_ISSET(tcp_, &rfds)) {connect();}
		for (auto &&con: conns_) {
			if (FD_ISSET(con.first, &rfds)) {con.second->read_cb();}
			if (FD_ISSET(con.first, &wfds)) {con.second->write_cb();}
		}
	}
	
	// process all msgs in read buffer
	void process() {
		// printf("---RPCS::process---\n");
		for (auto &&conn : conns_) {
			// for each conn, process its rbuf queue
			for (size_t i = 0; i < conn.second->rbuf_cnt(); i++) {
				buffer buf = conn.second->next_rbuf();
				VERIFY(buf.sz == buf.solong);
				process_msg(conn.second, buf.buf, buf.sz);
				free(buf.buf);
			}
		}
	}

	// remove all dead connections
	void sweep() {
		// printf("---RPCS::sweep---\n");

		// find dead connections
		std::vector<int> dump_fds;
		for (auto &&conn : conns_) {
			if (conn.second->is_dead()) 
				dump_fds.push_back(conn.first);
		}

		// remove dead connections
		for (auto &&fd : dump_fds) disconnect(fd);
	}
	
	// porcess a single msg
	void process_msg(Connection *c, char *buf, size_t sz) {
		// printf("---RPCS::process_msg(c = %d, buf = %p, sz = %lu)---\n", c->channo(), buf, sz);
		unmarshall req(buf, sz);

		// unpack msg
		req_header h;
		req.unpack_req_header(&h);
		int proc = h.proc;
		if(!req.ok()){
			printf("RPCS:process_msg unmarshall header failed!!!\n");
			// c->decref();
			return;
		}
		// printf("RPCS::process_msg: rpc %u (proc %x) from clt %u for srv instance %u \n",
		// 		h.rid, proc, h.clt_id, h.srv_id);

		// reply
		marshall rep;
		reply_header rh(h.rid, 0);

		// is client sending to an old instance of server?
		if(h.srv_id != 0 && h.srv_id != sid_){
			printf("RPCS::process_msg receive RPC for an old server instance %u (current %u) proc %x\n", h.srv_id, sid_, h.proc);
			rh.result = rpc_const::oldsrv_failure;
			goto send_reply;
		}
		
		// is RPC proc a registered procedure?
		if(procs_.count(proc) < 1){
			printf("RPCS::process_msg unknown proc %x.\n", proc);
			rh.result = rpc_const::unknown_proc;
			goto send_reply;
		}

		handler *f;
		f = procs_[proc];
		rh.result = f->fn(req, rep);
		if (rh.result == rpc_const::unmarshal_args_failure) {
			printf("RPCS::process_msg failed to unmarshall the arguments of type 0x%x RPC!\n", proc);
			// VERIFY(0);
			rh.result = rpc_const::unmarshal_args_failure;
			goto send_reply;
		}
		VERIFY(rh.result >= 0);

	send_reply:
		char *send_buf;
		int send_sz;
		rep.pack_reply_header(rh);
		rep.take_buf(&send_buf, &send_sz);
		// printf("RPCS::process_msg sending reply of size %d for rpc %u, proc %x result %d, clt %u\n",
		// 		send_sz, h.rid, proc, rh.result, h.clt_id);
		c->send(send_buf, send_sz);
	}

	// register a single handler
	void reg1(unsigned int proc, handler *h) {
		VERIFY(procs_.count(proc) == 0);
		procs_[proc] = h;
		VERIFY(procs_.count(proc) >= 1);
	}	

public:
	RPCS(unsigned int port, int counts = 0)
		:port_(port) {
		// single thread, no need for lock
		// VERIFY(pthread_mutex_init(&procs_m_, 0) == 0);

		// random server id
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		srandom((int)ts.tv_nsec^((int)getpid()));
		sid_ = random();
		
		FD_ZERO(&fds);
		reg(rpc_const::bind, this, &RPCS::rpcbind);
		VERIFY(tcp_conn(port_));
	}

	~RPCS() {
		// close all connections
		close(tcp_);
		for (auto &&conn : conns_)
			delete conn.second;
	}

	// a default RPC handler for client binding
	int rpcbind(int a, int &r) {
		// printf("RPCS::rpcbind called return sid %u\n", sid_);
		r = sid_;
		return 0;
	}

	// begin to listen on port and process msgs
	void start() {
		// constantly do polling pushing and processing
		while (1) {
			poll_and_push();
			if (errno == EINTR) return;
			process();
			if (errno == EINTR) return;
			sweep();
		}
	}



	// -----------register a handler of different parameters-----------
	// TODO: variable-length parameter list
	template<class S, class A1, class R> void
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, R & r))
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
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
	reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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

	// template<class S, class R, class ...Args> void
	// reg(unsigned int proc, S*sob, int (S::*meth)(R & r, const Args ... args))
	// {
	// 	class h1 : public handler {
	// 		private:
	// 			S * sob;
	// 			int (S::*meth)(R & r, const Args ... args);
	// 		public:
	// 			h1(S *xsob, int (S::*xmeth)(R & r, const Args ... args))
	// 				: sob(xsob), meth(xmeth) { }
	// 			int fn(unmarshall &input, marshall &ret) {
	// 				Args ... args;
	// 				R r;
	// 				(input >> ... >> args);
	// 				if(!input.okdone())
	// 					return rpc_const::unmarshal_args_failure;
	// 				int b = (sob->*meth)(r, args);
	// 				ret << r;
	// 				return b;
	// 			}
	// 	};
	// 	reg1(proc, new h1(sob, meth));
	// }
};
