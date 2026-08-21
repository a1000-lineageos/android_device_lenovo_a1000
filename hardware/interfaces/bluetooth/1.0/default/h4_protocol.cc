//
// Copyright 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "h4_protocol.h"

#define LOG_TAG "android.hardware.bluetooth-hci-h4"

#include <errno.h>
#include <fcntl.h>
#include <log/log.h>
#include <sys/uio.h>
#include <vector>
#include <unistd.h>

namespace android {
namespace hardware {
namespace bluetooth {
namespace hci {

size_t H4Protocol::Send(uint8_t type, const uint8_t* data, size_t length) {
  // A1000: одна запись вместо writev. /dev/sttybt0 — не UART, а SIPC-канал к
  // WCN-чипу, и каждая запись уходит туда отдельным сообщением. writev в ядре
  // 3.10 для tty разваливается на два write(), контроллер получает байт типа
  // отдельно от тела и команду не разбирает.
  std::vector<uint8_t> packet;
  packet.reserve(length + 1);
  packet.push_back(type);
  packet.insert(packet.end(), data, data + length);
  ssize_t ret = 0;
  do {
    ret = TEMP_FAILURE_RETRY(write(uart_fd_, packet.data(), packet.size()));
  } while (-1 == ret && EAGAIN == errno);

  if (ret == -1) {
    ALOGE("%s error writing to UART (%s)", __func__, strerror(errno));
  } else if (ret < static_cast<ssize_t>(length + 1)) {
    ALOGE("%s: %d / %d bytes written - something went wrong...", __func__,
          static_cast<int>(ret), static_cast<int>(length + 1));
  }
  return ret;
}

void H4Protocol::OnPacketReady() {
  switch (hci_packet_type_) {
    case HCI_PACKET_TYPE_EVENT:
      event_cb_(hci_packetizer_.GetPacket());
      break;
    case HCI_PACKET_TYPE_ACL_DATA:
      acl_cb_(hci_packetizer_.GetPacket());
      break;
    case HCI_PACKET_TYPE_SCO_DATA:
      sco_cb_(hci_packetizer_.GetPacket());
      break;
    default:
      LOG_ALWAYS_FATAL("%s: Unimplemented packet type %d", __func__,
                       static_cast<int>(hci_packet_type_));
  }
  // Get ready for the next type byte.
  hci_packet_type_ = HCI_PACKET_TYPE_UNKNOWN;
}

void H4Protocol::OnDataReady(int fd) {
  if (hci_packet_type_ == HCI_PACKET_TYPE_UNKNOWN) {
    uint8_t buffer[1] = {0};
    ssize_t bytes_read = TEMP_FAILURE_RETRY(read(fd, buffer, 1));
    if (bytes_read != 1) {
      if (bytes_read == 0) {
        LOG_ALWAYS_FATAL("%s: Unexpected EOF reading the packet type!",
                         __func__);
      } else if (bytes_read < 0) {
        LOG_ALWAYS_FATAL("%s: Read packet type error: %s", __func__,
                         strerror(errno));
      } else {
        LOG_ALWAYS_FATAL("%s: More bytes read than expected (%u)!", __func__,
                         static_cast<unsigned int>(bytes_read));
      }
    }
    hci_packet_type_ = static_cast<HciPacketType>(buffer[0]);
    if (hci_packet_type_ != HCI_PACKET_TYPE_ACL_DATA &&
        hci_packet_type_ != HCI_PACKET_TYPE_SCO_DATA &&
        hci_packet_type_ != HCI_PACKET_TYPE_EVENT) {
      // A1000: sprd-контроллер после заливки pskey оставляет в канале хвост
      // ответа, который вендорская либа не дочитала. Убивать из-за него весь
      // HAL нельзя — просто ресинхронизируемся на следующем байте.
      ALOGE("%s: skipping unexpected type byte 0x%02x", __func__,
            static_cast<unsigned int>(buffer[0]));
      hci_packet_type_ = HCI_PACKET_TYPE_UNKNOWN;
      return;
    }
  } else {
    hci_packetizer_.OnDataReady(fd, hci_packet_type_);
  }
}

}  // namespace hci
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
