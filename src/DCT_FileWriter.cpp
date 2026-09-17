#include "Data.hpp"
#include "RawData.hpp"
#include "TFile.h"
#include "TROOT.h"
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
  bool create_models()
  {
    m_rawdata_model = std::move( ROOT::RNTupleModel::Create() );
    if( !m_rawdata_model ) return false;
    m_intermediate_data_model = std::move( ROOT::RNTupleModel::Create() );
    if( !m_intermediate_data_model ) return false;
    m_data_model = std::move( ROOT::RNTupleModel::Create() );
    if( !m_data_model ) return false;
    return true;
  }
  bool create_fields()
  {
    m_rawdata = m_rawdata_model->MakeField<std::vector<DCT::RawData>>( "raw_data" );
    if( !m_rawdata )
    {
      error( "m_rawdata_model->MakeField<std::vector<DCT::RawData>>( \"raw_data\" ) failed" );
      return false;
    }

    m_intermediate_event = m_intermediate_data_model->MakeField<DCT::IntermediateEvent>( "intermediate_event" );
    if( !m_intermediate_event )
    {
      error( "m_intermediate_data_model->MakeField<std::vector<std::uint32_t>>(\"event\") failed" );
      return false;
    }

    m_event = m_data_model->MakeField<DCT::Event>( "event" );
    if( !m_event )
    {
      error( "m_data_model->MakeField<DCT::Event>(\"event\");" );
      return false;
    }
    return true;
  }

public:
  FileWriter( yaodaq::Config cfg, const std::string_view name ) : yaodaq::Module( cfg, "DCT", "FileWriter" )
  {
    Add( "getFileName", jsonrpc::GetHandle( &FileWriter::getFileName, *this ) );
    Add( "setFileName", jsonrpc::GetHandle( &FileWriter::setFileName, *this ) );
    ROOT::Experimental::DisableObjectAutoRegistration();
    ROOT::EnableImplicitMT();
    if( !create_models() ) throw yaodaq::Exception( "Model creations failed !" );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );  //ROOT is doing bad stufs
  }
  std::string_view getFileName() const noexcept { return m_name; }

  ~FileWriter() override {}
  bool on_initialize() override { return create_fields(); }

  bool on_stop() override
  {
    info( "Closing m_writer" );
    if( m_rawdata_writer ) { m_rawdata_writer.reset( nullptr ); }
    else
      error( "Writer for raw_data is nullptr" );
    if( m_intermediate_writer ) { m_intermediate_writer.reset( nullptr ); }
    else
      error( "Writer intermediate data is nullptr" );
    if( m_event_writer ) { m_event_writer.reset( nullptr ); }
    else
      error( "Writer for event is nullptr" );
    m_run_number.fetch_add( 1 );
    return true;
  }

  bool on_start() override
  {
    if( !create_models() ) return false;
    if( !create_fields() ) return false;
    // raw_data
    m_rawdata_file = m_path / m_folder / add_root_extension( m_name + "_raw_data_Run" + std::to_string( m_run_number.load() ) );
    info( "Creating ROOT file: {}", m_rawdata_file );
    m_rawdata_writer = ROOT::RNTupleWriter::Recreate( std::move( m_rawdata_model ), "raw_data", m_rawdata_file.string() );
    if( m_rawdata_writer ) m_rawdata_writer->EnableMetrics();

    // intermediate_data
    m_intermediate_data_file = m_path / m_folder / add_root_extension( m_name + "_intermediate_data_Run" + std::to_string( m_run_number.load() ) );
    info( "Creating ROOT file: {}", m_intermediate_data_file );
    m_intermediate_writer = ROOT::RNTupleWriter::Recreate( std::move( m_intermediate_data_model ), "intermediate_data", m_intermediate_data_file.string() );
    if( m_intermediate_writer ) m_intermediate_writer->EnableMetrics();

    m_data_file = m_path / m_folder / add_root_extension( m_name + "_hits_Run" + std::to_string( m_run_number.load() ) );
    info( "Creating ROOT file: {}", m_data_file );
    m_event_writer = ROOT::RNTupleWriter::Recreate( std::move( m_data_model ), "hits", m_data_file.string() );
    if( m_event_writer ) m_event_writer->EnableMetrics();
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
    if( raw->topic() == "MPI::DCT::Singlets::RawData" )
    {
      const std::string_view j( reinterpret_cast<const char*>( raw->payload().data() ), raw->payload().size() );
      const nlohmann::json   json = nlohmann::json::parse( j );  //  TODO use simdjson

      const std::uint64_t event_number{ json["event_number"].get<std::uint64_t>() };
      info( "Decoding event {}", event_number );

      // just some hints of how many hits will be stored (approximation but can but goo to reserve upfront the vectors)
      const std::size_t         estimated_hits_number = json["rawdata"].size();
      std::vector<DCT::RawData> raw;
      raw.reserve( event_number );

      DCT::IntermediateEvent intermediate( event_number );
      intermediate.reserve_hits( estimated_hits_number );

      DCT::Event event( event_number );
      event.reserve_hits( estimated_hits_number );

      for( const auto& element: json["rawdata"] )
      {
        const std::uint32_t word{ static_cast<std::uint32_t>( std::stoul( element["word"].get<std::string>(), nullptr, 16 ) ) };
        const std::uint32_t bcid{ static_cast<std::uint32_t>( std::stoul( element["bcid"].get<std::string>(), nullptr, 16 ) ) };
        raw.emplace_back( word, bcid );
        const DCT::DecodedRawData raw_data( word, bcid );  // intermediate step;
        intermediate.hits.push_back( raw_data );
        event.push_back( raw_data );

        std::string name = raw_data.is_trigger() ? "Trigger" : fmt::format( "Channel {:>3}", raw_data.get_channel() );

        auto style = raw_data.is_trigger() ? fmt::fg( fmt::color::red ) | fmt::emphasis::bold : fmt::fg( fmt::color::white );

        fmt::print( "{:<11} {}: bcid {:>3}, time_η1: {:>2}, time_η2: {:>2}\n", fmt::styled( name, style ),
                    raw_data.is_raise() ? fmt::styled( "↥", fmt::fg( fmt::color::red ) | fmt::emphasis::bold ) : fmt::styled( "↧", fmt::fg( fmt::color::green ) | fmt::emphasis::bold ), raw_data.get_bcid(), raw_data.get_eta1_fine_time(),
                    raw_data.get_eta2_fine_time() );
      }
      if( m_event ) *m_event = event;
      else
        error( "not writing to file {}", m_rawdata_file.string() );
      if( m_intermediate_event ) *m_intermediate_event = intermediate;
      else
        error( "not writing to file {}", m_intermediate_data_file.string() );
      if( m_rawdata ) *m_rawdata = raw;
      else
        error( "not writing to file {}", m_data_file.string() );
      fill();
      // send the events to our fellow analysers ;)
      //TString event_json = TBufferJSON::ToJSON( m_event.get() );
      //send( yaodaq::RawDataBuilder::from_text( event_json.Data(), "MPI::DCT::Singlets::Events" ) );
    }
    else
      info( "Received {}", raw->topic() );
  }

