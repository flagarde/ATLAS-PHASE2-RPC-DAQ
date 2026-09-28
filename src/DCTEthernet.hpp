#pragma once
#include <EthLayer.h>
#include <Packet.h>
#include <PcapFileDevice.h>
#include <PcapLiveDevice.h>
#include <PcapLiveDeviceList.h>
#include <TcpLayer.h>
#include <UdpLayer.h>
#include <cpp-terminal/color.hpp>
#include <cpp-terminal/input.hpp>
#include <cpp-terminal/iostream.hpp>
#include <cpp-terminal/terminal.hpp>
#include <memory>
#include <mutex>
#include <queue>

class DCTEthernet : public yaodaq::Board
{
public:
  DCTEthernet( yaodaq::BoardConfig& cfg, const std::string_view name ) : yaodaq::Board( cfg, name, "DCT" ) { this->setRun( fun ); }
  ~DCTEthernet() final
  {
    connect_finished.store( false );
    board_finished.store( false );
    dct_finished.store( false );
    disconnect_finished.store( false );
  }
  void setFirmwaresPath( const std::filesystem::path path ) noexcept { m_firmwares_path = path; }
  void setDCTFirmwareName( const std::string_view dct_firmware_name ) noexcept { m_dct_firmware_name = dct_firmware_name; }
  void setDCTSelfFirmwareName( const std::string_view dct_firmware_name ) noexcept { m_dct_firmware_self_name = dct_firmware_name; }
  void setBoardFirmwareName( const std::string_view board_firmware_name ) noexcept { m_board_firmware_name = board_firmware_name; }
  void setBoardSelfFirmwareName( const std::string_view board_firmware_name ) noexcept { m_board_firmware_self_name = board_firmware_name; }
  void setEthernetName( const std::string_view ethernet_name ) noexcept { m_ethernet_name = ethernet_name; }
  void setMacAdress( const std::string_view mac_adress ) noexcept { m_mac_adress = mac_adress; }
  void setSelfTrigger( const bool self ) noexcept { self_trigger = self; }
  void setDCTConfigured() { dct_finished.store( true ); }
  void setBoardConfigured() { board_finished.store( true ); }
  void setConnected() { connect_finished.store( true ); }
  void setDisconnected() { disconnect_finished.store( true ); }
  bool on_configure() override
  {
    m_ethernet.setBoard( m_ethernet_name );
    m_ethernet.setMac( m_mac_adress );
    m_ethernet.maxEvents( getMaxEvents() );
    if( !m_ethernet.open() )
    {
      error( "Impossible to open {} or read packet from {}", m_ethernet_name, m_mac_adress );
      return false;
    }
    info( "Interface name: {}", m_ethernet.name() );
    info( "Interface description: {}", m_ethernet.description() );
    info( "MAC address: {}", m_ethernet.mac_address() );
    info( "Default gateway: {}", m_ethernet.defaultGateway() );
    info( "Interface MTU: {}", m_ethernet.mtu() );
    info( "DNS server: {}", m_ethernet.defaultGateway() );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    if( !configure_dct() ) return false;
    if( !configure_board() ) return false;
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return true;
  }
  bool post_connect() override
  {
    warn( "Waiting connecting step to finish !" );
    sendCommand( "open_hw_manager" );
    sendCommand( "connect_hw_server" );
    sendCommand( "puts __YAODAQ_CONNECT_FINISHED__" );
    while( !connect_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
      info( "Connecting..." );
    }
    connect_finished.store( false );
    warn( "Connecting step finished !" );
    return true;
  }

