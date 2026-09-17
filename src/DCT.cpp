#include "csv.hpp"
#include "yaodaq/Board.hpp"
#include "yaodaq/Connector.hpp"
#include "yaodaq/codec/ProcessIOCodec.hpp"
#include "yaodaq/transport/Process.hpp"

#include <CLI/CLI.hpp>
#include <atomic>
#include <chrono>
#include <cpp-terminal/color.hpp>
#include <cpp-terminal/input.hpp>
#include <cpp-terminal/iostream.hpp>
#include <cpp-terminal/terminal.hpp>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

class Vivado : public yaodaq::Board
{
public:
  Vivado( yaodaq::BoardConfig& cfg, const std::string_view name ) : yaodaq::Board( cfg, name, "Vivado" ) { this->setRun( fun ); }
  ~Vivado() final {}
  std::function<bool( std::stop_token )> fun = [this]( std::stop_token stop ) -> bool
  {
    std::filesystem::path file = m_path / fmt::format( "event_{}.csv", event() );
    info( "Triggering event {}", event() );
    auto start = std::chrono::steady_clock::now();
    sendCommand( "run_hw_ila [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]" );
    sendCommand( "wait_on_hw_ila [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]" );
    sendCommand( "upload_hw_ila_data [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]" );
    sendCommand( fmt::format( "write_hw_ila_data -legacy_csv_file -force -quiet {} hw_ila_data_2", file.c_str() ) );
    while( !fileExists( file ) )
    {
      if( stop.stop_requested() ) return true;
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    auto end = std::chrono::steady_clock::now();
    info( "Event {} extracted in {} us", event(), std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() );

    auto lastSize = std::filesystem::file_size( file );
    while( !stop.stop_requested() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      auto newSize = std::filesystem::file_size( file );
      if( newSize == lastSize ) break;
      lastSize = newSize;
    }
    if( !stop.stop_requested() ) readFile( file, event() );
    return true;
  };

  bool pre_connect( const bool alreadyDone ) override
  {
    info( "pre-connect" );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return true;
  }

  bool post_connect() override
  {
    info( "post_connect()" );
    sendCommand( "open_hw_manager" );
    sendCommand( "connect_hw_server" );
    sendCommand( "open_hw_target" );
    sendCommand( "puts __YAODAQ_CONNECT_VIVADO_FINISHED__" );
    while( !connect_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
      warn( "Waiting configure step to finish !" );
    }
    connect_finished.store( false );
    return true;
  }

  bool pre_disconnect( const bool alreadyDone ) override
  {
    info( "pre_disconnect()" );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    sendCommand( "close_hw_target" );
    sendCommand( "disconnect_hw_server" );
    sendCommand( "close_hw_manager" );
    sendCommand( "puts __YAODAQ_DISCONNECT_VIVADO_FINISHED__" );
    sendCommand( "exit" );
    while( !disconnect_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
      warn( "Waiting disconnect step to finish !" );
    }
    disconnect_finished.store( false );
    return true;
  }

  bool post_disconnect() override
  {
    info( "running post_disconnect()" );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return true;
  }

  bool on_configure() override
  {
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    sendCommand( "current_hw_device [get_hw_devices xc7a200t_0]" );
    sendCommand( "refresh_hw_device -update_hw_probes false [lindex [get_hw_devices xc7a200t_0] 0]" );
    sendCommand( fmt::format( "set_property PROBES.FILE {}/top.ltx [get_hw_devices xc7a200t_0]", m_firmware_path ) );
    sendCommand( fmt::format( "set_property FULL_PROBES.FILE {}/top.ltx [get_hw_devices xc7a200t_0]", m_firmware_path ) );
    sendCommand( fmt::format( "set_property PROGRAM.FILE {}/top.bit [get_hw_devices xc7a200t_0]", m_firmware_path ) );
    sendCommand( "program_hw_devices [get_hw_devices xc7a200t_0]" );
    sendCommand( "refresh_hw_device [lindex [get_hw_devices xc7a200t_0] 0]" );
    sendCommand( "set daq_window 128" );
    sendCommand( "set eta1_1 000000000000000000000000" );
    sendCommand( "set eta1_2 000000000000000000000000" );
    sendCommand( "set eta1_3 000000000000000000000000" );
    sendCommand( "set eta1_4 000000000000000000000000" );
    sendCommand( "set eta1_5 000000000000000000000000" );
    sendCommand( "set eta1_6 000000000000000000000000" );

    //#          . layer2. layer1. layer0
    sendCommand( "set eta2_1 000000000000000000000000" );
    sendCommand( "set eta2_2 000000000000000000000000" );
    sendCommand( "set eta2_3 000000000000000000000000" );
    sendCommand( "set eta2_4 000000000000000000000000" );
    sendCommand( "set eta2_5 000000000000000000000000" );
    sendCommand( "set eta2_6 000000000000000000000000" );

    sendCommand( "set mask_eta1 $eta1_6$eta1_5$eta1_4$eta1_3$eta1_2$eta1_1" );
    sendCommand( "set mask_eta2 $eta2_6$eta2_5$eta2_4$eta2_3$eta2_2$eta2_1" );

    sendCommand( "display_hw_ila_data [ get_hw_ila_data hw_ila_data_2 -of_objects [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]]" );
    sendCommand( "set_property CONTROL.DATA_DEPTH $daq_window [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]" );
    sendCommand( "set_property CONTROL.TRIGGER_POSITION 30 [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]" );
    sendCommand( "set_property TRIGGER_COMPARE_VALUE eq1'bR [get_hw_probes SMA_in_buf  -of_objects [get_hw_ilas -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ila_elinks_inst\"}]]" );

    sendCommand( "set_property OUTPUT_VALUE_RADIX BINARY [get_hw_probes channel_mask_left -of_objects [get_hw_vios -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ctrl_word_vio\"}]]" );
    sendCommand( "set_property OUTPUT_VALUE $mask_eta1 [get_hw_probes channel_mask_left -of_objects [get_hw_vios -of_objects [get_hw_devices xc7a200t_0]	-filter	{CELL_NAME=~\"ctrl_word_vio\"}]]" );
    sendCommand( "commit_hw_vio [get_hw_probes channel_mask_left -of_objects [get_hw_vios -of_objects [get_hw_devices xc7a200t_0]	-filter	{CELL_NAME=~\"ctrl_word_vio\"}]]" );

    sendCommand( "set_property OUTPUT_VALUE_RADIX BINARY [get_hw_probes channel_mask_right -of_objects [get_hw_vios -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ctrl_word_vio\"}]]" );
    sendCommand( "set_property OUTPUT_VALUE $mask_eta2 [get_hw_probes channel_mask_right -of_objects [get_hw_vios -of_objects [get_hw_devices xc7a200t_0]  -filter {CELL_NAME=~\"ctrl_word_vio\"}]]" );
    sendCommand( "commit_hw_vio [get_hw_probes channel_mask_right -of_objects [get_hw_vios -of_objects [get_hw_devices xc7a200t_0] -filter {CELL_NAME=~\"ctrl_word_vio\"}]]" );
    sendCommand( "puts __YAODAQ_CONFIGURE_VIVADO_FINISHED__" );
    while( !configure_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
      warn( "Waiting connect step to finish !" );
    }
    configure_finished.store( false );
    return true;
  }

  bool on_start() override
  {
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return createTempDirectory();
  }

  void                     setFirmwarePath( const std::string_view path ) noexcept { m_firmware_path = path; }
  void                     setVivadoConfigured() { configure_finished.store( true ); }
  void                     setVivadoConnected() { connect_finished.store( true ); }
  void                     setVivadoDisconnected() { disconnect_finished.store( true ); }
  std::vector<std::string> splitMessage( const std::string_view message )
  {
    // split by line and remove INFO:
    std::vector<std::string> lines;
    std::string              re = std::string( message );
    std::istringstream       iss( re );
    std::string              line;
    while( std::getline( iss, line ) )
    {
      // Remove trailing whitespace characters
      while( !line.empty() && std::isspace( static_cast<unsigned char>( line.back() ) ) ) { line.pop_back(); }

      if( !line.empty() ) { lines.push_back( std::move( line ) ); }
    }
    return lines;
  }
  void setKeepRawFiles( const bool keep ) { keep_raw_files = keep; }
  bool KeepRawFiles() const noexcept { return keep_raw_files; }

private:
  bool readFile( const std::filesystem::path& file, const std::size_t i )
  {
    thread_local std::string json;
    json.reserve( 8192 );

    info( "Reading file {}", file.c_str() );
    const int word_col{ 3 };
    const int bcid_col{ 4 };
    try
    {
      //std::ifstream file_(file.c_str());
      //std::string content(std::istreambuf_iterator<char>(file_),std::istreambuf_iterator<char>());
      //file_.close();
      //send( yaodaq::RawDataBuilder::from_text( json, "MPI::DCT::Singlets::File" ) );

      csv::CSVReader reader( file.c_str() );

      json.clear();
      json += R"({"event_number":)";
      json += std::to_string( i );
      json += R"(,"rawdata":[)";

      bool first = true;
      for( auto& row: reader )
      {
        const auto word = row[word_col].get<std::string_view>();
        if( word == "5555555" ) continue;
        const auto bcid = row[bcid_col].get<std::string_view>();
        if( !first ) json += ',';
        json += R"({"word":")";
        json.append( word.data(), word.size() );
        json += R"(","bcid":")";
        json.append( bcid.data(), bcid.size() );
        json += R"("})";
        first = false;
      }
      json += "]}";

