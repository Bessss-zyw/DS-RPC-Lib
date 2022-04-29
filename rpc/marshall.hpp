#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <stdlib.h>
#include <string.h>
#include <cstddef>
#include <inttypes.h>
#include "utils/verify.h"
#include "utils/algorithm.h"

struct req_header {
	req_header(int r = 0, int p = 0, int c = 0, int s = 0):
		rid(r), proc(p), clt_id(c), srv_id(s){}
	int rid;				// request id
	int proc;				// rpc code
	unsigned int clt_id;	// client id
	unsigned int srv_id;	// server id
};

struct reply_header {
	reply_header(int r = 0, int result = 0): rid(r), result(result) {}
	int rid;				// request id
	int result;				// rpc reply code
};

typedef uint64_t rpc_checksum_t;
typedef int rpc_sz_t;

enum {
	//size of initial buffer allocation 
	DEFAULT_RPC_SZ = 1024,
#if RPC_CHECKSUMMING
	//size of rpc_header includes a 4-byte int to be filled by tcpchan and uint64_t checksum
	RPC_HEADER_SZ = static_max<sizeof(req_header), sizeof(reply_header)>::value + sizeof(rpc_sz_t) + sizeof(rpc_checksum_t)
#else
		RPC_HEADER_SZ = static_max<sizeof(req_header), sizeof(reply_header)>::value + sizeof(rpc_sz_t)
#endif
};

class marshall {
	private:
		char *_buf;     // Base of the raw bytes buffer (dynamically readjusted)
		int _capa;      // Capacity of the buffer
		int _ind;       // Read/write head position

	public:
		marshall() {
			_buf = (char *) malloc(sizeof(char)*DEFAULT_RPC_SZ);
			VERIFY(_buf);
			memset(_buf, 0, DEFAULT_RPC_SZ);
			_capa = DEFAULT_RPC_SZ;
			_ind = RPC_HEADER_SZ;
		}

		~marshall() { 
			// if (_buf) free(_buf); 
		}

		int size() { return _ind;}
		char *cstr() { return _buf;}

		void rawbyte(unsigned char x) {
			if(_ind >= _capa){
				_capa *= 2;
				VERIFY (_buf != NULL);
				_buf = (char *)realloc(_buf, _capa);
				VERIFY(_buf);
			}
			_buf[_ind++] = x;
		}

		void rawbytes(const char *p, int n) {
			if((_ind+n) > _capa){
				_capa = _capa > n? 2*_capa:(_capa+n);
				VERIFY (_buf != NULL);
				_buf = (char *)realloc(_buf, _capa);
				VERIFY(_buf);
			}
			memcpy(_buf+_ind, p, n);
			_ind += n;
		}

		// Return the current content (excluding header) as a string
		std::string get_content() { 
			return std::string(_buf + RPC_HEADER_SZ, _ind - RPC_HEADER_SZ);
		}

		std::string get_all() {
			return std::string(_buf, _capa);
		}

		void pack(int x) {
			rawbyte((x >> 24) & 0xff);
			rawbyte((x >> 16) & 0xff);
			rawbyte((x >> 8) & 0xff);
			rawbyte(x & 0xff);
		}

		void pack_req_header(const req_header &h) {
			int saved_sz = _ind;
			//leave the first 4-byte empty for channel to fill size of pdu
			_ind = sizeof(rpc_sz_t); 
#if RPC_CHECKSUMMING
			_ind += sizeof(rpc_checksum_t);
#endif
			pack(h.rid);
			pack(h.proc);
			pack((int)h.clt_id);
			pack((int)h.srv_id);
			_ind = saved_sz;
		}

		void pack_reply_header(const reply_header &h) {
			int saved_sz = _ind;
			//leave the first 4-byte empty for channel to fill size of pdu
			_ind = sizeof(rpc_sz_t); 
#if RPC_CHECKSUMMING
			_ind += sizeof(rpc_checksum_t);
#endif
			pack(h.rid);
			pack(h.result);
			_ind = saved_sz;
		}

		void take_buf(char **b, int *s) {
			*b = _buf;
			*s = _ind;
			_buf = NULL;
			_ind = 0;
			return;
		}

		// -----operators-----
		marshall &
		operator<<(bool x)
		{
			rawbyte(x);
			return *this;
		}

		marshall &
		operator<<(unsigned char x)
		{
			rawbyte(x);
			return *this;
		}

