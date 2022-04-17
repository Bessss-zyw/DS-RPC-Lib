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

#define MAX_MSG_SZ (10 << 20)    	// maximum MSG size is 10M
#define MAX_MSG_CNT 10				// maximum MSG number in a single read_cb/write_cb

struct buffer {
	char *buf;
	int sz;
	int solong; //amount of bytes written or read so far

	buffer(): buf(NULL), sz(0), solong(0) {}
	buffer (char *b, int s) : buf(b), sz(s), solong(0){}
	~buffer() {}
	bool empty() {
		if (!buf) {
			VERIFY(!sz && !solong);
			return true;
		} else {
			VERIFY(sz);
			return false;
		}
	}

    void reset() {
		buf = NULL;
		sz = solong = 0;
    }

	void clear() {
        if (buf) free(buf);
		reset();
	}
};

class connection {
	int fd_;
	bool dead_;
	buffer wbuf;    // curr write msg buffer
	buffer rbuf;    // curr read msg buffer
	std::queue<buffer> wbufq;    // write msg buffer queue
	std::queue<buffer> rbufq;    // read msg buffer queue

    bool read_msg();	// read msg to rbuffer, may not complete a msg
    bool write_msg();	// write wbuffer to a msg, may not complete a msg

	pthread_mutex_t m_; 	// protect channel
	pthread_mutex_t wm_; 	// protect wbuf and wbufq
	pthread_mutex_t rm_; 	// protect rbuf and rbufq

	fd_set get_fd_set();

public:
	connection(int fd);
	~connection();
	bool is_dead();
	int channo();				// connection num is the fd_

	bool empty_wbuf();			// if wbuf and wbufq is empty
	size_t rbuf_cnt();			// rbuf size
	size_t wbuf_cnt();			// wbuf size
	buffer next_rbuf();			// consume next rbuf
	buffer next_wbuf();			// consume next wbuf
	void add_rbuf(buffer buf);	// produce next rbuf
	void add_wbuf(buffer buf);	// produce next wbuf

	void read_cb();		// fd_ is ready to be read
	void write_cb();	// fd_ is ready to be write
	bool send(char *buf, size_t sz);	// send certain size of data
	void closeCh();
};

connection * connect_to_dst(const sockaddr_in &dst);

