#pragma once
#include "RawData.hpp"
#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TTree.h"
#include "fmt/format.h"
#include "yaodaq/Logging.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

class EthernetAnalysor : public yaodaq::Loggable
{
public:
  EthernetAnalysor() : yaodaq::Loggable( yaodaq::Identifier( yaodaq::Component::Role::Class, "Analysor", "Ethernet" ) )
  {
    cabling_maps = { { "BIL680", { 0, -1, 1, 2, -1, 3 } }, { "BIL520", { 0, -1, 1, 2, -1, -1 } }, { "BIL680_alt", { -1, 0, 1, 2, -1, 3 } }, { "BIL520_alt", { -1, 0, 1, 2, -1, -1 } },  // configuration for using the second slot from the top of the Vdct
                     { "BIS1", { 0, 1, 2, 3, 4, 5 } },     { "BIS2_6", { 0, 1, 2, 3, 4, -1 } } };
    prepareHistograms();
  }
  void setDTMax( const int dtmax ) noexcept { dt_max = dtmax; }
  void setDTMin( const int dtmin ) noexcept { dt_min = dtmin; }
  void setDAQWindows( const int windows ) noexcept { daq_windows = windows; }
  int  getDTMax() const noexcept { return dt_max; }
  int  getDTMin() const noexcept { return dt_min; }
  int  getDTWindows() const noexcept { return daq_windows; }
  void setRPCType( const std::string type )
  {
    if( cabling_maps.find( type ) == cabling_maps.end() ) throw std::runtime_error( fmt::format( "{} RPC type doesn't exist", type ) );
    else
    {
      rpc_type    = type;
      cabling_map = cabling_maps[type];
    }
  }
  std::string getRPCType() noexcept { return rpc_type; }

  void prepare_raw_file( const std::string name ) { m_rawdata_file.reset( new TFile( name.c_str(), "RECREATE" ) ); }

  void prepare_events_file( const std::string name ) { m_event_file.reset( new TFile( name.c_str(), "RECREATE" ) ); }

  void prepare_raw_tree()
  {
    raw_tree = new TTree( "tree", "Events" );
    raw_tree->Branch( "event", &event );
    raw_tree->Branch( "nHits", &nHits );
    raw_tree->Branch( "hit_dct", &hit_dct );
    raw_tree->Branch( "hit_channel", &hit_channel );
    raw_tree->Branch( "hit_clk", &hit_clk );
    raw_tree->Branch( "hit_rawbcid", &hit_rawbcid );
    raw_tree->Branch( "hit_time1", &hit_time1 );
    raw_tree->Branch( "hit_time2", &hit_time2 );
    raw_tree->Branch( "hit_rise", &hit_rise );
  }

  void clear_all()
  {
    clear_raw_data();
    clear_events_data();
    tot_eta_p_ly0_2D->Reset();
    tot_eta_p_ly1_2D->Reset();
    tot_eta_p_ly2_2D->Reset();
    tot_eta_m_ly0_2D->Reset();
    tot_eta_m_ly1_2D->Reset();
    tot_eta_m_ly2_2D->Reset();
    tot_eta_p_ly0_1D->Reset();
    tot_eta_p_ly1_1D->Reset();
    tot_eta_p_ly2_1D->Reset();
    tot_eta_m_ly0_1D->Reset();
    tot_eta_m_ly1_1D->Reset();
    tot_eta_m_ly2_1D->Reset();
    dt_trig_vs_strip_eta_p->Reset();
    dt_trig_vs_strip_eta_m->Reset();
    strips_eta_p_layer0->Reset();
    strips_eta_p_layer1->Reset();
    strips_eta_p_layer2->Reset();
    strips_eta_m_layer0->Reset();
    strips_eta_m_layer1->Reset();
    strips_eta_m_layer2->Reset();
    trig_evts = 0;
    // Efficiency stufs
    for( std::size_t l = 0; l < 3; l++ )
    {
      hEff1[l]->Reset();
      evts_eta_p[l] = 0;
      evts_eta_m[l] = 0;
      evts_or[l]    = 0;
      evts_and[l]   = 0;

      evts_eta_p_rpc_trig[l] = 0;
      evts_eta_m_rpc_trig[l] = 0;
      evts_OR_rpc_trig[l]    = 0;
      evts_AND_rpc_trig[l]   = 0;
      evts_rpc_trig_denom[l] = 0;
    }
  }

  void clear_raw_data()
  {
    hit_dct.clear();
    hit_channel.clear();
    hit_rawbcid.clear();
    hit_time1.clear();
    hit_time2.clear();
    hit_clk.clear();
    hit_rise.clear();
    nHits = 0;
  }

