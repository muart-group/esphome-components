#pragma once

#include "queue"
#include "esphome/components/uart/uart.h"
#include "esphome/core/helpers.h"
#include "itp_packetprocessor.h"

using namespace itp_packet;

namespace esphome {
namespace mitsubishi_itp {

static constexpr char BRIDGE_TAG[] = "mitp_bridge";
static const uint32_t RESPONSE_TIMEOUT_MS = 3000;  // Maximum amount of time to wait for an expected response packet
/* Maximum number of packets allowed to be queued for sending.  In some circumstances the equipment response
time can be very slow and packets would queue up faster than they were being received.  TODO: Not sure what size this
should be, 4ish should be enough for almost all situations, so 8 seems plenty.*/
static const size_t MAX_QUEUE_SIZE = 8;

// A UARTComponent wrapper to send and receieve packets
class MITPBridge {
 public:
  MITPBridge(uart::UARTComponent *uart_component, PacketProcessor *packet_processor);

  /* Queues a packet to be sent by the bridge.  If the queue is full, the packet will not be
  enqueued.*/
  template<typename PType> void send_packet(const PType &packet_to_send) {
    static_assert(std::is_base_of_v<Packet, PType>, "PType must derive from Packet");

    if (pkt_queue_.size() <= MAX_QUEUE_SIZE) {
      pkt_queue_.push(std::make_unique<PType>(packet_to_send));
    } else {
      ESP_LOGW(BRIDGE_TAG, "Packet queue full!  %x packet not sent.", packet_to_send.get_packet_type());
    }
  }

  // Checks for incoming packets, processes them, sends queued packets
  virtual void loop() = 0;

 protected:
  optional<RawPacket> receive_raw_packet_(SourceBridge source_bridge,
                                          ControllerAssociation controller_association) const;
  void write_raw_packet_(const RawPacket &packet_to_send) const;
  template<class P> void process_raw_packet_(RawPacket &pkt, bool expect_response = true) const;
  void classify_and_process_raw_packet_(RawPacket &pkt) const;

  uart::UARTComponent &uart_comp_;
  PacketProcessor &pkt_processor_;
  std::queue<std::unique_ptr<Packet>> pkt_queue_;
  std::unique_ptr<Packet> packet_awaiting_response_ = nullptr;
  uint32_t packet_sent_millis_;
};

class HeatpumpBridge : public MITPBridge {
 public:
  using MITPBridge::MITPBridge;
  void loop() override;
};

class ThermostatBridge : public MITPBridge {
 public:
  using MITPBridge::MITPBridge;
  // ThermostatBridge(uart::UARTComponent &uart_component, PacketProcessor &packet_processor) :
  // MITPBridge(uart_component, packet_processor){};
  void loop() override;
};

}  // namespace mitsubishi_itp
}  // namespace esphome
