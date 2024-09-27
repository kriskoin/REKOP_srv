#define DISP_SSL_DISCON_DETAILS	0
#define TIGHT_SSL_CRITSEC_PROTECTION	0	// set to put crit secs around all I/O (probably overkill)
#define SWITCH_CONTEXT_UNNECESSARILY	0	// for critsec testing only - never leave on

//*********************************************************
//
//	Low level IP routines.
//	Routines common to client and server.
//
// 
//
//*********************************************************

#define DISP 0

#include "llip.h"
#if INCL_SSL_SUPPORT
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif
#if !WIN32 && 1	// only needed for some test code
  #include <sys/stat.h>
  #include <fcntl.h>
#endif

// # of ms to delay before sending (or 0 if none).  Use for latency
// testing.  Note that this is about half the ping time because pings are
// return times, whereas this send delay is one way but happens on both ends.
// Therefore, to simulate an 800ms ping time, use 400 for SEND_PACKET_DELAY.
#if WIN32
  #define SEND_PACKET_DELAY		0	//2500
#else
  #define SEND_PACKET_DELAY		0
#endif
#define	VERSION_STRING				((const char *)"TropicanaPoker IP lib 1.0")
#define MAX_VERSION_STRING_LEN		30


// The VersionPacket structure is currently 64 bytes.  We'll try to
// keep it padded with unused bytes so that it remains that size.
struct VersionPacket {
	char version_string[MAX_VERSION_STRING_LEN];
	WORD16 version;
	char unused_b[32];
};

volatile WORD32 dwPacketSerialNumber;	// used for debugging only.
volatile WORD32 dwLivingPackets;		// # of packets still allocated.
volatile WORD32 dwPacketsConstructed;	// total # of packets ever constructed

#if 1	// 2022 kriskoin
  #undef malloc
  #undef free
#else
  #pragma message("Performance Warning: malloc/free calls zero memory used by packets.")
#endif

//****************************************************************
// 
//
// Constructor/destructor for the Packet object
//
Packet::Packet(void)
{
	packet_serial_number = dwPacketSerialNumber++;	// update s/n used for debugging
	dwLivingPackets++;		// inc number of packets currently alive.
	dwPacketsConstructed++;	// inc total number of packets ever constructed.
	pr(("%s(%3d) Packet %6d of %d constructor called.\n", _FL, packet_serial_number, dwLivingPackets));

	data_ptr = NULL;
	user_data_ptr = NULL;
	max_length = length = user_data_length = 0;
	next_ptr = NULL;
	packed_flag = 0;
	desired_send_ticks = 0;
	length_when_received = 0;
}

Packet::~Packet(void)
{
	dwLivingPackets--;
	pr(("%s(%3d) Packet %6d of %d destructor called. Freeing data_ptr ($%08lx) (size %d).\n",
				_FL, packet_serial_number, dwLivingPackets, data_ptr, max_length));
	if (data_ptr)
		free(data_ptr);
	data_ptr = NULL;
	user_data_ptr = NULL;
	max_length = length = user_data_length = 0;
	next_ptr = NULL;
	length_when_received = 0;
}


//****************************************************************
// 
//
// Allocate space for holding the packet's data (pass the user data
// size - anything needed internally by this class will be automatically
// added to this amount).
//
ErrorType Packet::Alloc(int amount)
{
	pr(("%s(%3d) Packet %6d of %d Alloc(%d) called.\n", _FL, packet_serial_number, dwLivingPackets, amount));
	if (data_ptr) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Packet::Alloc called more than once. Previous ptr = %p", _FL, data_ptr);
		return ERR_INTERNAL_ERROR;
	}
	if (amount < 1 || amount > MAX_PACKET_SIZE) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Packet::Alloc: illegal packet size (%d). Max is %d", _FL, amount, MAX_PACKET_SIZE);
		return ERR_INTERNAL_ERROR;
	}
	max_length = amount + sizeof(struct PacketHeader);

	data_ptr = (char *)malloc(max_length);
	if (!data_ptr) {
		Error(ERR_ERROR, "%s(%d) Packet::Alloc failed to malloc(%d)", _FL, max_length);
		max_length = 0;
		return ERR_ERROR;
	}

	((struct PacketHeader *)data_ptr)->sig = PACKET_HEADER_SIG;
	user_data_ptr = data_ptr + sizeof(struct PacketHeader);
	SetUserDataLength(0);	// default to empty.
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Set the # of bytes which are used in this packet's user
// data section.  Automatically fills in the header length
// and other lengths to compensate for the header, etc.
//
void Packet::SetUserDataLength(int new_len)
{
	if (new_len < 0 || new_len > (max_length - (int)sizeof(struct PacketHeader))) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) SetUserDataLength(%d) is an illegal value. max_length=%d",
					_FL, new_len, max_length);
		new_len = 0;
	}
	length = new_len + sizeof(struct PacketHeader);	// total bytes in packet
	((struct PacketHeader *)data_ptr)->len = (WORD16)length;
	user_data_length = new_len;	// user bytes in packet
}

//****************************************************************
// 
//
// Write data into the packet
//
ErrorType Packet::WriteData(char *src_pointer, int src_len, int dest_offset)
{
	if (!data_ptr) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Packet::WriteData failed: packet not Alloc()'d yet", _FL);
		return ERR_INTERNAL_ERROR;
	}
	if (!src_pointer && src_len) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Packet::WriteData failed: src_pointer==NULL", _FL);
		return ERR_INTERNAL_ERROR;
	}
	if (dest_offset < 0 || dest_offset > max_length) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Packet::WriteData failed: illegal dest_offset (%d)", _FL, dest_offset);
		return ERR_INTERNAL_ERROR;
	}
	if (src_len < 0 || src_len + dest_offset > max_length) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Packet::WriteData failed: illegal src_len(%d)+dest_offset(%d)", _FL, src_len, dest_offset);
		return ERR_INTERNAL_ERROR;
	}

	memmove(user_data_ptr+dest_offset, src_pointer, src_len);
	// Update the 'length' parameter for this packet.
	// It should indicate the total # of bytes in the packet.
	SetUserDataLength(max(user_data_length, dest_offset+src_len));
	return ERR_NONE;
}

//****************************************************************
// 
//
// Short-cut function for Allocating and Writing all in one go.  This
// is only useful for packets which are made up of packets of data that
// already exist in memory as one chunk.
//
ErrorType Packet::AllocData(char *src_pointer, int src_len)
{
	ErrorType err = Alloc(src_len);
	if (err!=ERR_NONE) return err;
	err = WriteData(src_pointer, src_len, 0);	// always write at offset 0.
	return err;
}

#if COMPRESS_PACKETS
//*********************************************************
// https://github.com/kriskoin//
// Constructor/destructor for the DiffEncoder class.
//
DiffEncoder::DiffEncoder(void)
{
	memset(SavedPackets, 0, sizeof(SavedPackets[0])*DIFFENCODER_MAX_PACKET_TYPES);
}

