//*********************************************************
//
// Packet Pool functions
//
// The packet pool allocator functions are designed to re-use memory so that
// we can minimize the number of malloc()/free() calls and re-use the most
// recently freed packets first, that way we maximize the use of the
// L2 memory cache.  These functions are global so that the same pool is shared
// by all user connections.
//
// Dec 11, 1999 - MB - initial version
//
//*********************************************************

#define BYPASS_POOL	0

#define DISP 0

#include "llip.h"

// Minimum user data size for each pool (must be lowest to highest):
static int iPoolSizeThresholds[PKTPOOL_COUNT] = {60, 124, 400, 650, 900, 1524};

static PPCRITICAL_SECTION PktPoolCritSec;
static int PktPoolInitialized;

volatile WORD32 dwPacketsAllocated;	// stat only: # of packets we've ever allocated
struct PacketPool PktPools[PKTPOOL_COUNT];

//*********************************************************
// https://github.com/kriskoin//
// Initialize the packet pool functions
//
static void PktPool_Init(void)
{
	PPInitializeCriticalSection(&PktPoolCritSec, CRITSECPRI_LOCAL, "PktPool");
	for (int i=0 ; i<PKTPOOL_COUNT ; i++) {
		zstruct(PktPools[i]);
		PktPools[i].min_packet_size = iPoolSizeThresholds[i];
	}
	PktPoolInitialized = TRUE;
}

