//*********************************************************
//
//	Low level IP routines for Poker server
//	Header file.
//
// 
//
//*********************************************************

#ifndef _LLIP_H_INCLUDED
#define _LLIP_H_INCLUDED

#define COMPRESS_PACKETS		1
#define ENABLE_SEND_QUEUING		1	// 0=send directly from ::SendPacket(), else queue it.
#define ENABLE_QUEUE_CRIT_SEC	0	// don't bother using critsecs on the queues... the whole thing is already protected by the player object.

#ifndef WIN32
  #include <sys/socket.h>
  #include <sys/errno.h>
  #include <sys/ioctl.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
#else
  #ifndef _WINSOCKAPI_
    #include <winsock.h>
  #endif
#endif
#ifndef _TIME_T_DEFINED
  #include <time.h>
#endif
#ifndef _PPLIB_H_INCLUDED
  #include "pplib.h"
#endif
#if COMPRESS_PACKETS
  #include "zlib.h"
#endif

#define USE_TCP	1	// if 1, use TCP, else use UDP as our transport protocol (not yet supported)

#define TCP_HEADER_OVERHEAD_AMOUNT	(20+20)	// # of bytes to add as an estimate for TCP and IP header overhead (20 bytes each)

#ifndef WIN32
// typedef's and defines to make socket.h stuff look like winsock.h stuff.
typedef int 				SOCKET;
typedef struct sockaddr		SOCKADDR;
typedef struct sockaddr_in	SOCKADDR_IN;
typedef SOCKADDR *			LPSOCKADDR;
#define INVALID_SOCKET		(-1)
#define SOCKET_ERROR		(-1)
#define WSAEWOULDBLOCK		EWOULDBLOCK
#define WSAENOTCONN			ENOTCONN
#define WSAECONNRESET		ECONNRESET
#define WSAECONNABORTED		ECONNABORTED
#define WSAESHUTDOWN		ESHUTDOWN
#define WSAEAGAIN			EAGAIN
#define WSAEHOSTUNREACH		EHOSTUNREACH
#define WSAEHOSTDOWN		EHOSTDOWN

#define closesocket(a)		close(a)

inline int WSAGetLastError(void)  {return errno;}
#endif // !WIN32

// Define the current local I/O Version.  See the
// SocketTool.remote_version variable to determine which version
// the remote program is running.
#define PACKET_IO_VERSION	(0x0100)

//	The Packet class is an object that represents and incoming or outgoing
//	packet of data.  They are created with 'new' and deleted with 'delete'.
//	Each incoming/outgoing packet must be unique and cannot be re-used.

#define MAX_PACKET_SIZE		25000	// 24/01/01 kriskoin:
#define PACKET_HEADER_SIG	(0xD3A4)
#if COMPRESS_PACKETS
  #define PACKET_HEADER_SIG_C	(0xD5A9)	// same but for compressed packets.
#endif

#if INCL_SSL_SUPPORT
#define EnterSSLCritSec()	EnterSSLCritSec0(_FL)
#define LeaveSSLCritSec()	LeaveSSLCritSec0(_FL)
void EnterSSLCritSec0(char *calling_file, int calling_line);
void LeaveSSLCritSec0(char *calling_file, int calling_line);
#endif

struct PacketHeader {
	WORD16 sig;	// signature ID for this packet (PACKET_HEADER_SIG)
	WORD16 len;	// total length of this packet (including this header).
};

class Packet {
public:
	Packet(void);
	~Packet(void);

	// Allocate space for holding the packet's data (pass the user data
	// size - anything needed internally by this class will be automatically
	// added to this amount).
	ErrorType Alloc(int amount);

	// Write data into the packet
	ErrorType WriteData(char *src_pointer, int src_len, int dest_offset);

	// Short-cut function for Allocating and Writing all in one go.  This
	// is only useful for packets which are made up of packets of data that
	// already exist in memory as one chunk.
	ErrorType AllocData(char *src_pointer, int src_len);

	// Set the # of bytes which are used in this packet's user
	// data section.  Automatically fills in the header length
	// and other lengths to compensate for the header, etc.
	void Packet::SetUserDataLength(int new_len);

