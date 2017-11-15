
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <iostream>

typedef struct sockaddr_in AddrType; // ipv4 host address

namespace {
int socket_fd = 0;
AddrType local_addr;
AddrType remote_addr;
}


void
sig_exit_handle( int )
{
    std::cerr << "\nKilled. Exiting..." << std::endl;
    std::exit( EXIT_FAILURE );
}

bool
open_socket()
{
    socket_fd = ::socket( AF_INET, SOCK_DGRAM, 0 );

    if ( socket_fd == -1 )
    {
        std::cerr << "***ERROR*** socket open." << std::endl;
        return false;
    }

    // set close on exec
    ::fcntl( socket_fd, F_SETFD, FD_CLOEXEC );

    return true;
}


int
main()
{
    if ( std::signal( SIGINT, &sig_exit_handle) == SIG_ERR
         || std::signal( SIGTERM, &sig_exit_handle ) == SIG_ERR
         || std::signal( SIGHUP, &sig_exit_handle ) == SIG_ERR )
    {
        std::cerr << __FILE__ << ": " << __LINE__
                  << ": could not set signal handler: "
                  << std::strerror( errno ) << std::endl;
        std::exit( EXIT_FAILURE );
    }

    std::cerr << "Hit Ctrl-C to exit." << std::endl;


    const int server_port = 6000;
    const char* server_host = "localhost";


    if ( ! open_socket() )
    {
        return 1;
    }

    // bind
    {
        std::memset( reinterpret_cast< char * >( &local_addr ),
                     0,
                     sizeof( AddrType ) );
        local_addr.sin_family = AF_INET; // internet connection
        local_addr.sin_addr.s_addr = htonl( INADDR_ANY );
        local_addr.sin_port = htons( 0 );

        if ( ::bind( socket_fd,
                     reinterpret_cast< struct sockaddr * >( &local_addr ),
                     sizeof( AddrType ) ) < 0 )
        {
            std::cerr << "***ERROR*** bind" << std::endl;
            return 1;
        }
    }

    // set server address
    {
        struct addrinfo hints;
        std::memset( &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = 0;

        struct addrinfo * res;
        int err = ::getaddrinfo( server_host, NULL, &hints, &res );
        if ( err != 0 )
        {
            std::cerr << "***ERROR*** could not resolve the host [" << server_host << "]" << std::endl;
            std::cerr << " error=" << err << ' ' << gai_strerror( err ) << std::endl;
            return 1;
        }

        remote_addr.sin_addr.s_addr = (reinterpret_cast< struct sockaddr_in * >(res->ai_addr))->sin_addr.s_addr;
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons( server_port );

        ::freeaddrinfo( res );
    }

    // set nonblocking
    {
        int flags = ::fcntl( socket_fd, F_GETFL, 0 );
        if ( flags == -1 )
        {
            return 1;
        }
        ::fcntl( socket_fd, F_SETFL, O_NONBLOCK | flags );
    }

    // message loop
    {
        fd_set read_fds;
        fd_set read_fds_back;
        char buf[8192];
        std::memset( &buf, 0, sizeof( char ) * 8192 );

        int in = fileno( stdin );
        FD_ZERO( &read_fds );
        FD_SET( in, &read_fds );
        FD_SET( socket_fd, &read_fds );
        read_fds_back = read_fds;

        int max_fd = socket_fd + 1;

        while ( 1 )
        {
            read_fds = read_fds_back;
            int ret = ::select( max_fd, &read_fds, NULL, NULL, NULL );
            if ( ret < 0 )
            {
                perror( "Error selecting input" );
                break;
            }
            else if ( ret != 0 )
            {
                // read from stdin
                if ( FD_ISSET( in, &read_fds ) )
                {
                    if ( std::fgets( buf, sizeof( buf ), stdin ) != NULL )
                    {
                        size_t len = std::strlen( buf );
                        if ( buf[len-1] == '\n' )
                        {
                            buf[len-1] = '\0';
                            --len;
                        }

                        int n = ::sendto( socket_fd, buf, len + 1, 0,
                                          reinterpret_cast< const sockaddr * >( &remote_addr ),
                                          sizeof( AddrType ) );
                        if ( n != len + 1 )
                        {
                            std::perror( "sendto" );
                            continue;
                        }

                        std::cout << buf << std::endl;
                    }
                }

                // read from socket
                if ( FD_ISSET( socket_fd, &read_fds ) )
                {
                    AddrType from_addr;
                    socklen_t from_size = sizeof( AddrType );

                    int n = ::recvfrom( socket_fd, buf, sizeof( buf ), 0,
                                        reinterpret_cast< struct sockaddr * >( &from_addr ),
                                        &from_size );

                    if ( n == -1 )
                    {
                        if ( errno == EWOULDBLOCK )
                        {
                            continue;
                        }
                        std::perror( "recvfrom" );
                        continue;
                    }

                    remote_addr = from_addr;

                    //std::cout << buf << std::endl;
                }
            }
        }
    }

    return 0;
}
