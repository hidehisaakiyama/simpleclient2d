
#include <unistd.h> // close()
#include <sys/types.h> // socket(),bind(),sendto()
#include <sys/socket.h> // socket(),bind(),sendto()
#include <netinet/in.h> // sockaddr_in
#include <netdb.h> // getaddrinfo(),freeaddrinfo()
#include <sys/epoll.h>
#include <signal.h>

#include <cstring>
#include <iostream>
#include <string>

/* --------------------------------------------------------------------------- */
class Client {
private:
    enum {
        MAX_EVENTS = 16,
    };

    bool M_alive;
    int M_socket_fd;
    struct sockaddr M_server_address;
public:

    Client();
    ~Client();

    bool createSocket( const std::string & server_host,
                       const int server_port );
    bool sendInit();
    void run();
    void exit();

private:
    bool send( const char * data,
               const size_t len );
    bool receive();
};

namespace {
Client client;
}

void
sigint_handle( int )
{
    std::cerr << "\nInterrupted. exiting..." << std::endl;
    client.exit();
}

/* --------------------------------------------------------------------------- */
int
main( int argc, char **argv )
{
    std::string server_host = "localhost";
    int server_port = 6000;

    signal( SIGINT, &sigint_handle );

    if ( ! client.createSocket( server_host, server_port ) )
    {
        std::cerr << "Could not connect to the server." << std::endl;
        return 1;
    }

    if ( ! client.sendInit() )
    {
        std::cerr << "Could not send a init command." << std::endl;
        return 1;
    }

    client.run();

    return 0;
}

/* --------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------- */
Client::Client()
    : M_alive( false ),
      M_socket_fd( -1 )
{

}

/* --------------------------------------------------------------------------- */
Client::~Client()
{
    if ( M_socket_fd >= 0 )
    {
        close( M_socket_fd );
        M_socket_fd = -1;
    }
}

/* --------------------------------------------------------------------------- */
bool
Client::createSocket( const std::string & server_host,
                      const int server_port )
{
    // resolve the host name, and get the address info
    struct addrinfo hints;
    std::memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC; // for IPv4/IPv6
    hints.ai_socktype = SOCK_DGRAM; // for datagram socket
    hints.ai_flags = 0;
    hints.ai_protocol = 0; // any protocol

    char service[16];
    std::snprintf( service, 15, "%d", server_port );

    // resolve the host and get the destination address
    struct addrinfo * result;
    int err = ::getaddrinfo( server_host.c_str(), service, &hints, &result );
    if ( err != 0 )
    {
        printf( "getaddrinfo %d : %s\n", err, gai_strerror( err ) );
        return false;
    }

    M_socket_fd = -1;

    // create a socket file descriptor, and save the destination address
    struct addrinfo * d;
    for ( d = result; d != nullptr; d = d->ai_next )
    {
        int fd = ::socket( d->ai_family, d->ai_socktype, d->ai_protocol );
        if ( fd == -1 )
        {
            continue;
        }

        M_socket_fd = fd;
        M_server_address = *d->ai_addr;
        break;
    }

    freeaddrinfo( result );

    if ( d == nullptr )
    {
        std::cerr << "Could not resolve the server address. host=[" << server_host << "] port=" << server_port << std::endl;
        return false;
    }

    return true;
}

/* --------------------------------------------------------------------------- */
bool
Client::sendInit()
{
    const char *msg = "(init Test)";
    if ( send( msg, std::strlen( msg ) ) )
    {
        M_alive = true;
    }

    return M_alive;
}

/* --------------------------------------------------------------------------- */
void
Client::run()
{
    const int timeout_seconds = 5;

    struct epoll_event events[MAX_EVENTS];

    const int epoll_fd = epoll_create( MAX_EVENTS );
    if ( epoll_fd == -1 )
    {
        perror( "epoll_create" );
        M_alive = false;
        return;
    }

    // register input events
    {
        struct epoll_event ev;

        // standard input
        memset( &ev, 0, sizeof( ev ) );
        ev.events = EPOLLIN;
        ev.data.fd = fileno( stdin );
        if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fileno( stdin ), &ev ) == -1 )
        {
            perror( "epoll_ctl" );
            close( epoll_fd );
            M_alive = false;
            return;
        }

        // socket
        memset( &ev, 0, sizeof( ev ) );
        ev.events = EPOLLIN;
        ev.data.fd = M_socket_fd;
        if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, M_socket_fd, &ev ) == -1 )
        {
            perror( "epoll_ctl" );
            close( epoll_fd );
            M_alive = false;
            return;
        }
    }

    // main loop
    while ( M_alive )
    {
        const int nfds = epoll_wait( epoll_fd, events, MAX_EVENTS, timeout_seconds * 1000 );

        if ( nfds < 0 )
        {
            perror( "epoll_wait" );
            return;
        }
        else if ( nfds == 0 )
        {
            // timeout
            std::cerr << "No message from the server. exit." << std::endl;
            M_alive = false;
        }
        else
        {
            for ( int i = 0; i < nfds; ++i )
            {
                if ( events[i].data.fd == fileno( stdin ) )
                {
                    // read from stdin
                }
                else
                {
                    if ( events[i].events & EPOLLIN
                         && events[i].data.fd == M_socket_fd )
                    {
                        receive();
                    }
                }
            }
        }
    }
}

/* --------------------------------------------------------------------------- */
void
Client::exit()
{
    M_alive = false;
}

/* --------------------------------------------------------------------------- */
bool
Client::send( const char * data,
              const size_t len )
{
    int n = ::sendto( M_socket_fd, data, len, 0, &M_server_address, sizeof( M_server_address ) );
    if ( n != static_cast< int >( len ) )
    {
        std::perror( "sendto" );
        return false;
    }

    return true;
}

/* --------------------------------------------------------------------------- */
bool
Client::receive()
{
    char buf[8192];

    struct sockaddr from_addr;
    socklen_t from_size = sizeof( struct sockaddr );
    int n = ::recvfrom( M_socket_fd, buf, sizeof( buf ), 0, &from_addr, &from_size );

    if ( n == -1 )
    {
        if ( errno == EWOULDBLOCK )
        {
            return true;
        }

        std::perror( "recvfrom" );
        return false;
    }

    // update the destination address
    M_server_address = from_addr;

    std::cout << buf << std::endl;

    return true;
}
