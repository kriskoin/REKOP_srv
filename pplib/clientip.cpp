//*********************************************************
//
//	Low level IP routines.
//	Routines specific to the client end.
//
// 
//
//*********************************************************

#define DISP 0

#include "llip.h"
#include <fcntl.h>
#if INCL_SSL_SUPPORT
  #include <openssl/ssl.h>
#endif

//****************************************************************
// 
//
// Constructor/destructor for the ClientSocket object
//
ClientSocket::ClientSocket(void)
{
	pr(("%s(%d) ClientSocket constructor called.\n",_FL));
}

ClientSocket::~ClientSocket(void)
{
	pr(("%s(%d) ClientSocket destructor called.\n",_FL));
}


//****************************************************************
// 
//
//	Create a socket which can be used to call a remote host
//	and connect it to that host (given the IP address and port).
//	Return 0 for success, else error code.
//	This function does not retry to establish a connection.  It returns
//	immediately if the connection fails.
//
#if INCL_SSL_SUPPORT
ErrorType ClientSocket::ConnectSocket(IPADDRESS host_ip, short host_port)
{	return ConnectSocket(host_ip, host_port, NULL);  }	

ErrorType ClientSocket::ConnectSocket(IPADDRESS host_ip, short host_port, void *ssl_ctx)
#else
ErrorType ClientSocket::ConnectSocket(IPADDRESS host_ip, short host_port)
#endif
{
	// Step 1: create the socket
	sock = socket(AF_INET,
			#if USE_TCP
				SOCK_STREAM,
			#else // use UDP
				SOCK_DGRAM,
			#endif
				0);
	if (sock==INVALID_SOCKET) {
		Error(ERR_ERROR, "%s(%d) Error creating socket (%d)", _FL, WSAGetLastError());
		return ERR_ERROR;	// unable to create socket.
	}

	// Step 2: set some options
  #if WIN32
	dword one = 1;
	int err = ioctlsocket(sock, FIONBIO, &one);	// enable non-blocking mode.
  #else
	int err = fcntl(sock, F_SETFL, O_NONBLOCK);	// enable non-blocking mode.
	if (err != -1) {
		err = 0;	// -1 is error, anything else is successfull.
	}
  #endif
	if (err) {
		Error(ERR_ERROR, "%s(%d) ioctrlsocket() for non-blocking I/O failed.  WSA error = %d", _FL, WSAGetLastError());
		shutdown(sock, 2);	//kriskoin: 		closesocket(sock);
		sock = INVALID_SOCKET;
		return ERR_ERROR;
	}

#if 1	// 2022 kriskoin
	// Disable the nagle algorithm so that fast retransmits can occur.
	// The idea here is that the receiver won't do an early nak (fast retransmit
	// request) until it realizes it missed a packet.
	// It won't realize it missed a packet unless we actually send one.
	// We won't actually send another small one until the first one is ack'd
	// because that's what the nagle algorithm does.  Disabling it should solve
	// this problem at the expense of a little extra bandwidth.
	// Our program is designed so that each packet should always be sent
	// immediately (rather than batching them up), so this should not be a problem.
  #if 0	// 2022 kriskoin
	{
		BOOL output = 0;
		int bytes = sizeof(BOOL);
		getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&output, &bytes);
		kp(("%s(%d) nagle disabled = %d (size = %d bytes)\n", _FL, output, bytes));
	}
  #endif
	BOOL true_bool = TRUE;
	err = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&true_bool, sizeof(true_bool));
	if (err) {
		Error(ERR_WARNING, "%s(%d) setsockopt() to disable nagle failed  WSA error = %d", _FL, WSAGetLastError());
		// this is a non-fatal error... just keep going.
	}
  #if 0	// 2022 kriskoin
	{
		BOOL output = 0;
		int bytes = sizeof(BOOL);
		getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&output, &bytes);
		kp(("%s(%d) nagle disabled = %d (size = %d bytes)\n", _FL, output, bytes));
	}
  #endif
#endif	// nagle disable

	// Step 3: connect it.
	zstruct(connection_address);
	connection_address.sin_family = AF_INET;
	connection_address.sin_addr.s_addr = host_ip ? host_ip : 0x0100007f;	// use 127.0.0.1 if none specified
	connection_address.sin_port = htons(host_port);
	err = connect(sock, (LPSOCKADDR)&connection_address, sizeof(connection_address));
	if (err) {
		err = WSAGetLastError();
		// WSAEWOULDBLOCK is a Normal error condition...
		// the connect() is actually pending.
		if (err==WSAENOBUFS) {
			kp(("%s %s(%d) connect() error: no more sockets available.\n", TimeStr(), _FL));
		}

		if (err!=WSAEWOULDBLOCK) {
			Error(ERR_ERROR, "%s(%d) Error connecting to port (%d)", _FL, err);
			shutdown(sock, 2);	//kriskoin: 			closesocket(sock);
			sock = INVALID_SOCKET;
			return ERR_ERROR;
		}
	}

  #if INCL_SSL_SUPPORT
	// Set up the SSL stuff if necessary...
	if (ssl_ctx) {
		//kp(("%s(%d) Calling SSL_*() from thread %d\n", _FL, GetCurrentThreadId()));
		ssl = (SSL *)SSL_new((SSL_CTX *)ssl_ctx);
		if (!ssl) {
			Error(ERR_ERROR, "%s(%d) SSL_new() returned NULL!",_FL);
			shutdown(sock, 2);	//kriskoin: 			closesocket(sock);
			sock = INVALID_SOCKET;
			return ERR_ERROR;
		}
	  #if 0
		static int printed = FALSE;
		if (!printed) {
			printed = TRUE;
			kp(("%s(%d) List of ciphers allowed by this client: (in order of preference)\n",_FL));
			int i = 0;
			const char *c = NULL;
			do {
			  #if 0	// 2022 kriskoin
				char desc[200];
				SSL_CIPHER_description(cipher, desc, 200);
				kp(("Cipher #%2d: %s\n", i+1, desc));
			  #endif

			  #if 1	// 2022 kriskoin
				c = SSL_get_cipher_list((SSL *)ssl, i);
				if (c) {
					kp(("Cipher #%2d: %s\n", i+1, c));
				}
			  #endif
				i++;
			} while(c);
		}
	  #endif

		SSL_set_fd((SSL *)ssl, sock);
		SSL_connect((SSL *)ssl);
	}
  #endif

	connected_flag = FALSE;	// set once we've completed a successful connection
	time_of_connect = SecondCounter;	// set time when this socket was first connected.

	SendVersionPacket();	// Send our version packet as the first packet out.

	return ERR_NONE;
}
