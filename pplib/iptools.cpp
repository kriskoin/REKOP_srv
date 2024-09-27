//******************************************************************

//

//      Misc. IP interface functions

//		Win32 only

//

//******************************************************************



#define DISP 0



#include <stdio.h>

#include <string.h>

#include <ctype.h>

#include "llip.h"

#if !WIN32

  #include <fcntl.h>

  #include <netdb.h>

  #include <arpa/inet.h> 

#endif

#if INCL_SSL_SUPPORT

  #include <openssl/ssl.h>

  #include <openssl/err.h>

#endif



#if WIN32

int WinSockVersionInstalled;		// 0x0201 would be version 2.1.

#endif



//****************************************************************

// 

//

//	Open/Initialize our various IP related functions.

//	Return 0 for success, else error code.

//

ErrorType IP_Open(void)

{

	pr(("%s(%d) IP_Open() has been called.\n",_FL));

  #if WIN32

	WSADATA wsa_data;

	zstruct(wsa_data);

	int result = WSAStartup(MAKEWORD(1, 1), &wsa_data);

	if (result) {

		Error(ERR_FATAL_ERROR, "%s(%d) Error: cannot initialize winsock functions. err=%d", _FL, result);

		return ERR_FATAL_ERROR;

	}



   #if 0	//kriskoin: 	kp(("%s(%d) This Windows platform supports winsock version %d.%d to %d.%d\n",

			_FL,

			LOBYTE(wsa_data.wVersion),

			HIBYTE(wsa_data.wVersion),

			LOBYTE(wsa_data.wHighVersion),

			HIBYTE(wsa_data.wHighVersion)));

	kp(("%s(%d)     Description: %s\n", _FL, wsa_data.szDescription));

	kp(("%s(%d)     System stat: %s\n", _FL, wsa_data.szSystemStatus));

	kp(("%s(%d)     Max sockets: %d (meaningless in 2.0 and later)\n", _FL, wsa_data.iMaxSockets));

   #endif



	WinSockVersionInstalled = (LOBYTE(wsa_data.wHighVersion) << 8) + HIBYTE(wsa_data.wHighVersion);

	//kp(("%s(%d) Winsock version supported = 0x%x\n", _FL, WinSockVersionInstalled));



  #endif // WIN32

	return ERR_NONE;	// no error

}





//****************************************************************

// 

//

//	Close/Shutdown our various IP related functions.

//	Return 0 for success, else error code.

//

ErrorType IP_Close(void)

{

	pr(("%s(%d) IP_Close() has been called.\n",_FL));

  #if WIN32

	WSACleanup();

  #endif // WIN32

	return ERR_NONE;	// no error

}





//****************************************************************

//  Wed January 28/98 - MB

//

//	Determine if a particular IP address is public or private.

//	Return 0 for private, 1 for public.

//



int IP_IsIPPublic(IPADDRESS ip)

{

	// The Internet Assigned Numbers Authority (IANA) has

	// reserved the following three blocks of the IP address

	// space for private networks:

	//  10.0.0.0        -   10.255.255.255

	//  172.16.0.0      -   172.31.255.255

	//  192.168.0.0     -   192.168.255.255

	#define MakeIP(a,b,c,d)	((a)|((b)<<8)|((c)<<16)|((d)<<24))

	IPADDRESS private_ips[] = {

		MakeIP(10,0,0,0),

		MakeIP(172,16,0,0),

		MakeIP(192,168,0,0),

		MakeIP(0,0,0,0)	// end of table.

	};

	IPADDRESS private_ip_masks[] = {

		MakeIP(255,0,0,0),

		MakeIP(255,0xF0,0,0),

		MakeIP(255,255,0,0),

		MakeIP(0,0,0,0)	// end of table.

	};



	if (ip==0) return 0;	// assume private if 0.



	for (int i=0 ; private_ips[i] ; i++) {

		if ((ip & private_ip_masks[i])==private_ips[i])

			return 0;	// this address is in a private range.

	}



	return 1;	// not in private address range, must be public.

}



//****************************************************************

//  Wed January 14/98 - MB

//

//	Determine the IP address of THIS computer.  If there is more than

//	one IP address, only one is returned (which one is undefined).

//	Returns 0 for success, else an error code.

//

int IP_GetLocalIPAddress(IPADDRESS *ip_address_ptr)

