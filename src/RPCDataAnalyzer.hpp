#pragma once
#include "Data.hpp"
#include "yaodaq/Logging.hpp"

#include <ROOT/RNTupleReader.hxx>
#include <Rtypes.h>
#include <TCanvas.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TLatex.h>
#include <TLegend.h>
#include <TLine.h>
#include <TPaveStats.h>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

static constexpr const char* kCanvasChannel      = "channel";
static constexpr const char* kCanvasChannelRate  = "rate";
static constexpr const char* kCanvasTot          = "tot";
static constexpr const char* kCanvasDelay        = "delay";
static constexpr const char* kCanvasTrigger      = "trigger";
static constexpr const char* kCanvasCluster      = "cluster";
static constexpr const char* kCanvasPos          = "pos";
static constexpr const char* kCanvasClusterCount = "cluster_count";
// =====================================================================
//  1. Configuration & Constants (runtime-configurable)
// =====================================================================
struct AnalyzerConfig
{
  // Feature switches
  bool enableChannelCount  = true;
  bool enableEfficiency    = true;
  bool enableTotHist       = true;
  bool enableSignalDelay   = true;
  bool enableTriggerCount  = true;
  bool enableClusterSize   = true;
  bool enablePositionRecon = true;

  // Physical parameters
  double detectorLength      = 1705.0;  // mm
  double detectorWidth       = 1107.0;  // mm
  double signalSpeed         = 220.0;   // mm/ns
  double bcidClock           = 25.0;    // ns
  int    bcidPeriod          = 256;
  int    maxChannelsPerGroup = 48;

  // Efficiency cuts (if enabled)
  bool   enableCut = false;
  int    chanTol   = 2;
  double timeTol   = 20.0;  // ns

  bool   enableDelayCut = false;  // if true, only hits with delay in [delayMin, delayMax] are used
  double delayMin       = -125;   // ns
  double delayMax       = -85;    // ns

  // Other
  int maxTriggersPerEvent = 10;
};

// =====================================================================
//  2. Data Structures
// =====================================================================
// Event data structure (adjust members according to your RNTuple)
struct Hit
{
  float getTime() const noexcept { return time; }
  bool  isRised() const noexcept { return isRising; }
  float time{ 0. };
  int   channel{ 0 };
  int   layer{ 0 };
  int   side{ 0 };
  bool  isRising{ false };  // true = rising edge, false = falling edge
};
struct EventData
{
  std::vector<Hit> hits;
  std::vector<Hit> triggerHits;
};

class EfficiencySource
{
public:
  EfficiencySource( const std::int16_t layer, const std::int16_t side ) : m_layer( layer ), m_side( side ) {}
  EfficiencySource() {}
  void         setLayer( const std::int16_t layer ) { m_layer = layer; }
  void         setSide( const std::int16_t side ) { m_side = side; }
  std::int16_t getLayer() { return m_layer; }
  std::int16_t getSide() { return m_side; }

private:
  std::int16_t m_layer{ -1 };
  std::int16_t m_side{ -1 };
};

class Efficiency
{
public:
  explicit Efficiency() = default;
  void efficient()
  {
    ++m_num;
    ++m_denum;
  }
  void        inefficient() { ++m_denum; }
  std::size_t numerator() const noexcept { return m_num; }
  std::size_t denominator() const noexcept { return m_denum; }
  double      efficiency() const noexcept { return m_num * 1.0 / m_denum; }
  double      error() const noexcept
  {
    double eff = efficiency();
    return std::sqrt( eff * ( 1.0 - eff ) / m_denum );
  }
  void reset()
  {
    m_num   = 0;
    m_denum = 0;
  }

private:
  std::size_t m_num{ 0 };
  std::size_t m_denum{ 0 };
};

class EfficiencyInfo
{
public:
  EfficiencyInfo() {}
  EfficiencyInfo( const std::int16_t layer, const std::int16_t side ) : m_source( layer, side ) {}
  EfficiencySource getSource() { return m_source; }
  Efficiency       getEfficiency() { return m_eff; }
  void             efficient() { m_eff.efficient(); }
  void             inefficient() { m_eff.inefficient(); }
  void             ResetEff() { m_eff.reset(); }

private:
  EfficiencySource m_source;
  Efficiency       m_eff;
};

// =====================================================================
//  3. Analyzer Class
// =====================================================================
class RPCDataAnalyzer : public yaodaq::Loggable
{
  // 3.1 Constructor & Destructor
public:
  RPCDataAnalyzer() : yaodaq::Loggable( yaodaq::Identifier( yaodaq::Component::Role::Class, "Analysor", "XuRan" ) )
  {
    ensureHistograms();
    createAllCanvas();
    Draws();
    PrepareEfficiencies();
    // No histograms created here – they will be created on demand by ensureHistograms()
    for( int ch = 40; ch < 48; ++ch ) { m_ignoredChannels.insert( ch ); }
  }
  //Clean up histograms to avoid memory leaks
  ~RPCDataAnalyzer()
  {
    for( auto h: hRisingCount ) delete h;
    for( auto h: hFallingCount ) delete h;
    for( auto h: hRisingCountRate ) delete h;
    for( auto h: hFallingCountRate ) delete h;
    for( auto h: hTot ) delete h;
    for( auto h: hDelay ) delete h;
    delete hTriggerCount;
    delete hTotalHits;
    delete hTriggerHits;
    for( auto h: hClusterSize ) delete h;
    for( auto h: hClusterCount ) delete h;
    for( auto h: hPosEtaPhi ) delete h;
    delete hRolling;
    for( auto& pair: m_canvas ) delete pair.second;
    m_canvas.clear();
  }

  // 3.2 Output Configuration
  // Set output directory (will be created by caller)
  void setOutputDir( const std::string& dir ) { fOutputDir = dir; }