  void clear_events_data()
  {
    nHits     = 0;
    nProc     = 0;
    nClus_l0  = 0;
    nClus_l1  = 0;
    nClus_l2  = 0;
    trig_time = -1;
    hit_channel.clear();
    hit_clk.clear();
    hit_rawbcid.clear();
    hit_bcid.clear();
    hit_rawbcout.clear();
    hit_bcout.clear();
    hit_time1.clear();
    hit_time2.clear();
    hit_rise.clear();
    hit_layer.clear();
    hit_strip.clear();
    proc_layer.clear();
    proc_strip.clear();
    proc_time1.clear();
    proc_time2.clear();
    proc_tot1.clear();
    proc_tot2.clear();
    proc_deltat.clear();
    proc_dt_trig1.clear();
    proc_dt_trig2.clear();
    clus_strip_l0.clear();
    clus_strip_l1.clear();
    clus_strip_l2.clear();
    clus_time1_l0.clear();
    clus_time1_l1.clear();
    clus_time1_l2.clear();
    clus_time2_l0.clear();
    clus_time2_l1.clear();
    clus_time2_l2.clear();
    clus_tot1_l0.clear();
    clus_tot1_l1.clear();
    clus_tot1_l2.clear();
    clus_tot2_l0.clear();
    clus_tot2_l1.clear();
    clus_tot2_l2.clear();
    clus_dt_l0.clear();
    clus_dt_l1.clear();
    clus_dt_l2.clear();
    clus_nstrips_l0.clear();
    clus_nstrips_l1.clear();
    clus_nstrips_l2.clear();
    clus_center_l0.clear();
    clus_center_l1.clear();
    clus_center_l2.clear();
    clus_mult_l0.clear();
    clus_mult_l1.clear();
    clus_mult_l2.clear();
  }

  void prepare_events_tree()
  {
    events_tree = new TTree( "tree", "Events" );
    events_tree->Branch( "event", &event );
    events_tree->Branch( "nHits", &nHits );
    events_tree->Branch( "hit_dct", &hit_dct );
    events_tree->Branch( "hit_channel", &hit_channel );
    events_tree->Branch( "hit_clk", &hit_clk );
    events_tree->Branch( "hit_rawbcid", &hit_rawbcid );
    events_tree->Branch( "hit_bcid", &hit_bcid );
    events_tree->Branch( "hit_rawbcout", &hit_rawbcout );
    events_tree->Branch( "hit_bcout", &hit_bcout );
    events_tree->Branch( "hit_time1", &hit_time1 );
    events_tree->Branch( "hit_time2", &hit_time2 );
    events_tree->Branch( "hit_rise", &hit_rise );
    events_tree->Branch( "hit_layer", &hit_layer );
    events_tree->Branch( "nProc", &nProc );
    events_tree->Branch( "proc_layer", &proc_layer );
    events_tree->Branch( "proc_strip", &proc_strip );
    events_tree->Branch( "proc_time1", &proc_time1 );
    events_tree->Branch( "proc_time2", &proc_time2 );
    events_tree->Branch( "proc_tot1", &proc_tot1 );
    events_tree->Branch( "proc_tot2", &proc_tot2 );
    events_tree->Branch( "proc_deltat", &proc_deltat );
    events_tree->Branch( "proc_dt_trig1", &proc_dt_trig1 );
    events_tree->Branch( "proc_dt_trig2", &proc_dt_trig2 );
    events_tree->Branch( "trig_time", &trig_time );
    events_tree->Branch( "nClus_l0", &nClus_l0 );
    events_tree->Branch( "nClus_l1", &nClus_l1 );
    events_tree->Branch( "nClus_l2", &nClus_l2 );
    events_tree->Branch( "clus_strip_l0", &clus_strip_l0 );
    events_tree->Branch( "clus_strip_l1", &clus_strip_l1 );
    events_tree->Branch( "clus_strip_l2", &clus_strip_l2 );
    events_tree->Branch( "clus_time1_l0", &clus_time1_l0 );
    events_tree->Branch( "clus_time1_l1", &clus_time1_l1 );
    events_tree->Branch( "clus_time1_l2", &clus_time1_l2 );
    events_tree->Branch( "clus_time2_l0", &clus_time2_l0 );
    events_tree->Branch( "clus_time2_l1", &clus_time2_l1 );
    events_tree->Branch( "clus_time2_l2", &clus_time2_l2 );
    events_tree->Branch( "clus_tot1_l0", &clus_tot1_l0 );
    events_tree->Branch( "clus_tot1_l1", &clus_tot1_l1 );
    events_tree->Branch( "clus_tot1_l2", &clus_tot1_l2 );
    events_tree->Branch( "clus_tot2_l0", &clus_tot2_l0 );
    events_tree->Branch( "clus_tot2_l1", &clus_tot2_l1 );
    events_tree->Branch( "clus_tot2_l2", &clus_tot2_l2 );
    events_tree->Branch( "clus_dt_l0", &clus_dt_l0 );
    events_tree->Branch( "clus_dt_l1", &clus_dt_l1 );
    events_tree->Branch( "clus_dt_l2", &clus_dt_l2 );
    events_tree->Branch( "clus_nstrips_l0", &clus_nstrips_l0 );
    events_tree->Branch( "clus_nstrips_l1", &clus_nstrips_l1 );
    events_tree->Branch( "clus_nstrips_l2", &clus_nstrips_l2 );
    events_tree->Branch( "clus_center_l0", &clus_center_l0 );
    events_tree->Branch( "clus_center_l1", &clus_center_l1 );
    events_tree->Branch( "clus_center_l2", &clus_center_l2 );
    events_tree->Branch( "clus_mult_l0", &clus_mult_l0 );
    events_tree->Branch( "clus_mult_l1", &clus_mult_l1 );
    events_tree->Branch( "clus_mult_l2", &clus_mult_l2 );
  }