DiffEncoder::~DiffEncoder(void)
{
	for (int i=0 ; i<DIFFENCODER_MAX_PACKET_TYPES ; i++) {
		if (SavedPackets[i].ptr) {
			free(SavedPackets[i].ptr);
			SavedPackets[i].ptr = NULL;
			SavedPackets[i].length = 0;
		}
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Set up for encoding/decoding
//
struct DiffEncoder_SavedPacketInfo * DiffEncoder::SetupPacketInfo(Packet *p)
{
	int packet_type = *p->user_data_ptr & (DIFFENCODER_MAX_PACKET_TYPES-1);
	struct DiffEncoder_SavedPacketInfo *spi = &SavedPackets[packet_type];
	// Determine if we need to allocate a new buffer for this packet type.
	if (p->user_data_length > spi->length) {
		char *new_ptr = (char *)malloc(p->user_data_length);
		if (!new_ptr) {
			Error(ERR_FATAL_ERROR, "%s(%d) malloc(%d) failed in EncodePacket.", _FL, p->user_data_length);
			DIE("memory problem");
		}
		memset(new_ptr, 0, p->user_data_length);	// always fill with zeroes

		// If there was an old buffer, copy it to the new buffer and free it
		if (spi->ptr) {
			memcpy(new_ptr, spi->ptr, spi->length);
			free(spi->ptr);
		}
		// Save ptrs to new buffer.
		spi->ptr = new_ptr;
		spi->length = p->user_data_length;
	}
	return spi;
}

//*********************************************************
// https://github.com/kriskoin//
// Encode a packet.
//
ErrorType DiffEncoder::EncodePacket(Packet *p)
{
	struct DiffEncoder_SavedPacketInfo *spi = SetupPacketInfo(p);
  #if 0	// 2022 kriskoin
	kp(("%s(%d) EncodePacket source data: (len=%d)\n",_FL, p->user_data_length));
	khexd(p->user_data_ptr, p->user_data_length);
	kp(("%s(%d) EncodePacket source XOR data:\n",_FL));
	khexd(spi->ptr, p->user_data_length);
  #endif
	// Now loop through the data and XOR encode it, saving to the
	// save buffer as we go along.  Don't do the first byte because
	// we need it as a decode key.
	char *new_data = p->user_data_ptr;
	char *old_data = spi->ptr;
	*old_data++ = *new_data++;	// just copy the first byte (no changes)
	for (int i=1 ; i<p->user_data_length ; i++, new_data++, old_data++) {
		char saved_data = *new_data;
		*new_data = (char)(*old_data ^ saved_data);
		*old_data = saved_data;
	}
  #if 0 	// 2022 kriskoin
	kp(("%s(%d) EncodePacket output data: (len=%d)\n",_FL, p->user_data_length));
	khexd(p->user_data_ptr, p->user_data_length);
	kp(("%s(%d) EncodePacket final XOR data:\n",_FL));
	khexd(spi->ptr, p->user_data_length);
	kp(("\n"));
  #endif
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Decode a packet.
//
ErrorType DiffEncoder::DecodePacket(Packet *p)
{
	struct DiffEncoder_SavedPacketInfo *spi = SetupPacketInfo(p);
  #if 0	// 2022 kriskoin
	kp(("%s(%d) DecodePacket source data: (len=%d)\n",_FL, p->user_data_length));
	khexd(p->user_data_ptr, p->user_data_length);
	kp(("%s(%d) DecodePacket source XOR data:\n",_FL));
	khexd(spi->ptr, p->user_data_length);
  #endif
	// Now loop through the data and XOR Decode it, saving to the
	// save buffer as we go along.  Don't do the first byte because
	// we need it as a decode key.
	char *new_data = p->user_data_ptr;
	char *old_data = spi->ptr;
	*old_data++ = *new_data++;	// just copy the first byte (no changes)
	for (int i=1 ; i<p->user_data_length ; i++, new_data++, old_data++) {
		*old_data = *new_data = (char)(*new_data ^ *old_data);
	}
  #if 0	// 2022 kriskoin
	kp(("%s(%d) DecodePacket output data: (len=%d)\n",_FL, p->user_data_length));
	khexd(p->user_data_ptr, p->user_data_length);
	kp(("%s(%d) DecodePacket final XOR data:\n",_FL));
	khexd(spi->ptr, p->user_data_length);
	kp(("\n"));
  #endif
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Reset the encoder state
//
void DiffEncoder::ResetEncoder(void)
{
	for (int i=0 ; i<DIFFENCODER_MAX_PACKET_TYPES ; i++) {
		if (SavedPackets[i].ptr) {
			memset(SavedPackets[i].ptr, 0, SavedPackets[i].length);
		}
	}
}

//*********************************************************
// https://github.com/kriskoin//
// memory alloc/free functions for zlib
//
voidpf zalloc(voidpf opaque, uInt items, uInt size)
{
	NOTUSED(opaque);
  #if 0
	static int total = 0;
	total += items*size;
	kp(("%s(%d) zalloc(%d bytes).  Total is now %d\n", _FL, items*size, total));
  #endif
	return malloc(items*size);
}
void zfree(voidpf opaque, voidpf address)
{
	//kp(("%s(%d) zfree()\n",_FL));
	NOTUSED(opaque);
	free(address);
}
#endif	// COMPRESS_PACKETS

//****************************************************************
// 
//
// Constructor/destructor for the SocketTools object
//
SocketTools::SocketTools(void)
{
	pr(("%s(%d) SocketTools constructor called.\n",_FL));
  #if ENABLE_QUEUE_CRIT_SEC
	PPInitializeCriticalSection(&SendCritSec, CRITSECPRI_LLIP_SEND, "llip:SendQueue");
	PPInitializeCriticalSection(&ReceiveQueueCritSec, CRITSECPRI_LLIP_RCVE, "llip:RcveQueue");
  #endif
  #if ENABLE_SEND_QUEUING
	SendQueueHead = SendQueueTail = NULL;
  #endif
	ReceiveQueueHead = ReceiveQueueTail = NULL;
	next_send_attempt_ms = 0;

	// Initialize ALL of our variables.
	sock = INVALID_SOCKET;
  #if INCL_SSL_SUPPORT
	ssl = NULL;
  #endif
	zstruct(connection_address);
	zstruct(ip_str);
	total_bytes_sent = 0;
	total_bytes_received = 0;
	total_bytes_sent_resettable = 0;
	total_bytes_received_resettable = 0;
	total_packets_sent = 0;
	total_packets_received = 0;
	remote_version = 0;
	connected_flag = FALSE;
	disconnected = FALSE;
	time_of_connect = 0;
	InitialConnectionTimeout = 10;
	time_of_disconnect = 0;
	time_of_last_sent_packet = 0;
	time_of_last_received_packet = 0;

	receive_state = 0;		// state = waiting for Packet header
	received_bytes = 0;		// no bytes received yet for current state.
	zstruct(received_header);
	received_packet = NULL;

  #if COMPRESS_PACKETS
	zstruct(inflate_zs);
	zstruct(deflate_zs);
	deflate_zs.zalloc = inflate_zs.zalloc = zalloc;
	deflate_zs.zfree  = inflate_zs.zfree  = zfree;
	#define MAX_CODE_SIZE	12
	int zerr = inflateInit2(&inflate_zs, MAX_CODE_SIZE);
	if (zerr != Z_OK) {
		kp(("%s(%d) inflateInit() returned %d\n", _FL, zerr));
	}
	pr(("%s(%d) inflateInit() is done... calling deflateInit()...\n",_FL));
	compression_level = Z_BEST_COMPRESSION;
	zerr = deflateInit2(&deflate_zs, compression_level,
			Z_DEFLATED, MAX_CODE_SIZE, 5, Z_FILTERED);


	if (zerr != Z_OK) {
		kp(("%s(%d) deflateInit() returned %d\n", _FL, zerr));
	}
  #endif // COMPRESS_PACKETS
	pr(("%s(%d) SocketTools constructor is finished.\n",_FL));
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
}

SocketTools::~SocketTools(void)
{
	pr(("%s(%d) SocketTools destructor called.\n",_FL));

	CloseSocket();

	// Delete any Packet objects in the send queue
  #if ENABLE_SEND_QUEUING
	PurgeSendQueue();
  #endif
	PurgeReceiveQueue();

	// If we were building up a received packet, delete it now.
	if (received_packet) {
		delete received_packet;
		received_packet = NULL;
	}
  #if COMPRESS_PACKETS
	inflateEnd(&inflate_zs);
	deflateEnd(&deflate_zs);
  #endif // COMPRESS_PACKETS

	// Close up and delete any remaining resources.
  #if ENABLE_QUEUE_CRIT_SEC
	PPDeleteCriticalSection(&SendQueueCritSec);
	zstruct(SendQueueCritSec);
	PPDeleteCriticalSection(&ReceiveQueueCritSec);
	zstruct(ReceiveQueueCritSec);
  #endif
	pr(("%s(%d) SocketTools destructor finished.\n",_FL));
}

#if COMPRESS_PACKETS
//*********************************************************
// https://github.com/kriskoin//
// Set the desired compression level (if necessary).
// Zlib accepts compression levels between 0 (none) and 9 (most).
//
void SocketTools::SetCompressionLevel(int new_level)
{
	if (compression_level != new_level) {
		compression_level = new_level;
		deflateParams(&deflate_zs, new_level, Z_FILTERED);
	}
}
#endif	// COMPRESS_PACKETS

//*********************************************************
// https://github.com/kriskoin//
// Set the connection address for this socket
//
void SocketTools::SetConnectionAddress(SOCKADDR_IN *sin)
{
	connection_address = *sin;
	zstruct(ip_str);
	IP_ConvertIPtoString(connection_address.sin_addr.s_addr, ip_str, sizeof(ip_str));
}

//****************************************************************
// https://github.com/kriskoin//
// Set the socket to use for this object.
// You can pass INVALID_SOCKET to close the socket.
//
ErrorType SocketTools::SetSocket(SOCKET new_socket)
{
	// If our previous socket is already open, close it.
	SOCKET oldsock = sock;
	sock = new_socket;
	if (oldsock != new_socket && oldsock != INVALID_SOCKET) {
	  #define TEST_CLOSING	0
	  #if TEST_CLOSING
		int s2 = open("/tmp/test_fd.tmp", O_RDWR|O_CREAT, S_IREAD|S_IWRITE);
		kp(("%s(%d) Before closing socket... next file is %d\n", _FL, s2));
		close(s2);
		kp(("%s(%d) Closing socket %d\n", _FL, oldsock));
	  #endif

		pr(("%s(%d) Closing client socket $%08lx\n", _FL, oldsock));
		int result;
		result = shutdown(oldsock, 2);	//kriskoin: 	  #if TEST_CLOSING
		// from asm/errno.h: #define	ENOTCONN	107	/* Transport endpoint is not connected */
		if (result != 0) {
			kp(("%s %s(%d) shutdown(%d,2) resulted in %d (errno=%d)!\n",
					TimeStr(), _FL, oldsock, result, errno));
		}
	  #endif
		result = closesocket(oldsock);

	  #if TEST_CLOSING
		if (result != 0) {
			kp(("%s %s(%d) closesocket(%d) resulted in %d (errno=%d)!\n",
					TimeStr(), _FL, oldsock, result, errno));
		}

		int s3 = open("/tmp/test_fd.tmp", O_RDWR|O_CREAT, S_IREAD|S_IWRITE);
		kp(("%s(%d) After closing socket... next file is %d (%s)\n", _FL, s3,
				s2==s3 ? "SAME!" : "different"));
		close(s3);
	  #endif
	}
	return ERR_NONE;
}

#if INCL_SSL_SUPPORT
//*********************************************************
// https://github.com/kriskoin//
// Set the SSL socket to use for this object.
//
ErrorType SocketTools::SetSSLSocket(void *new_ssl)
{
	EnterSSLCritSec();
	if (ssl) {
		//kp(("%s(%d) Calling SSL_*() from thread %d\n", _FL, GetCurrentThreadId()));
		SSL *temp_ssl = (SSL *)ssl;
		ssl = NULL;
		int success = SSL_shutdown(temp_ssl);  /* send SSL/TLS close_notify */
		if (!success) {
			//kp(("%s SSL_shutdown() failed.\n", TimeStr()));
		}
		SSL_free(temp_ssl);
	}

	SetSocket(INVALID_SOCKET);

	if (new_ssl) {
		ssl = new_ssl;
		//kp(("%s(%d) Calling SSL_*() from thread %d\n", _FL, GetCurrentThreadId()));
		sock = SSL_get_fd((SSL *)ssl);
		//kp(("%s(%d) SSL compress = $%08lx\n", _FL, ((SSL *)ssl)->compress));
	}
	LeaveSSLCritSec();
	return ERR_NONE;
}
#endif

//*********************************************************
// https://github.com/kriskoin//
// Close our socket if it's open.  Set sock to INVALID_SOCKET and
// set the disconnected flag.
//
void SocketTools::CloseSocket(void)
{
  #if INCL_SSL_SUPPORT
	if (ssl) {
		SetSSLSocket(NULL);
	}
  #endif


	SetSocket(INVALID_SOCKET);
	if (!disconnected) {
		disconnected = TRUE;
	  #if 1	// 2022 kriskoin
		time_of_disconnect = time_of_last_received_packet;
	  #else
		time_of_disconnect = SecondCounter;
	  #endif
	}
}

//****************************************************************
// 
//
//	Send our version packet.  This routine is called exactly once
//	when the connection is first set up.  It is the first packet to
//	be sent and the other end expects it to be the first packet
//	received.  This function should be called by whoever connects
//	the socket.
//
ErrorType SocketTools::SendVersionPacket(void)
{
	struct VersionPacket vp;
	zstruct(vp);
	strnncpy(vp.version_string, VERSION_STRING, MAX_VERSION_STRING_LEN);
	vp.version = PACKET_IO_VERSION;
	pr(("%s(%d) Queuing version packet\n",_FL));
	ErrorType err = SendBytes((char *)&vp, sizeof(vp), NULL);
	return err;
}

#if ENABLE_SEND_QUEUING
//****************************************************************
// 
//
//	Purge (delete) any packets in our send queue
//
ErrorType SocketTools::PurgeSendQueue(void)
{
	// Delete any Packet objects in the send queue
  #if ENABLE_QUEUE_CRIT_SEC
	EnterCriticalSection(&SendQueueCritSec);
  #endif
	Packet *p = SendQueueHead;
	while (p) {
		Packet *next = p->next_ptr;
		PktPool_ReturnToPool(p);
		p = next;
	}
	SendQueueHead = SendQueueTail = NULL;
  #if ENABLE_QUEUE_CRIT_SEC
	LeaveCriticalSection(&SendQueueCritSec);
  #endif
	return ERR_NONE;
}
#endif

//****************************************************************
// 
//
//	Purge (delete) any packets in our receive queue
//
ErrorType SocketTools::PurgeReceiveQueue(void)
{
	// Delete any Packet objects in the receive queue
  #if ENABLE_QUEUE_CRIT_SEC
	EnterCriticalSection(&ReceiveQueueCritSec);
  #endif
	Packet *p = ReceiveQueueHead;
	while (p) {
		Packet *next = p->next_ptr;
		PktPool_ReturnToPool(p);
		p = next;
	}
	ReceiveQueueHead = ReceiveQueueTail = NULL;
  #if ENABLE_QUEUE_CRIT_SEC
	LeaveCriticalSection(&ReceiveQueueCritSec);
  #endif
	return ERR_NONE;
}

//****************************************************************
// 
//
//	This function gets called with WSAENOTCONN is detected.  It needs
//	to determine if the connection has been lost to the remote machine,
//	and if so, it needs to set the disconnected flag.
//	Also used for WSAECONNRESET, WSAECONNABORTED, and WSAESHUTDOWN and
//	now many others.

//
void SocketTools::HandleNotConnErr(int err)
{
	// There are two possibilities... we're waiting for the initial
	// handshaking to complete, or we were connected but now we're
	// disconnected.
	pr(("%s(%d) HandleNotConnErr() was passed error %d\n", _FL, err));
	if (connected_flag
		#if WIN32
		 || err==WSAENOTSOCK
		#endif
	) {
		if (!disconnected) {
		  #if DISP_SSL_DISCON_DETAILS
			kp(("%s(%d) DISCONN DETECTED: HandleNotConnErr() was passed err = %d\n", _FL, err));
		  #endif
			//kp(("%s(%d) Disconnection detected for socket $%08lx (err=%d).\n",_FL,sock,err));
			disconnected = TRUE;
		  #if 1	// 2022 kriskoin
			time_of_disconnect = time_of_last_received_packet;
		  #else
			time_of_disconnect = SecondCounter;
		  #endif
		}
	} else if (!disconnected) {	// Only timeout once.
		WORD32 t = SecondCounter;
		if (t - time_of_connect > InitialConnectionTimeout) {
			// It's been too long... time to give up.
			//Error(ERR_NOTE, "%s(%d) Failed connection setup detected for socket $%08lx.  Treating as a disconnect.",_FL, sock);
		  #if 0	// 2022 kriskoin
			kp(("%s(%d) Failed connection setup for socket $%08lx (to %s:%u, err=%d).  Treating as a disconnect.\n", _FL, sock, ip_str, ntohs(connection_address.sin_port),err));
		  #endif
			disconnected = TRUE;
			time_of_disconnect = SecondCounter;
		}
	}
}

//****************************************************************
// 
//
//	Send an array of bytes (chars) to an already open and connected socket.
//	No streaming/encryption or anything is performed, just the raw bytes are sent.
//	Return 0 for success, else error code.
//
ErrorType SocketTools::RawSendBytes(char *buffer, int len)
{
	if (!buffer) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL buffer passed to RawSendBytes() (len = %d)",_FL, len);
		return ERR_INTERNAL_ERROR;
	}
	if (len <= 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) illegal buffer length (%d) passed to RawSendBytes()",_FL, len);
		return ERR_INTERNAL_ERROR;
	}
	if (sock==INVALID_SOCKET) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to write %d bytes of data to invalid socket",_FL, len);
		return ERR_INTERNAL_ERROR;
	}
#if 0
	kp(("%s(%d) Sending these bytes to socket $%08lx (len = %d):\n", _FL, sock, len));
	khexdump(buffer, len, 32, 1);
#endif
	//kp(("%s(%d) Sending %d bytes for this packet.\n",_FL, len));
	int bytes_sent = 0;
  #if INCL_SSL_SUPPORT
	if (ssl) {	// SSL connection...
		//kp(("%s(%d) Calling SSL_*() from thread %d\n", _FL, GetCurrentThreadId()));
	  #if TIGHT_SSL_CRITSEC_PROTECTION
		EnterSSLCritSec();
		if (!ssl) {
			// Our ssl connection got closed... don't try sending.
			// Treat as if we couldn't send.
			LeaveSSLCritSec();
			return ERR_ERROR;
		}
	  #endif
	  #if INCLUDE_FUNCTION_TIMING && 0	// 2022 kriskoin
		kp1((ANSI_BLACK_ON_YELLOW"%s(%d) **** SSL_write() timing is enabled!  Don't leave this on normally!\n",_FL));
		WORD32 start_ticks = GetTickCount();
		bytes_sent = SSL_write((SSL *)ssl, buffer, len);
		WORD32 elapsed = GetTickCount() - start_ticks;
		if (elapsed > 50) {
			kp(("%s %s(%d) Warning: SSL_write() took %dms to complete.\n", TimeStr(), _FL, elapsed));
		}
	  #else
		bytes_sent = SSL_write((SSL *)ssl, buffer, len);
	  #endif
		//kp(("%s(%d) bytes_sent = %d\n", _FL, bytes_sent));
		if (bytes_sent <= 0) {
			int err = SSL_get_error((SSL *)ssl, bytes_sent);
			bytes_sent = 0;
			int wsa_err = WSAGetLastError();
			int ssl_err = 0;
			if (err==SSL_ERROR_SSL) {
				ssl_err = ERR_get_error();
			}
			if (err == SSL_ERROR_SYSCALL && wsa_err==WSAEWOULDBLOCK) {
				// The output queue seems full.  This isn't good.
				pr(("%s(%d) send() received WSAEWOULDBLOCK... assuming output buffer is full.\n",_FL));
			  #if TIGHT_SSL_CRITSEC_PROTECTION
				LeaveSSLCritSec();
			  #endif
				return ERR_ERROR;
			}
			if (   err == SSL_ERROR_NONE
				|| err == SSL_ERROR_WANT_WRITE
				|| err == SSL_ERROR_WANT_READ
				|| err == SSL_ERROR_WANT_X509_LOOKUP
				|| (err == SSL_ERROR_SYSCALL && wsa_err==WSAENOTCONN)
			) {
				// No data sent.  Tell caller we could not send it.  Caller should
				// retry again in 100ms or so.
			  #if TIGHT_SSL_CRITSEC_PROTECTION
				LeaveSSLCritSec();
			  #endif
				return ERR_ERROR;
			} else  {
				// Assume connection lost
				pr(("%s(%d) Got SSL_ERROR_* # %d (see openssl/ssl.h). Assuming connection lost.\n", _FL, err));
			  #if DISP_SSL_DISCON_DETAILS
				if (connected_flag) {
					kp(("%s(%d) DISCONN DETECTED: SSL_write(x,x,%d) SSL_get_error=%d WSAErr=%d sslerr=%d\n", _FL, len, err, wsa_err, ssl_err));
				}
			  #endif
				HandleNotConnErr(WSAENOTCONN);
			  #if TIGHT_SSL_CRITSEC_PROTECTION
				LeaveSSLCritSec();
			  #endif
				return ERR_ERROR;
			}
		}
	  #if TIGHT_SSL_CRITSEC_PROTECTION
		LeaveSSLCritSec();
	  #endif
	} else
  #endif
	{	// Non-SSL connection...
		bytes_sent = send(sock, buffer, len, 0);
		if (bytes_sent == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err==WSAEWOULDBLOCK) {
				// The output queue seems full.  This isn't good.
				pr(("%s(%d) send() received WSAEWOULDBLOCK... assuming output buffer is full.\n",_FL));
				return ERR_ERROR;
			}
			if (err==WSAENOTCONN ||
				err==WSAECONNRESET ||
				err==WSAECONNABORTED ||
				err==WSAEHOSTUNREACH ||
				err==WSAEHOSTDOWN ||
			  #if !WIN32
				err==EPIPE ||
				err==ETIMEDOUT ||
			  #else
				err==WSAENOTSOCK ||
			  #endif
				err==WSAESHUTDOWN) {
				HandleNotConnErr(err);	// check if we've lost a connection.
				return ERR_ERROR;
			}
			Error(ERR_ERROR, "%s(%d) SOCKET_ERROR occurred during socket send().  socket=$%08lx, Error=%d. Treating as disconnect.",_FL,sock,err);
		  #if !WIN32	// 2022 kriskoin
			kp(("%s(%d) note: Linux error #'s can be found in 'include/asm/errno.h'\n",_FL));
		  #endif
			HandleNotConnErr(err);	// check if we've lost a connection.
			return ERR_ERROR;
		}
	}

	total_bytes_sent += bytes_sent + TCP_HEADER_OVERHEAD_AMOUNT;
	total_bytes_sent_resettable += bytes_sent + TCP_HEADER_OVERHEAD_AMOUNT;
  #if 0	//kriskoin: 	if (!connected_flag) {
		connected_flag = TRUE;	// we've successfully sent something, therefore we must be connected.
	  #if 0	// 2022 kriskoin
		kp(("%s(%d) Successfully connected socket $%08lx (data written successfully)\n", _FL, sock));
	   #if INCL_SSL_SUPPORT
		if (ssl) {
			EnterSSLCritSec();
			kp(("%s(%d) SSL connection is using %s\n", _FL, SSL_get_cipher_name((SSL *)ssl)));
			int bits = 0;
			int result = SSL_get_cipher_bits((SSL *)ssl, &bits);
			kp(("%s(%d) SSL_get_cipher_bits = %d,%d\n", _FL, result, bits));
			kp(("%s(%d) SSL_get_cipher_version = %s\n", _FL, SSL_get_cipher_version((SSL *)ssl)));
			LeaveSSLCritSec();
		}
	   #endif
	  #endif
	}
  #endif

	if (bytes_sent < len) {
		Error(ERR_ERROR, "%s(%d) not all bytes sent to %s (%d vs. %d) during socket send()",_FL,ip_str,bytes_sent,len);
		return ERR_ERROR;
	}
	return ERR_NONE;
}