  // 3.3 Event Processing
  //process a single event
  void processEvent( const EventData& ev )
  {
    // ----- Channel counting (rising/falling separated, per layer/side) -----
    std::lock_guard<std::mutex> lock( m_mutex_graph );  // Thread safety
    getCanvas( kCanvasChannelRate )->UpdateAsync();
    if( fConfig.enableChannelCount )
    {
      for( const auto& hit: ev.hits )
      {
        int layer = hit.layer;

        int side = hit.side;
        int ch   = hit.channel;
        // Check validity
        if( layer < 0 || layer >= 3 || side < 0 || side >= 2 ) continue;
        if( ch < 0 || ch >= fConfig.maxChannelsPerGroup ) continue;
        int idx = layer * 2 + side;
        if( hit.isRising ) hRisingCount[idx]->Fill( ch );
        else
          hFallingCount[idx]->Fill( ch );
      }
      // Update rate histograms once per event
      double scale = 1.0 / ( m_event * 400e-9 );

      for( int idx = 0; idx < 6; ++idx )
      {
        hRisingCountRate[idx]->Reset();
        hRisingCountRate[idx]->Add( hRisingCount[idx] );
        hRisingCountRate[idx]->Scale( scale );
        hRisingCountRate[idx]->SetTitle( std::to_string( m_event ).c_str() );

        hFallingCountRate[idx]->Reset();
        hFallingCountRate[idx]->Add( hFallingCount[idx] );
        hFallingCountRate[idx]->Scale( scale );
        hFallingCountRate[idx]->SetTitle( std::to_string( m_event ).c_str() );
      }

      // Refresh after histograms have changed
      getCanvas( kCanvasChannelRate )->Modified();
      getCanvas( kCanvasChannelRate )->UpdateAsync();
    }

    // ----- Efficiency (event-based: three-fold / two-fold) -----
    if( fConfig.enableEfficiency )
    {
      // --- Determine trigger time if delay cut is enabled ---
      bool  skipEvent   = false;
      float triggerTime = -99999.0f;
      if( fConfig.enableDelayCut )
      {
        // Find bool  skipEvent   = false;first rising trigger hit
        for( const auto& trg: ev.triggerHits )
        {
          if( trg.isRising )
          {
            triggerTime = trg.time;
            break;
          }
        }

        // No trigger rising edge -> skip this event entirely for efficiency
        if( triggerTime < -99998 ) { skipEvent = true; }
      }

      if( !skipEvent )
      {
        // Determine which hits are valid for efficiency calculation
        bool layerHasHit[3]   = { false, false, false };
        bool sideHasHit[3][2] = { { false, false }, { false, false }, { false, false } };

        for( const auto& hit: ev.hits )
        {
          int l = hit.layer;
          int s = hit.side;
          if( l < 0 || l >= 3 || s < 0 || s >= 2 ) continue;

          // Apply delay cut if enabled
          if( fConfig.enableDelayCut )
          {
            float delay = hit.time - triggerTime;
            if( delay < fConfig.delayMin || delay > fConfig.delayMax )
            {
              continue;  // this hit is excluded from efficiency
            }
          }

          // Hit passes all cuts
          layerHasHit[l]   = true;
          sideHasHit[l][s] = true;
        }

        // Update per-layer hit counts (original)
        for( int i = 0; i < 3; ++i )
        {
          if( layerHasHit[i] ) layerHitCount[i]++;
        }

        // Check three-fold coincidence (original)
        if( layerHasHit[0] && layerHasHit[1] && layerHasHit[2] ) threeFold++;

        // Update pair counts (original)
        if( layerHasHit[1] && layerHasHit[2] ) pairCount[0]++;
        if( layerHasHit[0] && layerHasHit[2] ) pairCount[1]++;
        if( layerHasHit[0] && layerHasHit[1] ) pairCount[2]++;

        // --- New efficiency per (layer, side) ---
        totalEvents++;  // Count total events (each event with at least one hit after cuts)

        for( int l = 0; l < 3; ++l )
        {
          bool otherLayersHaveHit = false;
          if( l == 0 ) otherLayersHaveHit = layerHasHit[1] && layerHasHit[2];
          else if( l == 1 )
            otherLayersHaveHit = layerHasHit[0] && layerHasHit[2];
          else
            otherLayersHaveHit = layerHasHit[0] && layerHasHit[1];
          if( otherLayersHaveHit )
          {
            for( int s = 0; s < 2; ++s )
            {
              int idx = l * 2 + s;
              if( sideHasHit[l][s] ) getEfficiencyInfo( l, s ).efficient();
              else
                getEfficiencyInfo( l, s ).inefficient();
            }
          }
        }
      }
    }
    // ----- TOT distribution (compute from rising/falling edges) -----
    if( fConfig.enableTotHist )
    {
      std::vector<const Hit*> validHits;
      for( const auto& hit: ev.hits )
      {
        if( hit.channel >= 0 && hit.channel < fConfig.maxChannelsPerGroup ) { validHits.push_back( &hit ); }
      }

      // Sort by (channel, layer, side, time) to group matching edges
      std::sort( validHits.begin(), validHits.end(),
                 []( const Hit* a, const Hit* b )
                 {
                   if( a->channel != b->channel ) return a->channel < b->channel;
                   if( a->layer != b->layer ) return a->layer < b->layer;
                   if( a->side != b->side ) return a->side < b->side;
                   return a->time < b->time;
                 } );

      // Iterate groups and match rising -> falling
      size_t i = 0;
      while( i < validHits.size() )
      {
        size_t j = i;
        // Find the range of hits with the same (channel, layer, side)
        while( j < validHits.size() && validHits[j]->channel == validHits[i]->channel && validHits[j]->layer == validHits[i]->layer && validHits[j]->side == validHits[i]->side ) { j++; }

        // Process this group [i, j)
        float startTime = -1.0f;  // Store time of last unmatched rising edge
        for( size_t k = i; k < j; ++k )
        {
          if( validHits[k]->isRising ) { startTime = validHits[k]->time; }
          else
          {  // Falling edge
            if( startTime >= 0 )
            {
              float tot = validHits[k]->time - startTime;
              if( tot > 0 )
              {
                int idx = validHits[k]->layer * 2 + validHits[k]->side;
                if( idx >= 0 && idx < 6 ) hTot[idx]->Fill( tot );
              }
              startTime = -1.0f;  // Reset after successful match
            }
            // else: orphan falling edge (no rising before), ignore
          }
        }
        i = j;  // Move to next group
      }
    }

    // ----- Signal delay (rising edge time difference with trigger) -----
    if( fConfig.enableSignalDelay )
    {
      if( std::count_if( ev.triggerHits.begin(), ev.triggerHits.end(), []( const Hit& hit ) { return hit.isRised(); } ) )
      {
        // Determine trigger reference time: use the first trigger hit that is rising edge
        float triggerTime = -1.0f;
        for( const auto& trg: ev.triggerHits )
        {
          if( trg.isRising )
          {
            triggerTime = trg.time;
            break;  // use the first rising trigger hit
          }
        }
        // Loop over ordinary hits, only rising edges
        for( const auto& hit: ev.hits )
        {
          if( !hit.isRising ) continue;
          int layer = hit.layer;
          int side  = hit.side;
          int ch    = hit.channel;  // optional, not used for grouping
          if( layer < 0 || layer >= 3 || side < 0 || side >= 2 ) continue;
          int   idx   = layer * 2 + side;
          float delay = hit.time - triggerTime;
          hDelay[idx]->Fill( delay );
        }
      }
    }

    // ----- Trigger count per event -----
    if( fConfig.enableTriggerCount )
    {
      int nTrig = ev.triggerHits.size();  // counts rising and falling together
      if( nTrig < fConfig.maxTriggersPerEvent ) { hTriggerCount->Fill( nTrig ); }
      else
      {
        // Optionally fill overflow bin (if you have an overflow bin, but we just ignore or fill last bin)
        // Here we simply fill the last bin as overflow
        hTriggerCount->Fill( fConfig.maxTriggersPerEvent - 1 );
      }
    }

    // ----- Cluster size per layer/side -----
    if( fConfig.enableClusterSize )
    {
      // Group hits by (layer, side) and collect unique channels
      std::array<std::array<std::set<int>, 2>, 3> channelSets;  // [layer][side] set of channels
      for( const auto& hit: ev.hits )
      {
        int l = hit.layer;
        int s = hit.side;
        if( l >= 0 && l < 3 && s >= 0 && s < 2 ) { channelSets[l][s].insert( hit.channel ); }
      }

      // For each (layer, side), sort channels and find clusters, and count clusters
      for( int l = 0; l < 3; ++l )
      {
        for( int s = 0; s < 2; ++s )
        {
          const auto& chSet = channelSets[l][s];
          if( chSet.empty() )
          {
            int idx = l * 2 + s;
            hClusterCount[idx]->Fill( 0 );
            continue;
          }
          std::vector<int> chVec( chSet.begin(), chSet.end() );
          std::sort( chVec.begin(), chVec.end() );
          int clusterSize  = 1;
          int clusterCount = 0;
          for( size_t i = 1; i < chVec.size(); ++i )
          {
            if( chVec[i] == chVec[i - 1] + 1 ) { clusterSize++; }
            else
            {
              int idx = l * 2 + s;
              hClusterSize[idx]->Fill( clusterSize );
              clusterCount++;
              clusterSize = 1;
            }
          }
          // fill the last cluster
          int idx = l * 2 + s;
          hClusterSize[idx]->Fill( clusterSize );
          clusterCount++;
          // fill cluster count for this (layer, side)
          hClusterCount[idx]->Fill( clusterCount );
        }
      }
    }

    //----- 2D position reconstruction -----
    if( fConfig.enablePositionRecon )
    {
      // Group rising edges by (layer, channel) for both sides
      struct SideTimes
      { float t0 = -99999.0f, t1 = -99999.0f; };
      std::map<std::pair<int, int>, SideTimes> timeMap;  // key = (layer, channel)

      for( const auto& hit: ev.hits )
      {
        if( !hit.isRising ) continue;
        int l  = hit.layer;
        int s  = hit.side;
        int ch = hit.channel;
        if( l < 0 || l >= 3 || s < 0 || s >= 2 ) continue;
        auto key = std::make_pair( l, ch );
        if( s == 0 ) timeMap[key].t0 = hit.time;
        else
          timeMap[key].t1 = hit.time;
      }

      // For each valid pair, compute eta and phi, fill 2D histogram
      for( const auto& item: timeMap )
      {
        int   layer = item.first.first;
        int   ch    = item.first.second;
        float t1    = item.second.t0;
        float t2    = item.second.t1;
        if( t1 < -99998 || t2 < -99998 ) continue;

        // Eta from time difference (side 1 => eta = 0)
        double eta = ( t1 - t2 ) * fConfig.signalSpeed / 2.0 + fConfig.detectorLength / 2.0;
        if( eta < 0 ) eta = 0;
        if( eta > fConfig.detectorLength ) eta = fConfig.detectorLength;

        // Phi from channel number (assuming uniform distribution over width)

        double phi = ( ch + 0.5 ) * fConfig.detectorWidth / fConfig.maxChannelsPerGroup;

        hPosEtaPhi[layer]->Fill( phi, eta );
      }
    }
    //other new functions
    ++m_event;
  }

