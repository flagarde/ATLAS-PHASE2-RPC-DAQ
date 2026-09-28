#pragma once
#include "csv.hpp"
#include "yaodaq/Board.hpp"
#include "yaodaq/Connector.hpp"
#include "yaodaq/codec/ProcessIOCodec.hpp"
#include "yaodaq/transport/Process.hpp"

#include <chrono>
#include <cpp-terminal/terminal.hpp>
#include <thread>

class DCT : public yaodaq::Board
{
public:
  DCT( yaodaq::BoardConfig& cfg, const std::string_view name ) : yaodaq::Board( cfg, name, "Vivado" ) { this->setRun( fun ); }
  ~DCT() final {}

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
