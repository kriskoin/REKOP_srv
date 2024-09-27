//*********************************************************
//
//	Low level IP routines.
//	Routines specific to the server end.
//
// 
//
//*********************************************************

#define DISP 0

#include "llip.h"
#include <fcntl.h>
#if INCL_SSL_SUPPORT
  #include <openssl/ssl.h>
  #include <openssl/crypto.h>
#endif

//****************************************************************
// 
//
// Constructor/destructor for the ServerSocket object
//
ServerSocket::ServerSocket(void)
{
	pr(("%s(%d) ServerSocket constructor called.\n",_FL));
	connected_flag = FALSE;	// set once we've completed a successful connection
	time_of_connect = SecondCounter;	// set time when this socket was first connected.
}

ServerSocket::~ServerSocket(void)
{
	pr(("%s(%d) ServerSocket destructor called.\n",_FL));
}

//****************************************************************
// 
//
// Constructor/destructor for the ServerListen object
//
ServerListen::ServerListen(void)
{
	pr(("%s(%d) ServerListen constructor called.\n",_FL));
	listen_socket = INVALID_SOCKET;	// initialize to 'not open'.
  #if INCL_SSL_SUPPORT
	ssl_ctx = NULL;
  #endif
}

ServerListen::~ServerListen(void)
{
	pr(("%s(%d) ServerListen destructor called.\n",_FL));
	CloseListenSocket();
}

//****************************************************************
// 
//
//	Create a socket which can be used to listen for incoming
//	connections.
//	If TCP is the protocol, this function calls listen() to make
//	the resulting socket all ready for accept() calls.
//	If UDP is the protocol, the socket is connectionless and
//	you must use recvfrom() to start reading the data, then use
//	connect() to connect it to whoever was sending.
//	Returns 0 for success, else error code.
//	If successful, the resulting SOCKET is stored at the location
//	pointed to by *output_socket.
//
#if INCL_SSL_SUPPORT
ErrorType ServerListen::OpenListenSocket(short port)
{	return OpenListenSocket(port, NULL);	}	

ErrorType ServerListen::OpenListenSocket(short port, void *ssl_server_ctx)
#else
ErrorType ServerListen::OpenListenSocket(short port)
#endif
{
	CloseListenSocket();	// close previous one, if necessary.

  #if INCL_SSL_SUPPORT
	ssl_ctx = ssl_server_ctx;
  #endif

	// Step 1: create the socket
	listen_socket = socket(AF_INET,
			#if USE_TCP
				SOCK_STREAM,
			#else // use UDP
				SOCK_DGRAM,
			#endif
				0);
	if (listen_socket==INVALID_SOCKET) {
		Error(ERR_ERROR, "%s(%d) Error creating listen socket (%d)", _FL, WSAGetLastError());
		return ERR_ERROR;	// unable to create socket.
	}

	// Step 2: set some I/O options
  #if WIN32
	dword one = 1;
	int err = ioctlsocket(listen_socket, FIONBIO, &one);	// enable non-blocking mode.
  #else
	int err = fcntl(listen_socket, F_SETFL, O_NONBLOCK);	// enable non-blocking mode.
	if (err != -1) {
		err = 0;	// -1 is error, anything else is successfull.
	}
  #endif
	if (err) {
		Error(ERR_ERROR, "%s(%d) ioctrlsocket() for non-blocking I/O failed (%d)", _FL, WSAGetLastError());
		CloseListenSocket();
		return ERR_ERROR;
	}

  #if USE_TCP
	// Step 3: set some socket options (only for TCP)
	// Enable address re-use for this socket (so multiple connections
	// can come into the same address).  This must be done before bind().
	BOOL reuse_option_val = 1;
	err = setsockopt (listen_socket,
			SOL_SOCKET,					// level
			SO_REUSEADDR,				// option name
			(char *)&reuse_option_val,	// pointer to option value
			sizeof(reuse_option_val));	// size of option value
	if (err) {
		Error(ERR_ERROR, "%s(%d) setsockopt() for REUSEADDR failed (%d)", _FL, WSAGetLastError());
		CloseListenSocket();
		return ERR_ERROR;
	}
  #endif // USE_TCP

	// Step 4: bind it.
	SOCKADDR_IN sin;
	zstruct(sin);
	sin.sin_family = AF_INET;
  #if 1	//kriskoin: 	sin.sin_addr.s_addr = INADDR_ANY;
  #else
	sin.sin_addr.s_addr = 0;
  #endif
	sin.sin_port = htons(port);

	err = bind(listen_socket, (LPSOCKADDR)&sin, sizeof(sin));
	if (err) {
		Error(ERR_ERROR, "%s(%d) Error binding to port (%d)", _FL, WSAGetLastError());
		CloseListenSocket();
		return ERR_ERROR;	// unable to bind to port.
	}

  #if USE_TCP
	// Step 5: enable listening on this socket... (only for TCP)
	err = listen(listen_socket, SOMAXCONN);
	if (err) {
		Error(ERR_ERROR, "%s(%d) Error telling socket to listen (%d)",_FL, WSAGetLastError());
		CloseListenSocket();
		return ERR_ERROR;
	}
  #endif // USE_TCP
	return ERR_NONE;	// success.
}

