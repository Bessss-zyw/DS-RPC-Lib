#include "utils/verify.h"
#include "utils/slock.h"
#include "connection.h"

connection::connection(int fd)
    :fd_(fd), dead_(false)
{
	VERIFY(pthread_mutex_init(&m_,0) == 0);
	VERIFY(pthread_mutex_init(&wm_,0) == 0);
	VERIFY(pthread_mutex_init(&rm_,0) == 0);
}
 
connection::~connection()
{
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

bool
connection::is_dead()
{
	return dead_;
}

int
connection::channo()
{
	return fd_;
}

void
connection::closeCh()
{
	ScopedLock lock(&m_);
	close(fd_);
}

bool 
connection::empty_wbuf()
{
	ScopedLock lock(&wm_);
	return (wbuf.empty() && wbufq.empty());
}

size_t
connection::rbuf_cnt()
{
	ScopedLock lock(&rm_);
	return rbufq.size();
}

size_t
connection::wbuf_cnt()
{
	ScopedLock lock(&wm_);
	return wbufq.size();
}

buffer
connection::next_rbuf()
{
	ScopedLock lock(&rm_);
	buffer buf = rbufq.front();
	rbufq.pop();
	return buf;
}

buffer
connection::next_wbuf()
{
	ScopedLock lock(&wm_);
	buffer buf = wbufq.front();
	wbufq.pop();
	return buf;
}

void
connection::add_rbuf(buffer buf)
{
	ScopedLock lock(&rm_);
	rbufq.push(buf);
}

void
connection::add_wbuf(buffer buf)
{
	ScopedLock lock(&wm_);
	wbufq.push(buf);
}

bool
connection::send(char *buf, size_t sz)
{
	printf("---connection::send(buf = %p, sz = %lu)---\n", buf, sz);
	{
		ScopedLock lock(&wm_);
		wbufq.push(buffer(buf, sz));
	}

	// try to send data
	write_cb();
	return empty_wbuf();
}

void 
connection::read_cb()
{
	printf("---connection::read_cb---\n");
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

void
connection::write_cb()
{
	printf("---connection::write_cb---\n");
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

bool 
connection::read_msg()
{
	printf("---connection::read_msg---\n");
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
			printf("connection::read_msg short read of sz\n");
			return false;
		}

        // network to host
		sz = ntohl(sz_raw);

		if (sz > MAX_MSG_SZ) {
			char *tmpb = (char *)&sz_raw;
			printf("connection::read_msg read msg TOO BIG %d network order=%x %x %x %x %x\n", sz, 
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
	printf("connection::read_msg try to read %d byte buffer from fd %d\n", (rbuf.sz - rbuf.solong), fd_);
	int n = read(fd_, rbuf.buf + rbuf.solong, rbuf.sz - rbuf.solong);
	printf("connection::read_msg read %d bytes\n", n);
	if (n <= 0) {
		if (errno == EAGAIN) return true;

        // reset rbuf for next message
		printf("connection::read_msg fd_ %d failure errno = %d\n", fd_, errno);
        return false;
	}
	rbuf.solong += n;
	return true;
}

bool 
connection::write_msg()
{
	printf("---connection::write_msg---\n");
    VERIFY(wbuf.buf);
    VERIFY(wbuf.sz);
    if (wbuf.sz == wbuf.solong) return true;    // already finished

    // host to network
	if (wbuf.solong == 0) {
		int sz = htonl(wbuf.sz);
		bcopy(&sz, wbuf.buf,sizeof(sz));
	}

    // write data
	printf("before write, wbuf.buf[7] = %x\n", wbuf.buf[7]);
	int n = write(fd_, wbuf.buf + wbuf.solong, (wbuf.sz - wbuf.solong));
	printf("connection::write_msg write %d bytes\n", n);
	if (n < 0) {
		if (errno != EAGAIN) {
			printf("connection::write_msg fd_ %d failure errno=%d\n", fd_, errno);
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

fd_set
connection::get_fd_set()
{
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(fd_, &rfds);
	return rfds;
}

// for creating connection to certain addr
connection *
connect_to_dst(const sockaddr_in &dst)
{
	int s= socket(AF_INET, SOCK_STREAM, 0);
	int yes = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
	if(connect(s, (sockaddr*)&dst, sizeof(dst)) < 0) {
		printf("rpcc::connect_to_dst failed to %s:%d\n", inet_ntoa(dst.sin_addr), (int)ntohs(dst.sin_port));
		close(s);
		return NULL;
	}
	printf("connect_to_dst fd=%d to dst %s:%d\n", s, inet_ntoa(dst.sin_addr), (int)ntohs(dst.sin_port));
	return new connection(s);
}