      send( yaodaq::RawDataBuilder::from_text( json, "MPI::DCT::Singlets::RawData" ) );

      if( !KeepRawFiles() )
      {
        if( std::filesystem::remove( file ) ) info( "removed {}", file.c_str() );
        else
          error( "can't remove {}", file.c_str() );
      }

      return true;
    }
    catch( const std::exception& e )
    {
      error( "problem reading/parsing {}: {}", file.c_str(), e.what() );
      return false;
    }
    catch( ... )
    {
      error( "problem reading/parsing {}", file.c_str() );
      return false;
    }
  }
  bool fileExists( const std::string& path ) { return std::filesystem::exists( path ); }
  bool createTempDirectory()
  {
    char  dirTemplate[] = "/tmp/DCTRawData_XXXXXX";
    char* result        = mkdtemp( dirTemplate );
    if( result )
    {
      m_path = result;
      return true;
    }
    return false;
  }
  std::filesystem::path m_path;

  std::atomic<bool> connect_finished{ false };
  std::atomic<bool> configure_finished{ false };
  std::atomic<bool> disconnect_finished{ false };
  std::string       m_firmware_path;
  bool              keep_raw_files{ false };
  void              sendCommand( const std::string_view command )
  {
    const std::string                com = std::string( command ) + '\n';
    //info("Sending command: {}",com);
    std::unique_ptr<yaodaq::RawData> raw = std::make_unique<yaodaq::RawData>( yaodaq::RawDataBuilder::from_text( com, "command" ) );
    send_to_device( std::move( raw ) );
  }
};

