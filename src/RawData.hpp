#pragma once
#include <cstdint>
#include <vector>

namespace DCT
{

class RawData
{
public:
  explicit RawData() noexcept = default;
  RawData( const std::uint32_t _word, const std::uint32_t _dct_bcid ) : word( _word ), dct_bcid( _dct_bcid ) {}

private:
  std::uint32_t word{ 0 };
  std::uint32_t dct_bcid{ 0 };
};

// Rising assume 8b Strip, 8b BC, 5b rise, 6b diff, 1b rise/fall
// Falling assume 8b Strip, 9b BC, 5b fall1, 5b fall2, 1b rise/fall
class DecodedRawData
{
public:
  explicit DecodedRawData() noexcept = default;
  bool          is_raise() const noexcept { return rise; }
  bool          is_fall() const noexcept { return !rise; }
  // It is DCT channel not strip !!!!
  std::uint8_t  get_channel() const noexcept { return channel; }
  std::uint32_t get_bcid() const noexcept { return bcid; }
  bool          is_trigger() const noexcept { return get_channel() == trigger_channel; }
  bool          is_hit() const noexcept { return !is_trigger(); }
  std::int16_t  get_eta1_fine_time() const noexcept { return eta1_time; }
  std::int16_t  get_eta2_fine_time() const noexcept { return eta2_time; }
  double        get_eta1_fine_time_ns() const noexcept { return eta1_time * m_tick_time; }
  double        get_eta2_fine_time_ns() const noexcept { return eta2_time * m_tick_time; }
  bool          has_both_side() const noexcept
  {
    return ( eta1_time >= 1 && eta2_time >= 1 );  //0 means no time recorded
  }
  bool          has_eta1() const noexcept { return eta1_time >= 1; }
  bool          has_eta2() const noexcept { return eta2_time >= 1; }
  std::uint32_t get_output_bcid() const noexcept { return output_bcid; }
  DecodedRawData( const std::uint32_t word, const std::uint32_t out_bcid ) noexcept : output_bcid( out_bcid )
  {
    setRise( word );  // Must be first;
    setChannel( word );
    setBCID( word );
    setEta1Time( word );
    setEta2Time( word );
  }

private:
  void setRise( const std::uint32_t word ) noexcept { rise = word & 0x1; }
  void setChannel( const std::uint32_t word ) noexcept { channel = ( word >> 20 ) & 0xFF; }
  void setBCID( const std::uint32_t word ) noexcept
  {
    if( rise ) bcid = ( word >> 12 ) & 0xFF;
    else
      bcid = ( word >> 11 ) & 0x1FF;
  }
  void setEta1Time( const std::uint32_t word ) noexcept
  {
    if( rise ) eta1_time = ( word >> 7 ) & 0x1F;
    else
      eta1_time = ( word >> 6 ) & 0x1F;
  }
  void setEta2Time( const std::uint32_t word ) noexcept
  {
    if( rise ) eta2_time = ( word >> 1 ) & 0x1F;
    else
      eta2_time = ( word >> 1 ) & 0x1F;
  }
  static constexpr std::uint8_t trigger_channel{ 143 };
  static constexpr double       m_tick_time{ 0.833 };  //ns
  std::uint32_t                 output_bcid{ 0 };
  bool                          rise{ false };
  std::uint8_t                  channel{ 0 };
  std::uint32_t                 bcid{ 0 };
  std::uint16_t                 eta1_time{ 0 };
  std::uint16_t                 eta2_time{ 0 };
};

class IntermediateEvent
{
public:
  IntermediateEvent() noexcept = default;
  IntermediateEvent( const std::uint64_t event_number ) noexcept : event_number( event_number ) {}
  std::vector<DecodedRawData> hits;
  std::uint64_t               event_number{ 0 };
  void                        clear()
  {
    hits.clear();
    event_number = 0;
  }
  void reserve_hits( const std::size_t i ) { hits.reserve( i ); }
};

}  // namespace DCT
