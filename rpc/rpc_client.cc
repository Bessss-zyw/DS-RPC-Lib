#include "rpc_client.h"
#include "utils/slock.h"

caller::caller(unsigned int id, unmarshall *xun)
: rid(id), un(xun), done(false)
{
	VERIFY(pthread_mutex_init(&m,0) == 0);
	VERIFY(pthread_cond_init(&c, 0) == 0);
}

caller::~caller()
{
	VERIFY(pthread_mutex_destroy(&m) == 0);
	VERIFY(pthread_cond_destroy(&c) == 0);
}

void *poll_thread(void *arg)
{
    rpcc *c = (rpcc *)arg;
	while (1) {
    	c->poll_and_push();
		if (errno == EINTR) break;
	}
    return NULL;
}

rpcc::rpcc(const char *host, const char *port)
    :rid_(1), sid_(0), bind_done_(false)
{
    // parse address
	in_addr_t a;
	bzero(&dst_, sizeof(dst_));
	dst_.sin_family = AF_INET;
	a = inet_addr(host);
	if(a != INADDR_NONE){
		dst_.sin_addr.s_addr = a;
	} else {
		struct hostent *hp = gethostbyname(host);
		if(hp == 0 || hp->h_length != 4){
			fprintf(stderr, "cannot find host name %s\n", host);
			exit(1);
		}
		dst_.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	}
	dst_.sin_port = htons(atoi(port));

    // initialize mutex
    VERIFY(pthread_mutex_init(&m_, 0) == 0);
	VERIFY(pthread_mutex_init(&chan_m_, 0) == 0);

	// random client id
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	srandom((int)ts.tv_nsec^((int)getpid()));
	cid_ = random();
    
    // connect to target server
    ch = connect_to_dst(dst_);
    if (!ch) {
        printf("rpcc::rpcc fail to connect with remote addr\n");
        exit(0);
    }

    // create polling thread
    int err = pthread_create(&poll_th_, NULL, poll_thread, this);
	if (err != 0) {
		fprintf(stderr, "pthread_create ret %d %s\n", err, strerror(err));
		exit(1);
	}
}

rpcc::~rpcc()
{
    if (ch) ch->closeCh();
    VERIFY(pthread_mutex_destroy(&m_) == 0);
	VERIFY(pthread_mutex_destroy(&chan_m_) == 0);
}

int
rpcc::bind(TO to)
{
	int sid;
	int ret = call(rpc_const::bind, sid, to, 0);
	if(ret == 0){
		bind_done_ = true;      // must bind first
		sid_ = sid;
	} else {
		printf("rpcc::bind %s failed %d\n", inet_ntoa(dst_.sin_addr), ret);
	}
	return ret;
};

int
rpcc::call1(unsigned int proc, marshall &req, unmarshall &rep, TO to)
{
	printf("---rpcc::call1(proc = %x, to = %d)---\n", proc, to);

    // check bind
    if((proc != rpc_const::bind && !bind_done_) ||
            (proc == rpc_const::bind && bind_done_)){
        printf("rpcc::call1 rpcc has not been bound to dst or binding twice\n");
        return rpc_const::bind_failure;
    }

    // update meta info
    caller ca(0, &rep);
    ca.rid = rid_++;
    calls_[ca.rid] = &ca;
    struct timespec now, nextDDL, finalDDL; 
	clock_gettime(CLOCK_REALTIME, &now);
	add_timespec(now, to, &finalDDL);

    // pack header
    req_header h(ca.rid, proc, cid_, sid_);
    req.pack_req_header(h);

    // send msg to dst server
    VERIFY(ch);
    ch->send(req.cstr(), req.size());
    printf("rpcc::call1 [CLT %u] just sent req rid %u(proc %x)\n", cid_, ca.rid, proc); 

    // wait for reply
    while (!ca.done)
    {
        // set timeout
        clock_gettime(CLOCK_REALTIME, &now);
		add_timespec(now, rpc_const::to_min, &nextDDL); 
		if(cmp_timespec(nextDDL,finalDDL) > 0){
			nextDDL = finalDDL;
			finalDDL.tv_sec = 0;
		}

        printf("rpcc:call1: wait for reply\n");
        if(pthread_cond_timedwait(&ca.c, &ca.m, &nextDDL) == ETIMEDOUT){
            printf("rpcc::call1: timeout\n");
            // return rpc_const::timeout_failure;
        }
    }

    // clear caller
    ScopedLock ml(&m_);
    calls_.erase(ca.rid);

    printf("rpcc::call1: reply received\n");
    return ca.result;
}