private:
  void fill()
  {
    std::size_t m_writter_bytes{ 0 };
    if( m_rawdata_writer )
    {
      m_writter_bytes = m_rawdata_writer->Fill();
      //std::ostringstream oss;
      //m_rawdata_writer->GetMetrics().Print(oss);
      info( "writing {} bytes to {}", m_writter_bytes, m_rawdata_file );
      //debug("{}",oss.str());
    }
    if( m_intermediate_writer )
    {
      m_writter_bytes = m_intermediate_writer->Fill();
      //std::ostringstream oss;
      //m_intermediate_writer->GetMetrics().Print(oss);
      info( "writing {} bytes to {}", m_writter_bytes, m_intermediate_data_file );
      //debug("{}",oss.str());
    }
    if( m_event_writer )
    {
      m_writter_bytes = m_event_writer->Fill();
      //std::ostringstream oss;
      //m_event_writer->GetMetrics().Print(oss);
      info( "writing {} bytes to {}", m_writter_bytes, m_data_file );
      //debug("{}",oss.str());
      //std::cout<<oss.str()<<std::endl;
    }
  }
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

  std::filesystem::path                      m_rawdata_file;            // raw_data full path
  std::filesystem::path                      m_intermediate_data_file;  // intermediate data
  std::filesystem::path                      m_data_file;               // data full path
  // rawdata
  std::unique_ptr<ROOT::RNTupleModel>        m_rawdata_model{ nullptr };
  std::shared_ptr<std::vector<DCT::RawData>> m_rawdata{ nullptr };
  std::unique_ptr<ROOT::RNTupleWriter>       m_rawdata_writer{ nullptr };

  // rawdata_decoded
  std::unique_ptr<ROOT::RNTupleModel>     m_intermediate_data_model{ nullptr };
  std::shared_ptr<DCT::IntermediateEvent> m_intermediate_event{ nullptr };
  std::unique_ptr<ROOT::RNTupleWriter>    m_intermediate_writer{ nullptr };

  // event
  std::unique_ptr<ROOT::RNTupleModel>  m_data_model{ nullptr };
  std::shared_ptr<DCT::Event>          m_event{ nullptr };
  std::unique_ptr<ROOT::RNTupleWriter> m_event_writer{ nullptr };
  std::atomic<std::uint64_t>           m_run_number{ 0 };
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