{

	int result;

	int returncode = 0;



		pr(("%s(%d) IP_GetLocalIPAddress() begin\n",_FL));

		memset(ip_address_ptr, 0, sizeof(*ip_address_ptr));



		#define MAX_HOSTNAME_LEN	80

		char hostname[MAX_HOSTNAME_LEN];

		result = gethostname(hostname, MAX_HOSTNAME_LEN-1);

		if (result) {

			kp(("%s(%d) Error: gethostname() failed. err=%u\n", _FL, WSAGetLastError()));

			returncode = 2;	// error: gethostname() failed.

		} else {

			hostname[MAX_HOSTNAME_LEN-1] = 0;	// ensure it's always terminated

			pr(("%s(%d) host name = '%s'\n", _FL, hostname));



			struct hostent *host;

			host = gethostbyname(hostname);

			if (!host) {

				kp(("%s(%d) Error: gethostbyname('%s') failed. err=%u\n",_FL,hostname,WSAGetLastError()));

				returncode = 3;	// error: gethostbyname() failed.

			} else {

				// We get back a list of addresses...

				// if one exists, return the first public one.

				// If no public ones exist, just return the first one.

				if (*host->h_addr_list) {

					int i=0;

					struct in_addr *ipptr;

					while ((ipptr=(struct in_addr *)host->h_addr_list[i++])!=NULL) {

						IPADDRESS ip = (IPADDRESS)ipptr->s_addr;

						// If this ip is public, save it and exit.

						if (IP_IsIPPublic(ip)) {

							*ip_address_ptr = ip;

							break;

						}

						// This ip is private, only overwrite if we haven't

						// yet found a valid ip.

						if (!*ip_address_ptr)

							*ip_address_ptr = ip;

					}

				}

			  #if DISP	//:::
				struct in_addr *ip;

				while ((ip=(struct in_addr *)*host->h_addr_list++)!=NULL) {

					pr(("%s(%d) ip address: $%08lx (%s) (%s)\n", _FL, ip->s_addr,

							inet_ntoa(*ip), IP_IsIPPublic(ip->s_addr)?"Public":"Private"));

				}

			  #endif

			}

		}

		pr(("%s(%d) IP_GetLocalIPAddress() end.  retcode=%d ip=$%08lx\n",_FL,returncode, *ip_address_ptr));

		return returncode;

}



//****************************************************************

//  Wed January 14/98 - MB

//

//	Convert an IPADDRESS into a string (such as "192.168.132.241")

//	(similar to inet_ntoa() but doesn't require WinSock to be open)

//

void IP_ConvertIPtoString(IPADDRESS ip_address, char *dest_str, int dest_str_len)

{

	char str[20];



		sprintf(str, "%d.%d.%d.%d", (ip_address) & 255,(ip_address>>8) & 255,

				(ip_address>>16) & 255,(ip_address>>24) & 255);

		strnncpy(dest_str, str, dest_str_len);

}



//****************************************************************

//  Mon March 23/98 - MB

//

//	Convert a host name ("www.mindspan.com" or "24.113.34.100") to

//	an IPADDRESS.  Does DNS lookup if necessary.

//	Returns the IP address or 0 if not found.

//

IPADDRESS IP_ConvertHostNameToIP(char *host_name_string)

{

	IPADDRESS result = 0;

	char str[200];

	zstruct(str);



		pr(("%s(%d) IP_ConvertHostNameToIP('%s')\n",_FL,host_name_string));

		// If the name contains nothing but numbers and '.', assume

		// it's just a numeric IP address.

		char *s = host_name_string;

		while (*s=='.' || isdigit(*s)) s++;

		if (*s) {	// look up the name

			struct hostent *host;

			host = gethostbyname(host_name_string);

			if (!host) {

				kp(("%s(%d) Error: cannot lookup '%s'. err=%u\n",_FL,host_name_string,WSAGetLastError()));

				goto done;

			}



			// We get back a list of addresses...

			// if one exists, return the first one.

			if (*host->h_addr_list) {

				struct in_addr *ipptr;

				if ((ipptr=(struct in_addr *)host->h_addr_list[0])!=NULL) {

					result = ipptr->s_addr;

				}

			}

		} else {

			// Just a numeric IP address...

			result = inet_addr(host_name_string);

		}



done:

		pr(("%s(%d) IP_ConvertHostNameToIP('%s') result = $%08lx\n",_FL,host_name_string, result));



	  #if 0	// 2022 kriskoin

		result = 0;

	  #endif

		if (result) {

		  #if 0	//kriskoin: 			//kriskoin: 			// We got a result, see if we need to write it to our cache file.

			#define DOMAIN_TO_CACHE		".kkrekop.io"

			if (strstr(host_name_string, DOMAIN_TO_CACHE)) {

				// Cache it.

				FILE *fd = fopen("hosts", "wt+");

				if (fd) {

					forever {

						zstruct(str);

						if (fgets(str, sizeof(str)-2, fd)) {

							TrimNewlines(str);

							//kp(("%s(%d) Testing '%s'\n", _FL, str));

							// Do a really lousy substring test.  This sucks pretty

							// badly, but it does the job for now.

							if (strstr(str, host_name_string)) {

								kp(("%s(%d) found %s in hosts line '%s'. Using it.\n",

										_FL, host_name_string, str));

								result = inet_addr(str);

								break;

							}

						} else {

							break;	// EOF.

						}

					}

					fclose(fd);

				}

			}

		  #endif

		} else {

			//kriskoin: 			// hosts file in the current directory.

			FILE *fd = fopen("hosts", "rt");

			if (fd) {

				forever {

					zstruct(str);

					if (fgets(str, sizeof(str)-2, fd)) {

						TrimNewlines(str);

						//kp(("%s(%d) Testing '%s'\n", _FL, str));

						// Do a really lousy substring test.  This sucks pretty

						// badly, but it does the job for now.

						if (strstr(str, host_name_string)) {

							kp(("%s(%d) found %s in hosts line '%s'. Using it.\n",

									_FL, host_name_string, str));

							result = inet_addr(str);

							break;

						}

					} else {

						break;	// EOF.

					}

				}

				fclose(fd);

			}

		}

		return result;

}



