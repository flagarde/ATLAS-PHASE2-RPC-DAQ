#pragma once
#include "RawData.hpp"

#include <cstdint>
#include <iostream>
#include <vector>
namespace DCT
{

class Hit
{
public:
  enum class Mask
  {
    nothing = 0,
    side1   = 1,
    side2   = 2,
  };
  Hit() noexcept = default;
  Hit( const DecodedRawData& decoded, const Mask mask = Mask::nothing ) noexcept
  {
    out_bcid = decoded.get_output_bcid();
    bcid     = decoded.get_bcid();
    rise     = decoded.is_raise();
    setTime( decoded, mask );
    setPosition( decoded.get_channel() );
    trigger = decoded.is_trigger();
  };
  std::uint16_t getFineTimeTick() const noexcept { return fine_time; }
  double        getFineTime() const noexcept { return fine_time * m_tick_time; }
  std::uint8_t  getSide() const noexcept { return eta; }
  std::uint32_t getOutBCID() const noexcept { return out_bcid; }
  std::uint32_t getBCID() const noexcept { return bcid; }
  std::uint8_t  getStrip() const noexcept { return strip; }
  std::uint8_t  getConnector() const noexcept { return connector; }
  std::uint8_t  getLayer() const noexcept { return layer; }
  bool          getRise() const noexcept { return rise; }
  bool          isTrigger() const noexcept { return trigger; }
  bool          isHit() const noexcept { return !trigger; }

private:
  void setPosition( const std::uint8_t channel )  // the DCT channel
  {
    connector = channel / 24;  // connector (spider) number;
    layer     = ( channel % 24 ) / 8;
    if( eta == 1 ) { strip = ( connector * 8 ) + ( 7 - channel % 8 ); }
    else
    {
      strip = ( connector * 8 ) + ( channel % 8 );
    }
  }
  void setTime( const DecodedRawData& decoded, const Mask mask ) noexcept
  {
    switch( mask )
    {
      case Mask::nothing:
      {
        if( decoded.get_eta1_fine_time() > 0 )
        {
          eta       = 1;
          fine_time = decoded.get_eta1_fine_time() - 1;  //0 means 0 here !
        }
        else
        {
          eta       = 2;
          fine_time = decoded.get_eta2_fine_time() - 1;  //0 means 0 here !
        }
        break;
      }

      case Mask::side1:
      {
        eta       = 1;
        fine_time = decoded.get_eta1_fine_time() - 1;  //0 means 0 here !
        break;
      }

      case Mask::side2:
      {
        eta       = 2;
        fine_time = decoded.get_eta2_fine_time() - 1;  //0 means 0 here !
        break;
      }
    }
  }
  static constexpr double m_tick_time{ 0.833 };  //ns
  bool                    rise{ false };
  std::uint8_t            strip{ 0 };
  std::uint8_t            layer{ 0 };
  std::uint8_t            eta{ 0 };
  std::uint8_t            connector{ 0 };
  std::uint32_t           bcid{ 0 };
  std::uint32_t           out_bcid{ 0 };
  std::uint16_t           fine_time{ 0 };
  bool                    trigger{ false };
};

class Event
{
public:
  Event() noexcept = default;
  Event( const std::uint64_t event_number ) noexcept : event_number( event_number ) {}
  void push_back( const DecodedRawData& decoded )  // decouple the hits if eta1 and eta2 are save in the same DecodedRawData
  {
    if( decoded.has_both_side() )
    {
      hits.push_back( Hit( decoded, Hit::Mask::side1 ) );
      hits.push_back( Hit( decoded, Hit::Mask::side2 ) );
    }
    else
      hits.push_back( Hit( decoded ) );
  }
  void clear()
  {
    hits.clear();
    event_number = 0;
  }
  void             reserve_hits( const std::size_t i ) { hits.reserve( i ); }
  std::vector<Hit> hits;
  std::uint64_t    event_number{ 0 };
};

}  // namespace DCT
