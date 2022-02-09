
// g++ asio-client.cpp -pthread

#include <unistd.h> // dup()

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <memory>

#define MAX_MESG 8192

/* --------------------------------------------------------------------------- */
class Client {
private:
    boost::asio::io_service M_io_service;

    boost::asio::signal_set M_signals;

    boost::asio::ip::udp::socket M_socket;
    boost::asio::ip::udp::endpoint M_server_endpoint;

    boost::asio::steady_timer M_timer;
    std::chrono::system_clock::time_point M_restarted_time_point;
    std::int64_t M_waited_msec;

    boost::asio::posix::stream_descriptor M_input;
    boost::asio::streambuf M_input_buffer;

    std::string M_send_message;
    char M_receive_buffer[MAX_MESG];
    std::string M_received_message;
public:

    Client( const std::string & server_host,
            const int server_port );

    void run();
private:

    void start();
    void startTimer();

    void sendMessage();
    std::size_t receiveMessage();
    void handleMessage();
};

/* --------------------------------------------------------------------------- */
int
main()
{
    std::string server_host = "localhost";
    int server_port = 6000;

    Client client( server_host, server_port );

    client.run();

    return 0;
}

/* --------------------------------------------------------------------------- */
Client::Client( const std::string & server_host,
                const int server_port )
    : M_signals( M_io_service, SIGINT ),
      M_socket( M_io_service, boost::asio::ip::udp::v4() ),
      M_timer( M_io_service ),
      M_waited_msec( 0 ),
      M_input( M_io_service, ::dup( STDIN_FILENO ) ),
      M_input_buffer( MAX_MESG )
{
    using namespace boost::asio::ip;

    udp::resolver resolver( M_io_service );
    udp::resolver::query q( udp::v4(), server_host, std::to_string( server_port ) );

    try
    {
        M_server_endpoint = *resolver.resolve( q );
        M_socket.non_blocking( true );
    }
    catch ( std::exception & e )
    {
        std::cerr << e.what() << std::endl;
        return;
    }

    start();
}

/* --------------------------------------------------------------------------- */
void
Client::run()
{
    M_io_service.run();
}

/* --------------------------------------------------------------------------- */
void
Client::start()
{
    M_signals.async_wait([this]( const boost::system::error_code & ec, int signal_number )
                           {
                               if ( ! ec
                                    && signal_number == SIGINT )
                               {
                                   std::cerr << "\nKilled. Exiting..." << std::endl;
                                   M_socket.cancel();
                                   M_timer.cancel();
                                   M_input.cancel();
                               }
                           } );

    M_send_message = "(init Test (version 16))";
    sendMessage();

    M_timer.expires_from_now( std::chrono::milliseconds( 0 ) );
    startTimer();
}

/* --------------------------------------------------------------------------- */
void
Client::startTimer()
{
    M_timer.expires_at( M_timer.expires_at() + std::chrono::milliseconds( 10 ) );
    M_timer.async_wait( [this]( const boost::system::error_code & ec )
                          {
                              if ( ! ec )
                              {
                                  const auto elapsed = std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::system_clock::now() - M_restarted_time_point );
                                  M_waited_msec += elapsed.count();

                                  handleMessage();

                                  if ( M_waited_msec < 5 * 1000 )
                                  {
                                      startTimer();
                                  }
                                  else
                                  {
                                      std::cout << "Waited " << M_waited_msec/1000 << " seconds. Quit.." << std::endl;
                                      M_signals.cancel();
                                      M_socket.cancel();
                                      M_input.cancel();
                                  }
                              }
                          } );

    boost::asio::async_read_until( M_input, M_input_buffer, '\n',
                                   [this]( const boost::system::error_code & ec, std::size_t bytes_read )
                                     {
                                         if ( ! ec )
                                         {
                                             char buf[MAX_MESG];
                                             M_input_buffer.sgetn( buf, bytes_read - 1 );
                                             M_input_buffer.consume( 1 ); // remove the last newline
                                             M_send_message = buf;
                                             sendMessage();
                                         }
                                     } );

    M_restarted_time_point = std::chrono::system_clock::now();
}

/* --------------------------------------------------------------------------- */
void
Client::sendMessage()
{
    try
    {
        M_socket.send_to( boost::asio::buffer( M_send_message ),
                          M_server_endpoint );
    }
    catch ( std::exception & e )
    {
        std::cerr << e.what() << std::endl;
    }
}

/* --------------------------------------------------------------------------- */
std::size_t
Client::receiveMessage()
{
    boost::asio::socket_base::bytes_readable command( true );
    try
    {
        M_socket.io_control( command );
    }
    catch ( std::exception & e )
    {
        std::cerr << e.what() << std::endl;
        return 0;
    }

    const std::size_t bytes_readable = command.get();
    if ( bytes_readable == 0 )
    {
        return 0;
    }

    char buf[MAX_MESG];
    std::size_t n = M_socket.receive_from( boost::asio::buffer( buf ), M_server_endpoint );
    M_received_message.assign( buf, n );

    return n;
}

/* --------------------------------------------------------------------------- */
void
Client::handleMessage()
{
    //int count = 0;
    while ( receiveMessage() > 0 )
    {
        M_waited_msec = 0;

        //std::cout << ++count << ": " << M_received_message << std::endl;
    }
}
