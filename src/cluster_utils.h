#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <vector>

// Layer stores all hits for eta_p/eta_m sides of a detector layer.
class Layer
{
public:
  // Hit keeps the first leading/trailing edges and the derived TOT.
  struct Hit
  {
    int  channel           = 0;
    int  leadingTime       = -1;
    int  trailingTime      = -1;
    int  timeOverThreshold = 0;
    int  dt_trig           = 9999;
    bool used              = false;

    Hit() = default;

    Hit( int ch, int lead, int trail, int tot, int dt, bool u ) : channel( ch ), leadingTime( lead ), trailingTime( trail ), timeOverThreshold( tot ), dt_trig( dt ), used( u ) {}
  };
  std::vector<Hit> eta_pHits;  // Hits belonging to eta_p side
  std::vector<Hit> eta_mHits;  // Hits belonging to eta_m side

  // Adds a leading or trailing edge to the channel, computing TOT when possible.
  void addHit( int channel, bool isEta1, int time, int tot, int dt_trig )
  {
    int fall_time = tot < 9999 ? time + tot : -1;
    Hit newHit{ channel, time, fall_time, tot, dt_trig, false };
    if( isEta1 ) eta_pHits.push_back( newHit );
    else
      eta_mHits.push_back( newHit );
  }
};

// Simple container describing a time-based cluster built from adjacent channels.
class Cluster
{
public:
  std::vector<int> channels;              // Contributing channels (relative indices)
  std::vector<int> timeChannels;          // Leading times of contributing channels
  std::vector<int> totChannels;           // ToTs of contributing channels
  int              time          = -1;    // Leading time of earliest hit
  int              tot           = 0;     // time over threshold of earliest hit
  int              totSum        = 0;     // Sum of TOTs for all hits
  int              centerChannel = -1;    // Channel having the highest TOT inside the cluster
  float            centroid      = -1;    // Weighted average of strip indices (tot is the weight)
  int              layer         = -1;    // Detector layer index (0, 1, 2)
  int              etaSide       = -1;    // Eta side (1 or 2)
  int              dt_trig       = 9999;  // difference between time of earliest hit and trigger time

  Cluster() = default;
  Cluster( std::vector<int> channelsIn, double clusterTime, double tot, int center, int layerIndex, int eta ) : channels( std::move( channelsIn ) ), time( clusterTime ), tot( tot ), centerChannel( center ), layer( layerIndex ), etaSide( eta ) {}
};

struct LayerClusters
{
  std::vector<Cluster> eta_p;
  std::vector<Cluster> eta_m;
};

std::vector<Cluster> buildClusters( const std::vector<Layer::Hit>& hits, int layerIndex, int etaSide );

void initializeMap( std::map<std::string, std::map<int, int>>& map );
void initializeMap( std::map<std::string, std::map<int, std::vector<int>>>& map );
void initializeMap( std::map<std::string, std::map<int, std::vector<int>>>& map, int defaultValue );