  // 3.4 Finalization
  // Finalize: produce and save plots after all events processed
  void finalize() { refresh(); }

  void Draws()
  {
    if( fConfig.enableChannelCount )
    {
      Draw( kCanvasChannel );
      Draw( kCanvasChannelRate );
    }
    if( fConfig.enableTotHist ) Draw( kCanvasTot );
    if( fConfig.enableSignalDelay ) Draw( kCanvasDelay );
    if( fConfig.enableTriggerCount ) Draw( kCanvasTrigger );
    if( fConfig.enableClusterSize )
    {
      Draw( kCanvasCluster );
      Draw( kCanvasClusterCount );
    }

    if( fConfig.enablePositionRecon ) Draw( kCanvasPos );
  }

  void summary()
  {
    // Write summary
    if( fConfig.enableEfficiency ) writeSummary();
  }

  void WritePDF()
  {
    // Produce PDF with separate pages for each canvas
    const std::string pdfFile = outPath( "analysis_results.pdf" );
    for( std::map<std::string, TCanvas*>::const_iterator it = m_canvas.begin(); it != m_canvas.end(); ++it )
    {
      it->second->Update();
      auto next = std::next( it );
      if( it == m_canvas.begin() ) it->second->Print( ( pdfFile + '(' ).c_str() );
      else if( next == m_canvas.end() )
        it->second->Print( ( pdfFile + ')' ).c_str() );
      else
        it->second->Print( pdfFile.c_str() );
    }
  }