//****************************************************************
// 
//
//	Read any data that has arrived at our socket.
//	No destreaming/decryption or anything is performed, just the raw bytes are received.
//	Does not block if nothing is ready.
//	Return ERR_NONE for success, else error code.
//
ErrorType SocketTools::RawReadBytes(char *buffer, int buffer_len, int *output_bytes_read)
{
	if (!output_bytes_read) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL output_bytes_read passed to RawReadBytes()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	*output_bytes_read = 0;
	if (!buffer) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL buffer passed to RawReadBytes()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	if (buffer_len <= 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Illegal buffer_len (%d) passed to RawReadBytes()",_FL, buffer_len);
		return ERR_INTERNAL_ERROR;
	}
	if (sock==INVALID_SOCKET) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to RawReadBytes() from invalid socket",_FL);
		return ERR_INTERNAL_ERROR;
	}
	if (disconnected) {
		return ERR_NONE;
	}


	// Check if anyone has sent data to us...
	int received;
  #if INCL_SSL_SUPPORT
	if (ssl) {
		// SSL connection...
		//kp(("%s(%d) Calling SSL_*() from thread %d\n", _FL, GetCurrentThreadId()));
	  #if TIGHT_SSL_CRITSEC_PROTECTION
		EnterSSLCritSec();
		if (!ssl) {
			// Our ssl connection got closed... don't try reading.
			// Treat as if we couldn't read.
			LeaveSSLCritSec();
			return ERR_NONE;
		}
	  #endif
		received = SSL_read((SSL *)ssl, buffer, buffer_len);
		if (received <= 0) {

			int err = SSL_get_error((SSL *)ssl, received);
			int ssl_err = 0;
			if (err==SSL_ERROR_SSL) {
				ssl_err = ERR_get_error();
			}
			//kp(("%s(%d) Got SSL_ERROR_* # %d (see openssl/ssl.h)\n", _FL, err));
			received = 0;
			if (   err == SSL_ERROR_NONE
				|| err == SSL_ERROR_WANT_WRITE
				|| err == SSL_ERROR_WANT_READ
				|| err == SSL_ERROR_WANT_X509_LOOKUP
			) {
				// No data received.  This is not an error we care about because
				// our timeout code will handle it eventually.
			  #if TIGHT_SSL_CRITSEC_PROTECTION
				LeaveSSLCritSec();
			  #endif
				return ERR_NONE;
			} else  {
				// Assume connection lost.
				pr(("%s(%d) Got SSL_ERROR_* # %d (see openssl/ssl.h). Assuming connection lost.\n", _FL, err));
			  #if DISP_SSL_DISCON_DETAILS
				if (connected_flag) {
					kp(("%s(%d) DISCONN DETECTED: SSL_read(x,x,%d) SSL_get_err=%d WSAErr=%d SSLerr=%d\n", _FL, buffer_len, err, WSAGetLastError(), ssl_err));
				}
			  #endif
				HandleNotConnErr(WSAENOTCONN);	// check if we've lost a connection.
			  #if TIGHT_SSL_CRITSEC_PROTECTION
				LeaveSSLCritSec();
			  #endif
				return ERR_NONE;
			}
		}
	  #if TIGHT_SSL_CRITSEC_PROTECTION
		LeaveSSLCritSec();
	  #endif
	} else
  #endif
	{
		// Non-SSL connection...
		received = recv(sock, buffer, buffer_len, 0);
		if (received == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK
				  #if !WIN32
					|| err==WSAEAGAIN
				  #endif
					|| err==0) {
				// No data received.  This is not an error we care about.
				return ERR_NONE;
			}
			if (err==WSAENOTCONN ||
				err==WSAECONNRESET ||
				err==WSAECONNABORTED ||
				err==WSAEHOSTUNREACH ||
				err==WSAEHOSTDOWN ||
			  #if !WIN32
				err==EPIPE ||
				err==ETIMEDOUT ||
			  #else
				err==WSAENOTSOCK ||
			  #endif
				err==WSAESHUTDOWN) {
				// Not connected.
				HandleNotConnErr(err);	// check if we've lost a connection.
				return ERR_NONE;
			}
			Error(ERR_ERROR, "%s(%d) RawReadBytes() recv()=%d WSAGetLastError() = %d. Treating as disconnect.", _FL, received, err);
			HandleNotConnErr(err);	// assume we've lost a connection.
			return ERR_NONE;
		}
		if (received < 0) {
			Error(ERR_ERROR, "%s(%d) Unexpected result (%d) from recv()", _FL, received);
			received = 0;
		}
	}
  #if DISP && 0
	kp(("%s(%d) Received these bytes to socket $%08lx (len = %d):\n", _FL, sock, received));
	khexdump(buffer, received, 32, 1);
  #endif

	// We seem to have received some data...
	*output_bytes_read = received;

	total_bytes_received += received + TCP_HEADER_OVERHEAD_AMOUNT;
	total_bytes_received_resettable += received + TCP_HEADER_OVERHEAD_AMOUNT;
	total_packets_received++;
	if (!connected_flag) {
		connected_flag = TRUE;	// we've successfully received something, therefore we must be connected.
	  #if 0	// 2022 kriskoin
		kp(("%s(%d) Successfully connected socket $%08lx (data read successfully)\n", _FL, sock));
	   #if INCL_SSL_SUPPORT
		if (ssl) {
			EnterSSLCritSec();
			kp(("%s(%d) SSL connection is using %s\n", _FL, SSL_get_cipher_name((SSL *)ssl)));
			int bits = 0;
			int result = SSL_get_cipher_bits((SSL *)ssl, &bits);
			kp(("%s(%d) SSL_get_cipher_bits = %d,%d\n", _FL, result, bits));
			kp(("%s(%d) SSL_get_cipher_version = %s\n", _FL, SSL_get_cipher_version((SSL *)ssl)));
			LeaveSSLCritSec();
		}
	   #endif
	  #endif
	}

	return ERR_NONE;
}