  bool close_raw_file()
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

  bool close_events_file()
  {
    if( m_event_file )
    {
      m_event_file->Write();
      m_event_file->Close();
      return true;
    }
    else
    {
      error( "m_event_file nullptr" );
      return true;
    }
  }

  void setNbHits() { nHits = hit_channel.size(); }

  void raw_analyse( const DCT::DecodedRawData& raw )
  {
    hit_channel.push_back( raw.get_channel() );
    hit_dct.push_back( raw.get_dct() );
    hit_rawbcid.push_back( raw.get_bcid() );
    hit_time1.push_back( raw.get_eta1_fine_time() );
    hit_time2.push_back( raw.get_eta2_fine_time() );
    hit_clk.push_back( clk );
    hit_rise.push_back( raw.is_raise() );
  }

  void events_analyse()
  {
    std::cout << nHits << std::endl;
    // find BCID0
    int bc0{ 0 };
    if( nHits > 0 ) bc0 = hit_rawbcid[0] % 256;

    // fill layer strip bcid
    hit_layer.resize( nHits );
    hit_strip.resize( nHits );
    hit_bcid.resize( nHits );

    for( std::size_t ih = 0; ih < nHits; ih++ )
    {
      int bcid = hit_rawbcid[ih] - bc0;
      if( bcid < -128 ) bcid += 256;
      if( bcid > 128 ) bcid -= 256;

      int connector = hit_channel[ih] / 24;
      int layer     = -1;
      int strip     = -1;
      if( connector < cabling_map.size() && cabling_map[connector] >= 0 )
      {
        layer = ( hit_channel[ih] % 24 ) / 8;
        strip = 8 * cabling_map[connector] + hit_channel[ih] % 8;
      }
      hit_layer[ih] = layer;
      hit_strip[ih] = strip;
      hit_bcid[ih]  = bcid;
    }

    // FAST LOOKUP: Group hits by channel
    std::vector<std::vector<int>> hits_by_ch( 256 );
    for( std::size_t ih = 0; ih < nHits; ih++ )
    {
      if( hit_channel[ih] >= 0 && hit_channel[ih] < 256 ) hits_by_ch[hit_channel[ih]].push_back( ih );
    }

    // Find trigger
    for( std::size_t ih = 0; ih < nHits; ih++ )
    {
      if( hit_channel[ih] == DCT::DecodedRawData::trigger_channel && hit_rise[ih] == 1 && trig_time == -1 )
      {
        if( hit_time2[ih] != 0 ) trig_time = ( hit_bcid[ih] % 256 ) * 30 + hit_time2[ih] - 1;
        else
          warn( "WARNING: trig time empty." );
      }
    }

    bool eta_p_hit_flag[3] = { false, false, false };
    bool eta_m_hit_flag[3] = { false, false, false };

    std::vector<int> hit_used1( nHits, 0 );
    std::vector<int> hit_used2( nHits, 0 );

    proc_layer.reserve( nHits );
    proc_strip.reserve( nHits );
    proc_time1.reserve( nHits );
    proc_time2.reserve( nHits );
    proc_tot1.reserve( nHits );
    proc_tot2.reserve( nHits );
    proc_deltat.reserve( nHits );
    proc_dt_trig1.reserve( nHits );
    proc_dt_trig2.reserve( nHits );

    for( int ih = 0; ih < nHits; ih++ )
    {
      if( hit_rise[ih] == 1 && hit_channel[ih] != DCT::DecodedRawData::trigger_channel && hit_time1[ih] > 0 && ( hit_bcid[ih] % 256 ) * 30 + hit_time1[ih] - 1 - trig_time < dt_max && ( hit_bcid[ih] % 256 ) * 30 + hit_time1[ih] - 1 - trig_time > dt_min )
      {
        eta_p_hit_flag[hit_layer[ih]] = true;
      }
      if( hit_rise[ih] == 1 && hit_channel[ih] != DCT::DecodedRawData::trigger_channel && hit_time2[ih] > 0 && ( hit_bcid[ih] % 256 ) * 30 + hit_time2[ih] - 1 - trig_time < dt_max && ( hit_bcid[ih] % 256 ) * 30 + hit_time2[ih] - 1 - trig_time > dt_min )
      {
        eta_m_hit_flag[hit_layer[ih]] = true;
      }

      if( hit_rise[ih] == 1 && hit_channel[ih] != DCT::DecodedRawData::trigger_channel && hit_layer[ih] >= 0 )
      {
        int strip    = hit_strip[ih];
        int layer    = hit_layer[ih];
        int ch       = hit_channel[ih];
        int time1    = -1;
        int time2    = -1;
        int tot1     = 9999;
        int tot2     = 9999;
        int ihrise1  = -1;
        int ihrise2  = -1;
        int deltat   = 9999;
        int dt_trig1 = 9999;
        int dt_trig2 = 9999;

        for( int ii: hits_by_ch[ch] )
        {
          if( ii < ih ) continue;
          if( time1 == -1 && hit_rise[ii] == 1 && hit_used1[ii] == 0 && hit_time1[ii] != 0 )
          {
            time1         = ( hit_bcid[ii] % 256 ) * 30 + ( hit_time1[ii] - 1 );
            ihrise1       = ii;
            hit_used1[ii] = 1;
            break;
          }
        }

        if( time1 != -1 )
        {
          for( int ii: hits_by_ch[ch] )
          {
            if( ii <= ihrise1 ) continue;
            if( tot1 == 9999 && hit_rise[ii] == 0 && hit_used1[ii] == 0 && hit_time1[ii] != 0 )
            {
              int tmp_tot = ( hit_bcid[ii] % 256 ) * 30 + ( hit_time1[ii] - 1 ) - time1;
              if( tmp_tot > 0 )
              {
                tot1          = tmp_tot;
                hit_used1[ii] = 1;
                break;
              }
            }
          }
        }

        if( tot1 != 9999 )
        {
          if( tot1 < -256 * 30 / 2 ) tot1 += 256 * 30;
          if( tot1 > 256 * 30 / 2 ) tot1 -= 256 * 30;
        }

        for( int ii: hits_by_ch[ch] )
        {
          if( ii < ih ) continue;
          if( time2 == -1 && hit_rise[ii] == 1 && hit_used2[ii] == 0 && hit_time2[ii] != 0 )
          {
            time2         = ( hit_bcid[ii] % 256 ) * 30 + ( hit_time2[ii] - 1 );
            ihrise2       = ii;
            hit_used2[ii] = 1;
            break;
          }
        }

        if( time2 > 0 )
        {
          for( int ii: hits_by_ch[ch] )
          {
            if( ii <= ihrise2 ) continue;
            if( tot2 == 9999 && hit_rise[ii] == 0 && hit_used2[ii] == 0 && hit_time2[ii] != 0 )
            {
              int tmp_tot = ( hit_bcid[ii] % 256 ) * 30 + ( hit_time2[ii] - 1 ) - time2;
              if( tmp_tot > 0 )
              {
                tot2          = tmp_tot;
                hit_used2[ii] = 1;
                break;
              }
            }
          }
        }

        if( tot2 != 9999 )
        {
          if( tot2 < -256 * 30 / 2 ) tot2 += 256 * 30;
          if( tot2 > 256 * 30 / 2 ) tot2 -= 256 * 30;
        }

        if( time1 > -1 && time2 > -1 ) deltat = time2 - time1;
        if( trig_time > 0 && time1 > -1 ) dt_trig1 = time1 - trig_time;
        if( trig_time > 0 && time2 > -1 ) dt_trig2 = time2 - trig_time;

        if( time1 > 0 || time2 > 0 )
        {
          proc_layer.push_back( layer );
          proc_strip.push_back( strip );
          proc_time1.push_back( time1 );
          proc_time2.push_back( time2 );
          proc_tot1.push_back( tot1 );
          proc_tot2.push_back( tot2 );
          proc_deltat.push_back( deltat );
          proc_dt_trig1.push_back( dt_trig1 );
          proc_dt_trig2.push_back( dt_trig2 );
          nProc++;
        }
      }
    }

    // FAST ARRAYS: Using memset
    int hit[3][49];
    int hitmult[3][49];
    memset( hit, -1, sizeof( hit ) );
    memset( hitmult, 0, sizeof( hitmult ) );

    for( int iprc = 0; iprc < nProc; iprc++ )
    {
      hitmult[proc_layer[iprc]][proc_strip[iprc]]++;
      if( hit[proc_layer[iprc]][proc_strip[iprc]] < 0 ) hit[proc_layer[iprc]][proc_strip[iprc]] = iprc;
    }

    std::vector<int>   vclus_w_l[3];
    std::vector<int>   vclus_indx_l[3];
    std::vector<float> vclus_center_l[3];
    std::vector<int>   vclus_mult_l[3];

    for( int ly = 0; ly < 3; ly++ )
    {
      int cl_start = -1;
      for( int jj = 0; jj < 49; jj++ )
      {
        if( cl_start == -1 )
        {
          if( hit[ly][jj] >= 0 ) cl_start = jj;
        }
        else
        {
          if( hit[ly][jj] < 0 )
          {
            vclus_w_l[ly].push_back( jj - cl_start );
            vclus_center_l[ly].push_back( 0.5 * (float)( jj + cl_start - 1. ) );

            int strip = ( jj + cl_start - 1 ) / 2;
            int indx  = hit[ly][strip];
            if( !( proc_time1[indx] > 0 && proc_time2[indx] > 0 ) )
            {
              if( strip + 1 < jj )
              {
                if( proc_time1[hit[ly][strip + 1]] > 0 && proc_time2[hit[ly][strip + 1]] > 0 ) { indx = hit[ly][strip + 1]; }
                else if( strip - 1 >= cl_start )
                {
                  if( proc_time1[hit[ly][strip - 1]] > 0 && proc_time2[hit[ly][strip - 1]] > 0 ) { indx = hit[ly][strip + 1]; }
                }
              }
            }
            vclus_indx_l[ly].push_back( indx );
            int mult = 0;
            for( int is = cl_start; is <= jj; is++ ) mult += hitmult[ly][is];
            vclus_mult_l[ly].push_back( mult );
            cl_start = -1;
          }
        }
      }
    }

    for( int ic = 0; ic < vclus_indx_l[0].size(); ic++ )
    {
      clus_nstrips_l0.push_back( vclus_w_l[0][ic] );
      clus_center_l0.push_back( vclus_center_l[0][ic] );
      clus_mult_l0.push_back( vclus_mult_l[0][ic] );
      int indx = vclus_indx_l[0][ic];
      clus_strip_l0.push_back( proc_strip[indx] );
      clus_tot1_l0.push_back( proc_tot1[indx] );
      clus_tot2_l0.push_back( proc_tot2[indx] );
      clus_time1_l0.push_back( proc_time1[indx] );
      clus_time2_l0.push_back( proc_time2[indx] );
      clus_dt_l0.push_back( proc_deltat[indx] );
      nClus_l0++;
    }
    for( int ic = 0; ic < vclus_indx_l[1].size(); ic++ )
    {
      clus_nstrips_l1.push_back( vclus_w_l[1][ic] );
      clus_center_l1.push_back( vclus_center_l[1][ic] );
      clus_mult_l1.push_back( vclus_mult_l[1][ic] );
      int indx = vclus_indx_l[1][ic];
      clus_strip_l1.push_back( proc_strip[indx] );
      clus_tot1_l1.push_back( proc_tot1[indx] );
      clus_tot2_l1.push_back( proc_tot2[indx] );
      clus_time1_l1.push_back( proc_time1[indx] );
      clus_time2_l1.push_back( proc_time2[indx] );
      clus_dt_l1.push_back( proc_deltat[indx] );
      nClus_l1++;
    }
    for( int ic = 0; ic < vclus_indx_l[2].size(); ic++ )
    {
      clus_nstrips_l2.push_back( vclus_w_l[2][ic] );
      clus_center_l2.push_back( vclus_center_l[2][ic] );
      clus_mult_l2.push_back( vclus_mult_l[2][ic] );
      int indx = vclus_indx_l[2][ic];
      clus_strip_l2.push_back( proc_strip[indx] );
      clus_tot1_l2.push_back( proc_tot1[indx] );
      clus_tot2_l2.push_back( proc_tot2[indx] );
      clus_time1_l2.push_back( proc_time1[indx] );
      clus_time2_l2.push_back( proc_time2[indx] );
      clus_dt_l2.push_back( proc_deltat[indx] );
      nClus_l2++;
    }

    for( std::size_t ip = 0; ip < nProc; ip++ )
    {
      if( proc_time1[ip] > -1 ) dt_trig_vs_strip_eta_p->Fill( proc_layer[ip] * 48 + proc_strip[ip], ( proc_time1[ip] - trig_time ) );
      if( proc_time2[ip] > -1 ) dt_trig_vs_strip_eta_m->Fill( proc_layer[ip] * 48 + proc_strip[ip], ( proc_time2[ip] - trig_time ) );

      if( proc_layer[ip] == 0 && proc_time1[ip] > -1 ) strips_eta_p_layer0->Fill( proc_strip[ip] );
      if( proc_layer[ip] == 1 && proc_time1[ip] > -1 ) strips_eta_p_layer1->Fill( proc_strip[ip] );
      if( proc_layer[ip] == 2 && proc_time1[ip] > -1 ) strips_eta_p_layer2->Fill( proc_strip[ip] );
      if( proc_layer[ip] == 0 && proc_time2[ip] > -1 ) strips_eta_m_layer0->Fill( proc_strip[ip] );
      if( proc_layer[ip] == 1 && proc_time2[ip] > -1 ) strips_eta_m_layer1->Fill( proc_strip[ip] );
      if( proc_layer[ip] == 2 && proc_time2[ip] > -1 ) strips_eta_m_layer2->Fill( proc_strip[ip] );

      if( proc_layer[ip] == 0 && proc_time1[ip] > -1 && proc_tot1[ip] != 9999 )
      {
        tot_eta_p_ly0_1D->Fill( proc_tot1[ip] );
        tot_eta_p_ly0_2D->Fill( proc_strip[ip], proc_tot1[ip] );
      }
      if( proc_layer[ip] == 1 && proc_time1[ip] > -1 && proc_tot1[ip] != 9999 )
      {
        tot_eta_p_ly1_1D->Fill( proc_tot1[ip] );
        tot_eta_p_ly1_2D->Fill( proc_strip[ip], proc_tot1[ip] );
      }
      if( proc_layer[ip] == 2 && proc_time1[ip] > -1 && proc_tot1[ip] != 9999 )
      {
        tot_eta_p_ly2_1D->Fill( proc_tot1[ip] );
        tot_eta_p_ly2_2D->Fill( proc_strip[ip], proc_tot1[ip] );
      }
      if( proc_layer[ip] == 0 && proc_time2[ip] > -1 && proc_tot2[ip] != 9999 )
      {
        tot_eta_m_ly0_1D->Fill( proc_tot2[ip] );
        tot_eta_m_ly0_2D->Fill( proc_strip[ip], proc_tot2[ip] );
      }
      if( proc_layer[ip] == 1 && proc_time2[ip] > -1 && proc_tot2[ip] != 9999 )
      {
        tot_eta_m_ly1_1D->Fill( proc_tot2[ip] );
        tot_eta_m_ly1_2D->Fill( proc_strip[ip], proc_tot2[ip] );
      }
      if( proc_layer[ip] == 2 && proc_time2[ip] > -1 && proc_tot2[ip] != 9999 )
      {
        tot_eta_m_ly2_1D->Fill( proc_tot2[ip] );
        tot_eta_m_ly2_2D->Fill( proc_strip[ip], proc_tot2[ip] );
      }
    }

    /////////////////////////////////
    if( trig_time > 0 && eta_m_hit_flag[1] && eta_m_hit_flag[2] )
    {
      evts_rpc_trig_denom[0]++;
      if( eta_p_hit_flag[0] ) evts_eta_p_rpc_trig[0]++;
      if( eta_m_hit_flag[0] ) evts_eta_m_rpc_trig[0]++;
      if( eta_p_hit_flag[0] || eta_m_hit_flag[0] ) evts_OR_rpc_trig[0]++;
      if( eta_p_hit_flag[0] && eta_m_hit_flag[0] ) evts_AND_rpc_trig[0]++;
    }

    if( trig_time > 0 && eta_m_hit_flag[0] && eta_m_hit_flag[2] )
    {
      evts_rpc_trig_denom[1]++;
      if( eta_p_hit_flag[1] ) evts_eta_p_rpc_trig[1]++;
      if( eta_m_hit_flag[1] ) evts_eta_m_rpc_trig[1]++;
      if( eta_p_hit_flag[1] || eta_m_hit_flag[1] ) evts_OR_rpc_trig[1]++;
      if( eta_p_hit_flag[1] && eta_m_hit_flag[1] ) evts_AND_rpc_trig[1]++;
    }

    if( trig_time > 0 && eta_m_hit_flag[0] && eta_m_hit_flag[1] )
    {
      evts_rpc_trig_denom[2]++;
      if( eta_p_hit_flag[2] ) evts_eta_p_rpc_trig[2]++;
      if( eta_m_hit_flag[2] ) evts_eta_m_rpc_trig[2]++;
      if( eta_p_hit_flag[2] || eta_m_hit_flag[2] ) evts_OR_rpc_trig[2]++;
      if( eta_p_hit_flag[2] && eta_m_hit_flag[2] ) evts_AND_rpc_trig[2]++;
    }

    if( trig_time > 0 )
    {
      trig_evts += 1;
      for( int l = 0; l < 3; l++ )
      {
        if( eta_p_hit_flag[l] ) evts_eta_p[l]++;
        if( eta_m_hit_flag[l] ) evts_eta_m[l]++;
        if( eta_p_hit_flag[l] || eta_m_hit_flag[l] ) evts_or[l]++;
        if( eta_p_hit_flag[l] && eta_m_hit_flag[l] ) evts_and[l]++;
      }
    }
  }

