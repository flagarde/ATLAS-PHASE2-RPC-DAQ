#include "cluster_utils.h"

#include <algorithm>
#include <cmath>
#include <vector>

std::vector<Cluster> buildClusters( const std::vector<Layer::Hit>& hits, int layerIndex, int etaSide )
{
  std::vector<Cluster> clusters;
  if( hits.empty() ) return clusters;

  std::vector<Layer::Hit> sortedHits = hits;
  std::sort( sortedHits.begin(), sortedHits.end(), []( const Layer::Hit& a, const Layer::Hit& b ) { return a.leadingTime < b.leadingTime; } );

  Cluster currentCluster;
  int     hitCount = 0;

  auto flushCluster = [&]()
  {
    if( hitCount == 0 ) return;
    clusters.push_back( currentCluster );
    currentCluster = Cluster();
    hitCount       = 0;
  };

  for( auto& hit: sortedHits )
  {
    if( hit.used ) continue;                                           //skip hits already added to clusters
    if( hit.timeOverThreshold <= 0 || hit.leadingTime < 0 ) continue;  // quality checks

    // take earliest hit as first hit of cluster
    currentCluster.time          = hit.leadingTime;
    currentCluster.tot           = hit.timeOverThreshold;
    currentCluster.dt_trig       = hit.dt_trig;
    currentCluster.layer         = layerIndex;
    currentCluster.etaSide       = etaSide;
    currentCluster.centerChannel = hit.channel;
    currentCluster.channels.push_back( hit.channel );
    currentCluster.timeChannels.push_back( hit.leadingTime );
    currentCluster.totChannels.push_back( hit.timeOverThreshold );
    hit.used = true;
    hitCount = 1;

    for( auto& subHit: sortedHits )
    {                              // look for other hits to be added to current cluster
      if( subHit.used ) continue;  //skip hits already added to clusters
      // check if subHit is close in space to any channel of currentCluster
      bool channelClose = false;
      for( int ch: currentCluster.channels )
      {
        if( std::abs( subHit.channel - ch ) <= 1 )
        {
          channelClose = true;
          break;
        }
      }
      if( !channelClose ) continue;

      // check if subHit is close in time to earliest hit of cluster
      bool timeClose = std::fabs( subHit.leadingTime - currentCluster.time ) <= 12;
      if( !timeClose ) continue;

      // ad subHit to current cluster
      currentCluster.channels.push_back( subHit.channel );
      currentCluster.timeChannels.push_back( subHit.leadingTime );
      currentCluster.totChannels.push_back( subHit.timeOverThreshold );
      subHit.used = true;
      ++hitCount;
    }

    // Update cluster properties based on all channels in currentCluster
    int   highestTot         = -1;
    int   channel_highestTot = -1;
    float centroid           = 0;
    for( int i = 0; i < currentCluster.channels.size(); ++i )
    {
      currentCluster.totSum += currentCluster.totChannels.at( i );
      centroid += currentCluster.totChannels.at( i ) * currentCluster.channels.at( i );
      if( currentCluster.totChannels.at( i ) > highestTot )
      {
        highestTot         = currentCluster.totChannels.at( i );
        channel_highestTot = currentCluster.channels.at( i );
      }
    }
    centroid /= currentCluster.totSum;
    currentCluster.centroid      = centroid;
    currentCluster.centerChannel = channel_highestTot;

    flushCluster();
  }
  return clusters;
}

void initializeMap( std::map<std::string, std::map<int, int>>& map )
{
  for( const std::string& eta: { "eta_p", "eta_m" } )
  {
    for( int l = 0; l < 3; l++ ) map[eta][l] = 0;
  }
}

void initializeMap( std::map<std::string, std::map<int, std::vector<int>>>& map )
{
  for( const std::string& eta: { "eta_p", "eta_m" } )
  {
    for( int l = 0; l < 3; l++ ) map[eta][l] = std::vector<int>( 48, 0 );
  }
}

void initializeMap( std::map<std::string, std::map<int, std::vector<int>>>& map, int defaultValue )
{
  for( const std::string& eta: { "eta_p", "eta_m" } )
  {
    for( int l = 0; l < 3; l++ ) map[eta][l] = std::vector<int>( 48, defaultValue );
  }
}