  void ProcessEvent( const DCT::Event& dctEvent )
  {
    info( "Analysing event: {} as {}", dctEvent.event_number, m_event );
    EventData localEv;
    bool      first = true;
    int       bc0   = 0;
    for( const auto& dctHit: dctEvent.hits )
    {
      if( first )
      {
        bc0   = dctHit.getBCID() % fConfig.bcidPeriod;
        first = false;
      }
      int bcid = dctHit.getBCID();
      if( bcid - bc0 > fConfig.bcidPeriod / 2 ) bcid = bcid - fConfig.bcidPeriod;
      else if( bcid - bc0 < -fConfig.bcidPeriod / 2 )
        bcid = bcid + fConfig.bcidPeriod;
      bool isRising = dctHit.getRise();

      // Compute absolute time using the appropriate period and rolling count
      float time = static_cast<float>( bcid * fConfig.bcidClock + dctHit.getFineTime() );

      // Fill hit or trigger
      if( !dctHit.isTrigger() )
      {
        if( isIgnored( dctHit.getStrip() ) ) { continue; }
        Hit h;
        h.time    = time;
        h.channel = dctHit.getStrip();

        h.layer    = dctHit.getLayer();
        h.side     = dctHit.getSide() - 1;
        h.isRising = isRising;
        localEv.hits.push_back( h );
      }
      else
      {
        Hit trigger;
        trigger.time     = time;
        trigger.channel  = dctHit.getStrip();
        trigger.layer    = dctHit.getLayer();
        trigger.side     = dctHit.getSide() - 1;
        trigger.isRising = isRising;
        localEv.triggerHits.push_back( trigger );
      }
    }
    std::sort( localEv.triggerHits.begin(), localEv.triggerHits.end(), []( const Hit& a, const Hit& b ) { return a.getTime() < b.getTime(); } );
    std::sort( localEv.hits.begin(), localEv.hits.end(), []( const Hit& a, const Hit& b ) { return a.getTime() < b.getTime(); } );
    processEvent( localEv );
  }