  void Fill_raw_file()
  {
    nHits = hit_rise.size();
    if( raw_tree ) raw_tree->Fill();
  }

  void Fill_events_file()
  {
    if( events_tree ) events_tree->Fill();
  }

  bool raw_file_open()
  {
    if( m_rawdata_file && m_rawdata_file->IsOpen() ) return true;
    else
      return false;
  }

  bool event_file_open()
  {
    if( m_event_file && m_event_file->IsOpen() ) return true;
    else
      return false;
  }

  void setEventNumber( const std::uint64_t event_nbr ) { event = event_nbr; }

  void calculateEfficiency()
  {
    double eff_eta_p[3] = { 0, 0, 0 }, eff_eta_m[3] = { 0, 0, 0 }, eff_or[3] = { 0, 0, 0 }, eff_and[3] = { 0, 0, 0 };
    double eff_err_eta_p[3] = { 0, 0, 0 }, eff_err_eta_m[3] = { 0, 0, 0 }, eff_err_or[3] = { 0, 0, 0 }, eff_err_and[3] = { 0, 0, 0 };
    if( trig_evts > 0.0 )
    {
      for( std::size_t l = 0; l < 3; l++ )
      {
        eff_eta_p[l] = static_cast<double>( evts_eta_p[l] ) / trig_evts;
        eff_eta_m[l] = static_cast<double>( evts_eta_m[l] ) / trig_evts;
        eff_or[l]    = static_cast<double>( evts_or[l] ) / trig_evts;
        eff_and[l]   = static_cast<double>( evts_and[l] ) / trig_evts;

        eff_err_eta_p[l] = sqrt( eff_eta_p[l] * ( 1.0 - eff_eta_p[l] ) / trig_evts );
        eff_err_eta_m[l] = sqrt( eff_eta_m[l] * ( 1.0 - eff_eta_m[l] ) / trig_evts );
        eff_err_or[l]    = sqrt( eff_or[l] * ( 1.0 - eff_or[l] ) / trig_evts );
        eff_err_and[l]   = sqrt( eff_and[l] * ( 1.0 - eff_and[l] ) / trig_evts );

        hEff1[l]->SetBinContent( 1, eff_eta_p[l] );
        hEff1[l]->SetBinError( 1, eff_err_eta_p[l] );
        hEff1[l]->SetBinContent( 2, eff_eta_m[l] );
        hEff1[l]->SetBinError( 2, eff_err_eta_m[l] );
        hEff1[l]->SetBinContent( 3, eff_or[l] );
        hEff1[l]->SetBinError( 3, eff_err_or[l] );
        hEff1[l]->SetBinContent( 4, eff_and[l] );
        hEff1[l]->SetBinError( 4, eff_err_and[l] );
      }
    }
    auto print_eff = [this]( std::string_view name, const auto& eff, const auto& err ) { info( "* {}:\t ly0 = {:.3f} +/- {:.3f}\t ly1 = {:.3f} +/- {:.3f}\t ly2 = {:.3f} +/- {:.3f}", name, eff[0], err[0], eff[1], err[1], eff[2], err[2] ); };

    info( "Applied cuts on timing window from trigger (ticks): [{}, {}]", dt_min, dt_max );
    info( "***************** Efficiency with external trigger only *****************" );

    print_eff( "eta_p", eff_eta_p, eff_err_eta_p );
    print_eff( "eta_m", eff_eta_m, eff_err_eta_m );
    print_eff( "OR", eff_or, eff_err_or );
    print_eff( "AND", eff_and, eff_err_and );

    info( "**********************************************************************" );
    info( "Triggered events: {}", trig_evts );
  }

private:
  void prepareHistograms()
  {
    strips_eta_p_layer0->SetLineColor( kRed );
    strips_eta_p_layer1->SetLineColor( kBlue );
    strips_eta_p_layer2->SetLineColor( kGreen );
    strips_eta_m_layer0->SetLineColor( kRed );
    strips_eta_m_layer1->SetLineColor( kBlue );
    strips_eta_m_layer2->SetLineColor( kGreen );
    for( std::size_t l = 0; l < 3; l++ )
    {
      hEff1[l] = new TH1F( Form( "efficiency_ext_trigger_layer%d", l ), Form( "Efficiency layer %d (with external trigger only);type;efficiency", l ), 4, 0.5, 4.5 );
      hEff1[l]->GetXaxis()->SetBinLabel( 1, "eta_p" );
      hEff1[l]->GetXaxis()->SetBinLabel( 2, "eta_m" );
      hEff1[l]->GetXaxis()->SetBinLabel( 3, "OR" );
      hEff1[l]->GetXaxis()->SetBinLabel( 4, "AND" );
    }
  }