	int user_data_length;	// Total # of user bytes in the packet
	char *user_data_ptr;	// Pointer to user data within the packet (or NULL)
	int max_length;			// Total # of bytes allocated in the packet buffer (including headers).
	int length;				// Total # of bytes written to the packet buffer (including headers).
	char *data_ptr;			// pointer to base of packet data (or NULL if not yet allocated)
	int packed_flag;		// set if this packet is compressed
	int packing_disabled;	// set if this packet should NOT be packed before sending.
	Packet *next_ptr;		// ptr to next Packet in queue (not used by the Packet object itself).

	WORD32 packet_serial_number;	// used for debugging only
	WORD32 desired_send_ticks;		// set to the GetTickCount() value when we want to send this packet (when doing latency testing).
	WORD32 length_when_received;	// receiving only: # of bytes read from socket (while compressed)

private:
};

//--------  Packet Pool functions --------
// The packet pool allocator functions are designed to re-use memory so that
// we can minimize the number of malloc()/free() calls and re-use the most
// recently freed packets first, that way we maximize the use of the
// L2 memory cache.  These functions are global so that the same pool is shared
// by all user connections.

// Free up any packets allocated and sitting in the packet pools...
// This can be called either at shutdown or any other time that
// you want everything to get freed up and start over.
void PktPool_FreePackets(void);

// Obtain a packet from the free packet pool or allocate a new one if necessary.
// The size passed is the minimum size needed for the user data area.
Packet *PktPool_Alloc(int minimum_size_needed);

// Add a packet back into the free packet pool for re-use by someone else.
// This is similar to deleting a packet... the caller is no longer allowed
// to use it after this function has been called.
void PktPool_ReturnToPool(Packet *pkt);

extern volatile WORD32 dwPacketSerialNumber;	// used for debugging only.
extern volatile WORD32 dwLivingPackets;			// # of packets still allocated.
extern volatile WORD32 dwPacketsAllocated;		// stat: total # of packets we've ever allocated in PktPool_Alloc()
extern volatile WORD32 dwPacketsConstructed;	// stat: total # of packets ever constructed (Packet constructor increments)

#define PKTPOOL_COUNT	6
extern struct PacketPool {
	int max_pool_size;	// # of packet ptrs allocated in the pool
	int packet_count;	// # of ptrs currently in the pool
	Packet **pool_ptr;	// ptr to the base of the packet ptr array
	int min_packet_size;// min packet size for this pool
	WORD32 alloc_count;	// # total of packets allocated from this pool (for stat purposes only)
} PktPools[PKTPOOL_COUNT];


#if COMPRESS_PACKETS
// The DiffEncoder class is common to both the client and server side.
// It is used to differentially encode outgoing packets.  It bases
// its key on the first byte of the user packet data, which happens
// to be the low byte of the packet type.

#define DIFFENCODER_MAX_PACKET_TYPES	16	// mask first byte by this-1 to get type.  Must be power of 2.

struct DiffEncoder_SavedPacketInfo {
	char *ptr;		// ptr to saved data (if any)
	int length;		// length of data at ptr
};

class DiffEncoder {
public:
	DiffEncoder(void);
	~DiffEncoder(void);

	// Encode a packet.
	ErrorType EncodePacket(Packet *p);

	// Decode a packet.
	ErrorType DecodePacket(Packet *p);

	// Reset the encoder state
	void ResetEncoder(void);

private:
	struct DiffEncoder_SavedPacketInfo SavedPackets[DIFFENCODER_MAX_PACKET_TYPES];
	struct DiffEncoder_SavedPacketInfo *SetupPacketInfo(Packet *p);
};
#endif // COMPRESS_PACKETS

//	The SocketTools class is common to both the client and server
//	side.  It contains some useful functions that are the same on
//	both ends.

#define START_CONNECTION_TIMEOUT	6	// # of seconds before we give up on new connections.

class SocketTools {
public:
	SocketTools(void);
	~SocketTools(void);

	//	Send any packets in the outgoing queue.
	ErrorType SocketTools::ProcessSendQueue(void);

	//	Read any incoming data as arriving packets and queue them up to be
	//  processed by ReadPacket().
	ErrorType SocketTools::ReadDataToQueue(void);

