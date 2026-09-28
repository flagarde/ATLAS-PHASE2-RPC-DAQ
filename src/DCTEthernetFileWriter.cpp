#include "Data.hpp"
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

struct TFileDeleter
{
  void operator()( TFile* f ) const
  {
    if( !f ) return;
    if( f->IsOpen() ) f->Close();
    delete f;
  }
};
using TFilePtr = std::unique_ptr<TFile, TFileDeleter>;

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
  }
  std::string_view getFileName() const noexcept { return m_name; }

  ~FileWriter() override {}
  bool on_initialize() override { return true; }

  bool on_stop() override
  {
    if( m_rawdata_file )
    {
      m_rawdata_file->Write();
      m_rawdata_file->Close();
      return true;
    }
    else
    {
      error( "m_rawdata_file nullptr" );
      return true;
    }
  }

  bool on_start() override
  {
    if( !m_rawdata_file )
    {
      error( "m_rawdata_file is nullprt! run Configure" );
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
    std::string root_file = m_path.string() + m_folder + "/" + add_root_extension( m_name );
    m_rawdata_file.reset( new TFile( root_file.c_str(), "RECREATE" ) );
    tree = new TTree( "tree", "Events" );
    tree->Branch( "event", &event );
    tree->Branch( "nHits", &nHits );
    tree->Branch( "hit_dct", &hit_dct );
    tree->Branch( "hit_channel", &hit_channel );
    tree->Branch( "hit_clk", &hit_clk );
    tree->Branch( "hit_rawbcid", &hit_rawbcid );
    tree->Branch( "hit_time1", &hit_time1 );
    tree->Branch( "hit_time2", &hit_time2 );
    tree->Branch( "hit_rise", &hit_rise );
    tree->Branch( "hit_layer", &hit_layer );
    tree->Branch( "hit_strip", &hit_strip );
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

  void clearVectors()
  {
    hit_dct.clear();
    hit_channel.clear();
    hit_rawbcid.clear();
    hit_time1.clear();
    hit_time2.clear();
    hit_clk.clear();
    hit_rise.clear();
    hit_layer.clear();
    hit_strip.clear();
    nHits = 0;
  }

  void onRawData( const std::unique_ptr<yaodaq::RawData> raw ) override
  {
    if( raw->topic() == "ca:02:03:04:05:06" )
    {
      clearVectors();
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
      event = doc["event_number"].get_uint64();

      for( auto packet: doc["packets"].get_array() )
      {
        //std::string_view packet_number = packet["packet_number"].get_string();
        //std::cout << "packet_number: " << packet_number << '\n';
        for( auto value: packet["data"].get_array() )
        {
          const std::string_view hex_value = value.get_string();
          std::uint32_t          word{ 0 };
          auto [ptr, ec] = std::from_chars( hex_value.data() + 2 /* skip "0x"*/, hex_value.data() + hex_value.size(), word, 16 );
          const int dct  = word >> 28 & 0xF;
          if( dct == 1 ) clk++;
          if( ( word & 0xFFFFFFF ) != 0x5555555 )
          {
            nHits++;
            int channel{ 0 };
            int bcid{ 0 };
            int time1{ 0 };
            int time2{ 0 };
            int rise = word & 0x1;
            if( rise )
            {
              // assume 8b Strip, 8b BC, 5b rise time, 6b diff, 1b rise/fall
              channel = word >> 20 & 0xFF;
              bcid    = word >> 12 & 0xFF;
              time1   = word >> 7 & 0x1F;
              time2   = word >> 1 & 0x3F;
            }
            else
            {
              // assume 8b Strip, 9b BC, 5b fall1 time, 5b fall2 time, 1b rise/fall
              channel = word >> 20 & 0xFF;
              bcid    = word >> 11 & 0x1FF;
              time1   = word >> 6 & 0x1F;
              time2   = word >> 1 & 0x1F;
            }
            const int connector = channel / 24;
            const int layer     = ( channel % 24 ) / 8;
            const int strip     = 8 * connector + channel % 8;

            hit_layer.push_back( layer );
            hit_strip.push_back( strip );
            hit_dct.push_back( dct );
            hit_channel.push_back( channel );
            hit_rawbcid.push_back( bcid );
            hit_time1.push_back( time1 );
            hit_time2.push_back( time2 );
            hit_clk.push_back( clk );
            hit_rise.push_back( rise );
            std::string name  = strip == 147 ? "Trigger" : fmt::format( "Channel {:>3}", strip );
            auto        style = strip == 147 ? fmt::fg( fmt::color::red ) | fmt::emphasis::bold : fmt::fg( fmt::color::white );
            std::string ret   = fmt::format( "{:<11} {}: bcid {:>3}, time_η1: {:>2}, time_η2: {:>2}", fmt::styled( name, style ),
                                             rise ? fmt::styled( "↥", fmt::fg( fmt::color::red ) | fmt::emphasis::bold ) : fmt::styled( "↧", fmt::fg( fmt::color::green ) | fmt::emphasis::bold ), bcid, time1, time2 );
            info( ret );
          }
        }
      }

      if( tree ) tree->Fill();
      else
        error( "tree is nullptr" );
    }
    else
      info( "Received {}", raw->topic() );
  }

private:
  void generate_folder_name()
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
  std::filesystem::path m_path;    // Path were to store the files
  std::string           m_name;    // name of the file
  std::string           m_folder;  // the folder with date

  std::atomic<std::uint64_t> m_run_number{ 0 };
  TFilePtr                   m_rawdata_file{ nullptr };
  int                        nHits = 0;
  std::vector<int>           hit_dct;      // DCT 0-3
  std::vector<int>           hit_channel;  // channel 1-144
  std::vector<int>           hit_clk;      // time of hit in readout window (tick = 3.125 ns)
  std::vector<int>           hit_rawbcid;
  std::vector<int>           hit_time1;  // time1
  std::vector<int>           hit_time2;  // time2
  std::vector<int>           hit_rise;   // rise or fall
  std::vector<int>           hit_layer;
  std::vector<int>           hit_strip;
  long int                   event{ 0 };
  int                        clk{ 0 };
  TTree*                     tree = nullptr;
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