  std::map<std::string, std::vector<int>> cabling_maps;
  std::string                             rpc_type;
  int                                     dt_max{ 300 };
  int                                     dt_min{ -300 };
  int                                     daq_windows{ 128 };

  // Raw things
  TFilePtr         m_rawdata_file{ nullptr };
  int              nHits = 0;
  std::vector<int> hit_dct;      // DCT 0-3
  std::vector<int> hit_channel;  // channel 1-144
  std::vector<int> hit_clk;      // time of hit in readout window (tick = 3.125 ns)
  std::vector<int> hit_rawbcid;
  std::vector<int> hit_time1;  // time1
  std::vector<int> hit_time2;  // time2
  std::vector<int> hit_rise;   // rise or fall
  long int         event{ 0 };
  int              clk{ 0 };
  TTree*           raw_tree = nullptr;

  // Events things
  std::vector<int> cabling_map;
  TFilePtr         m_event_file{ nullptr };
  TTree*           events_tree = nullptr;
  std::vector<int> hit_bcid;
  std::vector<int> hit_rawbcout;
  std::vector<int> hit_bcout;
  std::vector<int> hit_layer;
  std::vector<int> hit_strip;

  int              nProc = 0;
  std::vector<int> proc_layer;
  std::vector<int> proc_strip;
  std::vector<int> proc_time1;
  std::vector<int> proc_time2;
  std::vector<int> proc_tot1;
  std::vector<int> proc_tot2;
  std::vector<int> proc_deltat;
  std::vector<int> proc_dt_trig1;
  std::vector<int> proc_dt_trig2;
  int              trig_time = -1;

