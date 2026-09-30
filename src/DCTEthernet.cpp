#include "yaodaq/Board.hpp"
#include "yaodaq/Connector.hpp"
#include "yaodaq/codec/ProcessIOCodec.hpp"
#include "yaodaq/transport/Process.hpp"

#include <CLI/CLI.hpp>
#include <DCTEthernet.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cpp-terminal/color.hpp>
#include <cpp-terminal/input.hpp>
#include <cpp-terminal/iostream.hpp>
#include <cpp-terminal/terminal.hpp>
#include <csignal>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

int main( int argc, char* argv[] )
{
  Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
  CLI::App app{ "Ethernet" };
  argv = app.ensure_utf8( argv );
  std::string name{ "Ethernet" };
  app.add_option( "-n,--name", name, "Name of the client" );
  std::string host{ "127.0.0.1" };
  app.add_option( "-i,--ip", host, "IP of the server" ) /*->check( CLI::ValidIPV4 )*/;
  int port{ 8888 };
  app.add_option( "-p,--port", port, "Port to listen" )->check( CLI::Range( 0, 65535 ) );
  yaodaq::verbosity::level verbosity{ yaodaq::verbosity::level::info };
  app.add_option( "--verbosity", verbosity, "Verbosity" )->transform( CLI::CheckedTransformer( yaodaq::verbosity::map, CLI::ignore_case ) );
  std::uint64_t nbr_event{ ( std::numeric_limits<std::uint64_t>::max )() };
  app.add_option( "-e,--number_events", nbr_event, "Number of event to take" );

  std::string firmwares_path{ "/home/user/Desktop/Mattia_python/bi_dct_data_acquisition/" };
  app.add_option( "--firmwares_path", firmwares_path, "path of the firmwares" );
  std::string dct_firmware_name{ "top_sha_8525cd57" };
  app.add_option( "--dct_firmware", dct_firmware_name, "DCT firmware name" );
  std::string board_firmware_name{ "daq_kcu116_sha_1f11706" };
  app.add_option( "--board_firmware_name", board_firmware_name, "Board firmware name" );
  std::string dct_self_firmware_name{ "top_sha_8525cd57" };
  app.add_option( "--dct_self_firmware", dct_self_firmware_name, "DCT firmware name" );
  std::string board_self_firmware_name{ "daq_kcu116_sha_0e17e7d" };
  app.add_option( "--board_self_firmware_name", board_self_firmware_name, "Board firmware name" );
  std::string ethernet_board{ "eno1" };
  app.add_option( "--ethernet_name", ethernet_board, "Name of the ethernet" );
  std::string mac_address{ "ca:02:03:04:05:06" };
  app.add_option( "--mac", mac_address, "mac address" );
  bool self_trigger{ false };
  app.add_flag( "--self", self_trigger, "Use self triggering" );
  bool spy{ false };
  app.add_flag( "--spy", spy, "Just spy" );

  try
  {
    app.parse( argc, argv );
  }
  catch( const CLI::ParseError& e )
  {
    return app.exit( e );
  }

  yaodaq::BoardConfig cfg( std::make_unique<yaodaq::Connector>( std::make_unique<yaodaq::ProcessTransport>( "Vivado" ), std::make_unique<yaodaq::ProcessIOCodec>( "Vivado", "puts \"{}\"\n" ) ) );

  cfg.transportParameters().set( "executable", std::string( "/opt/vivado/2025.2/Vivado/bin/vivado" ) ).set( "args", yaodaq::Parameters::string_list{ "-nojournal", "-nolog", "-verbose", "-mode", "tcl" } );
  cfg.setPort( port ).setHost( host ).verbosity( verbosity );
  DCTEthernet board( cfg, name );
  board.setFirmwaresPath( firmwares_path );
  board.setDCTFirmwareName( dct_firmware_name );
  board.setDCTSelfFirmwareName( dct_self_firmware_name );
  board.setBoardFirmwareName( board_firmware_name );
  board.setBoardSelfFirmwareName( board_self_firmware_name );
  board.setEthernetName( ethernet_board );
  board.setMacAdress( mac_address );
  board.setMaxEvents( nbr_event );
  board.setSelfTrigger( self_trigger );
  board.setSpy( spy );
  board.dispatcher().subscribe<yaodaq::RawData>(
    [&board]( const yaodaq::RawData& msg )
    {
      std::string_view text( reinterpret_cast<const char*>( msg.payload().data() ), msg.payload().size() );
      if( text.find( "__YAODAQ_DCT_FINISHED__" ) != std::string_view::npos ) { board.setDCTConfigured(); }
      else if( text.find( "__YAODAQ_BOARD_FINISHED__" ) != std::string_view::npos ) { board.setBoardConfigured(); }
      else if( text.find( "__YAODAQ_CONNECT_FINISHED__" ) != std::string_view::npos ) { board.setConnected(); }
      else if( text.find( "__YAODAQ_DISCONNECT_FINISHED__" ) != std::string_view::npos ) { board.setDisconnected(); }
      else if( text.find( "INFO:" ) != std::string_view::npos )
      {
        std::vector<std::string>   lines  = board.splitMessage( text );
        constexpr std::string_view prefix = "INFO:";
        for( std::size_t i = 0; i != lines.size(); ++i )
        {
          if( lines[i].starts_with( prefix ) )
          {
            lines[i].erase( 0, prefix.size() );
            // Remove optional leading whitespace
            while( !lines[i].empty() && std::isspace( static_cast<unsigned char>( lines[i].front() ) ) ) lines[i].erase( lines[i].begin() );
          }
          if( !lines[i].empty() ) { board.info( "{}", lines[i] ); }
        }
      }
      else if( text.find( "ERROR:" ) != std::string_view::npos )
      {
        std::vector<std::string>   lines  = board.splitMessage( text );
        constexpr std::string_view prefix = "ERROR:";
        for( std::size_t i = 0; i != lines.size(); ++i )
        {
          if( lines[i].starts_with( prefix ) )
          {
            lines[i].erase( 0, prefix.size() );
            // Remove optional leading whitespace
            while( !lines[i].empty() && std::isspace( static_cast<unsigned char>( lines[i].front() ) ) ) lines[i].erase( lines[i].begin() );
          }
          if( !lines[i].empty() ) { board.error( "{}", lines[i] ); }
        }
      }
      else if( text.find( "WARNING:" ) != std::string_view::npos )
      {
        std::vector<std::string>   lines  = board.splitMessage( text );
        constexpr std::string_view prefix = "WARNING:";
        for( std::size_t i = 0; i != lines.size(); ++i )
        {
          if( lines[i].starts_with( prefix ) )
          {
            lines[i].erase( 0, prefix.size() );
            // Remove optional leading whitespace
            while( !lines[i].empty() && std::isspace( static_cast<unsigned char>( lines[i].front() ) ) ) lines[i].erase( lines[i].begin() );
          }
          if( !lines[i].empty() ) { board.warn( "{}", lines[i] ); }
        }
      }
      else if( msg.topic() == "stdout" )
      {
        std::vector<std::string> lines = board.splitMessage( text );
        for( std::size_t i = 0; i != lines.size(); ++i ) { board.info( lines[i] ); }
      }
      else if( msg.topic() == "stderr" )
        board.error( text );
    } );

  board.setMaxEvents( nbr_event );
  board.link();

  std::size_t nbrCTLC{ 3 };
  Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press " << std::to_string( nbrCTLC ) << " times CTRL+Q to stop" << Term::color_fg( Term::Color::Name::Default ) << std::endl;

  while( true )
  {
    Term::Event event = Term::read_event();

    switch( auto key = event.type() )
    {
      case Term::Event::Type::Key:
      {
        Term::Key key( event );

        if( key == Term::Key::Ctrl_Q )
        {
          --nbrCTLC;

          if( nbrCTLC == 0 ) return 0;

          Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press Ctrl+Q " << std::to_string( nbrCTLC ) << " times to quit" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
        }
        else
        {
          nbrCTLC = 3;
        }

        break;
      }

      default: break;
    }
  }
  return 0;
}
