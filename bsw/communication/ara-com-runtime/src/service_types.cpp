// SPDX-License-Identifier: MIT

#include "openautosar/com/service_types.h"

namespace openautosar::com {

const char* ToString(Binding binding) noexcept {
  switch (binding) {
    case Binding::kLocalIpc:
      return "local-ipc";
    case Binding::kSomeIp:
      return "someip";
    case Binding::kDds:
      return "dds";
  }

  return "unknown";
}

const char* ToString(OfferState state) noexcept {
  switch (state) {
    case OfferState::kOffered:
      return "offered";
    case OfferState::kStopped:
      return "stopped";
  }

  return "unknown";
}

const char* ToString(HealthState state) noexcept {
  switch (state) {
    case HealthState::kHealthy:
      return "healthy";
    case HealthState::kDegraded:
      return "degraded";
    case HealthState::kFailed:
      return "failed";
  }

  return "unknown";
}

}  // namespace openautosar::com