  bool configure_board()
  {
    warn( "Waiting board configuration step to finish !" );
    // Select the hardware target and configure JTAG frequency
    std::string target{ "*/xilinx_tcf/Digilent/210299BBCE9A" };
    std::string name{ "xcku5p_0" };
    sendCommand( "set targets [get_hw_targets *]" );
    sendCommand( "puts \"Available targets: $targets\"" );
    sendCommand( fmt::format( "current_hw_target [get_hw_targets {}]", target ) );
    sendCommand( fmt::format( "set_property PARAM.FREQUENCY 15000000 [get_hw_targets {}]", target ) );
    //  Open the selected hardware target
    sendCommand( "open_hw_target" );
    // Get the device only after the target is open
    sendCommand( fmt::format( "set HW_DEV [lindex [get_hw_devices {}] 0]", name ) );
    sendCommand( "if {$HW_DEV eq \"\"} {\nputs \"Available hardware devices: [get_hw_devices]\"\nerror \"Hardware device $HW_NAME not found.\"\n}" );
    // Select current hardware device
    sendCommand( fmt::format( "current_hw_device [get_hw_devices {}]", name ) );
    // Refresh device without updating probes yet
    sendCommand( "refresh_hw_device -update_hw_probes false $HW_DEV" );
    // ============================================================
    // Assign bitstream and probes files
    // ============================================================
    //std::string file = "/home/user/Desktop/Mattia_python/bi_dct_data_acquisition/KCU116_FW/daq_kcu116_sha_1f11706";
    std::string file;
    if( self_trigger ) file = m_firmwares_path.string() + "KCU116_FW/" + m_board_firmware_self_name;
    else
      file = m_firmwares_path.string() + "KCU116_FW/" + m_board_firmware_name;
    warn( "firmware {}", file );
    sendCommand( fmt::format( "set_property PROBES.FILE      {}.ltx $HW_DEV", file ) );
    sendCommand( fmt::format( "set_property FULL_PROBES.FILE {}.ltx $HW_DEV", file ) );
    sendCommand( fmt::format( "set_property PROGRAM.FILE     {}.bit $HW_DEV", file ) );
    // Program the FPGA and refresh the hardware device
    sendCommand( "program_hw_devices $HW_DEV" );
    sendCommand( "refresh_hw_device $HW_DEV" );
    // ============================================================
    // Print visible debug cores
    // ============================================================
    sendCommand( "puts \"Available ILA cores: [get_hw_ilas -of_objects $HW_DEV]\"" );
    sendCommand( "puts \"Available VIO cores: [get_hw_vios -of_objects $HW_DEV]\"" );
    // ============================================================
    // Get ILA handles by CELL_NAME
    // ============================================================
    sendCommand( "set ILA_COUNTER  [lindex [get_hw_ilas -of_objects $HW_DEV -filter {CELL_NAME =~ \"axi_ethernet_core_inst/counter_check_28_word/ila_cnt_28b_inst\"}] 0]" );
    sendCommand( "set ILA_DATA [lindex [get_hw_ilas -of_objects $HW_DEV -filter {CELL_NAME =~ \"axi_ethernet_core_inst/ila_check_inst\"}] 0]" );
    sendCommand( "puts \"ILA_COUNTER  = $ILA_COUNTER\"" );
    sendCommand( "puts \"ILA_DATA = $ILA_DATA\"" );
    // ============================================================
    // Get VIO handles by CELL_NAME
    // ============================================================
    sendCommand( "set VIO_AXI_CTRL [lindex [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME =~ \"axi_ethernet_core_inst/axi_lite_ctrl_inst/vio_axi_ctrl_inst\"}] 0]" );
    sendCommand( "set VIO_TRIG     [lindex [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME =~ \"axi_ethernet_core_inst/buffer_input_gen_inst/vio_trig_type\"}] 0]" );
    sendCommand( "set VIO_SC       [lindex [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME =~ \"sc_vio\"}] 0]" );
    sendCommand( "set VIO_DEBUG    [lindex [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME =~ \"vio_debug_inst\"}] 0]" );
    sendCommand( "puts \"VIO_AXI_CTRL = $VIO_AXI_CTRL\"" );
    sendCommand( "puts \"VIO_TRIG     = $VIO_TRIG\"" );
    sendCommand( "puts \"VIO_SC       = $VIO_SC\"" );
    sendCommand( "puts \"VIO_DEBUG    = $VIO_DEBUG\"" );
    // ============================================================
    // Basic checks on required VIOs
    // ============================================================
    sendCommand( R"(if { $VIO_SC eq "" } { error "VIO 'sc_vio' not found." })" );
    sendCommand( R"(if { $VIO_DEBUG eq "" } { error "VIO 'vio_debug_inst' not found." })" );
    // ============================================================
    // Open ILA dashboards if found
    // ============================================================
    sendCommand( R"(if { $ILA_COUNTER ne "" } { display_hw_ila_data [get_hw_ila_data hw_ila_data_1 -of_objects $ILA_COUNTER] } else { puts "WARNING: ILA 'axi_ethernet_core_inst/counter_check_28_word/ila_cnt_28b_inst' not found." })" );
    sendCommand( R"(if { $ILA_DATA ne "" } { display_hw_ila_data [get_hw_ila_data hw_ila_data_2 -of_objects $ILA_DATA] } else { puts "WARNING: ILA 'axi_ethernet_core_inst/ila_check_inst' not found." })" );
    // ============================================================
    // Commit all available VIOs
    // ============================================================
    sendCommand( R"(if { $VIO_AXI_CTRL ne "" } { commit_hw_vio $VIO_AXI_CTRL })" );
    sendCommand( R"(if { $VIO_TRIG ne "" } { commit_hw_vio $VIO_TRIG })" );
    sendCommand( R"(if { $VIO_SC ne "" } { commit_hw_vio $VIO_SC })" );
    sendCommand( R"(if { $VIO_DEBUG ne "" } { commit_hw_vio $VIO_DEBUG })" );
    // ============================================================
    // Set dct_ready_vio = 1
    // ============================================================
    sendCommand( R"(set_property OUTPUT_VALUE 1 [get_hw_probes dct_ready_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    sendCommand( R"(commit_hw_vio [get_hw_probes {dct_ready_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    // ============================================================
    // Set LPGBT I2C address = 70
    // ============================================================
    sendCommand( R"(set_property OUTPUT_VALUE 70 [get_hw_probes lpgbt_i2c_address_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    sendCommand( R"(commit_hw_vio [get_hw_probes {lpgbt_i2c_address_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    // Repeated write to the same I2C address (kept as in original script)
    sendCommand( R"(set_property OUTPUT_VALUE 70 [get_hw_probes lpgbt_i2c_address_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    sendCommand( R"(commit_hw_vio [get_hw_probes {lpgbt_i2c_address_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    // ============================================================
    // Set delay_trigger = hex18
    // ============================================================
    // Set the correct trigger delay inside the evb
    sendCommand( R"(set_property OUTPUT_VALUE 15 [get_hw_probes delay_trigger -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    sendCommand( R"(commit_hw_vio [get_hw_probes {delay_trigger} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]])" );
    // ============================================================
    // Set sampling phase for lpgbt = hex04 (for SN04) hex05 (for DCT at Rome)
    // ============================================================
    //Set the phase for the lpgbt sampling
    // set_property OUTPUT_VALUE 04 \
    // [get_hw_probes sampling_lpgbt_phase -of_objects \
    // [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]]
    // commit_hw_vio [get_hw_probes {sampling_lpgbt_phase} -of_objects \
    //# [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]]

    // ============================================================
    // Generate a pulse on lpgbtfpga_downlinkrst_vio
    // First set to 1, then back to 0
    // ============================================================
    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 1 [get_hw_probes lpgbtfpga_downlinkrst_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                commit_hw_vio [get_hw_probes {lpgbtfpga_downlinkrst_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                endgroup
                )" );

    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 0 [get_hw_probes lpgbtfpga_downlinkrst_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                commit_hw_vio [get_hw_probes {lpgbtfpga_downlinkrst_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                endgroup
                )" );

    // ============================================================
    // Generate a pulse on lpgbtfpga_uplinkrst_vio
    // First set to 1, then back to 0
    // ============================================================
    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 1 [get_hw_probes lpgbtfpga_uplinkrst_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                commit_hw_vio [get_hw_probes {lpgbtfpga_uplinkrst_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                endgroup
                )" );

    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 0 [get_hw_probes lpgbtfpga_uplinkrst_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                commit_hw_vio [get_hw_probes {lpgbtfpga_uplinkrst_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"vio_debug_inst"}]]
                endgroup
                )" );
    //# Wait
    sendCommand( "after 4000" );

    // ============================================================
    // Generate a pulse on start_lpgbt_programmer_vio
    // First set to 1, then back to 0
    // ============================================================
    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 1 [get_hw_probes start_lpgbt_programmer_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]]
                commit_hw_vio [get_hw_probes {start_lpgbt_programmer_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]]
                endgroup
                )" );

    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 0 [get_hw_probes start_lpgbt_programmer_vio -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]]
                commit_hw_vio [get_hw_probes {start_lpgbt_programmer_vio} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"sc_vio"}]]
                endgroup
                )" );

    //# ============================================================
    //# Generate a pulse for resetting the ethernet core
    //# First set to 1, then back to 0
    //# ============================================================
    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 0 [get_hw_probes axi_ethernet_core_inst/axi_lite_ctrl_inst/vio_axi_resetn -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_axi_ctrl_inst"}]]
                commit_hw_vio [get_hw_probes {axi_ethernet_core_inst/axi_lite_ctrl_inst/vio_axi_resetn} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_axi_ctrl_inst"}]]
                endgroup
                )" );

    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 1 [get_hw_probes axi_ethernet_core_inst/axi_lite_ctrl_inst/vio_axi_resetn -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_axi_ctrl_inst"}]]
                commit_hw_vio [get_hw_probes {axi_ethernet_core_inst/axi_lite_ctrl_inst/vio_axi_resetn} -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_axi_ctrl_inst"}]]
                endgroup
                )" );
    sendCommand( "after 4000" );
    //else sendCommand("close_hw_target [current_hw_target]");
    // Wait for ethernet to work
    sendCommand( "puts __YAODAQ_BOARD_FINISHED__" );
    while( !board_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
      info( "Configuring board..." );
    }
    board_finished.store( false );
    warn( "Board configured !" );
    return true;
  }

  void startTriggers()
  {
    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 1 [get_hw_probes -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_trig_type*"}] -filter {NAME=~"*rand_en_vio*"}]
                commit_hw_vio [get_hw_probes -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_trig_type*"}] -filter {NAME=~"*rand_en_vio*"}]
                endgroup
                )" );
  }

  void stopTriggers()
  {
    sendCommand( R"(
                startgroup
                set_property OUTPUT_VALUE 0 [get_hw_probes -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_trig_type*"}] -filter {NAME=~"*rand_en_vio*"}]
                commit_hw_vio [get_hw_probes -of_objects [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME=~"*vio_trig_type*"}] -filter {NAME=~"*rand_en_vio*"}]
                endgroup
                )" );
  }

  bool configure_dct()
  {
    warn( "Waiting DCT configuration step to finish !" );
    // Hardware target and device name
    std::string target{ "*/xilinx_tcf/Digilent/210299BBCD87" };
    std::string name{ "xc7a200t_0" };
    sendCommand( "set targets [get_hw_targets *]" );
    sendCommand( "puts \"Available targets: $targets\"" );
    sendCommand( fmt::format( "current_hw_target [get_hw_targets {}]", target ) );
    //  Open the selected hardware target
    sendCommand( "open_hw_target" );
    // Get the device only after the target is open
    sendCommand( fmt::format( "set HW_DEV [lindex [get_hw_devices {}] 0]", name ) );
    sendCommand( "if {$HW_DEV eq \"\"} {\nputs \"Available hardware devices: [get_hw_devices]\"\nerror \"Hardware device $HW_NAME not found.\"\n}" );
    // Select current hardware device
    sendCommand( fmt::format( "current_hw_device [get_hw_devices {}]", name ) );
    // Refresh device without updating probes yet
    sendCommand( "refresh_hw_device -update_hw_probes false $HW_DEV" );
    // ============================================================
    // Assign bitstream and probes files
    // ============================================================
    std::string file;
    if( self_trigger ) file = m_firmwares_path.string() + "BI_DCT_FW/" + m_dct_firmware_self_name;
    else
      file = m_firmwares_path.string() + "BI_DCT_FW/" + m_dct_firmware_name;
    warn( "firmware {}", file );
    sendCommand( fmt::format( "set_property PROBES.FILE      {}.ltx $HW_DEV", file ) );
    sendCommand( fmt::format( "set_property FULL_PROBES.FILE {}.ltx $HW_DEV", file ) );
    sendCommand( fmt::format( "set_property PROGRAM.FILE     {}.bit $HW_DEV", file ) );
    // Program the FPGA and refresh the hardware device
    sendCommand( "program_hw_devices $HW_DEV" );
    sendCommand( "refresh_hw_device $HW_DEV" );
    // ============================================================
    // Print visible debug cores
    // ============================================================
    sendCommand( R"(puts "Available ILA cores: [get_hw_ilas -of_objects $HW_DEV]")" );
    sendCommand( R"(puts "Available VIO cores: [get_hw_vios -of_objects $HW_DEV]")" );
    // ============================================================
    // Get ILA handles by CELL_NAME
    // ============================================================
    sendCommand( R"(set ILA_ELINKS  [lindex [get_hw_ilas -of_objects $HW_DEV -filter {CELL_NAME =~"ila_elinks_inst"}] 0])" );
    sendCommand( R"(puts "ILA_ELINKS  = $ILA_ELINKS")" );
    // ============================================================
    // Get VIO handles by CELL_NAME
    // ============================================================
    sendCommand( R"(set VIO_MASK_CTRL [lindex [get_hw_vios -of_objects $HW_DEV -filter {CELL_NAME =~"ctrl_word_vio"}] 0])" );
    sendCommand( R"(puts "VIO_MASK_CTRL = $VIO_MASK_CTRL")" );
    // ============================================================
    // Basic checks on required VIOs
    // ============================================================
    sendCommand( R"(if { $VIO_MASK_CTRL eq "" } { error "VIO 'ctrl_word_vio' not found." })" );
    // ============================================================
    // Open ILA dashboards if found
    // ============================================================
    sendCommand( R"(if {$ILA_ELINKS ne ""} { display_hw_ila_data [get_hw_ila_data hw_ila_data_1 -of_objects $ILA_ELINKS] } else { puts "WARNING: ILA 'ila_elinks_inst' not found." })" );
    // ============================================================
    // Commit all available VIOs
    // ============================================================
    sendCommand( R"(if {$VIO_MASK_CTRL ne ""} { commit_hw_vio $VIO_MASK_CTRL })" );

    // ============================================================
    // Get handle to channel_mask_left probe from script
    // ============================================================
    // Retrieve the probe object associated to 'channel_mask_left'
    sendCommand( "set CHANNEL_MASK_LEFT [lindex [get_hw_probes channel_mask_left -of_objects $VIO_MASK_CTRL] 0]" );
    // Set the radix (display/interpretation format) to binary
    sendCommand( "set_property OUTPUT_VALUE_RADIX BINARY $CHANNEL_MASK_LEFT" );
    // channel mask eta+
    //          . layer2. layer1. layer0
    sendCommand( "set eta_p_1 000000000000000000000000" );
    sendCommand( "set eta_p_2 000000000000000000000000" );
    sendCommand( "set eta_p_3 000000000000000000000000" );
    sendCommand( "set eta_p_4 000000000000000000000000" );
    sendCommand( "set eta_p_5 000000000000000000000000" );
    sendCommand( "set eta_p_6 000000000000000000000000" );
    sendCommand( "set mask_eta_p $eta_p_6$eta_p_5$eta_p_4$eta_p_3$eta_p_2$eta_p_1" );
    // Assign the desired mask value
    sendCommand( "set_property OUTPUT_VALUE $mask_eta_p $CHANNEL_MASK_LEFT" );
    // Commit the value to the VIO
    sendCommand( "commit_hw_vio $CHANNEL_MASK_LEFT" );

    // ============================================================
    // Get handle to channel_mask_right probe from script
    // ============================================================
    // Retrieve the probe object associated to 'channel_mask_right'
    sendCommand( "set CHANNEL_MASK_RIGHT [lindex [get_hw_probes channel_mask_right -of_objects $VIO_MASK_CTRL] 0]" );
    // Set the radix (display/interpretation format) to binary
    sendCommand( "set_property OUTPUT_VALUE_RADIX BINARY $CHANNEL_MASK_RIGHT" );
    //          . layer2. layer1. layer0b eta-
    sendCommand( "set eta_m_1 000000000000000000000000" );
    sendCommand( "set eta_m_2 000000000000000000000000" );
    sendCommand( "set eta_m_3 000000000000000000000000" );
    sendCommand( "set eta_m_4 000000000000000000000000" );
    sendCommand( "set eta_m_5 000000000000000000000000" );
    sendCommand( "set eta_m_6 000000000000000000000000" );
    sendCommand( "set mask_eta_m $eta_m_6$eta_m_5$eta_m_4$eta_m_3$eta_m_2$eta_m_1" );
    // Assign the desired mask value
    sendCommand( "set_property OUTPUT_VALUE $mask_eta_m $CHANNEL_MASK_RIGHT" );
    // Commit the value to the VIO
    sendCommand( "commit_hw_vio $CHANNEL_MASK_RIGHT" );

    // Set dead time value in VIO output
    sendCommand( "set DEAD_TIME_VIO [lindex [get_hw_probes dead_time -of_objects $VIO_MASK_CTRL] 0]" );
    sendCommand( "set_property OUTPUT_VALUE_RADIX UNSIGNED $DEAD_TIME_VIO" );
    // set dead time in number of BCs (must be < 8)
    sendCommand( "set dead_time 0" );
    sendCommand( "set_property OUTPUT_VALUE $dead_time $DEAD_TIME_VIO" );
    sendCommand( "commit_hw_vio $DEAD_TIME_VIO" );
    // disconnect target
    sendCommand( "close_hw_target [current_hw_target]" );
    sendCommand( "puts __YAODAQ_DCT_FINISHED__" );
    while( !dct_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
      info( "Configuring DCT..." );
    }
    warn( "DCT configured!" );
    dct_finished.store( false );
    return true;
  }

  bool on_start() override
  {
    info( "Starting Ethernet capture" );
    if( !m_ethernet.start() )
    {
      error( "m_ethernet.start() failed" );
      return false;
    }

    if( self_trigger ) startTriggers();
    return true;
  }

  bool on_pause() override
  {
    info( "Pausing Ethernet processing" );
    if( self_trigger ) stopTriggers();
    m_ethernet.pause();
    return true;
  }

  bool on_resume() override
  {
    info( "Resuming Ethernet processing" );
    if( !m_ethernet.start() )
    {
      error( "m_ethernet.start() failed" );
      return false;
    }
    if( self_trigger ) startTriggers();
    return true;
  }

  bool on_stop() override
  {
    info( "Stopping Ethernet capture" );
    if( self_trigger ) stopTriggers();
    m_ethernet.stop();
    info( "Ethernet capture stopped" );
    return true;
  }

  std::function<bool( std::stop_token )> fun = [this]( std::stop_token stop ) -> bool
  {
    auto ret = m_ethernet.state.getTwoPacket( stop );
    parseAndSend( ret );
    return true;
  };

  void parseAndSend( std::optional<std::pair<std::unique_ptr<pcpp::Packet>, std::unique_ptr<pcpp::Packet>>>& packets )
  {
    if( !packets ) return;
    auto* eth1 = packets->first->getLayerOfType<pcpp::EthLayer>();
    auto* eth2 = packets->second->getLayerOfType<pcpp::EthLayer>();
    if( eth1 == nullptr || eth2 == nullptr )
    {
      error( "oups" );
      return;
    }
    if( eth1->getLayerPayloadSize() < 2 || eth2->getLayerPayloadSize() < 2 ) return;

    thread_local std::string json;
    json.clear();
    json.reserve( 8192 );

    json += R"({"event_number":)";
    json += std::to_string( event() );
    json += R"(,"packets":[{)";

    json += R"("packet_number":")";
    json += std::format( "0x{:02x}", eth1->getLayerPayload()[0] );
    json += R"(","data":[)";

    bool first = true;

    for( std::size_t pos = 2; pos + sizeof( std::uint32_t ) <= eth1->getLayerPayloadSize(); pos += sizeof( std::uint32_t ) )
    {
      std::uint32_t word{ 0 };

      std::memcpy( &word, eth1->getLayerPayload() + pos, sizeof( word ) );

      if( !first ) json += ',';

      json += '"';
      json += std::format( "0x{:08x}", word );
      json += '"';

      first = false;
    }

    json += "]}";
    json += ",{";

    json += R"("packet_number":")";
    json += std::format( "0x{:02x}", eth2->getLayerPayload()[0] );
    json += R"(","data":[)";

    first = true;

    for( std::size_t pos = 2; pos + sizeof( std::uint32_t ) <= eth2->getLayerPayloadSize(); pos += sizeof( std::uint32_t ) )
    {
      std::uint32_t word{ 0 };

      std::memcpy( &word, eth2->getLayerPayload() + pos, sizeof( word ) );

      if( ( word & 0x0FFFFFFF ) != 0x05555555 )
      {
        if( !first ) json += ',';

        json += '"';
        json += std::format( "0x{:08x}", word );
        json += '"';

        first = false;
      }
    }

    json += "]}";
    json += "]}";

    send( yaodaq::RawDataBuilder::from_text( json, eth1->getSourceMac().toString() ) );
    //std::cout<<json<<std::endl;
    /*for (std::size_t pos = 2; pos + sizeof(std::uint32_t) <= eth1->getLayerPayloadSize(); pos += sizeof(std::uint32_t))
  {
    std::uint32_t word{0};
    std::memcpy(&word, eth1->getLayerPayload() + pos, sizeof(word));
    if ((word & 0x0FFFFFFF) != 0x05555555) info("0x{:08x}", word);
  }
  for (std::size_t pos = 2; pos + sizeof(std::uint32_t) <= eth1->getLayerPayloadSize(); pos += sizeof(std::uint32_t))
  {
    std::uint32_t word{0};
    std::memcpy(&word, eth2->getLayerPayload() + pos, sizeof(word));
    if ((word & 0x0FFFFFFF) != 0x05555555) info("0x{:08x}", word);
  }*/
  }

  std::vector<std::string> splitMessage( const std::string_view message )
  {
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

  bool pre_disconnect( const bool alreadyDone ) override
  {
    info( "pre_disconnect()" );
    m_ethernet.stop();
    m_ethernet.close();
    sendCommand( "close_hw_target" );
    sendCommand( "disconnect_hw_server" );
    sendCommand( "close_hw_manager" );
    sendCommand( "puts __YAODAQ_DISCONNECT_FINISHED__" );
    sendCommand( "exit" );
    while( !disconnect_finished.load() )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
      warn( "Waiting disconnect step to finish !" );
    }
    disconnect_finished.store( false );
    Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
    return true;
  }

private:
  std::filesystem::path m_firmwares_path;
  std::string           m_dct_firmware_name;
  std::string           m_dct_firmware_self_name;
  std::string           m_board_firmware_name;
  std::string           m_board_firmware_self_name;
  std::string           m_ethernet_name;
  std::string           m_mac_adress;
  bool                  self_trigger{ false };

  std::atomic<bool> board_finished{ false };
  std::atomic<bool> dct_finished{ false };
  std::atomic<bool> connect_finished{ false };
  std::atomic<bool> disconnect_finished{ false };

  struct CaptureState
  {
    mutable std::mutex                        mutex;
    mutable std::condition_variable           cv;
    std::queue<std::unique_ptr<pcpp::Packet>> packets;
    std::atomic<bool>                         accepting{ false };
    std::atomic<bool>                         running{ false };
    std::size_t                               max_event{ 0 };
    std::size_t                               packet_captured{ 0 };
    void                                      clear()
    {
      std::queue<std::unique_ptr<pcpp::Packet>> old;
      {
        std::lock_guard<std::mutex> lock( mutex );
        packets.swap( old );
        packet_captured = 0;
      }
    }
    std::size_t size() const
    {
      std::lock_guard<std::mutex> lock( mutex );
      return packets.size();
    }

    std::optional<std::pair<std::unique_ptr<pcpp::Packet>, std::unique_ptr<pcpp::Packet>>> getTwoPacket( std::stop_token stop )
    {
      std::unique_lock<std::mutex> lock( mutex );
      while( !stop.stop_requested() )
      {
        // Need at least one packet.
        cv.wait( lock, [&] { return !packets.empty() || !running.load( std::memory_order_acquire ) || stop.stop_requested(); } );

        if( stop.stop_requested() || !running.load( std::memory_order_acquire ) ) return std::nullopt;

        // ------------------------------------------------------------
        // Synchronize on 0x0a.
        // Discard everything before the first 0x0a.
        // ------------------------------------------------------------
        while( !packets.empty() )
        {
          auto* eth = packets.front()->getLayerOfType<pcpp::EthLayer>();
          if( eth && eth->getLayerPayloadSize() >= 1 && static_cast<std::uint8_t>( eth->getLayerPayload()[0] ) == 0x0a ) break;
          packets.pop();
        }
        if( packets.empty() ) continue;
        // ------------------------------------------------------------
        // We have 0x0a at the front.
        // Wait until the next packet arrives.
        // ------------------------------------------------------------
        cv.wait( lock, [&] { return packets.size() >= 2 || !running.load( std::memory_order_acquire ) || stop.stop_requested(); } );

        if( stop.stop_requested() || !running.load( std::memory_order_acquire ) ) return std::nullopt;

        if( packets.size() < 2 ) continue;

        // ------------------------------------------------------------
        // Temporarily remove the 0x0a packet.
        // Now packets.front() is the SECOND packet.
        // ------------------------------------------------------------
        auto packet1 = std::move( packets.front() );
        packets.pop();

        auto* eth1 = packet1->getLayerOfType<pcpp::EthLayer>();
        if( !eth1 || eth1->getLayerPayloadSize() < 1 ) { continue; }

        const auto type1 = static_cast<std::uint8_t>( eth1->getLayerPayload()[0] );
        // This should be 0x0a because we synchronized above.
        if( type1 != 0x0a ) { continue; }

        // ------------------------------------------------------------
        // Now the queue is guaranteed to contain the second packet.
        // ------------------------------------------------------------
        if( packets.empty() )
        {
          // Should not normally happen because we checked size >= 2,
          // but keep the invariant safe.
          continue;
        }

        auto* eth2 = packets.front()->getLayerOfType<pcpp::EthLayer>();
        if( !eth2 || eth2->getLayerPayloadSize() < 1 )
        {
          // Second packet is invalid. Discard packet1 and continue.
          packets.pop();
          continue;
        }

        const auto type2 = static_cast<std::uint8_t>( eth2->getLayerPayload()[0] );

        // ------------------------------------------------------------
        // Correct pair: 0x0a 0x0b
        // ------------------------------------------------------------
        if( type2 == 0x0b )
        {
          auto packet2 = std::move( packets.front() );
          packets.pop();

          return std::make_pair( std::move( packet1 ), std::move( packet2 ) );
        }

        // ------------------------------------------------------------
        // We got:
        //
        //   0x0a <something-not-0x0b>
        //
        // The 0x0a cannot form a valid pair, so discard packet1.
        // Keep the second packet in the queue because it might be
        // the beginning of the next event.
        // ------------------------------------------------------------
      }

      return std::nullopt;
    }
  };

  void sendCommand( const std::string_view command )
  {
    const std::string                com = std::string( command ) + '\n';
    //info("Sending command: {}",com);
    std::unique_ptr<yaodaq::RawData> raw = std::make_unique<yaodaq::RawData>( yaodaq::RawDataBuilder::from_text( com, "command" ) );
    send_to_device( std::move( raw ) );
  }

  class EthernetCapture
  {
  public:
    EthernetCapture() = default;
    ~EthernetCapture()
    {
      if( dev )
      {
        stop();
        close();
      }
    }
    EthernetCapture( const std::string_view board, const std::string_view mac_adress ) : m_board( board ), m_mac( mac_adress ) {}
    void setBoard( const std::string_view board ) { m_board = board; }
    void setMac( const std::string_view mac ) { m_mac = mac; }
    bool open()
    {
      dev = pcpp::PcapLiveDeviceList::getInstance().getDeviceByName( m_board );
      if( !dev )
      {
        //error("pcpp::PcapLiveDeviceList::getInstance().getDeviceByName({}) failed", m_ethernet_name);
        //Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
        return false;
      }
      if( !dev->open() )
      {
        //error("Can't open {}",m_ethernet_name);
        //Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
        return false;
      }
      if( !dev->setFilter( fmt::format( "ether src {}", m_mac ) ) )
      {
        //error("Could not set BPF filter with mac: {}",m_mac_adress);
        dev->close();
        //Term::terminal.setOptions( Term::Option::Raw, Term::Option::Cursor );
        return false;
      }
      return true;
    }
    std::string name()
    {
      if( dev ) return dev->getName();
      else
        return "";
    }
    std::string description()
    {
      if( dev ) return dev->getDesc();
      else
        return "";
    }
    std::string mac_address() { return m_mac; }
    std::string defaultGateway()
    {
      if( dev ) return dev->getDefaultGateway().toString();
      else
        return "";
    }
    std::string mtu()
    {
      if( dev ) return std::to_string( dev->getMtu() );
      else
        return "";
    }
    std::string dmsServer()
    {
      if( dev ) dev->getDnsServers().front().toString();
      else
        return "";
    }
    void         maxEvents( const std::size_t i ) { state.max_event = i; }
    CaptureState state;
    bool         start()
    {
      if( !dev || !dev->isOpened() ) return false;
      // Already capturing?
      if( state.running.exchange( true ) )
      {
        // We were already running: just resume accepting packets.
        state.accepting.store( true, std::memory_order_release );
        state.cv.notify_all();
        return true;
      }
      state.accepting.store( true );
      if( !dev->startCapture( m_callback, &state ) )
      {
        state.running.store( false );
        state.accepting.store( false );
        state.cv.notify_all();
        return false;
      }
      return true;
    }

    void pause()
    {
      state.accepting.store( false );
      state.cv.notify_all();
    }

    void stop()
    {
      state.accepting.store( false );
      stopCapture();
      state.clear();
      state.cv.notify_all();
    }
    void stopCapture()
    {
      if( dev && state.running.load() ) { dev->stopCapture(); }
      state.running.store( false );
    }
    void close()
    {
      if( dev ) dev->close();
    }

  private:
    using Callback = pcpp::OnPacketArrivesCallback;
    pcpp::PcapLiveDevice* dev{ nullptr };
    std::string           m_board;
    std::string           m_mac;
    Callback              m_callback = [this]( pcpp::RawPacket* packet, pcpp::PcapLiveDevice* /*device*/, void* cookie )
    {
      auto* state = static_cast<CaptureState*>( cookie );

      if( !state->accepting.load( std::memory_order_acquire ) ) return;

      // Make an independent copy of the RawPacket.
      auto rawCopy = std::make_unique<pcpp::RawPacket>( *packet );

      // Packet owns rawCopy now.
      auto parsedPacket = std::make_unique<pcpp::Packet>( rawCopy.release(), true );

      {
        std::lock_guard<std::mutex> lock( state->mutex );

        if( !state->accepting.load( std::memory_order_acquire ) ) return;
        if( state->packet_captured / 2 > state->max_event )
        {
          state->accepting.store( false, std::memory_order_release );
          state->cv.notify_all();
          return;
        }
        state->packets.push( std::move( parsedPacket ) );
        ++state->packet_captured;
      }
      state->cv.notify_one();
    };
  };

  EthernetCapture m_ethernet;
};