//*********************************************************
// https://github.com/kriskoin//
// Free up any packets allocated and sitting in the packet pools...
// This can be called either at shutdown or any other time that
// you want everything to get freed up and start over.
//
void PktPool_FreePackets(void)
{
	WORD32 start_ticks = GetTickCount();
	if (!PktPoolInitialized) {
		PktPool_Init();
	}
	EnterCriticalSection(&PktPoolCritSec);
	for (int i=0 ; i<PKTPOOL_COUNT ; i++) {
		while (PktPools[i].packet_count > 0) {
			Packet *p = PktPools[i].pool_ptr[--(PktPools[i].packet_count)];
			if (p) {
				delete p;
			}
			if (GetTickCount() - start_ticks >= 3000) {
				// It's taking far too long to get the job done.
				// Don't hang things; give up early instead.
				break;	// give up early!
			}
		}
	}
	LeaveCriticalSection(&PktPoolCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Obtain a packet from the free packet pool or allocate a new one if necessary.
// The size passed is the minimum size needed for the user data area.
//
Packet *PktPool_Alloc(int minimum_size_needed)
{
	if (!PktPoolInitialized) {
		PktPool_Init();
	}
	Packet *p = NULL;
  #if !BYPASS_POOL
	// Search the pools for something appropriate.
	// First, determine which pool to look in:
	int desired_alloc_size = 0;
	for (int i=0 ; i<PKTPOOL_COUNT ; i++) {
		if (minimum_size_needed <= PktPools[i].min_packet_size) {	// this pool works!
			// Keep track of the size to allocate if we need to.
			if (!desired_alloc_size) {
				desired_alloc_size = PktPools[i].min_packet_size;
			}
			// Grab the first packet out of this pool (if any) and get it
			// ready for use.
			EnterCriticalSection(&PktPoolCritSec);
			if (PktPools[i].packet_count > 0) {
				p = PktPools[i].pool_ptr[--(PktPools[i].packet_count)];
				if (p) {
					// Give it a new serial number (for debugging only)
					PktPools[i].alloc_count++;
					p->packet_serial_number = dwPacketSerialNumber++;
				  #if 0
				  	if (i<=1)
				  	{
						kp(("%s(%d) PktPool_Alloc(%4d): got packet of size %4d from pool. Now #%d (pool #%d has %d packets)\n",
							_FL, minimum_size_needed, p->max_length, p->packet_serial_number,
							i, PktPools[i].packet_count));
				  	}
				  #endif
				} else {
					kp(("%s(%d) Error: empty packet pointer retrieved from pool!\n",_FL));
				}
			} else {
				//kp(("%s(%d) Pool #%d is empty... no %d byte packets available.\n",_FL,i,minimum_size_needed));
			}
			LeaveCriticalSection(&PktPoolCritSec);
			break;
		}
	}
  #else		// BYPASS_POOL
	int desired_alloc_size = minimum_size_needed;
  #endif	// BYPASS_POOL
	if (!p) {	// didn't find one in the pool...
		dwPacketsAllocated++;	// stats: increase count of # of packets ever allocated.
		p = new Packet;
		if (p) {
			int new_packet_size = desired_alloc_size;
			if (minimum_size_needed > new_packet_size) {
				new_packet_size = minimum_size_needed;
			}
			ErrorType err = p->Alloc(new_packet_size);
			if (err) {
				delete p;
				p = NULL;
			}
		  #if 0
			if (p) {
				kp(("%s(%d) PktPool_Alloc(%4d): created a new packet (#%d) (alloc'd=%4d, max_length=%4d)\n", _FL, minimum_size_needed, p->packet_serial_number, new_packet_size, p->max_length));
			}
		  #endif
		}
	}
	if (p) {
		// (re-)initialize some of the fields...
		p->length = p->user_data_length = 0;
		p->next_ptr = NULL;
		p->packed_flag = 0;
		p->desired_send_ticks = 0;
		p->length_when_received = 0;
		((struct PacketHeader *)p->data_ptr)->sig = PACKET_HEADER_SIG;
		p->SetUserDataLength(0);	// default to empty.
	}
	return p;
}

//*********************************************************
// https://github.com/kriskoin//
// Add a packet back into the free packet pool for re-use by someone else.
// This is similar to deleting a packet... the caller is no longer allowed
// to use it after this function has been called.
//
void PktPool_ReturnToPool(Packet *p)
{
	if (!PktPoolInitialized) {
		PktPool_Init();
	}

  #if 0	// 2022 kriskoin
  	kp1(("%s(%d) PERFORMANCE WARNING: testing all packet pool memory before returning to pool!\n",_FL));
	MemCheck(p->data_ptr, _FL);
  #endif
  #if !BYPASS_POOL
	int usable_length = p->max_length-sizeof(struct PacketHeader);
	//kp(("%s(%d) PktPool_ReturnToPool(#%5d): usable size is %d\n", _FL, p->packet_serial_number, usable_length));
	// Determine which pool to put it into.
	int i;
	for (i=PKTPOOL_COUNT-1 ; i>=0 ; i--) {
		struct PacketPool *pool = &PktPools[i];
		if (usable_length >= pool->min_packet_size) {
			// this pool is for items smaller than this packet.
			if (usable_length > pool->min_packet_size) {
				// This packet is too big to keep around forever...
				// don't add it to the pool.
				//kp(("%s(%d) Packet #%d (usable size = %4d, total size = %4d) is too big for pool. Deleting it.\n",_FL,p->packet_serial_number, usable_length, p->max_length));
				break;
			}
			// Add this packet to this pool.
			EnterCriticalSection(&PktPoolCritSec);
			// First, make sure there's room for another packet.
			if (pool->packet_count >= pool->max_pool_size) {
				// Pool is full... make it bigger.
				int new_pool_size = max(32, pool->max_pool_size*2);
				//kp(("%s(%d) Increasing pool size from %d to %d items\n", _FL, pool->max_pool_size, new_pool_size));
				Packet **new_pool_ptr = (Packet **)malloc(sizeof(Packet *)*new_pool_size);
				if (!new_pool_ptr) {
					LeaveCriticalSection(&PktPoolCritSec);
					kp(("%s(%d) New pool malloc(%d) failed... leaving pool same size.\n",_FL,new_pool_size));
					break;
				}
				if (pool->pool_ptr) {	// was there an old pool?
					memmove(new_pool_ptr, pool->pool_ptr, sizeof(Packet *)*pool->max_pool_size);
					free(pool->pool_ptr);
				}
				pool->pool_ptr = new_pool_ptr;
				pool->max_pool_size = new_pool_size;
			}
			// Finally, add it to the pool.
			pool->pool_ptr[pool->packet_count++] = p;
			p = NULL;	// we're done with it... make sure it doesn't get deleted.
			LeaveCriticalSection(&PktPoolCritSec);
			break;	// stop looping through pools.
		}
	}
  #endif	// !BYPASS_POOL

	// If we've still got a pointer to it then it wasn't put into the
	// free packet pool for some reason... just delete it.
	if (p) {
		//kp(("%s(%d) packet #%d of size %d found no home in the free packet pool... deleting it.\n",_FL, p->packet_serial_number, p->max_length));
		delete p;
	}

  #if 0
	static int last_print_seconds = 0;
	if (SecondCounter - last_print_seconds > 30) {
		last_print_seconds = SecondCounter;
		int pool_packet_total = 0;
		for (i=0 ; i<PKTPOOL_COUNT ; i++) {
			struct PacketPool *pool = &PktPools[i];
			kp(("%s(%d) --------- pool #%d : %d packets (%d bytes) ---------\n", _FL, i, pool->packet_count, pool->min_packet_size));
			pool_packet_total += pool->packet_count;
			for (int j=0 ; j<pool->packet_count ; j++) {
				Packet *p = pool->pool_ptr[j];
				kp(("%s(%d)  #%3d: p=$%08lx  s/n =%5d  size = %4d\n", _FL, j, p, p->packet_serial_number, p->max_length));
			}
		}
		kp(("%s(%d) Total packets in all pools: %d, living packets should be %d (diff=%d)\n",
					_FL, pool_packet_total, dwLivingPackets, pool_packet_total - dwLivingPackets));
	}
  #endif
}
