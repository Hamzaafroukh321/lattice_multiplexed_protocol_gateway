#include "lattice/health.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace lattice {
namespace {

[[nodiscard]] std::uint32_t bounded_subtract(std::uint32_t value, std::uint32_t penalty) {
  if (penalty >= value) {
    return 0U;
  }
  return value - penalty;
}

[[nodiscard]] std::uint32_t capped_weight(std::size_t count, std::uint32_t per_item,
                                          std::uint32_t cap) {
  const std::uint64_t total = static_cast<std::uint64_t>(count) *
                              static_cast<std::uint64_t>(per_item);
  if (total > static_cast<std::uint64_t>(cap)) {
    return cap;
  }
  return static_cast<std::uint32_t>(total);
}

[[nodiscard]] std::uint8_t level_rank(HealthLevel level) {
  switch (level) {
    case HealthLevel::healthy:
      return 0U;
    case HealthLevel::degraded:
      return 1U;
    case HealthLevel::failed:
      return 2U;
  }
  return 0U;
}

void record_healthy(HealthReport& report) {
  ++report.healthy_checks;
}

void add_signal(HealthReport& report, HealthSignalCode code, HealthLevel level,
                std::uint32_t weight, std::string subject, std::string detail) {
  report.signals.push_back(
      HealthSignal{code, level, weight, std::move(subject), std::move(detail)});
  report.level = combine_health_level(report.level, level);
  if (level == HealthLevel::failed) {
    ++report.failed_checks;
    report.score = bounded_subtract(report.score, weight == 0U ? 25U : weight);
    return;
  }
  if (level == HealthLevel::degraded) {
    ++report.degraded_checks;
    report.score = bounded_subtract(report.score, weight == 0U ? 5U : weight);
    return;
  }
  ++report.healthy_checks;
}

void finalize_report(HealthReport& report) {
  if (report.failed_checks != 0U) {
    report.level = HealthLevel::failed;
    if (report.score > 49U) {
      report.score = 49U;
    }
    return;
  }
  if (report.degraded_checks != 0U) {
    report.level = HealthLevel::degraded;
    if (report.score > 89U) {
      report.score = 89U;
    }
    return;
  }
  report.level = HealthLevel::healthy;
  report.score = std::max<std::uint32_t>(report.score, 90U);
}

[[nodiscard]] std::string count_detail(const char* label, std::size_t count) {
  std::ostringstream out;
  out << label << '=' << count;
  return out.str();
}

[[nodiscard]] std::string percent_detail(const char* label, std::size_t numerator,
                                         std::size_t denominator) {
  std::ostringstream out;
  out << label << '=';
  if (denominator == 0U) {
    out << "n/a";
    return out.str();
  }
  out << ((numerator * 100U) / denominator) << '%';
  return out.str();
}

[[nodiscard]] bool route_has_same_endpoint(const RoutePolicyEntry& route) {
  return route.source == route.destination;
}

[[nodiscard]] std::string route_subject(const RoutePolicyEntry& route, std::size_t index) {
  if (!route.name.empty()) {
    return route.name;
  }
  std::ostringstream out;
  out << "route[" << index << ']';
  return out.str();
}

}  // namespace

const char* to_string(HealthLevel level) {
  switch (level) {
    case HealthLevel::healthy:
      return "healthy";
    case HealthLevel::degraded:
      return "degraded";
    case HealthLevel::failed:
      return "failed";
  }
  return "unknown";
}