void
rpcc::poll_and_push() {
    printf("---rpcc::poll_and_push--- on fd_set: (%d) \n", ch->channo());

	int fd_ = ch->channo();
	fd_set rfds;
	FD_SET(fd_, &rfds);

	int ret = select(fd_ + 1, &rfds, NULL, NULL, NULL);
	printf("%d socket ready...\n", ret);

	if (ret < 0) {
		if (errno == EINTR) {
			return;
		} else {
			perror("accept_conn select:");
			printf("tcpsconn::accept_conn failure errno %d\n",errno);
			VERIFY(0);
		}
	}

	if (FD_ISSET(fd_, &rfds)) {ch->read_cb();}
	// for each conn, process its rbuf queue
    for (size_t i = 0; i < ch->rbuf_cnt(); i++) {
        buffer buf = ch->next_rbuf();
        VERIFY(buf.sz == buf.solong);
        process_msg(ch, buf.buf, buf.sz);
    }
}

void
rpcc::process_msg(connection *c, char *buf, size_t sz)
{
	printf("---rpcc::process_msg(buf = %p, sz = %lu)---\n", buf, sz);
    // unpack header
    unmarshall rep(buf, sz);
	reply_header h;
	rep.unpack_reply_header(&h);

	if(!rep.ok()){
		printf("rpcc:got_pdu unmarshall header failed!!!\n");
		return;
	}

	ScopedLock ml(&m_);
	if(calls_.find(h.rid) == calls_.end()){
		printf("rpcc::got_pdu rid %d no pending request\n", h.rid);
		return;
	}
	caller *ca = calls_[h.rid];

    // unmarshall result and update caller
	ScopedLock cl(&ca->m);
	if(!ca->done){
		ca->un->take_in(rep);
		ca->result = h.result;
		if(ca->result < 0)
			printf("rpcc::got_pdu: RPC reply error for rid %d intret %d\n", h.rid, ca->result);
		ca->done = 1;
	}

    // finish the caller
	VERIFY(pthread_cond_broadcast(&ca->c) == 0);

	return;
}


// -------util functions-------
int
cmp_timespec(const struct timespec &a, const struct timespec &b)
{
	if(a.tv_sec > b.tv_sec)
		return 1;
	else if(a.tv_sec < b.tv_sec)
		return -1;
	else {
		if(a.tv_nsec > b.tv_nsec)
			return 1;
		else if(a.tv_nsec < b.tv_nsec)
			return -1;
		else
			return 0;
	}
}

void
add_timespec(const struct timespec &a, int b, struct timespec *result)
{
	// convert to millisec, add timeout, convert back
	result->tv_sec = a.tv_sec + b/1000;
	result->tv_nsec = a.tv_nsec + (b % 1000) * 1000000;
	VERIFY(result->tv_nsec >= 0);
	while (result->tv_nsec > 1000000000){
		result->tv_sec++;
		result->tv_nsec-=1000000000;
	}
}

int
diff_timespec(const struct timespec &end, const struct timespec &start)
{
	int diff = (end.tv_sec > start.tv_sec)?(end.tv_sec-start.tv_sec)*1000:0;
	VERIFY(diff || end.tv_sec == start.tv_sec);
	if(end.tv_nsec > start.tv_nsec){
		diff += (end.tv_nsec-start.tv_nsec)/1000000;
	} else {
		diff -= (start.tv_nsec-end.tv_nsec)/1000000;
	}
	return diff;
}
