#include "Data.hpp"
#include "EthernetAnalysor.hpp"
#include "RPCDataAnalyzer.hpp"
#include "RawData.hpp"
#include "TApplication.h"
#include "TSystem.h"
#include "fmt/chrono.h"
#include "fmt/std.h"

#include <CLI/CLI.hpp>
#include <TBufferJSON.h>
#include <THttpServer.h>
#include <chrono>
#include <cpp-terminal/color.hpp>
#include <cpp-terminal/input.hpp>
#include <cpp-terminal/iostream.hpp>
#include <cpp-terminal/terminal.hpp>
#include <filesystem>
#include <memory>
#include <simdjson.h>
#include <yaodaq/Module.hpp>

class Analyser : public yaodaq::Module
{
public:
  Analyser( yaodaq::Config cfg, const std::string_view name ) : yaodaq::Module( cfg, name, "Analyser" )
  {
    m_analyse.setLogger( this->get_logger() );
    std::string url = "http:" + std::string( cfg.getHost() ) + ":" + std::to_string( cfg.getPort() + 1 );
    m_server        = std::make_unique<THttpServer>( url.c_str() );
    m_server->SetTimer( 0, kTRUE );
    //m_analyse.finalize();
    m_thread = std::jthread(
      [this]( std::stop_token st )
      {
        while( !st.stop_requested() ) { m_server->ProcessRequests(); }
      } );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
  }

  void setDTMax( const int dtmax ) noexcept { m_analyse.setDTMax( dtmax ); }
  void setDTMin( const int dtmin ) noexcept { m_analyse.setDTMin( dtmin ); }
  void setRPCType( const std::string type ) { m_analyse.setRPCType( type ); }

  ~Analyser() override { m_thread.request_stop(); }

  bool on_initialize() override
  {
    clear();
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return true;
  }

  void onRawData( const std::unique_ptr<yaodaq::RawData> raw ) override
  {
    if( raw->topic() == "ca:02:03:04:05:06" )
    {
      m_analyse.clear_raw_data();
      m_analyse.clear_events_data();
      thread_local simdjson::ondemand::parser parser;
      const char*                             data = reinterpret_cast<const char*>( raw->payload().data() );
      std::size_t                             len  = raw->payload().size();
      simdjson::padded_string                 json( data, len );
      auto                                    doc = parser.iterate( json );
      if( doc.error() )
      {
        error( "JSON error: {}", simdjson::error_message( doc.error() ) );
        return;
      }
      m_analyse.setEventNumber( doc["event_number"].get_uint64() );
      std::uint8_t clk{ 0 };
      for( auto packet: doc["packets"].get_array() )
      {
        //std::string_view packet_number = packet["packet_number"].get_string();
        //std::cout << "packet_number: " << packet_number << '\n';
        for( auto value: packet["data"].get_array() )
        {
          const std::string_view hex_value = value.get_string();
          std::uint32_t          word{ 0 };
          auto [ptr, ec] = std::from_chars( hex_value.data() + 2 /* skip "0x"*/, hex_value.data() + hex_value.size(), word, 16 );
          DCT::DecodedRawData raw( word, 0 );
          if( raw.get_dct() == 1 )
          {
            clk++;
            raw.setClock( clk );
          }
          if( ( word & 0xFFFFFFF ) != 0x5555555 )
          {
            m_analyse.raw_analyse( raw );
            std::string name  = raw.is_trigger() ? "Trigger" : fmt::format( "Channel {:>3}", raw.get_channel() );
            auto        style = raw.is_trigger() ? fmt::fg( fmt::color::red ) | fmt::emphasis::bold : fmt::fg( fmt::color::white );
            std::string ret = fmt::format( "{:<11} {}: dct {}, bcid {:>3}, time_η1: {:>2}, time_η2: {:>2}", fmt::styled( name, style ),
                                           raw.is_raise() ? fmt::styled( "↥", fmt::fg( fmt::color::red ) | fmt::emphasis::bold ) : fmt::styled( "↧", fmt::fg( fmt::color::green ) | fmt::emphasis::bold ), raw.get_dct(), raw.get_bcid(), raw.get_eta1_fine_time(),
                                           raw.get_eta2_fine_time() );
            info( ret );
          }
        }
      }
      m_analyse.setNbHits();
      m_analyse.events_analyse();
      m_analyse.calculateEfficiency();
    }
  }

  bool on_configure() override
  {
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
    return true;
  }

  bool on_stop() override
  {
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
    return true;
  }

  bool on_start() override { return true; }

  void clear()
  {
    warn( "Clearing histograms" );
    reset_event();
    //m_analyse.reset();
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
  }

private:
  EthernetAnalysor             m_analyse;
  std::jthread                 m_thread;
  std::unique_ptr<THttpServer> m_server{ nullptr };
};

int main( int argc, char* argv[] )
try
{
  Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
  CLI::App app{ "YAODAQ client" };
  argv = app.ensure_utf8( argv );
  //TApplication rootApp( "ROOT", &argc, argv,nullptr,-1 );
  std::string host{ "127.0.0.1" };
  app.add_option( "-i,--ip", host, "IP of the server" ) /*->check( CLI::ValidIPV4 )*/;
  int port{ 8888 };
  app.add_option( "-p,--port", port, "Port to listen" )->check( CLI::Range( 0, 65535 ) );
  std::size_t event_number{ 1000 };
  app.add_option( "-e,--events", event_number, "Number of events to take" );
  std::string name{ "Analysor" };
  app.add_option( "-n,--name", name, "Name of the client" );
  std::string rpc_type{ "BIS2_6" };
  app.add_option( "--rpc_type", rpc_type, "Name of the RPC type" );
  int dt_min{ -190 };
  app.add_option( "--dt_min", dt_min, "DT min" );
  int dt_max{ -100 };
  app.add_option( "--dt_max", dt_max, "DT max" );
  try
  {
    app.parse( argc, argv );
  }
  catch( const CLI::ParseError& e )
  {
    return app.exit( e );
  }
  yaodaq::Config cfg;
  cfg.setPort( port ).setHost( host );
  Analyser module( cfg, name );
  module.setDTMax( dt_max );
  module.setDTMin( dt_min );
  module.setRPCType( rpc_type );
  std::size_t nbrCTLC{ 3 };
  Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press " << std::to_string( nbrCTLC ) << " times CTRL+C to stop" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
  module.link();
  while( true )
  {
    Term::Event event = Term::read_event();
    switch( event.type() )
    {
      case Term::Event::Type::Key:
      {
        Term::Key key( event );
        if( key == Term::Key::Ctrl_Q )
        {
          --nbrCTLC;
          if( nbrCTLC == 0 ) return 0;
          else
            Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press Ctrl+Q " << std::to_string( nbrCTLC ) << " times to quit" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
        }
        else if( key == Term::Key::c ) { module.clear(); }
        else
        {
          nbrCTLC = 3;
          Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press Ctrl+Q " << std::to_string( nbrCTLC ) << " times to quit" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
        }
        break;
      }
      default:
      {
        break;
      }
    }
  };
  return 0;
}
catch( const yaodaq::Exception& exception )
{
  Term::cerr << Term::color_fg( Term::Color::Name::Red ) << exception.what() << Term::color_fg( Term::Color::Name::Default ) << std::endl;
}
catch( const std::exception& exception )
{
  Term::cerr << Term::color_fg( Term::Color::Name::Red ) << exception.what() << Term::color_fg( Term::Color::Name::Default ) << std::endl;
}
catch( ... )
{
  Term::cerr << Term::color_fg( Term::Color::Name::Red ) << "error" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
}