const char* to_string(HealthSignalCode code) {
  switch (code) {
    case HealthSignalCode::stream_empty:
      return "stream_empty";
    case HealthSignalCode::stream_decoder_errors:
      return "stream_decoder_errors";
    case HealthSignalCode::stream_policy_errors:
      return "stream_policy_errors";
    case HealthSignalCode::stream_warnings:
      return "stream_warnings";
    case HealthSignalCode::stream_payload_pressure:
      return "stream_payload_pressure";
    case HealthSignalCode::stream_eof_missing:
      return "stream_eof_missing";
    case HealthSignalCode::stream_control_only:
      return "stream_control_only";
    case HealthSignalCode::connection_not_negotiated:
      return "connection_not_negotiated";
    case HealthSignalCode::connection_no_delivery:
      return "connection_no_delivery";
    case HealthSignalCode::connection_diagnostics:
      return "connection_diagnostics";
    case HealthSignalCode::connection_resets:
      return "connection_resets";
    case HealthSignalCode::connection_closed:
      return "connection_closed";
    case HealthSignalCode::connection_timer_pressure:
      return "connection_timer_pressure";
    case HealthSignalCode::route_policy_empty:
      return "route_policy_empty";
    case HealthSignalCode::route_policy_invalid:
      return "route_policy_invalid";
    case HealthSignalCode::route_policy_sparse:
      return "route_policy_sparse";
    case HealthSignalCode::route_policy_loopback:
      return "route_policy_loopback";
    case HealthSignalCode::route_policy_duplicate_family:
      return "route_policy_duplicate_family";
    case HealthSignalCode::aggregate_failure:
      return "aggregate_failure";
  }
  return "unknown";
}

HealthLevel combine_health_level(HealthLevel lhs, HealthLevel rhs) {
  if (lhs == HealthLevel::failed || rhs == HealthLevel::failed) {
    return HealthLevel::failed;
  }
  if (lhs == HealthLevel::degraded || rhs == HealthLevel::degraded) {
    return HealthLevel::degraded;
  }
  return HealthLevel::healthy;
}

bool health_level_at_least(HealthLevel actual, HealthLevel threshold) {
  return level_rank(actual) >= level_rank(threshold);
}

HealthReport evaluate_stream_health(const StreamInspectionReport& inspection,
                                    const HealthOptions& options) {
  HealthReport report;
  if (inspection.frames.empty()) {
    add_signal(report, HealthSignalCode::stream_empty, HealthLevel::failed, 35U,
               "stream", "no complete frames were decoded");
  } else {
    record_healthy(report);
  }

  const std::size_t errors = inspection.error_count();
  if (errors > options.max_stream_errors) {
    const bool decoder_failed = std::any_of(
        inspection.findings.begin(), inspection.findings.end(),
        [](const FrameFinding& finding) {
          return finding.severity == InspectionSeverity::error &&
                 finding.code == FrameFindingCode::decoder_error;
        });
    add_signal(report,
               decoder_failed ? HealthSignalCode::stream_decoder_errors
                              : HealthSignalCode::stream_policy_errors,
               HealthLevel::failed, capped_weight(errors, 18U, 60U), "stream",
               count_detail("errors", errors));
  } else {
    record_healthy(report);
  }

  const std::size_t warnings = inspection.warning_count();
  if (warnings > options.max_stream_warnings) {
    add_signal(report, HealthSignalCode::stream_warnings, HealthLevel::degraded,
               capped_weight(warnings - options.max_stream_warnings, 3U, 18U),
               "stream", count_detail("warnings", warnings));
  } else {
    record_healthy(report);
  }

  if (options.require_stream_eof && !inspection.decoder_reached_eof) {
    add_signal(report, HealthSignalCode::stream_eof_missing, HealthLevel::degraded,
               8U, "stream", "decoder was left waiting for more bytes");
  } else {
    record_healthy(report);
  }

  if (!inspection.frames.empty() && inspection.data_frames == 0U) {
    add_signal(report, HealthSignalCode::stream_control_only, HealthLevel::degraded,
               5U, "stream", "stream contains control frames but no DATA frames");
  } else {
    record_healthy(report);
  }

  if (inspection.decoded_bytes != 0U) {
    const std::size_t pressure = (inspection.payload_bytes * 100U) / inspection.decoded_bytes;
    if (pressure > options.max_payload_pressure_percent) {
      add_signal(report, HealthSignalCode::stream_payload_pressure,
                 HealthLevel::degraded, 7U, "stream",
                 percent_detail("payload_pressure", inspection.payload_bytes,
                                inspection.decoded_bytes));
    } else {
      record_healthy(report);
    }
  } else {
    record_healthy(report);
  }

  finalize_report(report);
  return report;
}