		marshall &
		operator<<(char x)
		{
			*this << (unsigned char) x;
			return *this;
		}


		marshall &
		operator<<(unsigned short x)
		{
			rawbyte((x >> 8) & 0xff);
			rawbyte(x & 0xff);
			return *this;
		}

		marshall &
		operator<<(short x)
		{
			*this << (unsigned short) x;
			return *this;
		}

		marshall &
		operator<<(unsigned int x)
		{
			// network order is big-endian
				// lab7: write marshall code for unsigned int type here
			rawbyte((x >> 24) & 0xff);
			rawbyte((x >> 16) & 0xff);
			rawbyte((x >> 8) & 0xff);
			rawbyte(x & 0xff);
			return *this;
		}

		marshall &
		operator<<(int x)
		{
			*this << (unsigned int) x;
			return *this;
		}

		marshall &
		operator<<(const std::string &s)
		{
			*this << (unsigned int) s.size();
			rawbytes(s.data(), s.size());
			return *this;
		}

		marshall &
		operator<<(unsigned long long x)
		{
			*this << (unsigned int) (x >> 32);
			*this << (unsigned int) x;
			return *this;
		}

		marshall &
		operator<<(uint64_t x)
		{
			*this << (unsigned int) (x >> 32);
			*this << (unsigned int) x;
			return *this;
		}

		template <class C> marshall &
		operator<<(std::vector<C> v)
		{
			*this << (unsigned int) v.size();
			for(unsigned i = 0; i < v.size(); i++)
				*this << v[i];
			return *this;
		}

		template <class A, class B> marshall &
		operator<<(const std::map<A,B> &d) {
			typename std::map<A,B>::const_iterator i;

			*this << (unsigned int) d.size();

			for (i = d.begin(); i != d.end(); i++) {
				*this << i->first << i->second;
			}
			return *this;
		}
};
// marshall& operator<<(marshall &, bool);
// marshall& operator<<(marshall &, unsigned int);
// marshall& operator<<(marshall &, int);
// marshall& operator<<(marshall &, unsigned char);
// marshall& operator<<(marshall &, char);
// marshall& operator<<(marshall &, unsigned short);
// marshall& operator<<(marshall &, short);
// marshall& operator<<(marshall &, unsigned long long);
// marshall& operator<<(marshall &, uint64_t);
// marshall& operator<<(marshall &, const std::string &);

class unmarshall {
	private:
		char *_buf;
		int _sz;
		int _ind;
		bool _ok;
	public:
		unmarshall(): _buf(NULL),_sz(0),_ind(0),_ok(false) {}
		unmarshall(char *b, int sz): _buf(b),_sz(sz),_ind(),_ok(true) {}
		unmarshall(const std::string &s) : _buf(NULL),_sz(0),_ind(0),_ok(false) 
		{
			//take the content which does not exclude a RPC header from a string
			take_content(s);
		}
		~unmarshall() {
			// if (_buf) free(_buf);
		}

		// take the contents from another unmarshall object
		void take_in(unmarshall &another) {
			if(_buf)
				free(_buf);
			another.take_buf(&_buf, &_sz);
			_ind = RPC_HEADER_SZ;
			_ok = _sz >= RPC_HEADER_SZ?true:false;
		}

		//take the content which does not exclude a RPC header from a string
		void take_content(const std::string &s) {
			_sz = s.size() + RPC_HEADER_SZ;
			_buf = (char *)realloc(_buf,_sz);
			VERIFY(_buf);
			_ind = RPC_HEADER_SZ;
			memcpy(_buf+_ind, s.data(), s.size());
			_ok = true;
		}

		bool ok() { return _ok; }
		char *cstr() { return _buf;}

		bool okdone() {
			if(ok() && _ind == _sz){
				return true;
			} else {
				return false;
			}
		}


		unsigned int rawbyte() {
			char c = 0;
			if(_ind >= _sz)
				_ok = false;
			else
				c = _buf[_ind++];
			return c;
		}

		void rawbytes(std::string &ss, unsigned int n) {
			if((_ind+n) > (unsigned)_sz){
				_ok = false;
			} else {
				std::string tmps = std::string(_buf+_ind, n);
				swap(ss, tmps);
				VERIFY(ss.size() == n);
				_ind += n;
			}
		}

		int ind() { return _ind;}
		int size() { return _sz;}

