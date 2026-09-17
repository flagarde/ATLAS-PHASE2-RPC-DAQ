#include "RPCDataAnalyzer.hpp"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

// =====================================================================
//  RPC Data Analyzer - Code Structure (Index)
// =====================================================================
//  1. Configuration & Constants*
//  2. Data Structures (Hit, EventData)
//  3. Analyzer Class
//     3.1 Constructor* & Destructor*
//     3.2 Configuration (setOutputDir)
//     3.3 Event Processing (processEvent)
//         3.3.x Functions*
//     3.4 Finalization (finalize)
//         3.4.x Functions*
//     3.5 File Processing (runFromFile)
//     3.6 Realtime Mode (runRealtime)
//  4. Private Member Variables*
//  5. Main Function
// =====================================================================
// need to add something when adding new functions
namespace fs = std::filesystem;

int main( int argc, char** argv )
{
  CLI::App    app{ "RPC Analyzer" };
  std::string file_name;
  app.add_option( "-f,--file", file_name, "Input ROOT file" )->required()->check( CLI::ExistingFile );

  //define all variables that can be configured
  bool        enableChannel    = true;
  bool        enableEfficiency = true;
  bool        enableTot        = true;
  bool        enableDelay      = true;
  bool        enableTrigger    = true;
  bool        enableCluster    = true;
  bool        enablePosition   = true;
  double      detectorLength   = 1705.0;
  double      detectorWidth    = 1107.0;
  double      signalSpeed      = 220.0;
  double      bcidClock        = 25.0;
  int         bcidPeriod       = 256;
  int         maxChannels      = 48;
  bool        enableCut        = false;
  int         chanTol          = 2;
  double      timeTol          = 20.0;
  int         maxTriggers      = 10;
  bool        enableDelayCut   = false;
  double      delayMin         = -150;
  double      delayMax         = -75;
  std::string ignoreList;
  bool        noIgnore = false;

  app.add_option( "--enable-channel", enableChannel, "Enable channel counting" );
  app.add_option( "--enable-efficiency", enableEfficiency, "Enable efficiency" );
  app.add_option( "--enable-tot", enableTot, "Enable TOT histograms" );
  app.add_option( "--enable-delay", enableDelay, "Enable signal delay" );
  app.add_option( "--enable-trigger-count", enableTrigger, "Enable trigger count" );
  app.add_option( "--enable-cluster", enableCluster, "Enable cluster size" );
  app.add_option( "--enable-position", enablePosition, "Enable position reconstruction" );
  app.add_option( "--detector-length", detectorLength, "Detector length (mm)" );
  app.add_option( "--detector-width", detectorWidth, "Detector width (mm)" );
  app.add_option( "--signal-speed", signalSpeed, "Signal speed (mm/ns)" );
  app.add_option( "--bcid-clock", bcidClock, "BCID clock (ns)" );
  app.add_option( "--bcid-period", bcidPeriod, "BCID period" );
  app.add_option( "--max-channels", maxChannels, "Max channels per group" );
  app.add_option( "--enable-cut", enableCut, "Enable efficiency cuts" );
  app.add_option( "--chan-tol", chanTol, "Channel tolerance for cuts" );
  app.add_option( "--time-tol", timeTol, "Time tolerance for cuts (ns)" );
  app.add_option( "--max-triggers", maxTriggers, "Max triggers per event" );
  app.add_option( "--enable-delay-cut", enableDelayCut, "Enable delay cut for efficiency" );
  app.add_option( "--delay-min", delayMin, "Minimum delay (ns) for efficiency hits" );
  app.add_option( "--delay-max", delayMax, "Maximum delay (ns) for efficiency hits" );
  app.add_option( "--ignore-channels", ignoreList, "Comma-separated channel numbers to ignore (overrides default)." );
  app.add_flag( "--no-ignore-channels", noIgnore, "Do not ignore any channels (overrides default and --ignore-channels)." );

  try
  {
    app.parse( argc, argv );
  }
  catch( const CLI::ParseError& e )
  {
    return app.exit( e );
  }

  //create analyzer and set all the parameters
  RPCDataAnalyzer analyzer;
  analyzer.setEnableChannelCount( enableChannel );
  analyzer.setEnableEfficiency( enableEfficiency );
  analyzer.setEnableTotHist( enableTot );
  analyzer.setEnableSignalDelay( enableDelay );
  analyzer.setEnableTriggerCount( enableTrigger );
  analyzer.setEnableClusterSize( enableCluster );
  analyzer.setEnablePositionRecon( enablePosition );
  analyzer.setDetectorLength( detectorLength );
  analyzer.setDetectorWidth( detectorWidth );
  analyzer.setSignalSpeed( signalSpeed );
  analyzer.setBcidClock( bcidClock );
  analyzer.setBcidPeriod( bcidPeriod );
  analyzer.setMaxChannelsPerGroup( maxChannels );
  analyzer.setEnableCut( enableCut );
  analyzer.setChanTol( chanTol );
  analyzer.setTimeTol( timeTol );
  analyzer.setMaxTriggersPerEvent( maxTriggers );
  analyzer.setEnableDelayCut( enableDelayCut );
  analyzer.setDelayWindow( delayMin, delayMax );

  // set output directory
  fs::path    p( file_name );
  std::string stem   = p.stem().string();
  std::string outDir = stem + "_analysis";
  try
  {
    if( fs::create_directories( outDir ) ) { std::cout << "Created output directory: " << outDir << std::endl; }
    else
    {
      std::cout << "Using existing output directory: " << outDir << std::endl;
    }
  }
  catch( const fs::filesystem_error& e )
  {
    std::cerr << "Warning: Failed to create directory '" << outDir << "'. Output will be written to current directory.\n"
              << "Error: " << e.what() << std::endl;
    outDir.clear();
  }
  analyzer.setOutputDir( outDir );

  // ----- Configure ignored channels -----
  if( noIgnore ) { analyzer.clearIgnoredChannels(); }
  else if( !ignoreList.empty() )
  {
    std::vector<int>  channels;
    std::stringstream ss( ignoreList );
    std::string       token;
    while( std::getline( ss, token, ',' ) )
    {
      if( !token.empty() )
      {
        try
        {
          channels.push_back( std::stoi( token ) );
        }
        catch( ... )
        { /* warning */
        }
      }
    }
    analyzer.setIgnoredChannels( channels );
  }
  else
  {
    // default: 40-47
    std::vector<int> defaultChannels;
    for( int ch = 40; ch < 48; ++ch ) defaultChannels.push_back( ch );
    analyzer.setIgnoredChannels( defaultChannels );
  }

  // run the analysis
  analyzer.runFromFile( file_name );
  analyzer.finalize();
  analyzer.WritePDF();
  analyzer.summary();

  return 0;
}