int main( int argc, char* argv[] )
{
  Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
  CLI::App app{ "DCT" };
  argv = app.ensure_utf8( argv );
  std::string name{ "controller" };
  app.add_option( "-n,--name", name, "Name of the client" );
  std::string host{ "127.0.0.1" };
  app.add_option( "-i,--ip", host, "IP of the server" ) /*->check( CLI::ValidIPV4 )*/;
  int port{ 8888 };
  app.add_option( "-p,--port", port, "Port to listen" )->check( CLI::Range( 0, 65535 ) );
  yaodaq::verbosity::level verbosity{ yaodaq::verbosity::level::info };
  app.add_option( "--verbosity", verbosity, "Verbosity" )->transform( CLI::CheckedTransformer( yaodaq::verbosity::map, CLI::ignore_case ) );
  std::string firmware_path{ "/home/user/Desktop/Mattia_python/bi_dct_data_acquisition_update_M/BI_DCT_FW/" };
  app.add_option( "--firmware_path", firmware_path, "Firmware to burn the FPGA" );
  bool keep_raw_files{ false };
  app.add_flag( "-k,--keep_raw_files", keep_raw_files, "keep or delete raw files" );
  std::uint64_t nbr_event{ ( std::numeric_limits<std::uint64_t>::max )() };
  app.add_option( "-e,--number_events", nbr_event, "Number of event to take" );
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
  Vivado board( cfg, "MyVivado" );
  board.setFirmwarePath( firmware_path );
  board.setKeepRawFiles( keep_raw_files );
  board.setMaxEvents( nbr_event );
  board.link();
  board.dispatcher().subscribe<yaodaq::RawData>(
    [&board]( const yaodaq::RawData& msg )
    {
      std::string_view text( reinterpret_cast<const char*>( msg.payload().data() ), msg.payload().size() );
      if( text.find( "__YAODAQ_CONFIGURE_VIVADO_FINISHED__" ) != std::string_view::npos ) { board.setVivadoConfigured(); }
      else if( text.find( "__YAODAQ_CONNECT_VIVADO_FINISHED__" ) != std::string_view::npos ) { board.setVivadoConnected(); }
      else if( text.find( "__YAODAQ_DISCONNECT_VIVADO_FINISHED__" ) != std::string_view::npos ) { board.setVivadoDisconnected(); }
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
      else if( msg.topic() == "stdout" )
      {
        std::vector<std::string> lines = board.splitMessage( text );
        for( std::size_t i = 0; i != lines.size(); ++i ) { board.info( lines[i] ); }
      }
      else if( msg.topic() == "stderr" )
        board.error( text );
    } );

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