#if ENABLE_SEND_QUEUING
//*********************************************************
// https://github.com/kriskoin//
// Determine if there is anything in the send queue
// returns TRUE if send queue is empty.
//
int SocketTools::SendQueueEmpty(void)
{
	int result = TRUE;	// default to 'empty'
  #if ENABLE_QUEUE_CRIT_SEC
	EnterCriticalSection(&SendQueueCritSec);
  #endif
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
	if (SendQueueHead) {
		result = FALSE;	// there's something in the queue
	}
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
  #if ENABLE_QUEUE_CRIT_SEC
	LeaveCriticalSection(&SendQueueCritSec);
  #endif
	return result;
}	
#endif

//****************************************************************
// 
//
//	Send any packets in the outgoing queue.
//	Returns ERR_NONE if queue is now empty,
//	Returns ERR_WARNING if something could not be sent immediately.
//
ErrorType SocketTools::ProcessSendQueue(void)
{
	// First, check if there is anything to be sent...
	if (disconnected) {
		PurgeReceiveQueue();
	  #if ENABLE_SEND_QUEUING
		PurgeSendQueue();
	  #endif
		return ERR_NONE;
	}
#if ENABLE_SEND_QUEUING
  #if ENABLE_QUEUE_CRIT_SEC
	EnterCriticalSection(&SendQueueCritSec);
  #endif
	while (
		SendQueueHead
		&& (!SendQueueHead->desired_send_ticks ||
			 SendQueueHead->desired_send_ticks <= GetTickCount())
	) {
		// There's something in the queue... attempt to send it.
		Packet *p = SendQueueHead;
	  #if SWITCH_CONTEXT_UNNECESSARILY
		kp1(("%s(%d) Switching context unnecessarily\n",_FL));
		Sleep(0);
	  #endif
		//kp(("%s(%3d) Packet %6d of %d Sending %d bytes (user_data_len=%d)\n", _FL, p->packet_serial_number, dwLivingPackets, p->length, p->user_data_length));
		ErrorType err = ERR_NONE;
		if (p->data_ptr && p->length) {
			err = RawSendBytes(p->data_ptr, p->length);
		} else {
			kp(("%s %s(%d) Somehow a packet with data at $%08lx and length %d got into the send queue for socket $%04d. Discarding.\n",
					TimeStr(), _FL, p->data_ptr, p->length, sock));
		}
	  #if 0	// 2022 kriskoin
		if (!random(50)) {
			kp1(("%s(%d) Sending extra data to confuse receiver.\n",_FL));
			RawSendBytes("12345", 5);
		}
	  #endif
	  #if 0	//kriskoin: 		kp1(("%s(%d) *** WARNING: TREATING ALL PACKETS AS IF THEY GOT SENT!\n", _FL));
		//if (!iRunningLiveFlag)
		{
			err = ERR_NONE;
		}
	  #endif

		if (err == ERR_NONE) {
			// It got sent... remove it from the queue
			SendQueueHead = p->next_ptr;
			if (!SendQueueHead)
				SendQueueTail = NULL;	// if nothing left, tail should also be null.
			pr(("%s(%3d) Packet %6d of %d Send of %d bytes was successful.  deleting packet...\n", _FL, p->packet_serial_number, dwLivingPackets, p->length));
		  #if DISP
		  	// Count the send queue length...
		  	{
				int count = 0;
				Packet *p2 = SendQueueHead;
				while (p2) {
					count++;
					p2 = p2->next_ptr;
				}
				pr(("%s(%3d) Packet %6d of %d Send queue is %d packets after removing current packet.\n", _FL, p->packet_serial_number, dwLivingPackets, count));
		  	}
		  #endif
		  #if SWITCH_CONTEXT_UNNECESSARILY
			kp1(("%s(%d) Switching context unnecessarily\n",_FL));
			Sleep(0);
		  #endif
			PktPool_ReturnToPool(p);	// now that it has been sent, we're done with it.
		  #if SWITCH_CONTEXT_UNNECESSARILY
			kp1(("%s(%d) Switching context unnecessarily\n",_FL));
			Sleep(0);
		  #endif
		} else {
			pr(("%s %-15.15s %s(%d) warning: RawSendBytes() failed (%d). leaving packet in queue.\n",
					TimeStr(), ip_str, _FL, err));
			p->desired_send_ticks = GetTickCount() + 500;	// try again in n ms (at the earliest)
			break;	// break out of the while() loop on any error.
		}
	}
  #if ENABLE_QUEUE_CRIT_SEC
	LeaveCriticalSection(&SendQueueCritSec);
  #endif
#endif	// ENABLE_SEND_QUEUING
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
	if (SendQueueHead) {
		next_send_attempt_ms = SendQueueHead->desired_send_ticks;
		return ERR_WARNING;	// tell caller we had to give up before sending everything
	} else {
		next_send_attempt_ms = 0;	// queue got emptied. try again any time.
	}
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
//	Read any incoming data as arriving packets and queue them up to be
//  processed by ReadPacket().

//
ErrorType SocketTools::ReadDataToQueue(void)
{
	// Now check if there is anything to be received...
	// Put incoming data in different places depending on our receive mode.
readagain:
	if (receive_state==0) {
		// We're building up a Packet header, read just enough bytes to fill it.
		int bytes_read;
		if (received_bytes >= sizeof(received_header)) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) We're in receive_state 0 but we've already read %d bytes. Resetting state.",_FL,received_bytes);
			received_bytes = 0;
		}
		if (sock==INVALID_SOCKET) {
			return ERR_ERROR;	// socket got closed... nothing more to read.
		}
		ErrorType err = RawReadBytes((char *)&received_header + received_bytes,
					sizeof(received_header) - received_bytes, &bytes_read);
		if (err) return err;
		received_bytes += bytes_read;
		if (received_bytes >= sizeof(received_header)) {
			// We've got the whole thing... move on to the next state.
			// Verify the contents...
			if (received_header.sig != PACKET_HEADER_SIG
			   #if COMPRESS_PACKETS
				 && received_header.sig != PACKET_HEADER_SIG_C
			   #endif
			) {
			  #if 1	// 2022 kriskoin
				Error(ERR_NOTE, "%-14.14s %s(%d) Bad Packet header sig received ($%04x). Closing socket %d.",
						ip_str,  _FL, received_header.sig, sock);
				CloseSocket();
				received_bytes = 0;
				receive_state = 0;	// always force back to state 0
				return ERR_NOTE;
			  #else
				Error(ERR_NOTE, "%s(%d) Bad Packet header sig received ($%04x). Shifting and trying again.", _FL, received_header.sig);
				// Shift received data down by a byte and check it again.
				received_bytes = sizeof(received_header)-1;
				memmove((char *)(&received_header), (char *)(&received_header)+1, received_bytes);
				receive_state = 0;	// always force back to state 0
			  #endif
				goto readagain;		// loop back to reading
			}
			received_bytes = 0;	// Always reset our counter
			int user_data_len = received_header.len - sizeof(struct PacketHeader);
			if (user_data_len <= 0 || user_data_len > MAX_PACKET_SIZE) {
				Error(ERR_NOTE, "%-14.14s %s(%d) Bad Packet length received (%d bytes). Tossing packet.",
						ip_str, _FL, received_header.len);
				goto readagain;		// loop back to reading
			}
			// We seem to have received a valid header.  Allocate a packet to store
			// the incoming data.
			received_packet = PktPool_Alloc(user_data_len);
			if (!received_packet) {
				goto readagain;	// loop back to reading.
			}
		  #if COMPRESS_PACKETS
			if (received_header.sig == PACKET_HEADER_SIG_C) {
				received_packet->packed_flag = TRUE;
			}
		  #endif
			// Default to making all data in packet used.
			received_packet->SetUserDataLength(user_data_len);
			// Everything looks good... switch to reading data mode.
			receive_state = 1;
		}
	}
	if (receive_state==1) {
		// We're building up a Packet's data.  Read just enough bytes to fill it.
		int bytes_read;
		if (received_bytes >= received_packet->user_data_length) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) We're in receive_state 1 but we've already read %d bytes (of %d).",_FL,received_bytes, received_packet->user_data_length);
		}
		ErrorType err = RawReadBytes(received_packet->user_data_ptr + received_bytes,
					received_packet->user_data_length - received_bytes, &bytes_read);
		if (err)
			return err;
		received_bytes += bytes_read;
		if (received_bytes >= received_packet->user_data_length) {
			// We've got a full packet.  Add it to the queue.
			// If we haven't received the initial version number packet, process

			// that right here rather than adding it to the queue.
			//pr(("%s(%d) remote_version = $%04x\n",_FL,remote_version));
			if (!remote_version) {
				struct VersionPacket *vp = (struct VersionPacket *)received_packet->user_data_ptr;
				remote_version = vp->version;
				pr(("%s(%d) Remote packet version string = '%s'. Version=$%04x.  Socket=$%08lx, total_bytes_received=%d\n",
							_FL, vp->version_string, vp->version, sock, total_bytes_received));
				delete received_packet;
			} else {
				// Normal program flow... add packet to queue.
				received_packet->length_when_received = received_packet->user_data_length + sizeof(received_header);
				pr(("%s(%d) received_packet->length_when_received = %d (%d + %d) (serial #%d)\n",
						_FL, received_packet->user_data_length + sizeof(received_header),
						received_packet->user_data_length, sizeof(received_header),
						received_packet->packet_serial_number));
			  #if COMPRESS_PACKETS
				DeCompressPacket(&received_packet);	// decompress if necessary
			  #endif
			  #if ENABLE_QUEUE_CRIT_SEC
				EnterCriticalSection(&ReceiveQueueCritSec);
			  #endif
				if (ReceiveQueueTail) {
					// Something is already in the queue... make it point to us.
					ReceiveQueueTail->next_ptr = received_packet;
				} else {
					// We're the first in the queue... update the head.
					ReceiveQueueHead = received_packet;
					// Possible future performance enhancement: notify the other
					// thread immediately that something has arrived.
				}
				ReceiveQueueTail = received_packet;	// we're now the tail... point to us.
				pr(("%s(%d) Added packet %d of %d to receive queue.\n", _FL, received_packet->packet_serial_number, dwLivingPackets));
			  #if ENABLE_QUEUE_CRIT_SEC
				LeaveCriticalSection(&ReceiveQueueCritSec);
			  #endif
				time_of_last_received_packet = SecondCounter;	// update time of last received packet.
			}
			received_packet = NULL;	// always clear so we don't use it again.
			received_bytes = 0;		// reset for next time
			receive_state = 0;		// go back to building header state.
		}
		if (bytes_read)
			goto readagain;	// Keep looping as long as we find data.
	}
	return ERR_NONE;
}