  int              nClus_l0 = 0;
  int              nClus_l1 = 0;
  int              nClus_l2 = 0;
  std::vector<int> clus_strip_l0;
  std::vector<int> clus_strip_l1;
  std::vector<int> clus_strip_l2;

  std::vector<int> clus_time1_l0;
  std::vector<int> clus_time1_l1;
  std::vector<int> clus_time1_l2;

  std::vector<int> clus_time2_l0;
  std::vector<int> clus_time2_l1;
  std::vector<int> clus_time2_l2;

  std::vector<int> clus_tot1_l0;
  std::vector<int> clus_tot1_l1;
  std::vector<int> clus_tot1_l2;

  std::vector<int> clus_tot2_l0;
  std::vector<int> clus_tot2_l1;
  std::vector<int> clus_tot2_l2;

  std::vector<int> clus_dt_l0;
  std::vector<int> clus_dt_l1;
  std::vector<int> clus_dt_l2;

  std::vector<int> clus_nstrips_l0;
  std::vector<int> clus_nstrips_l1;
  std::vector<int> clus_nstrips_l2;

  std::vector<float> clus_center_l0;
  std::vector<float> clus_center_l1;
  std::vector<float> clus_center_l2;

  std::vector<int> clus_mult_l0;
  std::vector<int> clus_mult_l1;
  std::vector<int> clus_mult_l2;

