#pragma once

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <queue>

#include "utils/verify.h"
#include "utils/slock.h"

#define MAX_MSG_SZ (10 << 20)    	// maximum MSG size is 10M
#define MAX_MSG_CNT 10				// maximum MSG number in a single read_cb/write_cb

// one buffer obj for each msg
struct buffer {
	char *buf;
	int sz;
	int solong; //amount of bytes written or read so far

	buffer(): buf(NULL), sz(0), solong(0) {}
	buffer (char *b, int s) : buf(b), sz(s), solong(0){}
	~buffer() {}

	// if the buffer is empty
	bool empty() {
		if (!buf) {
			VERIFY(!sz && !solong);
			return true;
		} else {
			VERIFY(sz);
			return false;
		}
	}

	// reset buffer ptr and sz
    void reset() {
		buf = NULL;
		sz = solong = 0;
    }

	// only called when connection is over
	void clear() {
        if (buf) free(buf);
		reset();
	}
};

// one connection obj for one socket connection
class Connection {
	int fd_;
	bool dead_;
	buffer wbuf;    // curr write msg buffer
	buffer rbuf;    // curr read msg buffer
	std::queue<buffer> wbufq;    // write msg buffer queue
	std::queue<buffer> rbufq;    // read msg buffer queue
	pthread_mutex_t m_; 		// protect channel
	pthread_mutex_t wm_; 		// protect wbuf and wbufq
	pthread_mutex_t rm_; 		// protect rbuf and rbufq

	// read msg to rbuffer, may not complete a msg
    bool read_msg() {
		// printf("---Connection::read_msg---\n");
		// allocate new buffer for new message
		if (!rbuf.sz) {
			uint32_t sz, sz_raw;
			int n = read(fd_, &sz_raw, sizeof(sz_raw));

			if (n == 0) return false;
			if (n < 0) {
				VERIFY(errno!=EAGAIN);
				return false;
			}
			if (n > 0 && n!= sizeof(sz)) {
				printf("Connection::read_msg(fd_ %d) short read of sz\n", fd_);
				return false;
			}

			// network to host
			sz = ntohl(sz_raw);

			if (sz > MAX_MSG_SZ) {
				char *tmpb = (char *)&sz_raw;
				printf("Connection::read_msg(fd_ %d) read msg TOO BIG %d network order=%x %x %x %x %x\n", fd_, sz, 
						sz_raw, tmpb[0],tmpb[1],tmpb[2],tmpb[3]);
				return false;
			}

			rbuf.sz = sz;
			VERIFY(rbuf.buf == NULL);
			rbuf.buf = (char *)malloc(sz);
			VERIFY(rbuf.buf);
			bcopy(&sz_raw, rbuf.buf, sizeof(sz));
			rbuf.solong = sizeof(sz);
		}

		// read data
		// printf("Connection::read_msg try to read %d byte buffer from fd %d\n", (rbuf.sz - rbuf.solong), fd_);
		int n = read(fd_, rbuf.buf + rbuf.solong, rbuf.sz - rbuf.solong);
		// printf("Connection::read_msg read %d bytes\n", n);
		if (n <= 0) {
			if (errno == EAGAIN) return true;

			// reset rbuf for next message
			printf("Connection::read_msg(fd_ %d) failure, errno = %d\n", fd_, errno);
			return false;
		}
		rbuf.solong += n;
		return true;
	}

	// write wbuffer to a msg, may not complete a msg
    bool write_msg() {
		// printf("---Connection::write_msg---\n");
		VERIFY(wbuf.buf);
		VERIFY(wbuf.sz);
		if (wbuf.sz == wbuf.solong) return true;    // already finished

		// host to network
		if (wbuf.solong == 0) {
			int sz = htonl(wbuf.sz);
			bcopy(&sz, wbuf.buf,sizeof(sz));
		}

		// write data
		// printf("Connection::write_msg before write, wbuf.buf[7] = %x\n", wbuf.buf[7]);
		int n = write(fd_, wbuf.buf + wbuf.solong, (wbuf.sz - wbuf.solong));
		// printf("Connection::write_msg write %d bytes\n", n);
		if (n < 0) {
			if (errno != EAGAIN) {
				printf("Connection::write_msg(fd_ %d) failure, errno = %d\n", fd_, errno);
				wbuf.solong = -1;
				wbuf.sz = 0;
			}
			return (errno == EAGAIN);
		}
		wbuf.solong += n;
		if (wbuf.sz == wbuf.solong) {
			free(wbuf.buf);
			wbuf.reset();
		}
		return true;
	}
		
	// get the fd_set(only include fd_) of connection
	fd_set get_fd_set() {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd_, &rfds);
		return rfds;
	}		