//****************************************************************
// 
//
//	Read any complete packets that have arrived at our socket.
//	Does not block if nothing is ready.
//	Return ERR_NONE for success, else error code.
//
ErrorType SocketTools::ReadPacket(Packet **output_packet_ptr)
{
	if (!output_packet_ptr) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL output_packet_ptr passed to ReadPacket()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	*output_packet_ptr = NULL;	// always clear to NULL by default.

	if (disconnected) {
		return ERR_NONE;
	}

  #if ENABLE_QUEUE_CRIT_SEC
	EnterCriticalSection(&ReceiveQueueCritSec);
  #endif
	if (ReceiveQueueHead) {
		// Something is in the queue... pop it off and update the queue.
		if (ReceiveQueueTail == ReceiveQueueHead)
			ReceiveQueueTail = NULL;			// clear tail if no items left in queue.
		*output_packet_ptr = ReceiveQueueHead;	// pass this pointer back to caller.
		ReceiveQueueHead = ReceiveQueueHead->next_ptr;	// update new head.
		(*output_packet_ptr)->next_ptr = NULL;	// de-link popped Packet.
		pr(("%s(%3d) Packet %6d of %d has been popped from the read queue.\n", _FL, (*output_packet_ptr)->packet_serial_number, dwLivingPackets));
	}

  #if ENABLE_QUEUE_CRIT_SEC
	LeaveCriticalSection(&ReceiveQueueCritSec);
  #endif
	return ERR_NONE;
}