//*********************************************************

// https://github.com/kriskoin
//

//	Convert an IP address to a hostname (reverse DNS).

//	Returns empty string if DNS lookup failed.

//

void IP_ConvertIPToHostName(IPADDRESS ip, char *host_name_string, int host_name_string_len)

{

	memset(host_name_string, 0, host_name_string_len);

	struct hostent *h = gethostbyaddr((char *)&ip, sizeof(ip), AF_INET);

	//kp(("%s(%d) h = $%08lx\n", _FL, h));

	if (h) {

		strnncpy(host_name_string, h->h_name, host_name_string_len);

	}

}



#if INCL_SSL_SUPPORT

static PPCRITICAL_SECTION SSLCritSec;

//*********************************************************

// https://github.com/kriskoin
//

// Grab/Release the SSL Critical Section

// Makes sure there are no pending SSL errors left before

// releasing.

//

void EnterSSLCritSec0(char *calling_file, int calling_line)

{

	static int initialized = FALSE;

	if (!initialized) {

		PPInitializeCriticalSection(&SSLCritSec, CRITSECPRI_SSL, "SSL");

		initialized = TRUE;

	}

	PPEnterCriticalSection0(&SSLCritSec, calling_file, calling_line);



	// make sure we don't have any errors that need popping...

	int count = 0;

	int err = 0;

	do {

		err = ERR_get_error();

		if (err) {

			kp(("%s(%d) Warning: ERR_get_error() returned %d when we weren't expecting it.\n", _FL, err));

			count++;

		}

	} while (err && count < 20);

}



void LeaveSSLCritSec0(char *calling_file, int calling_line)

{

	// make sure we don't have any errors that need popping...

	int count = 0;

	int err;

	do {

		err = ERR_get_error();

		if (err) {

			count++;

		}

	} while (err && count < 20);



	PPLeaveCriticalSection0(&SSLCritSec, calling_file, calling_line);

}

#endif // INCL_SSL_SUPPORT



//*********************************************************

// https://github.com/kriskoin
//

// Extract a field from an http header.  Returns a pointer to the

// first character after the field name.  The returned string

// might be EOL terminated (either <cr><lf> or just <lf>) or it

// might be null terminated (the case when it is the final field in the header).

// Returns NULL if not found.

//

static char *ExtractHTTPHeaderField(char *http_header, const char *field_name)

{

	char search_string[80];

	search_string[0] = '\n';	// field must start with '\n'

	strnncpy(search_string+1, field_name, 80-1);

	char *p = strstr(http_header, search_string);

	if (p) {

		p += strlen(search_string);	// skip over field name

	}

	return p;

}



//*********************************************************

// https://github.com/kriskoin
//

// Break apart a URL into its component parts

//

ErrorType BreakApartURL(char *input_url,

			char *output_protocol,  int output_protocol_len,

			char *output_host_name, int output_host_name_len,

			unsigned short *output_port,

			char *output_filename,  int output_filename_len)

{

	// First initialize everything in case of an error.

	strnncpy(output_protocol, "http", output_protocol_len);

	*output_port = 80;		// default to the http port

	memset(output_host_name, 0, output_host_name_len);

	memset(output_filename, 0, output_filename_len);



	// Take apart the url.  Remove the leading "http://" (however much

	// exists), seperate out the machine name, and the rest is the

	// file to get.

	// note: this code will fail if there's no leading http and the machine name

	// starts with an h.

	char *url = input_url;

	char *m = "http://";

	if (!strnicmp(url, "https", 5)) {

		m = "https://";

		*output_port = 443;		// switch to https port

		strnncpy(output_protocol, "https", output_protocol_len);

	}

	while (*m && *m==tolower(*url)) {

		url++;

		m++;

	}

	strnncpy(output_host_name, url, output_host_name_len);

	m = strchr(output_host_name, '/');

	if (!m) {

		// No filename after the hostname

		strnncpy(output_filename, "/", output_filename_len);

	} else {

		*m = 0;	// terminate the host name

		strnncpy(output_filename, url+(m-output_host_name), output_filename_len);

	}

	return ERR_NONE;

}