HealthReport evaluate_connection_health(const ConnectionEventSummary& summary,
                                        const HealthOptions& options) {
  HealthReport report;
  if (summary.total == 0U) {
    add_signal(report, HealthSignalCode::connection_not_negotiated,
               HealthLevel::failed, 40U, "connection", "event log is empty");
    finalize_report(report);
    return report;
  }

  if (options.require_negotiation && summary.negotiated == 0U) {
    add_signal(report, HealthSignalCode::connection_not_negotiated,
               HealthLevel::failed, 30U, "connection", "no negotiation event was recorded");
  } else {
    record_healthy(report);
  }

  if (options.require_delivery_for_payload && summary.payload_bytes != 0U &&
      summary.delivered == 0U) {
    add_signal(report, HealthSignalCode::connection_no_delivery,
               HealthLevel::degraded, 12U, "connection",
               "payload bytes were observed without a delivered message");
  } else {
    record_healthy(report);
  }

  if (summary.diagnostics != 0U) {
    add_signal(report, HealthSignalCode::connection_diagnostics,
               HealthLevel::degraded, capped_weight(summary.diagnostics, 4U, 24U),
               "connection", count_detail("diagnostics", summary.diagnostics));
  } else {
    record_healthy(report);
  }

  if (summary.resets != 0U) {
    add_signal(report, HealthSignalCode::connection_resets,
               HealthLevel::degraded, capped_weight(summary.resets, 5U, 20U),
               "connection", count_detail("resets", summary.resets));
  } else {
    record_healthy(report);
  }

  if (summary.closed != 0U && summary.delivered == 0U) {
    add_signal(report, HealthSignalCode::connection_closed,
               HealthLevel::degraded, 8U, "connection",
               "connection closed before delivering any messages");
  } else {
    record_healthy(report);
  }

  if (summary.timers > summary.total / 2U && summary.timers > 2U) {
    add_signal(report, HealthSignalCode::connection_timer_pressure,
               HealthLevel::degraded, capped_weight(summary.timers, 2U, 18U),
               "connection", count_detail("timers", summary.timers));
  } else {
    record_healthy(report);
  }

  finalize_report(report);
  return report;
}

HealthReport evaluate_route_policy_health(const RoutePolicyDocument& document,
                                          const HealthOptions& options) {
  HealthReport report;
  const RoutePolicyValidation validation = validate_route_policy(document);
  if (!validation.ok()) {
    for (const RoutePolicyDiagnostic& diagnostic : validation.diagnostics) {
      add_signal(report, HealthSignalCode::route_policy_invalid, HealthLevel::failed,
                 25U, "policy", diagnostic.detail);
    }
    finalize_report(report);
    return report;
  }
  record_healthy(report);

  if (document.routes.empty()) {
    add_signal(report, HealthSignalCode::route_policy_empty, HealthLevel::failed,
               45U, "policy", "no routes are configured");
    finalize_report(report);
    return report;
  }
  record_healthy(report);

  if (document.routes.size() < options.min_routes) {
    add_signal(report, HealthSignalCode::route_policy_sparse, HealthLevel::degraded,
               8U, "policy", count_detail("routes", document.routes.size()));
  } else {
    record_healthy(report);
  }

  std::map<std::uint32_t, std::size_t> families;
  std::size_t empty_payloads = 0;
  std::size_t loopbacks = 0;
  for (std::size_t i = 0; i < document.routes.size(); ++i) {
    const RoutePolicyEntry& route = document.routes[i];
    ++families[route.family_id];
    if (route.payload.empty()) {
      ++empty_payloads;
    }
    if (route_has_same_endpoint(route)) {
      ++loopbacks;
      add_signal(report, HealthSignalCode::route_policy_loopback,
                 HealthLevel::failed, 20U, route_subject(route, i),
                 "source and destination are the same channel");
    }
  }
  if (loopbacks == 0U) {
    record_healthy(report);
  }

  if (options.require_route_payload && empty_payloads != 0U) {
    add_signal(report, HealthSignalCode::route_policy_sparse, HealthLevel::degraded,
               capped_weight(empty_payloads, 3U, 15U), "policy",
               count_detail("empty_payloads", empty_payloads));
  } else {
    record_healthy(report);
  }

  const std::size_t duplicate_family_groups = static_cast<std::size_t>(std::count_if(
      families.begin(), families.end(),
      [](const auto& item) { return item.second > 1U; }));
  if (duplicate_family_groups != 0U) {
    add_signal(report, HealthSignalCode::route_policy_duplicate_family,
               HealthLevel::degraded, capped_weight(duplicate_family_groups, 2U, 10U),
               "policy", count_detail("shared_families", duplicate_family_groups));
  } else {
    record_healthy(report);
  }

  finalize_report(report);
  return report;
}