public:
	Connection(int fd): fd_(fd), dead_(false) {
		VERIFY(pthread_mutex_init(&m_,0) == 0);
		VERIFY(pthread_mutex_init(&wm_,0) == 0);
		VERIFY(pthread_mutex_init(&rm_,0) == 0);
	}

	// for creating Connection to certain addr
	Connection(const sockaddr_in &dst) {
		int s= socket(AF_INET, SOCK_STREAM, 0);
		int yes = 1;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
		if(connect(s, (sockaddr*)&dst, sizeof(dst)) < 0) {
			printf("Connection::connect_to_dst failed to connect to %s:%d\n", inet_ntoa(dst.sin_addr), (int)ntohs(dst.sin_port));
			close(s);
			fd_ = -1;
			dead_ = true;
			return;
		}
		// printf("connect_to_dst fd=%d to dst %s:%d\n", s, inet_ntoa(dst.sin_addr), (int)ntohs(dst.sin_port));
		
		fd_ = s;
		dead_ = false;
		VERIFY(pthread_mutex_init(&m_,0) == 0);
		VERIFY(pthread_mutex_init(&wm_,0) == 0);
		VERIFY(pthread_mutex_init(&rm_,0) == 0);
	}

	~Connection() {
		// free buffer
		wbuf.clear();
		rbuf.clear();
		for (size_t i = 0; i < wbufq.size(); i++) {
			wbufq.front().clear();
			wbufq.pop();
		}
		for (size_t i = 0; i < rbufq.size(); i++) {
			rbufq.front().clear();
			rbufq.pop();
		}

		// close connection
		closeCh();
		// free mutex
		VERIFY(pthread_mutex_destroy(&m_) == 0);
		VERIFY(pthread_mutex_destroy(&wm_) == 0);
		VERIFY(pthread_mutex_destroy(&rm_) == 0);
	}

	void closeCh() {
		ScopedLock lock(&m_);
		close(fd_);
		dead_ = true;
	}

	bool is_dead() {return dead_;}	// if connection has ended
	int channo() {return fd_;}		// connetion fd_			

	// if wbuf and wbufq is empty
	bool empty_wbuf() {
		ScopedLock lock(&wm_);
		return (wbuf.empty() && wbufq.empty());
	}
		
	// rbuf size
	size_t rbuf_cnt() {
		ScopedLock lock(&rm_);
		return rbufq.size();
	}

	// wbuf size
	size_t wbuf_cnt() {
		ScopedLock lock(&wm_);
		return wbufq.size();
	}

	// consume next rbuf
	buffer next_rbuf() {
		ScopedLock lock(&rm_);
		buffer buf = rbufq.front();
		rbufq.pop();
		return buf;
	}

	// consume next wbuf
	buffer next_wbuf() {
		ScopedLock lock(&wm_);
		buffer buf = wbufq.front();
		wbufq.pop();
		return buf;
	}

	// produce next rbuf
	void add_rbuf(buffer buf) {
		ScopedLock lock(&rm_);
		rbufq.push(buf);
	}

	// produce next wbuf
	void add_wbuf(buffer buf) {
		ScopedLock lock(&wm_);
		wbufq.push(buf);
	}

	// fd_ is ready to be read
	void read_cb() {
		// printf("---Connection::read_cb---\n");
		if (dead_) return;

		// when socket is ready for read
		int cnt = 0;	// msg cnt
		while (cnt < MAX_MSG_CNT) {
			// non-blocking select to check read readiness
			fd_set rfds = get_fd_set();
			struct timeval timeout = {0,0};
			if (select(fd_ + 1, &rfds, NULL, NULL, &timeout) != 1) break;

			// read buffer
			ScopedLock cl(&m_);
			ScopedLock rl(&rm_);
			bool succ = true;
			if (rbuf.empty() || rbuf.solong < rbuf.sz)	// if rbuf is not filled yet
				succ = read_msg();

			if (!succ) {dead_ = true; break;}

			if (!rbuf.empty() && rbuf.sz == rbuf.solong) {
				cnt++;
				// enqueue
				rbufq.push(rbuf);
				// reset rbuf
				rbuf.reset();
			}
		}
	}

	// fd_ is ready to be write
	void write_cb() {
		// printf("---Connection::write_cb---\n");
		if (dead_) return;

		// when socket is ready for write
		int cnt = 0;	// msg cnt
		while (cnt < MAX_MSG_CNT) {
			if (empty_wbuf()) return;	// nothing to write
			if (wbuf.empty()) {
				wbuf = wbufq.front();
				wbuf.solong = 0;
				wbufq.pop();
			}

			// non-blocking select to check write readiness
			fd_set wfds = get_fd_set();
			struct timeval timeout = {0,0};
			if (select(fd_ + 1, NULL, &wfds, NULL, &timeout) != 1) break;

			// write buffer
			ScopedLock cl(&m_);
			ScopedLock wl(&wm_);
			bool succ = true;
			if (wbuf.buf && wbuf.solong < wbuf.sz)	// if wbuf is not finished yet
				succ = write_msg();

			if (!succ) {dead_ = true; break;}
		}
	}
		
	// send certain size of data
	bool send(char *buf, size_t sz) {
		// printf("---Connection::send(buf = %p, sz = %lu)---\n", buf, sz);
		{
			ScopedLock lock(&wm_);
			wbufq.push(buffer(buf, sz));
		}

		// try to send data
		write_cb();
		return empty_wbuf();
	}
};