//*********************************************************

// https://github.com/kriskoin
//

// Post a header to a socket (possibly using SSL) and wait for an http reply.

// server_name format is of the form: "https://www.secureserver.com".

// Does not yet handle specific ports (it assumes 80 for http and 443 for https).

// One function writes to a memory buffer, the other writes to a file.

//

// The callback function is called periodically to tell the caller

// what sort of progress is being made.  The callback function can return

// an 'exit flag' or 'cancel flag', which if non-zero will cause this

// function to terminate immediately with an error code.

// A null callback function can be used if you don't care.

//

ErrorType GetHttpPostResult(char *server_name, char *hdr_to_post,

			char *dest_fname, char *dest_buffer, WORD32 dest_buffer_len,

			int (*progress_callback_function)(WORD32 additional_bytes_received_and_written),

			int port_override,

		  #if INCL_SSL_SUPPORT

			void *ssl_ctx,

		  #endif

			int *output_http_result

		)

{

	ErrorType post_result_err = ERR_ERROR;

	SOCKET sock = INVALID_SOCKET;		// the socket we're associated with.

	SOCKADDR_IN connection_address;		// address of who we're actually connected with (once connected)

	int got_headers = FALSE;

	FILE *fd = NULL;

	char *download_buffer = NULL;

	int bytes_in_buffer = 0;

	WORD32 timeout_time = 0;

	WORD32 total_bytes_written = 0;

	WORD32 file_length = 0;				// total # of expected bytes

	int sockerr = 0;

	int bytes_sent = 0;

	int cancel_flag = FALSE;

	int got_eof = FALSE;

	IPADDRESS source_ip = 0;

	unsigned short source_port = 80;	// default to the http port

	char protocol[10];

	char machine_name[50];

	char source_file[1];

	zstruct(protocol);

	zstruct(machine_name);

	zstruct(source_file);

  #if INCL_SSL_SUPPORT

	int use_ssl = FALSE;

	SSL *ssl = NULL;

  #endif



	if (output_http_result) {

		*output_http_result = 500;	// default to 500: internal error

	}

	// Take apart the url.

	if (BreakApartURL(server_name, protocol, sizeof(protocol),

			machine_name, sizeof(machine_name), &source_port,

			source_file, sizeof(source_file)) != ERR_NONE)

	{

		Error(ERR_ERROR, "%s(%d) Malformed url: '%s'", _FL, server_name);

		goto done;

	}

	pr(("%s(%d) GetHttpPostResult(): protocol '%s', server '%s', port %d\n",

			_FL, protocol, machine_name, source_port));

	if (!strcmp(protocol, "https")) {

	  #if INCL_SSL_SUPPORT

		use_ssl = TRUE;

	  #else

		kp(("%s(%d) Warning: https connections are not supported on this platform. url='%s'\n",_FL, server_name));

	  #endif

	}



	source_ip = IP_ConvertHostNameToIP(machine_name);

	if (!source_ip) {

		Error(ERR_ERROR, "%s(%d) Unable to resolve '%s'", _FL, machine_name);

		goto done;

	}



	// Now we've got the machine name turned into an IP address... open the socket.

	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock==INVALID_SOCKET) {

		Error(ERR_ERROR, "%s(%d) Error creating socket (%d)", _FL, WSAGetLastError());

		goto done;

	}



	// set some options

  #if WIN32

	{

  		dword one = 1;

		sockerr = ioctlsocket(sock, FIONBIO, &one);	// enable non-blocking mode.

	}

  #else

	sockerr = fcntl(sock, F_SETFL, O_NONBLOCK);	// enable non-blocking mode.

	if (sockerr != -1) {

		sockerr = 0;	// -1 is error, anything else is successfull.

	}

  #endif

	if (sockerr) {

		Error(ERR_ERROR, "%s(%d) ioctrlsocket() for non-blocking I/O failed.  WSA error = %d", _FL, WSAGetLastError());

		goto done;

	}



	if (port_override) {

		source_port = (short)port_override;

	}

	// connect it.

	pr(("%s(%d) connecting to port %d, use_ssl = %d\n", _FL, source_port, use_ssl));

	zstruct(connection_address);

	connection_address.sin_family = AF_INET;

	connection_address.sin_addr.s_addr = source_ip;

	connection_address.sin_port = htons(source_port);

	sockerr = connect(sock, (LPSOCKADDR)&connection_address, sizeof(connection_address));

	if (sockerr) {

		sockerr = WSAGetLastError();

		// WSAEWOULDBLOCK is a Normal error condition...

		// the connect() is actually pending.

		if (sockerr!=WSAEWOULDBLOCK

		  #if !WIN32

			&& sockerr != EINPROGRESS

			&& sockerr != EAGAIN

		  #endif

		) {

			Error(ERR_ERROR, "%s(%d) Error connecting to port (%d)", _FL, sockerr);

			goto done;

		}

	}



	// Now we're waiting for the socket to connect.

  #if INCL_SSL_SUPPORT

	if (use_ssl) {

		if (!ssl_ctx) {

			kp(("%s(%d) Error: ssl_ctx not passed to GetHttpPostResult() for '%s'\n", _FL, server_name));

			goto done;

		}

		ssl = SSL_new((SSL_CTX *)ssl_ctx);

		if (!ssl) {

			kp(("%s(%d) Error: SSL_new() returned NULL!\n",_FL));

			goto done;

		}

		EnterSSLCritSec();

		SSL_set_fd(ssl, sock);

		SSL_connect(ssl);

		LeaveSSLCritSec();

	}

  #endif



	// Send off the request string. Loop until we can send the request...

	// 24/01/01 kriskoin:

	// Timeout relatively quickly if we haven't sent anything (connection

	// setup is failing), or wait a fairly long time if we've sent a

	// request and we're just waiting for a response.

  #if 1	// 2022 kriskoin

	// increased significantly.  If server is really loaded, it could easily be 45+ seconds

	// before we hear anything back from them.

	#define PRE_POST_TIMEOUT	120	// wait up to n seconds before giving up if we haven't written anything

  #else

	#define PRE_POST_TIMEOUT	45	// wait up to n seconds before giving up if we haven't written anything

  #endif

  #if 1	//kriskoin: 	#define AFTER_POST_TIMEOUT	600	// after writing, wait up to n additional seconds with no data before giving up.

  #else

	#define AFTER_POST_TIMEOUT	240	// after writing, wait up to n additional seconds with no data before giving up.

  #endif

	timeout_time = SecondCounter + PRE_POST_TIMEOUT;

	forever {

		//kp(("%s(%d) Sending request '%s'\n", _FL, request_str));

	  #if INCL_SSL_SUPPORT

		if (use_ssl) {

			EnterSSLCritSec();



			// Check the SSL state... if we're not in the OK state yet, don't

			// try to send anything.

			// Attempt to write nothing to force the connection state to start

			// initializing... (undocumented)

			char zero = 0;

			SSL_write(ssl, &zero, 0);

			//kp(("%s(%d) SSL_state() = $%08lx (%d)\n", _FL, SSL_state(ssl), SSL_state(ssl)));

			if (SSL_is_init_finished(ssl)) {

				// initialization is finished...

				//kp(("%s(%d) SSL initialization finished... sending.\n",_FL));



				// if we got here, we're going to try sending

				post_result_err = ERR_SERIOUS_WARNING;	// we use this to figure out if we _may_ have sent something

				if (output_http_result) {

					*output_http_result = 204;	// new error is 204: no response from server

				}



				// if bytes sent == 0, probably nothing got sent.. but to be safe, if we got to

				// this point, we will have to assume that something has gotten through.

				timeout_time = SecondCounter + AFTER_POST_TIMEOUT;	// update timeout

				bytes_sent = SSL_write(ssl, hdr_to_post, strlen(hdr_to_post));

				if (bytes_sent <= 0) {

					int err = SSL_get_error(ssl, bytes_sent);

					int ssl_err = 0;

					if (err==SSL_ERROR_SSL) {

						ssl_err = ERR_get_error();

					}

					bytes_sent = 0;

					if (   err == SSL_ERROR_NONE

						|| err == SSL_ERROR_WANT_WRITE

						|| err == SSL_ERROR_WANT_READ

						|| err == SSL_ERROR_WANT_X509_LOOKUP

						|| (err == SSL_ERROR_SYSCALL && WSAGetLastError()==WSAENOTCONN)

					) {

						// No data sent.  This is not an error we care about because

						// our timeout code will handle it eventually.

					} else  {

						// Assume end of file (connection closed)

						kp(("%s(%d) Got SSL_ERROR_* # %d (see openssl/ssl.h). Assuming no connection.\n", _FL, err));

						if (err==SSL_ERROR_SYSCALL) {

							kp(("%s(%d) SSL_ERROR_SYSCALL: errno = %d, WSAGetLastError() = %d\n", _FL, errno, WSAGetLastError()));

						}

						LeaveSSLCritSec();

						goto done;

					}

				} else {

					LeaveSSLCritSec();

					break;	// successfully sent (probably the whole thing)

				}

			} else {

				//kp(("%s(%d) SSL initialization is NOT finished... not sending yet.\n",_FL));

			}

			LeaveSSLCritSec();

		} else

	  #endif

		{

			// if we got here, we're going to try sending

			post_result_err = ERR_SERIOUS_WARNING;	// we use this to figure out if we _may_ have sent something

			timeout_time = SecondCounter + AFTER_POST_TIMEOUT;	// update timeout

			pr(("%s(%d) Sending request: '%s'\n", _FL, hdr_to_post));

			bytes_sent = send(sock, hdr_to_post, strlen(hdr_to_post), 0);

			if (bytes_sent == SOCKET_ERROR) {

				sockerr = WSAGetLastError();

				if (sockerr != WSAENOTCONN

				  #if !WIN32

					&& sockerr != EINPROGRESS

					&& sockerr != EAGAIN

				  #endif

				) {

					Error(ERR_ERROR, "%s(%d) send() request failed. err = %d", _FL, WSAGetLastError());

					goto done;

				}

			} else {

				break;	// successfully sent (probably the whole thing)

			}

		}

		if (progress_callback_function) {

			cancel_flag = (*progress_callback_function)(0);

		}

		if (cancel_flag) {

			goto done;

		}

		if (SecondCounter >= timeout_time) {

			Error(ERR_ERROR, "%s(%d) Timeout waiting to connect before sending request in GetHttpPostResult()", _FL);

			goto done;

		}

		Sleep(100);	// sleep briefly, then try again.

	}



	// The request has now been sent... all we have to do now is loop

	// waiting for data and write anything that comes in.



	if (dest_fname) {

		fd = fopen(dest_fname, "wb");

		if (!fd) {

			Error(ERR_ERROR, "%s(%d) could not open '%s' for writing.",_FL,dest_fname);

			goto done;

		}

	}



	// It opened ok... start writing data to it...

	#define BUFFER_SIZE 2048	// about .5s worth on a 56K modem

	download_buffer = (char *)malloc(BUFFER_SIZE+1);	// always leave one extra byte to terminate string overflows.

	if (!download_buffer) {

		Error(ERR_ERROR, "%s(%d) malloc(%d) failed.",_FL,BUFFER_SIZE+1);

		goto done;

	}



	bytes_in_buffer = 0;

	forever {

		memset(download_buffer+bytes_in_buffer, 0, BUFFER_SIZE+1-bytes_in_buffer);	// always zero it out to aid debugging

		int received = 0;

	  #if INCL_SSL_SUPPORT

		if (use_ssl) {

			EnterSSLCritSec();

			received = SSL_read(ssl, download_buffer+bytes_in_buffer, BUFFER_SIZE-bytes_in_buffer);

			if (received <= 0) {

				int err = SSL_get_error(ssl, received);

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

				} else  {

					// Assume end of file (connection probably closed)

					//kp(("%s(%d) Got SSL_ERROR_* # %d (see openssl/ssl.h). Assuming EOF.\n", _FL, err));

					got_eof = TRUE;

				}

			}

			LeaveSSLCritSec();

		} else

	  #endif

		{

			received = recv(sock, download_buffer+bytes_in_buffer, BUFFER_SIZE-bytes_in_buffer, 0);

			if (received == SOCKET_ERROR) {

				received = 0;

				int err = WSAGetLastError();

				if (err == WSAEWOULDBLOCK

					  #if !WIN32

						|| err==WSAEAGAIN

					  #endif

						|| err==0)

				{

					// No data received.  This is not an error we care about because

					// our timeout code will handle it eventually.

				} else {

					// Assume not connected.

					kp(("%s(%d) Got error %d. Assuming not connected\n", _FL, err));

					got_eof = TRUE;

				}

			}

		}



		if (received < 0) {

			// unexpected result, but don't crash.

			received = 0;

		}



		if (received) {

			timeout_time = SecondCounter + AFTER_POST_TIMEOUT;	// update timeout

			bytes_in_buffer += received;

		  #if 0

			kp(("%s(%d) Received %d more bytes, bytes_in_buffer is now %d:\n", _FL, received, bytes_in_buffer));

			khexdump(download_buffer, bytes_in_buffer, 16, 1);

		  #endif

			int data_bytes_left = bytes_in_buffer;	// default to assuming all is data.

			char *data_ptr = download_buffer;		// default to start of buffer

			// Parse the header if necessary...

			if (!got_headers) {

				// Look for \n\n (the signal for the end of the header)

				//kp(("%s(%d) strlen(download_buffer) = %d\n", _FL, strlen(download_buffer)));

				char *p = strstr(download_buffer, "\n\n");

				char *p2 = strstr(download_buffer, "\r\n\r\n");

				//kp(("%s(%d) p = $%08lx, p2 = $%08lx\n", _FL, p, p2));

				int end_hdr_len = 2;

				if (p && p2) {	// found both?

					if (p2 < p) {	// did one come before the other?

						p = p2;		// use the first one.

						end_hdr_len = 4;

					}

				} else if (p2) {	// just found p2.

					p = p2;

					end_hdr_len = 4;

				}

				if (p) {	// found the end...

					*p = 0;

					got_headers = TRUE;

					int header_bytes = p - download_buffer + end_hdr_len;

					data_bytes_left -= header_bytes;

					bytes_in_buffer -= header_bytes;	// might go to zero.

					data_ptr += header_bytes;

				  #if 0	// 2022 kriskoin

					kp(("%s(%d) header_bytes = %d, data bytes left = %d (data_ptr is now $%08lx)\n", _FL, header_bytes, data_bytes_left, data_ptr));

					khexdump(download_buffer, header_bytes, 16, 1);

				  #endif



					// Check the result code and make sure it succeeded...

					// It should look like this: "HTTP/1.1 200 OK\n"

					if (strncmp(download_buffer, "HTTP/", 5)) {

						// Not found.

						Error(ERR_ERROR, "%s(%d) HTTP header error: unrecognized header type",_FL);

						goto done;

					}

					int http_result = 500;	// default error

					p = strchr(download_buffer, ' ');

					if (p) {

						http_result = atoi(p+1);

					}

					if (output_http_result) {

						*output_http_result = http_result;	// save result code

					}

					

					//kp(("%s(%d) http result = %d\n", _FL, http_result));

					if (http_result != 200) {

						Error(ERR_ERROR, "%s(%d) HTTP error %d getting file '%s'",_FL, http_result, server_name);

						goto done;

					}



				  #if 0

				  	kp1(("%s(%d) WARNING: disabling Content-length testing for testing purposes\n",_FL));

					file_length = (WORD32)-1;	// assume infinite size if not known.

				  #else

					// Determine how many more bytes to expect...

					p = ExtractHTTPHeaderField(download_buffer, "Content-Length: ");

					if (p) {

						file_length = atoi(p);

						if (!file_length) {

							break;	// We're finished!  Content length was zero.

						}

					} else {

						pr(("%s(%d) HTTP warning: we don't know the size of '%s'\n",_FL, server_name));

						file_length = (WORD32)-1;	// assume infinite size if not known.

					}

					pr(("%s(%d) Content-Length = %d\n",  _FL, file_length));

				  #endif

				} else {

					// Haven't found the end of the headers yet... make sure we

					// didn't run out of room.

					if (bytes_in_buffer >= BUFFER_SIZE) {

						Error(ERR_ERROR, "%s(%d) Error: http headers are too big!", _FL);

						goto done;

					}

				}

			}



			// If we've gotten the header and there's data left in the

			// buffer, write it to disk.

			if (got_headers && data_bytes_left > 0) {

			  #if 0	// 2022 kriskoin

				kp(("%s(%d) data_bytes_left = %d\n", _FL, data_bytes_left));

				khexdump(data_ptr, data_bytes_left, 16, 1);

			  #endif

				if (fd) {	// write it to a file...

					size_t bytes_written = fwrite(data_ptr, 1, data_bytes_left, fd);

					if (bytes_written != (size_t)data_bytes_left) {

						// Not everything got written.

						// For now, assume a disk full error.

						Error(ERR_ERROR, "%s(%d) InternetReadFile('%s') couldn't write all bytes to disk. Disk full?",_FL,server_name);

						goto done;

					}

				}

				if (dest_buffer) {	// write it to a memory buffer

					int bytes_to_copy = data_bytes_left;

					if (total_bytes_written + bytes_to_copy > dest_buffer_len) {

						// Dest buffer overflow.

						kp(("%s(%d) Warning: dest buffer overflow retrieving URL\n",_FL));

						bytes_to_copy = dest_buffer_len - total_bytes_written;

					}

					if (bytes_to_copy > 0) {

						memcpy(dest_buffer + total_bytes_written, data_ptr, bytes_to_copy);

					}

				}

				total_bytes_written += data_bytes_left;

				pr(("%s(%d) total_bytes_written = %d (added %d to it). file_length = %d\n", _FL, total_bytes_written, data_bytes_left, file_length));

				if (progress_callback_function) {

					cancel_flag = (*progress_callback_function)(data_bytes_left);

				}

				if (cancel_flag) {

					goto done;

				}

				// Clear out the buffer to make room for all new data.

				bytes_in_buffer = 0;

			}

		} else {

			Sleep(100);	// pause briefly, then try for more data.

		}

		if (progress_callback_function) {

			cancel_flag = (*progress_callback_function)(0);

		}

		if (cancel_flag) {

			goto done;

		}



		// If our time is up, give an error and exit.

		if (SecondCounter >= timeout_time) {

			Error(ERR_ERROR, "%s(%d) Timeout reading data in GetHttpPostResult()", _FL);

			goto done;

		}

		if (got_eof || (file_length && total_bytes_written >= file_length)) {

			break;	// We're finished!

		}

	}

	if (got_headers) {

		post_result_err = ERR_NONE;	// success!

	}



