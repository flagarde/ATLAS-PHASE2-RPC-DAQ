#include "Data.hpp"
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
#include <yaodaq/Module.hpp>

class Analyser : public yaodaq::Module
{
public:
  Analyser( yaodaq::Config cfg, const std::string_view name ) : yaodaq::Module( cfg, name, "Analyser" )
  {
    m_analyse.setLogger( this->get_logger() );
    m_analyse.setEnableDelayCut( true );
    std::string url = "http:" + std::string( cfg.getHost() ) + ":" + std::to_string( cfg.getPort() + 1 );
    m_server        = std::make_unique<THttpServer>( url.c_str() );
    m_server->SetTimer( 0, kTRUE );
    m_analyse.finalize();
    m_thread = std::jthread(
      [this]( std::stop_token st )
      {
        while( !st.stop_requested() )
        {
          m_server->ProcessRequests();
          //std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
      } );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
  }

  ~Analyser() override { m_thread.request_stop(); }

  bool on_initialize() override
  {
    clear();
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return true;
  }

  void onRawData( const std::unique_ptr<yaodaq::RawData> raw ) override
  {
    if( raw->topic() == "MPI::DCT::Singlets::RawData" )
    {
      const std::string_view j( reinterpret_cast<const char*>( raw->payload().data() ), raw->payload().size() );
      const nlohmann::json   json = nlohmann::json::parse( j );  //  TODO use simdjson

      const std::uint64_t event_number{ json["event_number"].get<std::uint64_t>() };

      // just some hints of how many hits will be stored (approximation but can but goo to reserve upfront the vectors)
      const std::size_t estimated_hits_number = json["rawdata"].size();

      DCT::Event event( event_number );
      event.reserve_hits( estimated_hits_number );

      for( const auto& element: json["rawdata"] )
      {
        const std::uint32_t       word{ static_cast<std::uint32_t>( std::stoul( element["word"].get<std::string>(), nullptr, 16 ) ) };
        const std::uint32_t       bcid{ static_cast<std::uint32_t>( std::stoul( element["bcid"].get<std::string>(), nullptr, 16 ) ) };
        const DCT::DecodedRawData raw_data( word, bcid );
        event.push_back( raw_data );
      }

      m_analyse.ProcessEvent( event );
      m_analyse.finalize();
      auto effi = m_analyse.getEfficiencies();
      info( "Event {}", event_number );
      for( std::size_t i = 0; i != effi.size(); ++i ) { info( "Efficiency layer: {}, side: {}, {:.3f} ± {:.3f}", effi[i].getSource().getLayer(), effi[i].getSource().getSide(), effi[i].getEfficiency().efficiency(), effi[i].getEfficiency().error() ); }
      info( "\n" );
      Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
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
    m_analyse.reset();
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
  }

private:
  RPCDataAnalyzer              m_analyse;
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
  std::string path_binary{ "/home/user/Desktop/Mattia_python/bi_dct_data_acquisition_update_M/BI_DCT_FW/" };
  app.add_option( "--path", path_binary, "binaries path to load to the DCT" );
  std::size_t event_number{ 1000 };
  app.add_option( "-e,--events", event_number, "Number of events to take" );
  std::string vivado_path{ "/opt/vivado/2025.2/Vivado/bin/" };
  app.add_option( "--vivado", vivado_path, "Vivado installation path" );
  std::string name{ "Analysor" };
  app.add_option( "-n,--name", name, "Name of the client" );
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
  Analyser    module( cfg, name );
  //client.setTLS("/home/work/YAODAQ-1/localhost.crt","/home/work/YAODAQ-1/localhost.key","NONE");
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