HealthReport merge_health_reports(const std::vector<HealthReport>& reports,
                                  const HealthOptions&) {
  HealthReport merged;
  if (reports.empty()) {
    add_signal(merged, HealthSignalCode::aggregate_failure, HealthLevel::failed,
               50U, "aggregate", "no reports were provided");
    finalize_report(merged);
    return merged;
  }

  std::uint64_t score_total = 0;
  for (const HealthReport& report : reports) {
    merged.level = combine_health_level(merged.level, report.level);
    merged.healthy_checks += report.healthy_checks;
    merged.degraded_checks += report.degraded_checks;
    merged.failed_checks += report.failed_checks;
    score_total += report.score;
    merged.signals.insert(merged.signals.end(), report.signals.begin(),
                          report.signals.end());
  }
  merged.score = static_cast<std::uint32_t>(score_total / reports.size());
  if (merged.failed_checks != 0U) {
    add_signal(merged, HealthSignalCode::aggregate_failure, HealthLevel::failed,
               10U, "aggregate", "one or more component reports failed");
  } else if (merged.degraded_checks != 0U) {
    add_signal(merged, HealthSignalCode::aggregate_failure, HealthLevel::degraded,
               4U, "aggregate", "one or more component reports degraded");
  } else {
    record_healthy(merged);
  }
  finalize_report(merged);
  return merged;
}

std::vector<HealthSignal> filter_health_signals(const HealthReport& report,
                                                HealthLevel minimum_level) {
  std::vector<HealthSignal> filtered;
  filtered.reserve(report.signals.size());
  for (const HealthSignal& signal : report.signals) {
    if (health_level_at_least(signal.level, minimum_level)) {
      filtered.push_back(signal);
    }
  }
  return filtered;
}

std::vector<HealthSignalBucket> summarize_health_signals(const HealthReport& report) {
  std::map<HealthSignalCode, HealthSignalBucket> by_code;
  for (const HealthSignal& signal : report.signals) {
    HealthSignalBucket& bucket = by_code[signal.code];
    bucket.code = signal.code;
    bucket.strongest_level = combine_health_level(bucket.strongest_level, signal.level);
    ++bucket.count;
    bucket.total_weight += signal.weight;
  }
  std::vector<HealthSignalBucket> buckets;
  buckets.reserve(by_code.size());
  for (const auto& [code, bucket] : by_code) {
    (void)code;
    buckets.push_back(bucket);
  }
  std::sort(buckets.begin(), buckets.end(),
            [](const HealthSignalBucket& lhs, const HealthSignalBucket& rhs) {
              if (lhs.strongest_level != rhs.strongest_level) {
                return level_rank(lhs.strongest_level) > level_rank(rhs.strongest_level);
              }
              if (lhs.total_weight != rhs.total_weight) {
                return lhs.total_weight > rhs.total_weight;
              }
              return static_cast<std::uint8_t>(lhs.code) <
                     static_cast<std::uint8_t>(rhs.code);
            });
  return buckets;
}

std::string format_health_report(const HealthReport& report) {
  std::ostringstream out;
  out << "health=" << to_string(report.level)
      << " score=" << report.score
      << " healthy=" << report.healthy_checks
      << " degraded=" << report.degraded_checks
      << " failed=" << report.failed_checks << '\n';
  for (const HealthSignal& signal : report.signals) {
    out << to_string(signal.level)
        << " code=" << to_string(signal.code)
        << " weight=" << signal.weight
        << " subject=" << signal.subject
        << " detail=\"" << signal.detail << "\"\n";
  }
  return out.str();
}

std::string format_health_signal_summary(const std::vector<HealthSignalBucket>& buckets) {
  std::ostringstream out;
  out << "signal_buckets=" << buckets.size() << '\n';
  for (const HealthSignalBucket& bucket : buckets) {
    out << "code=" << to_string(bucket.code)
        << " strongest=" << to_string(bucket.strongest_level)
        << " count=" << bucket.count
        << " weight=" << bucket.total_weight << '\n';
  }
  return out.str();
}

}  // namespace lattice
