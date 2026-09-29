#include "Data.hpp"
#include "EthernetAnalysor.hpp"
#include "RawData.hpp"
#include "TBranch.h"
#include "TFile.h"
#include "TROOT.h"
#include "TTree.h"
#include "fmt/chrono.h"
#include "fmt/std.h"

#include <CLI/CLI.hpp>
#include <ROOT/RNTupleWriter.hxx>
#include <TBufferJSON.h>
#include <chrono>
#include <cpp-terminal/color.hpp>
#include <cpp-terminal/input.hpp>
#include <cpp-terminal/iostream.hpp>
#include <cpp-terminal/terminal.hpp>
#include <filesystem>
#include <memory>
#include <simdjson.h>
#include <yaodaq/Module.hpp>

class FileWriter : public yaodaq::Module
{
private:
public:
  FileWriter( yaodaq::Config cfg, const std::string_view name ) : yaodaq::Module( cfg, "EthernetDCT", "FileWriter" )
  {
    Add( "getFileName", jsonrpc::GetHandle( &FileWriter::getFileName, *this ) );
    Add( "setFileName", jsonrpc::GetHandle( &FileWriter::setFileName, *this ) );
    ROOT::Experimental::DisableObjectAutoRegistration();
    ROOT::EnableImplicitMT();
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
    analysor.setLogger( this->get_logger() );
  }
  std::string_view getFileName() const noexcept { return m_name; }

  ~FileWriter() override {}
  bool on_initialize() override { return true; }

  bool on_stop() override
  {
    analysor.close_raw_file();
    analysor.close_events_file();
    return true;
  }

  bool on_start() override
  {
    if( !analysor.raw_file_open() )
    {
      error( "raw_file is not created or opened !" );
      return false;
    }
    if( !analysor.event_file_open() )
    {
      error( "event_file is not created or opened !" );
      return false;
    }
    return true;
  }

  bool on_configure() override
  {
    generate_folder_name();
    if( m_name.empty() )
    {
      error( "A filename should be given" );
      return false;
    }

    std::error_code ec;
    if( !std::filesystem::create_directories( m_path / m_folder, ec ) )
    {
      if( ec )
      {
        error( "Failed to create directory: {}", ec.message() );
        return false;
      }
    }
    std::filesystem::create_directories( m_path / m_folder );
    std::string root_file = m_path.string() + m_folder + "/" + add_root_extension( m_name + "_raw" );
    analysor.prepare_raw_file( root_file );
    analysor.prepare_raw_tree();
    std::string root_file_event = m_path.string() + m_folder + "/" + add_root_extension( m_name + "_events" );
    analysor.prepare_events_file( root_file_event );
    analysor.prepare_events_tree();
    return true;
  }

  std::string_view setFileName( const std::string& name )
  {
    m_name = std::string( name );
    info( "filename changed to {}", m_name );
    return m_name;
  }

  void setPath( const std::string_view path )
  {
    m_path = path;
    if( !std::filesystem::exists( m_path ) || !m_path.is_absolute() || !m_path.filename().empty() ) throw yaodaq::Exception( "Path must be absolute and must exist" );
  }

  void onRawData( const std::unique_ptr<yaodaq::RawData> raw ) override
  {
    if( raw->topic() == "ca:02:03:04:05:06" )
    {
      analysor.clear_raw_data();
      analysor.clear_events_data();
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
      analysor.setEventNumber( doc["event_number"].get_uint64() );
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
            analysor.raw_analyse( raw );
            std::string name  = raw.is_trigger() ? "Trigger" : fmt::format( "Channel {:>3}", raw.get_channel() );
            auto        style = raw.is_trigger() ? fmt::fg( fmt::color::red ) | fmt::emphasis::bold : fmt::fg( fmt::color::white );
            std::string ret =
              fmt::format( "{:<11} {}: bcid {:>3}, time_η1: {:>2}, time_η2: {:>2}", fmt::styled( name, style ),
                           raw.is_raise() ? fmt::styled( "↥", fmt::fg( fmt::color::red ) | fmt::emphasis::bold ) : fmt::styled( "↧", fmt::fg( fmt::color::green ) | fmt::emphasis::bold ), raw.get_bcid(), raw.get_eta1_fine_time(), raw.get_eta2_fine_time() );
            info( ret );
          }
        }
      }
      analysor.setNbHits();
      analysor.events_analyse();
      analysor.Fill_raw_file();
      analysor.Fill_events_file();
      analysor.calculateEfficiency();
    }
  }
  void setDTMax( const int dtmax ) noexcept { analysor.setDTMax( dtmax ); }
  void setDTMin( const int dtmin ) noexcept { analysor.setDTMin( dtmin ); }
  void setRPCType( const std::string type ) { analysor.setRPCType( type ); }

private:
  std::string rpc_type;
  void        generate_folder_name()
  {
    const auto now = std::chrono::system_clock::now();
    const auto t   = std::chrono::system_clock::to_time_t( now );
    std::tm    tm{};
#ifdef _WIN32
    localtime_s( &tm, &t );
#else
    localtime_r( &t, &tm );
#endif
    m_folder = fmt::format( "{:%Y%m%d}", tm );
  }
  std::string add_root_extension( const std::string_view filename )
  {
    std::filesystem::path p( filename );
    if( !p.has_extension() )
    {
      p += ".root";  // or p.replace_extension(".root");
      warn( "Appending .root to {}", m_name );
    }
    return p.string();
  }
  //TFile m_file;
  std::filesystem::path      m_path;    // Path were to store the files
  std::string                m_name;    // name of the file
  std::string                m_folder;  // the folder with date
  std::atomic<std::uint64_t> m_run_number{ 0 };
  EthernetAnalysor           analysor;
};

int main( int argc, char* argv[] )
try
{
  Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
  CLI::App app{ "YAODAQ client" };
  argv = app.ensure_utf8( argv );
  std::string host{ "127.0.0.1" };
  app.add_option( "-i,--ip", host, "IP of the server" ) /*->check( CLI::ValidIPV4 )*/;
  int port{ 8888 };
  app.add_option( "-p,--port", port, "Port to listen" )->check( CLI::Range( 0, 65535 ) );
  std::string path{ "/data/RPC/YAODAQ/" };
  app.add_option( "--path", path, "Path where to store the file" );
  std::string file_name{ "Unknown" };
  app.add_option( "--file_name", file_name, "filename folder" );
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
  FileWriter module( cfg, "DCT" );
  module.setPath( path );
  module.setFileName( file_name );
  module.setDTMax( dt_max );
  module.setDTMin( dt_min );
  module.setRPCType( rpc_type );
  module.link();

  std::size_t nbrCTLC{ 3 };
  Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press " << std::to_string( nbrCTLC ) << " times CTRL+C to stop" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
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
        else
        {
          nbrCTLC = 3;
          Term::cout << Term::color_fg( Term::Color::Name::Red ) << "Press Ctrl+Q " << std::to_string( nbrCTLC ) << " times to quit" << Term::color_fg( Term::Color::Name::Default ) << std::endl;
        }
        break;
      }
      default: break;
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
