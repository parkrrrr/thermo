
#include <iostream>
#include <boost/asio.hpp>

class Controller {
  private:
  boost::asio::io_service io_service; 
  boost::asio::serial_port port;

  public:
    Controller( std::string device );
    ~Controller( void );

    std::string Command( std::string cmdString );
    int Temperature( void );
};

Controller::Controller( std::string device ) :
  io_service(), port( io_service, device )
{
  port.set_option(boost::asio::serial_port_base::baud_rate(2400));
  port.set_option(boost::asio::serial_port_base::stop_bits());
  port.set_option(boost::asio::serial_port_base::parity());
  port.set_option(boost::asio::serial_port_base::character_size(8));
  port.set_option(boost::asio::serial_port_base::flow_control());  
}

Controller::~Controller( void ) {
}

std::string Controller::Command( std::string cmdString ) {
  boost::asio::streambuf read_buffer;
  std::string req=cmdString;
  req += "\r\n";
  boost::asio::write( port, boost::asio::buffer(req));
  boost::asio::read_until( port, read_buffer, "\r\n" );
  std::istream is(&read_buffer);
  std::string line;
  std::getline(is, line ); 
  return line;
}

int Controller::Temperature( void ) {
  std::string line = Command( "*V01");

  std::stringstream stream(line.substr(4));
  int tempval = 0;  
  stream >> tempval;
  return tempval;
}

int main( int argc, char **argv ) {
  Controller controller( "/dev/ttyUSB0" );
  int temp = controller.Temperature(); 
  std::cout << temp << "\n\n";
}