//****************************************************************
// 
//
//	Close the listening socket (if it's open).
//
ErrorType ServerListen::CloseListenSocket(void)
{
	if (listen_socket != INVALID_SOCKET) {
		shutdown(listen_socket, 2);	//kriskoin: 		closesocket(listen_socket);
		listen_socket = INVALID_SOCKET;
	}
	return ERR_NONE;
}

//****************************************************************
// 
//
// Accept an incoming connection (if available) and create a
// ServerSocket object to go with it.  No communication is actually
// done with the client.
//
ErrorType ServerListen::AcceptConnection(ServerSocket **output_server_socket)
{
	if (!output_server_socket) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) AcceptConnection passed a NULL output_server_socket",_FL);
		return ERR_INTERNAL_ERROR;	// error - no dest address passed.
	}
	*output_server_socket = NULL;	// initialize output to NULL.

	if (listen_socket==INVALID_SOCKET) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) AcceptConnection called with no listen_port", _FL);
		return ERR_INTERNAL_ERROR;
	}

	SOCKET new_socket = INVALID_SOCKET;
	SOCKADDR_IN sin;
	zstruct(sin);
	int sin_len = sizeof(sin);
	new_socket = accept(listen_socket, (sockaddr *)&sin,
		  #if WIN32	// 2022 kriskoin
			&sin_len
		  #else
			(unsigned *)&sin_len
		  #endif
		);
	if (new_socket == INVALID_SOCKET) {
		int err = WSAGetLastError();
		// 24/01/01 kriskoin:
		// we seem to get this error (104) about every 30 minutes under normal usage.
		// #define ECONNRESET      104     /* Connection reset by peer */
		if (err==WSAEWOULDBLOCK || err==WSAECONNRESET) {
			// Nobody is ready to connect yet.
			return ERR_NONE;	// no error, but no connection either.
		}

		// From /usr/src/linux/include/asm/errno.h:
		// #define ENOTCONN        107     /* Transport endpoint is not connected */

		Error(ERR_ERROR, "%s(%d) AcceptConnect() got back INVALID_SOCKET. Error=%d",_FL,WSAGetLastError());
	  #if !WIN32
		// This code was only tested for Linux.  I have no idea
		// what it might do under Windows.
		// The problem is quite serious and probably indicates
		// corrupted memory on the server.
		// Alert any administrators of the problem.
		// 24/01/01 kriskoin:
		// players suddenly disconnect (like more than 4000).  It seems to be
		// self-correcting once some new players log back in.  Further testing
		// should be done at some point.

		IssueCriticalAlert("accept() is returning INVALID_SOCKET. Memory might be corrupted.");
	  #endif

		return ERR_ERROR;
	}

  #if WIN32
	dword one = 1;
	ioctlsocket(new_socket, FIONBIO, &one);	// enable non-blocking mode.
  #else
	fcntl(new_socket, F_SETFL, O_NONBLOCK);	// enable non-blocking mode.
  #endif

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
		getsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&output, &bytes);
		kp(("%s(%d) nagle disabled = %d (size = %d bytes)\n", _FL, output, bytes));
	}
  #endif
	BOOL true_bool = TRUE;
	int err = setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&true_bool, sizeof(true_bool));
	if (err) {
		Error(ERR_WARNING, "%s(%d) setsockopt() to disable nagle failed  WSA error = %d", _FL, WSAGetLastError());
		// this is a non-fatal error... just keep going.
	}
  #if 0	// 2022 kriskoin
	{
		BOOL output = 0;
		int bytes = sizeof(BOOL);
		getsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&output, &bytes);
		kp(("%s(%d) nagle disabled = %d (size = %d bytes)\n", _FL, output, bytes));
	}
  #endif
