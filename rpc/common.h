#pragma once

#include "marshall.hpp"

typedef int TO;		// timeout

class handler {
	public:
		handler() { }
		virtual ~handler() { }
		virtual int fn(unmarshall &, marshall &) = 0;
};

// consts for rpc
class rpc_const {
	public:
		// handler number reserved for bind
		static const unsigned int bind = 1;

		// error numbers
		static const int timeout_failure = -1;
		static const int unmarshal_args_failure = -2;
		static const int unmarshal_reply_failure = -3;
		static const int atmostonce_failure = -4;
		static const int oldsrv_failure = -5;
		static const int bind_failure = -6;
		static const int cancel_failure = -7;
		static const int unknown_proc = -8;

		// timeout limits
		static const int to_max = 120000;
		static const int to_min = 1000;
};