  std::array<EfficiencyInfo, 6> getEfficiencies()
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_efficiencies;
  }

  // ----- Two running modes -----
  // 3.5 File Processing
  // Mode 1: Process from a ROOT file containing DCT::Event RNTuple (via RDataFrame)
  void runFromFile( const std::string& filename )
  {
    auto reader = ROOT::RNTupleReader::Open( "hits", filename );
    if( !reader )
    {
      std::cerr << "Error: Cannot open RNTuple 'hits' in file " << filename << std::endl;
      return;
    }
    auto viewEvent    = reader->GetView<DCT::Event>( "event" );
    auto totalEntries = reader->GetNEntries();

    info( "\n************************************\nProcessing {} events...", totalEntries );

    uint64_t processed = 0;
    for( auto entryId: reader->GetEntryRange() )
    {
      const DCT::Event& dctEvent = viewEvent( entryId );
      ProcessEvent( dctEvent );
      processed++;
      if( processed % 1000 == 0 ) { info( "Processed {}/{} events", processed, totalEntries ); }
    }
    info( "Finished processing {} events.\n************************************\n", processed );
  }

  void writeSummary()
  {
    // Write to file: include original stats + new efficiencies
    std::ofstream fout( outPath( "run_summary.txt" ) );
    if( fout.is_open() )
    {
      fout << "Total events processed: " << totalEvents << "\n";
      fout << "Delay cut enabled: " << ( fConfig.enableDelayCut ? "Yes" : "No" ) << "\n";
      if( fConfig.enableDelayCut ) { fout << "Delay window: [" << fConfig.delayMin << ", " << fConfig.delayMax << "] ns\n"; }
      fout << "\n";
      fout << "Hits per layer (events with at least one hit):\n";
      for( int i = 0; i < 3; ++i ) fout << "  Layer " << i << ": " << layerHitCount[i] << "\n";
      fout << "Three-fold coincidence events: " << threeFold << "\n";
      fout << "Pair counts (other two layers) and layer efficiencies:\n";
      for( int i = 0; i < 3; ++i )
      {
        double oldEff = ( pairCount[i] > 0 ) ? (double)threeFold / pairCount[i] : 0.0;
        fout << "  Layer " << i << ": denominator = " << pairCount[i] << ", efficiency = " << oldEff << "\n";
      }
      fout << "\nEfficiency per (layer, side) with uncertainty:\n";
      for( int l = 0; l < 3; ++l )
      {
        for( int s = 0; s < 2; ++s )
        {
          int idx = l * 2 + s;
          fout << "  Layer " << l << ", Side " << getEfficiencyInfo( l, s ).getSource().getSide() << ": efficiency = " << getEfficiencyInfo( l, s ).getEfficiency().efficiency() << " +/- " << getEfficiencyInfo( l, s ).getEfficiency().error()
               << " (denominator = " << getEfficiencyInfo( l, s ).getEfficiency().denominator() << ")\n";
        }
      }
      fout.close();
    }
    else
    {
      std::cerr << "Warning: Could not write " << outPath( "run_summary.txt" ) << "\n";
    }

    // Simplified terminal output
    std::cout << "========================================" << std::endl;
    std::cout << "Brief Report of the analysis" << std::endl;
    std::cout << "Total events processed: " << totalEvents << std::endl;
    std::cout << "Delay cut enabled: " << ( fConfig.enableDelayCut ? "Yes" : "No" ) << std::endl;
    if( fConfig.enableDelayCut ) { std::cout << "Delay window: [" << fConfig.delayMin << ", " << fConfig.delayMax << "] ns" << std::endl; }
    for( int l = 0; l < 3; ++l )
    {
      for( int s = 0; s < 2; ++s )
      {
        int idx = l * 2 + s;
        std::cout << "  Layer " << l << ", Side " << getEfficiencyInfo( l, s ).getSource().getSide() << ": efficiency = " << getEfficiencyInfo( l, s ).getEfficiency().efficiency() << " +/- " << getEfficiencyInfo( l, s ).getEfficiency().error()
                  << " (denominator = " << getEfficiencyInfo( l, s ).getEfficiency().denominator() << ")\n";
      }
    }
    std::cout << "========================================" << std::endl;
  }

  /// Reset all histograms and counters to zero (keeps configuration unchanged).
  void reset()
  {
    std::lock_guard<std::mutex> lock( m_mutex_graph );  // Thread safety

    // Helper lambda to reset a histogram if not null
    auto resetHist = []( TH1* h )
    {
      if( h ) h->Reset();
    };

    // Reset all existing histograms (do NOT delete/recreate)
    for( auto& h: hRisingCount ) resetHist( h );
    for( auto& h: hFallingCount ) resetHist( h );
    for( auto& h: hRisingCountRate ) resetHist( h );
    for( auto& h: hFallingCountRate ) resetHist( h );
    for( auto& h: hTot ) resetHist( h );
    for( auto& h: hDelay ) resetHist( h );
    if( hTriggerCount ) hTriggerCount->Reset();
    if( hTotalHits ) hTotalHits->Reset();
    if( hTriggerHits ) hTriggerHits->Reset();
    for( auto& h: hClusterSize ) resetHist( h );
    for( auto& h: hClusterCount ) resetHist( h );
    for( auto& h: hPosEtaPhi ) resetHist( h );
    if( hRolling ) hRolling->Reset();

    // Reset all event counters
    totalEvents = 0;
    for( int i = 0; i < 3; ++i )
    {
      layerHitCount[i] = 0;
      pairCount[i]     = 0;
    }
    threeFold = 0;

    for( auto& info: m_efficiencies )
    {
      info.ResetEff();  // Calls Efficiency::reset()
    }
    refresh();
  }

  // Set all parameters at once (optional)
  void setConfig( const AnalyzerConfig& cfg )
  {
    fConfig   = cfg;
    fNeedInit = true;
  }

  // Individual setters for each parameter
  void setEnableChannelCount( bool v )
  {
    fConfig.enableChannelCount = v;
    fNeedInit                  = true;
  }
  void setEnableEfficiency( bool v )
  {
    fConfig.enableEfficiency = v;
    fNeedInit                = true;
  }
  void setEnableTotHist( bool v )
  {
    fConfig.enableTotHist = v;
    fNeedInit             = true;
  }
  void setEnableSignalDelay( bool v )
  {
    fConfig.enableSignalDelay = v;
    fNeedInit                 = true;
  }
  void setEnableTriggerCount( bool v )
  {
    fConfig.enableTriggerCount = v;
    fNeedInit                  = true;
  }
  void setEnableClusterSize( bool v )
  {
    fConfig.enableClusterSize = v;
    fNeedInit                 = true;
  }
  void setEnablePositionRecon( bool v )
  {
    fConfig.enablePositionRecon = v;
    fNeedInit                   = true;
  }
  void setDetectorLength( double v )
  {
    fConfig.detectorLength = v;
    fNeedInit              = true;
  }
  void setDetectorWidth( double v )
  {
    fConfig.detectorWidth = v;
    fNeedInit             = true;
  }
  void setSignalSpeed( double v )
  {
    fConfig.signalSpeed = v;
    fNeedInit           = true;
  }
  void setBcidClock( double v )
  {
    fConfig.bcidClock = v;
    fNeedInit         = true;
  }
  void setBcidPeriod( int v )
  {
    fConfig.bcidPeriod = v;
    fNeedInit          = true;
  }
  void setMaxChannelsPerGroup( int v )
  {
    fConfig.maxChannelsPerGroup = v;
    fNeedInit                   = true;
  }
  void setEnableCut( bool v )
  {
    fConfig.enableCut = v;
    fNeedInit         = true;
  }
  void setChanTol( int v )
  {
    fConfig.chanTol = v;
    fNeedInit       = true;
  }
  void setTimeTol( double v )
  {
    fConfig.timeTol = v;
    fNeedInit       = true;
  }
  void setMaxTriggersPerEvent( int v )
  {
    fConfig.maxTriggersPerEvent = v;
    fNeedInit                   = true;
  }

  void setEnableDelayCut( bool v ) { fConfig.enableDelayCut = v; }
  // Set delay window (in ns)
  void setDelayWindow( double min, double max )
  {
    fConfig.delayMin = min;
    fConfig.delayMax = max;
  }

  // ----- Ignored channels management (setters) -----
  void setIgnoredChannels( const std::vector<int>& channels )
  {
    m_ignoredChannels.clear();
    for( int ch: channels ) m_ignoredChannels.insert( ch );
  }
  void addIgnoredChannel( int ch ) { m_ignoredChannels.insert( ch ); }
  void clearIgnoredChannels() { m_ignoredChannels.clear(); }
  bool isIgnored( int ch ) const { return m_ignoredChannels.find( ch ) != m_ignoredChannels.end(); }

  //  4. Private Member Variables