//****************************************************************
// 
//
//	Write a complete packet to an already open and connected socket.
//	The packet will get deleted once delivered.  Don't use it again after
//	calling SendPacket.  In fact, *packet_to_send will be set to NULL just
//	to be sure.
//	Does not block (even if the output queue is full).
//	Return ERR_NONE for success, else error code.
//
ErrorType SocketTools::SendPacket(Packet **packet_to_send, WORD32 *output_length)

{
	if (!packet_to_send) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL packet_to_send passed to SendPacket()",_FL);
		return ERR_INTERNAL_ERROR;
	}

	(*packet_to_send)->next_ptr = NULL;	// nothing comes after us (yet).
	if (output_length) {
		*output_length = 0;
	}
	if (disconnected) {
		// We've been disconnected... purge this packet (rather than
		// queuing a whole bunch of them into memory).
		PktPool_ReturnToPool(*packet_to_send);
		*packet_to_send = NULL;
		return ERR_NONE;
	}

  #if COMPRESS_PACKETS && 1
	// If necessary, compress this packet.
	//kriskoin: 	// sometimes for some unknown reason.  To avoid decompression problems,
	// we won't compress the first few packets.
   #if 1	//kriskoin: 	if (total_packets_sent >= 1)
   #endif
	{
		//kp(("%s(%d) packet %d: packing disabled = %d\n", _FL, (*packet_to_send)->packet_serial_number, (*packet_to_send)->packing_disabled));
		if (!(*packet_to_send)->packed_flag && !(*packet_to_send)->packing_disabled) {
			//kp(("%s(%d) Compressing packet %d.\n", _FL, (*packet_to_send)->packet_serial_number));
			CompressPacket(packet_to_send);
		}
	}
  #else
	kp1(("%s(%d) **** WARNING: OUTGOING PACKETS ARE NOT BEING COMPRESSED! ****\n",_FL));
  #endif

	Packet *p = *packet_to_send;	

  #if SEND_PACKET_DELAY
	kp1(("%s(%d) Delaying packets by %dms to simulate a connection with %dms ping times.\n",
			_FL,SEND_PACKET_DELAY,SEND_PACKET_DELAY*2));
	p->desired_send_ticks = GetTickCount() + SEND_PACKET_DELAY;
  #endif

	if (output_length) {
		*output_length = p->length + TCP_HEADER_OVERHEAD_AMOUNT;
	}

  #if 0	// 2022 kriskoin
	kp(("%s(%d) SendPacket is queuing packet %d: len=%d, user_data_len=%d, total output_len=%d\n",
			_FL, p->packet_serial_number,
			p->length,
			p->user_data_length,
			p->length + TCP_HEADER_OVERHEAD_AMOUNT));
  #endif
