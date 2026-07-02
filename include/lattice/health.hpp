#pragma once

#include "lattice/inspection.hpp"
#include "lattice/route_policy.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lattice {

enum class HealthLevel : std::uint8_t {
  healthy,
  degraded,
  failed
};

enum class HealthSignalCode : std::uint8_t {
  stream_empty,
  stream_decoder_errors,
  stream_policy_errors,
  stream_warnings,
  stream_payload_pressure,
  stream_eof_missing,
  stream_control_only,
  connection_not_negotiated,
  connection_no_delivery,
  connection_diagnostics,
  connection_resets,
  connection_closed,
  connection_timer_pressure,
  route_policy_empty,
  route_policy_invalid,
  route_policy_sparse,
  route_policy_loopback,
  route_policy_duplicate_family,
  aggregate_failure
};

struct HealthSignal {
  HealthSignalCode code{HealthSignalCode::aggregate_failure};
  HealthLevel level{HealthLevel::degraded};
  std::uint32_t weight{1};
  std::string subject;
  std::string detail;
};

struct HealthOptions {
  std::size_t max_stream_warnings{8};
  std::size_t max_stream_errors{0};
  std::uint32_t max_payload_pressure_percent{85};
  std::size_t min_routes{1};
  bool require_stream_eof{true};
  bool require_negotiation{true};
  bool require_delivery_for_payload{true};
  bool require_route_payload{true};
};

struct HealthReport {
  HealthLevel level{HealthLevel::healthy};
  std::uint32_t score{100};
  std::size_t healthy_checks{0};
  std::size_t degraded_checks{0};
  std::size_t failed_checks{0};
  std::vector<HealthSignal> signals;

  [[nodiscard]] bool ok() const { return level != HealthLevel::failed; }
};

struct HealthSignalBucket {
  HealthSignalCode code{HealthSignalCode::aggregate_failure};
  HealthLevel strongest_level{HealthLevel::healthy};
  std::size_t count{0};
  std::uint32_t total_weight{0};
};

[[nodiscard]] const char* to_string(HealthLevel level);
[[nodiscard]] const char* to_string(HealthSignalCode code);
[[nodiscard]] HealthLevel combine_health_level(HealthLevel lhs, HealthLevel rhs);
[[nodiscard]] bool health_level_at_least(HealthLevel actual, HealthLevel threshold);
[[nodiscard]] HealthReport evaluate_stream_health(
    const StreamInspectionReport& inspection,
    const HealthOptions& options = {});
[[nodiscard]] HealthReport evaluate_connection_health(
    const ConnectionEventSummary& summary,
    const HealthOptions& options = {});
[[nodiscard]] HealthReport evaluate_route_policy_health(
    const RoutePolicyDocument& document,
    const HealthOptions& options = {});
[[nodiscard]] HealthReport merge_health_reports(
    const std::vector<HealthReport>& reports,
    const HealthOptions& options = {});
[[nodiscard]] std::vector<HealthSignal> filter_health_signals(
    const HealthReport& report,
    HealthLevel minimum_level);
[[nodiscard]] std::vector<HealthSignalBucket> summarize_health_signals(
    const HealthReport& report);
[[nodiscard]] std::string format_health_report(const HealthReport& report);
[[nodiscard]] std::string format_health_signal_summary(
    const std::vector<HealthSignalBucket>& buckets);

}  // namespace lattice