	//	Read any complete packets that have arrived at our socket.
	//	Does not block if nothing is ready.
	//	The caller is responsible for deleteing the returned Packet once
	//	they are finished with it.
	//	Return ERR_NONE for success, else error code.
	ErrorType ReadPacket(Packet **output_packet_ptr);

	//	Write a complete packet to an already open and connected socket.
	//	The packet will get deleted once delivered.  Don't use it again after
	//	calling SendPacket.  In fact, *packet_to_send will be set to NULL just
	//	to be sure.
	//	Does not block (even if the output queue is full).
	//	Return ERR_NONE for success, else error code.
	ErrorType SendPacket(Packet **packet_to_send, WORD32 *output_length);

	//	Send an array of bytes (chars) to an already open and connected socket.
	//	The bytes are sent as a packet and the other end will receive them as
	//	a complete packet.
	//	Return 0 for success, else error code.
	ErrorType SendBytes(char *buffer, int len, WORD32 *output_length);

	//	Send a string of ASCII text to an already open and connected socket.
	//	The NUL byte at the end of the string is also sent.
	//	The bytes are sent as a packet and the other end will receive them as
	//	a complete packet.
	//	Return 0 for success, else error code.
	ErrorType SendString(char *str);

	// Set the desired compression level (if necessary).
	// Zlib accepts compression levels between 0 (none) and 9 (most).
	void SocketTools::SetCompressionLevel(int new_level);

	// Set the socket to use for this object.
	// You can pass INVALID_SOCKET to close the socket.
	ErrorType SetSocket(SOCKET new_socket);
  #if INCL_SSL_SUPPORT
	ErrorType SetSSLSocket(void *ssl);
  #endif
	inline SOCKET GetSocket(void) {return sock;};

	// Set the connection address for this socket
	void SocketTools::SetConnectionAddress(SOCKADDR_IN *sin);

	// Close our socket if it's open.  Set sock to INVALID_SOCKET and
	// set the disconnected flag.
	void CloseSocket(void);

	// Send the initial version packet (called internally - not for public use).
	ErrorType SendVersionPacket(void);

  #if ENABLE_SEND_QUEUING
	// Purge (delete) all Packets in the Send Queue
	ErrorType PurgeSendQueue(void);

	// Determine if there is anything in the send queue
	// returns TRUE if send queue is empty.
	int SendQueueEmpty(void);
  #endif

	// Purge (delete) all Packets in the Receive Queue
	ErrorType PurgeReceiveQueue(void);

	SOCKET sock;						// the socket we're associated with.
	SOCKADDR_IN connection_address;		// address of who we're actually connected with (once connected)
	char   ip_str[20];					// ip address converted to text
	WORD32 total_bytes_sent;			// # of bytes sent by this SocketTools object (over TCP, updated by this object)
	WORD32 total_bytes_sent_resettable;	// same as total_bytes_sent but resettable to zero.
	WORD32 total_bytes_received;		// # of bytes received by this SocketTools object (over TCP, updated by this object)
	WORD32 total_bytes_received_resettable;	// same as total_bytes_received but resettable to zero.
	WORD32 total_packets_sent;			// # of packets we have sent (queued up)
	WORD32 total_packets_received;		// # of packets we have received (read from socket)
	WORD16 remote_version;				// version number of remote's connection library (also see PACKET_IO_VERSION)

	int connected_flag;					// set once we've completed a successful connection
	int disconnected;					// set if we've been disconnected.
	WORD32 time_of_connect;				// SecondCounter when this socket was first connected.
	WORD32 InitialConnectionTimeout;	// # of seconds to wait for initial connect before timing out.
	WORD32 time_of_disconnect;			// SecondCounter when this socket was first disconnected.
	WORD32 time_of_last_sent_packet;	// SecondCounter when last packet was sent (used for sending KEEP_ALIVEs)
	WORD32 time_of_last_received_packet;// SecondCounter when last packet was received

	int compression_level;				// current compression level (0-9)

	WORD32 next_send_attempt_ms;		// minimum GetTickCount() when sending should be attempted again

  #if INCL_SSL_SUPPORT
	void *ssl;
  #endif
private:
	//	Send an array of bytes (chars) to an already open and connected socket.
	//	No streaming/encryption or anything is performed, just the raw bytes are sent.
	//	Return 0 for success, else error code.
	ErrorType RawSendBytes(char *buffer, int len);