		void unpack(int *x) {	//non-const ref
			(*x) = (rawbyte() & 0xff) << 24;
			(*x) |= (rawbyte() & 0xff) << 16;
			(*x) |= (rawbyte() & 0xff) << 8;
			(*x) |= rawbyte() & 0xff;
		}

		void take_buf(char **b, int *sz) {
			*b = _buf;
			*sz = _sz;
			_sz = _ind = 0;
			_buf = NULL;
		}

		void unpack_req_header(req_header *h) {
			//the first 4-byte is for channel to fill size of pdu
			_ind = sizeof(rpc_sz_t); 
#if RPC_CHECKSUMMING
			_ind += sizeof(rpc_checksum_t);
#endif
			unpack(&h->rid);
			unpack(&h->proc);
			unpack((int *)&h->clt_id);
			unpack((int *)&h->srv_id);
			_ind = RPC_HEADER_SZ;
		}

		void unpack_reply_header(reply_header *h) {
			//the first 4-byte is for channel to fill size of pdu
			_ind = sizeof(rpc_sz_t); 
#if RPC_CHECKSUMMING
			_ind += sizeof(rpc_checksum_t);
#endif
			unpack(&h->rid);
			unpack(&h->result);
			_ind = RPC_HEADER_SZ;
		}

		// -----operators-----
		unmarshall &
		operator>>(bool &x)
		{
			x = (bool) rawbyte() ;
			return *this;
		}

		unmarshall &
		operator>>(unsigned char &x)
		{
			x = (unsigned char) rawbyte() ;
			return *this;
		}

		unmarshall &
		operator>>(char &x)
		{
			x = (char) rawbyte();
			return *this;
		}


		unmarshall &
		operator>>(unsigned short &x)
		{
			x = (rawbyte() & 0xff) << 8;
			x |= rawbyte() & 0xff;
			return *this;
		}

		unmarshall &
		operator>>(short &x)
		{
			x = (rawbyte() & 0xff) << 8;
			x |= rawbyte() & 0xff;
			return *this;
		}

		unmarshall &
		operator>>(unsigned int &x)
		{
				// lab7: write marshall code for unsigned int type here
			x = (rawbyte() & 0xff) << 24;
			x |= (rawbyte() & 0xff) << 16;
			x |= (rawbyte() & 0xff) << 8;
			x |= rawbyte() & 0xff;
			return *this;
		}

		unmarshall &
		operator>>(int &x)
		{
			x = (rawbyte() & 0xff) << 24;
			x |= (rawbyte() & 0xff) << 16;
			x |= (rawbyte() & 0xff) << 8;
			x |= rawbyte() & 0xff;
			return *this;
		}

		unmarshall &
		operator>>(unsigned long long &x)
		{
			unsigned int h, l;
			*this >> h;
			*this >> l;
			x = l | ((unsigned long long) h << 32);
			return *this;
		}

		unmarshall &
		operator>>(uint64_t &x)
		{
			unsigned int h, l;
			*this >> h;
			*this >> l;
			x = l | ((uint64_t) h << 32);
			return *this;
		}

		unmarshall &
		operator>>(std::string &s)
		{
			unsigned sz;
			*this >> sz;
			if(ok())
				rawbytes(s, sz);
			return *this;
		}

		template <class C> unmarshall &
		operator>>(std::vector<C> &v)
		{
			unsigned n;
			*this >> n;
			for(unsigned i = 0; i < n; i++){
				C z;
				*this >> z;
				v.push_back(z);
			}
			return *this;
		}

		template <class A, class B> unmarshall &
		operator>>(std::map<A,B> &d) {
			unsigned int n;
			*this >> n;

			d.clear();

			for (unsigned int lcv = 0; lcv < n; lcv++) {
				A a;
				B b;
				*this >> a >> b;
				d[a] = b;
			}
			return *this;
		}
};

// unmarshall& operator>>(unmarshall &, bool &);
// unmarshall& operator>>(unmarshall &, unsigned char &);
// unmarshall& operator>>(unmarshall &, char &);
// unmarshall& operator>>(unmarshall &, unsigned short &);
// unmarshall& operator>>(unmarshall &, short &);
// unmarshall& operator>>(unmarshall &, unsigned int &);
// unmarshall& operator>>(unmarshall &, int &);
// unmarshall& operator>>(unmarshall &, unsigned long long &);
// unmarshall& operator>>(unmarshall &, uint64_t &);
// unmarshall& operator>>(unmarshall &, std::string &);

















