#include "marshall.h"

void
marshall::rawbyte(unsigned char x)
{
	if(_ind >= _capa){
		_capa *= 2;
		VERIFY (_buf != NULL);
		_buf = (char *)realloc(_buf, _capa);
		VERIFY(_buf);
	}
	_buf[_ind++] = x;
}

void
marshall::rawbytes(const char *p, int n)
{
	if((_ind+n) > _capa){
		_capa = _capa > n? 2*_capa:(_capa+n);
		VERIFY (_buf != NULL);
		_buf = (char *)realloc(_buf, _capa);
		VERIFY(_buf);
	}
	memcpy(_buf+_ind, p, n);
	_ind += n;
}

marshall &
operator<<(marshall &m, bool x)
{
	m.rawbyte(x);
	return m;
}

marshall &
operator<<(marshall &m, unsigned char x)
{
	m.rawbyte(x);
	return m;
}

marshall &
operator<<(marshall &m, char x)
{
	m << (unsigned char) x;
	return m;
}


marshall &
operator<<(marshall &m, unsigned short x)
{
	m.rawbyte((x >> 8) & 0xff);
	m.rawbyte(x & 0xff);
	return m;
}

marshall &
operator<<(marshall &m, short x)
{
	m << (unsigned short) x;
	return m;
}

marshall &
operator<<(marshall &m, unsigned int x)
{
	// network order is big-endian
        // lab7: write marshall code for unsigned int type here
	m.rawbyte((x >> 24) & 0xff);
	m.rawbyte((x >> 16) & 0xff);
	m.rawbyte((x >> 8) & 0xff);
	m.rawbyte(x & 0xff);
	return m;
}

marshall &
operator<<(marshall &m, int x)
{
	m << (unsigned int) x;
	return m;
}

marshall &
operator<<(marshall &m, const std::string &s)
{
	m << (unsigned int) s.size();
	m.rawbytes(s.data(), s.size());
	return m;
}

marshall &
operator<<(marshall &m, unsigned long long x)
{
	m << (unsigned int) (x >> 32);
	m << (unsigned int) x;
	return m;
}

marshall &
operator<<(marshall &m, uint64_t x)
{
	m << (unsigned int) (x >> 32);
	m << (unsigned int) x;
	return m;
}

void
marshall::pack(int x)
{
	rawbyte((x >> 24) & 0xff);
	rawbyte((x >> 16) & 0xff);
	rawbyte((x >> 8) & 0xff);
	rawbyte(x & 0xff);
}

void
unmarshall::unpack(int *x)
{
	(*x) = (rawbyte() & 0xff) << 24;
	(*x) |= (rawbyte() & 0xff) << 16;
	(*x) |= (rawbyte() & 0xff) << 8;
	(*x) |= rawbyte() & 0xff;
}

// take the contents from another unmarshall object
void
unmarshall::take_in(unmarshall &another)
{
	if(_buf)
		free(_buf);
	another.take_buf(&_buf, &_sz);
	_ind = RPC_HEADER_SZ;
	_ok = _sz >= RPC_HEADER_SZ?true:false;
}

bool
unmarshall::okdone()
{
	if(ok() && _ind == _sz){
		return true;
	} else {
		return false;
	}
}

unsigned int
unmarshall::rawbyte()
{
	char c = 0;
	if(_ind >= _sz)
		_ok = false;
	else
		c = _buf[_ind++];
	return c;
}

unmarshall &
operator>>(unmarshall &u, bool &x)
{
	x = (bool) u.rawbyte() ;
	return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned char &x)
{
	x = (unsigned char) u.rawbyte() ;
	return u;
}

unmarshall &
operator>>(unmarshall &u, char &x)
{
	x = (char) u.rawbyte();
	return u;
}


unmarshall &
operator>>(unmarshall &u, unsigned short &x)
{
	x = (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, short &x)
{
	x = (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned int &x)
{
        // lab7: write marshall code for unsigned int type here
	x = (u.rawbyte() & 0xff) << 24;
	x |= (u.rawbyte() & 0xff) << 16;
	x |= (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, int &x)
{
	x = (u.rawbyte() & 0xff) << 24;
	x |= (u.rawbyte() & 0xff) << 16;
	x |= (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned long long &x)
{
	unsigned int h, l;
	u >> h;
	u >> l;
	x = l | ((unsigned long long) h << 32);
	return u;
}

unmarshall &
operator>>(unmarshall &u, uint64_t &x)
{
	unsigned int h, l;
	u >> h;
	u >> l;
	x = l | ((uint64_t) h << 32);
	return u;
}

unmarshall &
operator>>(unmarshall &u, std::string &s)
{
	unsigned sz;
	u >> sz;
	if(u.ok())
		u.rawbytes(s, sz);
	return u;
}

void
unmarshall::rawbytes(std::string &ss, unsigned int n)
{
	if((_ind+n) > (unsigned)_sz){
		_ok = false;
	} else {
		std::string tmps = std::string(_buf+_ind, n);
		swap(ss, tmps);
		VERIFY(ss.size() == n);
		_ind += n;
	}
}