private:
  mutable std::mutex              m_mutex;
  mutable std::mutex              m_mutex_graph;
  std::map<std::string, TCanvas*> m_canvas;
  void                            createCanvas( const std::string& key )
  {
    if( m_canvas.find( key ) != m_canvas.end() )
    {
      warn( "Canvas {} already created", key );
      return;
    }
    m_canvas[key] = new TCanvas( ( "gl" + key ).c_str(), key.c_str(), 1200, 800 );
    if( !m_canvas[key] )
    {
      error( "Canvas for {} nullptr", key );
      std::exit( 1 );
    }
    if( key == kCanvasChannel || key == kCanvasTot || key == kCanvasDelay || key == kCanvasCluster || key == kCanvasClusterCount || key == kCanvasChannelRate ) { m_canvas[key]->Divide( 2, 3 ); }
    else if( key == kCanvasPos ) { m_canvas[key]->Divide( 3, 1 ); }
    else if( key == kCanvasTrigger )
    {
      // no division
    }
    else
    {
      // default no division
    }
  }

  void Draw( const std::string& key )
  {
    TCanvas* c = getCanvas( key );
    if( !c ) return;

    if( ( key == kCanvasChannel || key == kCanvasChannelRate ) && fConfig.enableChannelCount )
    {
      int pad = 1;
      for( int layer = 0; layer < 3; ++layer )
      {
        for( int side = 0; side < 2; ++side )
        {
          int idx = layer * 2 + side;
          c->cd( pad );
          gPad->Clear();
          if( key == kCanvasChannel )
          {
            hRisingCount[idx]->SetLineColor( kBlue );
            hRisingCount[idx]->SetFillColorAlpha( kBlue, 0.3 );
            hRisingCount[idx]->Draw( "hist" );
            hFallingCount[idx]->SetLineColor( kRed );
            hFallingCount[idx]->SetFillColorAlpha( kRed, 0.3 );
            hFallingCount[idx]->Draw( "hist same" );
          }
          else if( key == kCanvasChannelRate )
          {
            hRisingCountRate[idx]->SetLineColor( kBlue );
            hRisingCountRate[idx]->SetFillColorAlpha( kBlue, 0.3 );
            hRisingCountRate[idx]->Draw( "hist" );
            hFallingCountRate[idx]->SetLineColor( kRed );
            hFallingCountRate[idx]->SetFillColorAlpha( kRed, 0.3 );
            hFallingCountRate[idx]->Draw( "hist same" );
          }
          if( key == kCanvasChannel )
          {
            TLegend* leg = new TLegend( 0.12, 0.8, 0.32, 0.9 );
            leg->AddEntry( hRisingCount[idx], "Rising", "f" );
            leg->AddEntry( hFallingCount[idx], "Falling", "f" );
            leg->Draw();
          }
          else if( key == kCanvasChannelRate )
          {
            TLegend* leg = new TLegend( 0.12, 0.8, 0.32, 0.9 );
            leg->AddEntry( hRisingCountRate[idx], "Rising", "f" );
            leg->AddEntry( hFallingCountRate[idx], "Falling", "f" );
            leg->Draw();
          }

          // Vertical lines to separate front‑end boards (every 8 channels)
          gPad->Update();
          int nGroups = fConfig.maxChannelsPerGroup / 8;
          for( int g = 1; g < nGroups; ++g )
          {
            double x    = g * 8 - 0.5;  // boundary between channel (g*8-1) and (g*8)
            double ymin = gPad->GetUymin();
            double ymax = gPad->GetUymax();
            if( ymin == ymax )
            {
              ymin = 0;
              ymax = 1;
            }
            TLine* line = new TLine( x, ymin, x, ymax );
            line->SetLineColor( kGray + 2 );
            line->SetLineStyle( 2 );
            line->SetLineWidth( 1 );
            line->Draw();
          }
          pad++;
        }
      }
    }
    else if( key == kCanvasTot && fConfig.enableTotHist )
    {
      int pad = 1;
      for( int i = 0; i < 6; ++i )
      {
        c->cd( pad++ );
        hTot[i]->Draw();
      }
    }
    else if( key == kCanvasDelay && fConfig.enableSignalDelay )
    {
      int pad = 1;
      for( int layer = 0; layer < 3; ++layer )
      {
        for( int side = 0; side < 2; ++side )
        {
          int idx = layer * 2 + side;
          c->cd( pad++ );
          hDelay[idx]->Draw();
        }
      }
    }
    else if( key == kCanvasTrigger && fConfig.enableTriggerCount )
    {
      c->SetLogy();
      hTriggerCount->Draw();
    }
    else if( key == kCanvasCluster && fConfig.enableClusterSize )
    {
      int pad = 1;
      for( int l = 0; l < 3; ++l )
      {
        for( int s = 0; s < 2; ++s )
        {
          int idx = l * 2 + s;
          c->cd( pad++ );
          hClusterSize[idx]->Draw();
        }
      }
    }
    else if( key == kCanvasClusterCount && fConfig.enableClusterSize )
    {
      int pad = 1;
      for( int l = 0; l < 3; ++l )
      {
        for( int s = 0; s < 2; ++s )
        {
          int idx = l * 2 + s;
          c->cd( pad++ );
          hClusterCount[idx]->Draw();
        }
      }
    }
    else if( key == kCanvasPos && fConfig.enablePositionRecon )
    {
      for( int layer = 0; layer < 3; ++layer )
      {
        c->cd( layer + 1 );
        gPad->SetLeftMargin( 0.12 );
        gPad->SetBottomMargin( 0.15 );
        gPad->SetTopMargin( 0.15 );
        gPad->SetRightMargin( 0.14 );
        hPosEtaPhi[layer]->GetYaxis()->SetTitleOffset( 1.6 );
        hPosEtaPhi[layer]->GetXaxis()->SetTitleOffset( 0.8 );
        hPosEtaPhi[layer]->GetXaxis()->SetLabelOffset( -0.0 );
        hPosEtaPhi[layer]->Draw( "COLZ" );
        gPad->Update();
        TPaveStats* st = (TPaveStats*)gPad->GetPrimitive( "stats" );
        if( st )
        {
          st->SetX1NDC( 0.72 );
          st->SetX2NDC( 1 );
          st->SetY1NDC( 0.85 );
          st->SetY2NDC( 0.95 );
          gPad->Modified();
          gPad->Update();
        }
        TLatex* latex = new TLatex();
        latex->SetTextSize( 0.045 );
        latex->SetTextAlign( 22 );
        latex->DrawLatexNDC( 0.5, 0.1, "Side eta 1" );
        latex->DrawLatexNDC( 0.5, 0.88, "Side eta 2" );
        latex->DrawLatexNDC( 0.08, 0.1, "Ch 0" );
        latex->DrawLatexNDC( 0.92, 0.1, "Ch 48" );
        delete latex;
      }
    }
  }
  void refreshCanvas( const std::string& key )
  {
    TCanvas* c = getCanvas( key );
    if( !c ) return;
    c->Update();
  }

  void createAllCanvas()
  {
    if( fConfig.enableChannelCount )
    {
      createCanvas( kCanvasChannel );
      createCanvas( kCanvasChannelRate );
    }
    if( fConfig.enableTotHist ) createCanvas( kCanvasTot );
    if( fConfig.enableSignalDelay ) createCanvas( kCanvasDelay );
    if( fConfig.enableTriggerCount ) createCanvas( kCanvasTrigger );
    if( fConfig.enableClusterSize )
    {
      createCanvas( kCanvasCluster );
      createCanvas( kCanvasClusterCount );
    }
    if( fConfig.enablePositionRecon ) createCanvas( kCanvasPos );
  }

  void refresh()
  {
    if( fConfig.enableChannelCount )
    {
      refreshCanvas( kCanvasChannel );
      refreshCanvas( kCanvasChannelRate );
    }

    if( fConfig.enableTotHist ) refreshCanvas( kCanvasTot );
    if( fConfig.enableSignalDelay ) refreshCanvas( kCanvasDelay );
    if( fConfig.enableTriggerCount ) refreshCanvas( kCanvasTrigger );
    if( fConfig.enableClusterSize )
    {
      refreshCanvas( kCanvasCluster );
      refreshCanvas( kCanvasClusterCount );
    }
    if( fConfig.enablePositionRecon ) refreshCanvas( kCanvasPos );
  }

  TCanvas* getCanvas( const std::string& key )
  {
    auto it = m_canvas.find( key );
    if( it != m_canvas.end() ) return it->second;
    else
      return nullptr;
  }

  std::string outPath( const std::string& fname )
  {
    if( fOutputDir.empty() ) return fname;
    return fOutputDir + "/" + fname;
  }

  void print_page( TCanvas* can, const std::string file, const std::string title, bool first_page = true )
  {
    can->SetTitle( title.c_str() );
    if( first_page ) can->Print( ( file + "(" ).c_str(), ( "Title:" + title ).c_str() );
    else
      can->Print( file.c_str(), ( "Title:" + title ).c_str() );
  }

  // Histogram pointers
  // Histograms for channel counting: rising and falling edges, per (layer,side)
  std::array<TH1F*, 6> hRisingCount{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
  std::array<TH1F*, 6> hFallingCount{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
  std::array<TH1F*, 6> hRisingCountRate{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
  std::array<TH1F*, 6> hFallingCountRate{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
  TH1F*                hTotalHits{ nullptr };
  TH1F*                hTriggerHits{ nullptr };
  std::array<TH1F*, 6> hTot{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

  std::array<TH1F*, 6> hDelay{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

  TH1F*       hTriggerCount{ nullptr };  // Histogram of number of trigger hits per event
  std::size_t m_event{ 1 };

  // Counters for new efficiency definition (event-based)
  long long totalEvents      = 0;
  long long layerHitCount[3] = { 0, 0, 0 };  // events with at least one hit in each layer
  long long threeFold        = 0;            // events with hits in all 3 layers
  long long pairCount[3]     = { 0, 0, 0 };  // for layer i: events with hits in the other two layers

  std::string fOutputDir;  // output directory for all result files

  void PrepareEfficiencies()
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    for( std::size_t i = 0; i != m_efficiencies.size(); ++i )
    {
      std::size_t side  = i % 2;
      std::size_t layer = i / 2;
      m_efficiencies[i] = EfficiencyInfo( layer, side );
    }
  }
  EfficiencyInfo& getEfficiencyInfo( const std::int16_t layer, const std::size_t side )
  {
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_efficiencies[layer * 2 + side];
  }
  // For efficiency per (layer, side)
  std::array<EfficiencyInfo, 6> m_efficiencies;

  std::array<TH1F*, 6> hClusterSize{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

  std::array<TH1F*, 6> hClusterCount{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

  std::array<TH2F*, 3> hPosEtaPhi{ nullptr, nullptr, nullptr };  // Eta vs Phi per layer

  TH1F* hRolling{ nullptr };  // Histogram of BCID rolling count per hit

  AnalyzerConfig fConfig;

  bool fNeedInit = true;  // flag to recreate histograms when config changes

  std::unordered_set<int> m_ignoredChannels;  // Channels to be excluded from all analyses

  void ensureHistograms()
  {
    if( !fNeedInit ) return;  // already up-to-date

    // Delete existing histograms (if any) to avoid leaks
    // (Assume all pointers are either nullptr or valid)
    for( auto& h: hRisingCount )
    {
      delete h;
      h = nullptr;
    }
    for( auto& h: hFallingCount )
    {
      delete h;
      h = nullptr;
    }
    for( auto& h: hRisingCountRate )
    {
      delete h;
      h = nullptr;
    }
    for( auto& h: hFallingCountRate )
    {
      delete h;
      h = nullptr;
    }
    for( auto& h: hTot )
    {
      delete h;
      h = nullptr;
    }
    for( auto& h: hDelay )
    {
      delete h;
      h = nullptr;
    }
    delete hTriggerCount;
    hTriggerCount = nullptr;
    delete hTotalHits;
    hTotalHits = nullptr;
    delete hTriggerHits;
    hTriggerHits = nullptr;
    for( auto& h: hClusterSize )
    {
      delete h;
      h = nullptr;
    }
    for( auto& h: hPosEtaPhi )
    {
      delete h;
      h = nullptr;
    }
    delete hRolling;
    hRolling = nullptr;

    // Now create new histograms based on fConfig
    if( fConfig.enableChannelCount )
    {
      for( int layer = 0; layer < 3; ++layer )
      {
        for( int side = 0; side < 2; ++side )
        {
          int idx                = layer * 2 + side;
          hRisingCount[idx]      = new TH1F( Form( "rising_layer%d_side%d", layer, side ), Form( "Layer %d, Side %d Channel Distribution;Channel Number;Counts", layer, side + 1 ), fConfig.maxChannelsPerGroup, -0.5, fConfig.maxChannelsPerGroup - 0.5 );
          hFallingCount[idx]     = new TH1F( Form( "falling_layer%d_side%d", layer, side ), Form( "Layer %d, Side %d Channel Distribution;Channel Number;Counts", layer, side + 1 ), fConfig.maxChannelsPerGroup, -0.5, fConfig.maxChannelsPerGroup - 0.5 );
          hRisingCountRate[idx]  = new TH1F( Form( "rising_layer%d_side%d_rate", layer, side ), Form( "Layer %d, Side %d rate;Channel Number;Rate", layer, side + 1 ), fConfig.maxChannelsPerGroup, -0.5, fConfig.maxChannelsPerGroup - 0.5 );
          hFallingCountRate[idx] = new TH1F( Form( "falling_layer%d_side%d_rate", layer, side ), Form( "Layer %d, Side %d rate;Channel Number;Rate", layer, side + 1 ), fConfig.maxChannelsPerGroup, -0.5, fConfig.maxChannelsPerGroup - 0.5 );
        }
      }
    }
    if( fConfig.enableEfficiency )
    {
      hTotalHits   = new TH1F( "total_hits", "Total Hits per Layer;Layer;Hits", 3, 0, 3 );
      hTriggerHits = new TH1F( "trigger_hits", "Trigger Hits per Layer;Layer;Hits", 3, 0, 3 );
    }
    if( fConfig.enableTotHist )
    {
      for( int layer = 0; layer < 3; ++layer )
      {
        for( int side = 0; side < 2; ++side )
        {
          int idx   = layer * 2 + side;
          hTot[idx] = new TH1F( Form( "tot_layer%d_side%d", layer, side + 1 ), Form( "Layer %d, Side %d TOT;TOT (ns);Counts", layer, side + 1 ), 30, 0, 30 );
        }
      }
    }
    if( fConfig.enableSignalDelay )
    {
      for( int layer = 0; layer < 3; ++layer )
      {
        for( int side = 0; side < 2; ++side )
        {
          int idx     = layer * 2 + side;
          hDelay[idx] = new TH1F( Form( "delay_layer%d_side%d", layer, side + 1 ), Form( "Layer %d, Side %d Signal Delay;Delay (ns);Counts", layer, side + 1 ), 100, -200, 50 );
        }
      }
    }
    if( fConfig.enableTriggerCount ) { hTriggerCount = new TH1F( "trigger_count", "Number of Trigger Hits per Event;Trigger Hits;Events", fConfig.maxTriggersPerEvent, -0.5, fConfig.maxTriggersPerEvent - 0.5 ); }
    if( fConfig.enableClusterSize )
    {
      for( int layer = 0; layer < 3; ++layer )
      {
        for( int side = 0; side < 2; ++side )
        {
          int idx            = layer * 2 + side;
          hClusterSize[idx]  = new TH1F( Form( "cluster_layer%d_side%d", layer, side + 1 ), Form( "Layer %d, Side %d Cluster Size;Cluster Size;Counts", layer, side + 1 ), 11, -0.5, 10.5 );
          hClusterCount[idx] = new TH1F( Form( "cluster_count_layer%d_side%d", layer, side ), Form( "Layer %d, Side %d Cluster Count per Event;Cluster Count;Events", layer, side + 1 ), 11, -0.5, 10.5 );
        }
      }
    }
    if( fConfig.enablePositionRecon )
    {
      for( int layer = 0; layer < 3; ++layer )
      {
        int etaBins       = static_cast<int>( fConfig.detectorLength / ( fConfig.signalSpeed * 5.0 / 6.0 / 2.0 ) );
        hPosEtaPhi[layer] = new TH2F( Form( "pos_eta_phi_layer%d", layer ), Form( "Layer %d 2D Hitmap;Phi (mm);Eta (mm)", layer ), 48, 0, fConfig.detectorWidth, etaBins, 0, fConfig.detectorLength );
      }
    }
    // if (ENABLE_ROLLING) { ... } // optional

    fNeedInit = false;
  }
};