#if ENABLE_SEND_QUEUING
  #if ENABLE_QUEUE_CRIT_SEC
	EnterCriticalSection(&SendQueueCritSec);
  #endif
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
	if (SendQueueTail) {
		// Something is already in the queue... make it point to us.
		SendQueueTail->next_ptr = p;
	} else {
		// We're the first in the queue... update the head.
		SendQueueHead = p;
		// Possible future performance enhancement: notify the other
		// thread immediately that something is ready to send.
	}
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
	SendQueueTail = p;		// we're now the tail... point to us.
	total_packets_sent++;	// another has been queued.
	*packet_to_send = NULL;	// always clear so the caller doesn't use it again.
	time_of_last_sent_packet = SecondCounter;	// update time when last packet queued.
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
  #if ENABLE_QUEUE_CRIT_SEC

	LeaveCriticalSection(&SendQueueCritSec);
  #endif
#else		// !ENABLE_SEND_QUEUING
	//kp(("%s(%3d) Packet %6d of %d Sending %d bytes (user_data_len=%d)\n", _FL, p->packet_serial_number, dwLivingPackets, p->length, p->user_data_length));
	ErrorType err = RawSendBytes(p->data_ptr, p->length);
  #if 0	// 2022 kriskoin
	kp1(("%s(%d) Sending extra data to confuse receiver.\n",_FL));
	RawSendBytes("12345", 5);
  #endif
	if (err != ERR_NONE) {
		kp(("%s %-15.15s %s(%d) warning: RawSendBytes() failed (%d). Tossing output packet.\n",
					TimeStr(), ip_str, _FL, err));
		*packet_to_send = NULL;		// always clear so the caller doesn't use it again.
		PktPool_ReturnToPool(p);	// now that it has been sent, we're done with it.
		return ERR_ERROR;	// did not get sent.
	}
	// It got sent ok... (or at least passed on to the next queueing stage)
	*packet_to_send = NULL;		// always clear so the caller doesn't use it again.
	PktPool_ReturnToPool(p);	// now that it has been sent, we're done with it.