	//	Read any data that has arrived at our socket.
	//	Does not block if nothing is ready.
	//	No destreaming/decryption or anything is performed, just the raw bytes are read.
	//	Return ERR_NONE for success, else error code.
	ErrorType RawReadBytes(char *buffer, int buffer_len, int *output_bytes_read);

	//	This function gets called with WSAENOTCONN is detected.  It needs
	//	to determine if the connection has been lost to the remote machine,
	//	and if so, it needs to set the disconnected flag.
	void HandleNotConnErr(int err);

  #if COMPRESS_PACKETS
	// Compress a packet.  Takes a ptr to a ptr to a packet.  The
	// original packet is discarded and the ptr is set to the newly
	// compressed packet.
	ErrorType CompressPacket(Packet **p);

	// Decompress a packet.  Takes a ptr to a ptr to a packet.  The
	// original packet is discarded and the ptr is set to the newly
	// uncompressed packet.
	ErrorType DeCompressPacket(Packet **p);

	z_stream deflate_zs, inflate_zs;	// zstream's for compressing/decompressing

	DiffEncoder compress_encoder;
	DiffEncoder decompress_encoder;
  #endif	// COMPRESS_PACKETS

  #if ENABLE_SEND_QUEUING
	Packet *SendQueueHead, *SendQueueTail;
  #endif
	Packet *ReceiveQueueHead, *ReceiveQueueTail;

  #if ENABLE_QUEUE_CRIT_SEC
	PPCRITICAL_SECTION SendQueueCritSec;		// critsec for access to the send queue
	PPCRITICAL_SECTION ReceiveQueueCritSec;	// critsec for access to the receive queue
  #endif

	int receive_state;	// 0=waiting for Packet header, 1=waiting for Packet data.
	int received_bytes;	// current # of bytes received for either header or packet data (depending on receive_state)
	struct PacketHeader received_header;	// current PacketHeader that we're building up.
	Packet *received_packet;	// ptr to current packet that we're building up (if any).
};

// Client-specific socket tools.
class ClientSocket : public SocketTools {
public:
	ClientSocket(void);
	~ClientSocket(void);

	//	Create a socket which can be used to call a remote host
	//	and connect it to that host (given the IP address and port).
	//	Return 0 for success, else error code.
	//	This function does not retry to establish a connection.  It returns
	//	immediately if the connection fails.
  #if INCL_SSL_SUPPORT
	// Use this ConnectSocket() function with a non-null ssl_ctx if you want to
	// make a secure connection to the server (SSLv3 or TLSv1).
	ErrorType ConnectSocket(IPADDRESS host_ip, short host_port, void *ssl_ctx);
  #endif
	ErrorType ConnectSocket(IPADDRESS host_ip, short host_port);

private:
};

// Server-specific socket tools.  This object usually gets created
// by the ServerListen object.  Each socket gets one of these objects.
// If the client connects again to another socket, the first instance
// should get destroyed.
class ServerSocket : public SocketTools {
public:
	ServerSocket(void);
	~ServerSocket(void);

private:
};

// Server's main 'listen' object.  This object listens for
// incoming connections and spawns them off to a ServerSocket.
// There is usually only one of these objects for a given listening
// port.  It will handle all incoming connections and give back
// ServerSocket objects to handle to individual connections to different
// clients.
class ServerListen {
public:
	ServerListen(void);
	~ServerListen(void);

	// Start listening on port.  Doesn't actually accept connections.
	ErrorType OpenListenSocket(short port_number);
  #if INCL_SSL_SUPPORT
	ErrorType OpenListenSocket(short port_number, void *ssl_server_ctx);
  #endif

	ErrorType CloseListenSocket(void);

	// Accept an incoming connection (if available) and create a
	// ServerSocket object to go with it.  No communication is actually
	// done with the client.
	ErrorType AcceptConnection(ServerSocket **output_client_socket);

	SOCKET listen_socket;	// our main listening socket.
  #if INCL_SSL_SUPPORT
	void *ssl_ctx;	// OpenSSL context if this is to be an SSL connection
  #endif
private:
};

#endif // !_LLIP_H_INCLUDED