done:

	pr(("%s(%d) Done receiving file.\n", _FL));

	if (download_buffer) {

		free(download_buffer);

		download_buffer = NULL;

	}

	if (fd) {

		fclose(fd);

		fd = NULL;

	}



  #if INCL_SSL_SUPPORT

	if (ssl) {

		EnterSSLCritSec();

		SSL_shutdown(ssl);  /* send SSL/TLS close_notify */

		SSL_free(ssl);

		LeaveSSLCritSec();

		ssl = NULL;

	}

  #endif



	if (sock != INVALID_SOCKET) {

		shutdown(sock, 2);	//kriskoin: 		closesocket(sock);

		sock = INVALID_SOCKET;

	}

	return post_result_err;

}



ErrorType GetHttpPostResult(char *server_name, char *hdr_to_post, char *dest_buffer,

			WORD32 dest_buffer_len,

			int (*progress_callback_function)(WORD32 additional_bytes_received_and_written),

			int port_override, 

		  #if INCL_SSL_SUPPORT

			void *ssl_ctx,

		  #endif

			int *output_http_result

		)

{

	return GetHttpPostResult(server_name, hdr_to_post, NULL,

			dest_buffer, dest_buffer_len,

			progress_callback_function, port_override,

		  #if INCL_SSL_SUPPORT

			ssl_ctx,

		  #endif

			output_http_result

		);

}