#endif	// !ENABLE_SEND_QUEUING
  #if SWITCH_CONTEXT_UNNECESSARILY
	kp1(("%s(%d) Switching context unnecessarily\n",_FL));
	Sleep(0);
  #endif
	return ERR_NONE;
}

//****************************************************************
// 
//
//	Send an array of bytes (chars) to an already open and connected socket.
//	The bytes are sent as a packet and the other end will receive them as
//	a complete packet.
//	Return 0 for success, else error code.
//
ErrorType SocketTools::SendBytes(char *buffer, int len, WORD32 *output_length)
{
	if (!buffer) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL buffer passed to SendBytes()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	if (len <= 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) illegal buffer length (%d) passed to SendBytes()",_FL, len);
		return ERR_INTERNAL_ERROR;
	}

	// Allocate a new Packet object.  SendPacket() will delete it for us.
	Packet *pkt = PktPool_Alloc(len);
	if (!pkt) {
		Error(ERR_ERROR, "%s(%d) SendBytes() failed while allocating a Packet object",_FL);
		return ERR_ERROR;
	}

	ErrorType err = pkt->WriteData(buffer, len, 0);
	if (err != ERR_NONE) {
		delete pkt;	// we don't send to SendPacket() so we must delete it ourselves.
		return err;
	}
	err = SendPacket(&pkt, output_length);
	return err;
}


//*********************************************************
// https://github.com/kriskoin//
//	Send a string of ASCII text to an already open and connected socket.
//	The NUL byte at the end of the string is also sent.
//	The bytes are sent as a packet and the other end will receive them as
//	a complete packet.
//	Return 0 for success, else error code.
//
ErrorType SocketTools::SendString(char *str)
{
	if (!str) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL string passed to SendString()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	if (sock==INVALID_SOCKET) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to write string ('%s') to invalid socket",_FL, str);
		return ERR_INTERNAL_ERROR;
	}
	return SendBytes(str, strlen(str)+1, NULL);
}


#if COMPRESS_PACKETS
//*********************************************************
// https://github.com/kriskoin//
// Compress a packet.  Takes a ptr to a ptr to a packet.  The
// original packet is discarded and the ptr is set to the newly
// compressed packet.
//
ErrorType SocketTools::CompressPacket(Packet **p)
{
	if ((*p)->packed_flag) {
		return ERR_NONE;	// no work to do... it's already compressed.
	}

	compress_encoder.EncodePacket(*p);	// difference encode the packet.

	// Allocate a new packet which is a little larger than the original.
	Packet *np = PktPool_Alloc((*p)->length+40);
	if (!np) {
		Error(ERR_ERROR, "%s(%d) new Packet failed.", _FL);
		return ERR_ERROR;
	}

	// Now compress our data into it.
	deflate_zs.next_in = (Bytef *)(*p)->user_data_ptr;
	deflate_zs.avail_in = (*p)->user_data_length;
	deflate_zs.next_out = (Bytef *)(np->user_data_ptr + sizeof(WORD16));	// leave room for length
	deflate_zs.avail_out = np->max_length - sizeof(struct PacketHeader) - sizeof(WORD16);
	*(WORD16 *)(np->user_data_ptr) = (WORD16)(*p)->user_data_length;	// write uncompressed length of user portion
	uLong previous_total_out = deflate_zs.total_out;
	int zerr = deflate(&deflate_zs, Z_SYNC_FLUSH);
	if (zerr != Z_OK) {
		kp(("%s(%d) deflate() returned %d\n", _FL, zerr));
		delete np;
		return ERR_ERROR;
	}
	uLong compressed_length = deflate_zs.total_out - previous_total_out;
  #if 0	// 2022 kriskoin
	kp(("%s(%d) Here's what we got back from deflate: (len=%d)\n",
				_FL, compressed_length));

	khexd(np->user_data_ptr + sizeof(WORD16), compressed_length);
  #endif

	// Compression succeeded.
	np->SetUserDataLength(compressed_length + sizeof(WORD16));
	((struct PacketHeader *)np->data_ptr)->sig = PACKET_HEADER_SIG_C;
	np->packed_flag = TRUE;

	//kp(("%s(%d) Compress:   Input packet size = %3d (%3d user), output packet size = %3d\n",_FL, (*p)->length, *(WORD16 *)(np->user_data_ptr), np->length));

	// Delete the uncompressed packet and put our newly compressed
	// one in its place.
	PktPool_ReturnToPool(*p);
	*p = np;
	np = NULL;
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Decompress a packet.  Takes a ptr to a ptr to a packet.  The
// original packet is discarded and the ptr is set to the newly
// uncompressed packet.
//
ErrorType SocketTools::DeCompressPacket(Packet **p)
{
	if (!(*p)->packed_flag) {
		return ERR_NONE;	// no work to do... it's already uncompressed.
	}

	int uncompressed_size = (int)(*(WORD16 *)((*p)->user_data_ptr));
	// Allocate a new packet which is the uncompressed size.
	Packet *np = PktPool_Alloc(uncompressed_size);
	if (!np) {
		Error(ERR_ERROR, "%s(%d) new Packet failed.", _FL);
		return ERR_ERROR;
	}
	//kp(("%s(%d) Decompress: decompressed Packet size will be %d for user portion\n", _FL, uncompressed_size));

	inflate_zs.next_in = (Bytef *)((*p)->user_data_ptr + sizeof(WORD16));
	inflate_zs.avail_in = (*p)->user_data_length - sizeof(WORD16);
	inflate_zs.next_out = (Bytef *)np->user_data_ptr;
	inflate_zs.avail_out = np->max_length - sizeof(struct PacketHeader);
	np->SetUserDataLength(uncompressed_size);
  #if 0	// 2022 kriskoin
	kp(("%s(%d) Here's what we're going to pass to inflate: (len=%d)\n",
				_FL, inflate_zs.avail_in));
	khexd(inflate_zs.next_in, inflate_zs.avail_in);
  #endif
	int zerr = inflate(&inflate_zs, Z_SYNC_FLUSH);
	if (zerr != Z_OK) {
		kp(("%s(%d) inflate() returned %d (socket $%08lx). Closing connection.\n", _FL, zerr, sock));
		delete np;
		CloseSocket();
		return ERR_ERROR;
	}

	// Decompression succeeded.
	np->packed_flag = FALSE;
	np->length_when_received = (*p)->length_when_received;	// copy it.

	decompress_encoder.DecodePacket(np);	// difference decode the packet.

	//kp(("%s(%d) Decompress: Input packet size = %d, output packet size = %d\n",_FL, (*p)->length, np->length));

	// Delete the compressed packet and put our newly uncompressed
	// one in its place.
	PktPool_ReturnToPool(*p);
	*p = np;
	np = NULL;

	return ERR_NONE;

}
#endif // COMPRESS_PACKETS