#endif	// nagle disable

  #if 0	// 2022 kriskoin
	kp(("%s(%d) BEFORE 'new ServerSocket'\n",_FL));
	MemDisplayMemoryMap();
	*output_server_socket = new ServerSocket;
	kp(("%s(%d) AFTER  'new ServerSocket'\n",_FL));
	MemDisplayMemoryMap();
  #else
	*output_server_socket = new ServerSocket;
  #endif

	if (!*output_server_socket) {
		Error(ERR_ERROR, "%s(%d) Cannot create new ServerSocket object",_FL);
		shutdown(new_socket, 2);	//kriskoin: 		closesocket(new_socket);
		return ERR_ERROR;
	}

  #if INCL_SSL_SUPPORT
	if (ssl_ctx) {
		EnterSSLCritSec();
		SSL *ssl = SSL_new((SSL_CTX *)ssl_ctx);
		if (!ssl) {
			Error(ERR_ERROR, "%s(%d) Cannot create new SSL object",_FL);
			LeaveSSLCritSec();
			//kriskoin: 			// because the ServerSocket object doesn't know about it yet.
			shutdown(new_socket, 2);	//kriskoin: 			closesocket(new_socket);
			delete *output_server_socket;
			*output_server_socket = NULL;
			return ERR_ERROR;
		}
		SSL_set_fd(ssl, new_socket);

	  #if 0
		kp(("%s(%d) List of ciphers allowed by this server: (in order of preference)\n",_FL));
		int i = 0;
		const char *c = NULL;
		do {
			c = SSL_get_cipher_list((SSL *)ssl, i);
			if (c) {
				kp(("%s(%d) Cipher #%2d: %s\n", _FL, i+1, c));
			}
			i++;
		} while(c);
	  #endif

		//kriskoin: 		// complete on a 700MHz P3 Xeon, even though it's supposed to be
		// non-blocking.  Further tests need to be done to see if network
		// latency makes this problem worse or if it makes no difference at all.
		// More info: it seems to be totally cpu load and has nothing to do with
		// network latency.  It also seems related to the client requesting EDH-RSA
		// for public key exchange.  Straight EDH seems to complete in about 1ms.
		//WORD32 start_ticks = GetTickCount();
		int err = SSL_accept(ssl);
		//kp(("%s(%d) SSL_accept has returned with %d (elapsed time = %dms)\n", _FL, err, GetTickCount()-start_ticks));
		if (err <= 0) {
			err = SSL_get_error(ssl, err);
			// The accept might just be pending... don't flag an error in those
			// pending situations.
			if (   err != SSL_ERROR_NONE
				&& err != SSL_ERROR_WANT_WRITE
				&& err != SSL_ERROR_WANT_READ
				&& err != SSL_ERROR_WANT_X509_LOOKUP
			) {
			  #if WIN32
				kp(("SSL_accept() failed. No common ciphers? SSL err=%d, WSAGetLastError()=%d\n", err, WSAGetLastError()));
			  #else
				//kp(("SSL_accept() failed. No common ciphers? SSL err=%d, errno=%d\n", err, errno));
			  #endif
				LeaveSSLCritSec();
				//kriskoin: 				// because the ServerSocket object doesn't know about it yet.
				shutdown(new_socket, 2);	//kriskoin: 				closesocket(new_socket);
				delete *output_server_socket;
				*output_server_socket = NULL;
				return ERR_ERROR;
			}
		}
		(*output_server_socket)->SetSSLSocket((void *)ssl);
		LeaveSSLCritSec();
	} else
  #endif	// INCL_SSL_SUPPORT
	{
		(*output_server_socket)->SetSocket(new_socket);
	}
	(*output_server_socket)->SetConnectionAddress(&sin);

  #if 0	// 2022 kriskoin
	kp(("%s(%d) AFTER  SSL setup stuff...\n",_FL));
	MemDisplayMemoryMap();
  #endif

  #if DISP
	// We're connected to someone.  Log the IP address.
	char str[20];
	IP_ConvertIPtoString(sin.sin_addr.s_addr, str, 20);
	pr(("%s(%d) **** ServerListen accepted an IP connection from %s:%d (socket=$%08lx) ****\n",_FL, str, ntohs((*output_server_socket)->connection_address.sin_port),new_socket));
  #endif

	(*output_server_socket)->SendVersionPacket();	// Send our version packet as the first packet out.

	return ERR_NONE;
}