ErrorType GetHttpPostResult(char *server_name, char *hdr_to_post, char *dest_fname,

			int (*progress_callback_function)(WORD32 additional_bytes_received_and_written),

			int port_override,

		  #if INCL_SSL_SUPPORT

			void *ssl_ctx,

		  #endif

			int *output_http_result

		)

{

	return GetHttpPostResult(server_name, hdr_to_post, dest_fname, NULL, 0,

			progress_callback_function, port_override,

		  #if INCL_SSL_SUPPORT

			ssl_ctx,

		  #endif

			output_http_result

		);

}



//*********************************************************

// https://github.com/kriskoin
//

// Grab a file from a URL and write it to a specific disk file

// using our own direct socket functions.  No proxies, no caching,

// just a direct socket connection to the web server.  If we can get

// a port out, this call should succeed. Uses GetHttpPostResult() to do

// the low level work.

// Note: this function always uses our socket code.  The client has a function

// in upgrade.cpp which will also try using proxy servers.  Use it on the

// client, only use these on the server.

//

#if INCL_SSL_SUPPORT

ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written), int port_override, void *ssl_ctx)

#else

ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written), int port_override)

#endif

{

	// First, break up the url into server name and url



	unsigned short source_port = 80;		// default to the http port

	char protocol[10];

	char machine_name[50];

	char source_file[MAX_FNAME_LEN];

	char request_str[MAX_FNAME_LEN+30];

	char protocol_and_server[100];

	zstruct(protocol_and_server);

	zstruct(protocol);

	zstruct(machine_name);

	zstruct(source_file);

	zstruct(request_str);



	// Take apart the url.

	if (BreakApartURL(source_url, protocol, sizeof(protocol),

			machine_name, sizeof(machine_name), &source_port,

			source_file, sizeof(source_file)) != ERR_NONE)

	{

		Error(ERR_ERROR, "%s(%d) Malformed url: '%s'", _FL, source_url);

		return ERR_ERROR;

	}



	strcpy(protocol_and_server, protocol);

	strcat(protocol_and_server, "://");

	strcat(protocol_and_server, machine_name);



	sprintf(request_str, "GET %s HTTP/1.0\n\n", source_file);



	return GetHttpPostResult(protocol_and_server, request_str, dest_path,

			progress_callback_function, port_override,

		  #if INCL_SSL_SUPPORT

			ssl_ctx,

		  #endif

			NULL	// http_result: don't put it anywhere

		);

}



#if INCL_SSL_SUPPORT

ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written), int port_override)

{

	return WriteFileFromUrlUsingSockets(source_url, dest_path, progress_callback_function, port_override, NULL);

}

#endif

ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written))

{

  #if INCL_SSL_SUPPORT

	return WriteFileFromUrlUsingSockets(source_url, dest_path, progress_callback_function, 0, NULL);

  #else

	return WriteFileFromUrlUsingSockets(source_url, dest_path, progress_callback_function, 0);

  #endif

}



ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path)

{

	return WriteFileFromUrlUsingSockets(source_url, dest_path, NULL, 0);

}