  TH2D*                tot_eta_p_ly0_2D       = new TH2D( "tot_eta_p_ly0_2D", "tot_eta_p_ly0_2D", 48, 0, 48, 30, 0, 30 );
  TH2D*                tot_eta_p_ly1_2D       = new TH2D( "tot_eta_p_ly1_2D", "tot_eta_p_ly1_2D", 48, 0, 48, 30, 0, 30 );
  TH2D*                tot_eta_p_ly2_2D       = new TH2D( "tot_eta_p_ly2_2D", "tot_eta_p_ly2_2D", 48, 0, 48, 30, 0, 30 );
  TH2D*                tot_eta_m_ly0_2D       = new TH2D( "tot_eta_m_ly0_2D", "tot_eta_m_ly0_2D", 48, 0, 48, 30, 0, 30 );
  TH2D*                tot_eta_m_ly1_2D       = new TH2D( "tot_eta_m_ly1_2D", "tot_eta_m_ly1_2D", 48, 0, 48, 30, 0, 30 );
  TH2D*                tot_eta_m_ly2_2D       = new TH2D( "tot_eta_m_ly2_2D", "tot_eta_m_ly2_2D", 48, 0, 48, 30, 0, 30 );
  TH1D*                tot_eta_p_ly0_1D       = new TH1D( "tot_eta_p_ly0_1D", "tot_eta_p_ly0_1D", 30, 0, 30 );
  TH1D*                tot_eta_p_ly1_1D       = new TH1D( "tot_eta_p_ly1_1D", "tot_eta_p_ly1_1D", 30, 0, 30 );
  TH1D*                tot_eta_p_ly2_1D       = new TH1D( "tot_eta_p_ly2_1D", "tot_eta_p_ly2_1D", 30, 0, 30 );
  TH1D*                tot_eta_m_ly0_1D       = new TH1D( "tot_eta_m_ly0_1D", "tot_eta_m_ly0_1D", 30, 0, 30 );
  TH1D*                tot_eta_m_ly1_1D       = new TH1D( "tot_eta_m_ly1_1D", "tot_eta_m_ly1_1D", 30, 0, 30 );
  TH1D*                tot_eta_m_ly2_1D       = new TH1D( "tot_eta_m_ly2_1D", "tot_eta_m_ly2_1D", 30, 0, 30 );
  TH2D*                dt_trig_vs_strip_eta_p = new TH2D( "dt_trig_vs_strip_eta_p", "dt_trig_vs_strip_eta_p", 144, 0, 144, 350, -300, 50 );
  TH2D*                dt_trig_vs_strip_eta_m = new TH2D( "dt_trig_vs_strip_eta_m", "dt_trig_vs_strip_eta_m", 144, 0, 144, 350, -300, 50 );
  TH1D*                strips_eta_p_layer0    = new TH1D( "strips_eta_p_layer0", "strips_eta_p_layer0", 48, 0, 48 );
  TH1D*                strips_eta_p_layer1    = new TH1D( "strips_eta_p_layer1", "strips_eta_p_layer1", 48, 0, 48 );
  TH1D*                strips_eta_p_layer2    = new TH1D( "strips_eta_p_layer2", "strips_eta_p_layer2", 48, 0, 48 );
  TH1D*                strips_eta_m_layer0    = new TH1D( "strips_eta_m_layer0", "strips_eta_m_layer0", 48, 0, 48 );
  TH1D*                strips_eta_m_layer1    = new TH1D( "strips_eta_m_layer1", "strips_eta_m_layer1", 48, 0, 48 );
  TH1D*                strips_eta_m_layer2    = new TH1D( "strips_eta_m_layer2", "strips_eta_m_layer2", 48, 0, 48 );
  int                  trig_evts              = 0;
  // Efficiency stufs
  std::array<TH1F*, 3> hEff1;
  int                  evts_eta_p[3] = { 0, 0, 0 };
  int                  evts_eta_m[3] = { 0, 0, 0 };
  int                  evts_or[3]    = { 0, 0, 0 };
  int                  evts_and[3]   = { 0, 0, 0 };

  int evts_eta_p_rpc_trig[3] = { 0, 0, 0 };
  int evts_eta_m_rpc_trig[3] = { 0, 0, 0 };
  int evts_OR_rpc_trig[3]    = { 0, 0, 0 };
  int evts_AND_rpc_trig[3]   = { 0, 0, 0 };
  int evts_rpc_trig_denom[3] = { 0, 0, 0 };
};
