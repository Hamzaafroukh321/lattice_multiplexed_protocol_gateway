#pragma once

#include "lattice/gateway.hpp"

#include <string>
#include <vector>

namespace lattice {

struct RoutePolicyEntry {
  std::string name;
  ChannelId source{1U, 1U};
  ChannelId destination{2U, 1U};
  std::uint32_t family_id{7U};
  Bytes payload;
};

struct RoutePolicyDocument {
  std::vector<RoutePolicyEntry> routes;
};

enum class RoutePolicyDiagnosticCode : std::uint8_t {
  missing_key,
  duplicate_key,
  malformed_line,
  invalid_integer,
  invalid_channel,
  invalid_hex,
  empty_route,
  duplicate_source,
  duplicate_name,
  unknown_key
};

struct RoutePolicyDiagnostic {
  RoutePolicyDiagnosticCode code{RoutePolicyDiagnosticCode::malformed_line};
  std::size_t line{0};
  std::string detail;
};

struct RoutePolicyValidation {
  std::vector<RoutePolicyDiagnostic> diagnostics;
  [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

[[nodiscard]] const char* to_string(RoutePolicyDiagnosticCode code);
[[nodiscard]] Result<RoutePolicyDocument> parse_route_policy_text(const std::string& text);
[[nodiscard]] Result<RoutePolicyDocument> parse_route_policy_file(const std::string& path);
[[nodiscard]] RoutePolicyValidation validate_route_policy(const RoutePolicyDocument& document);
[[nodiscard]] Result<std::string> serialize_route_policy(const RoutePolicyDocument& document);
[[nodiscard]] Result<GatewayRoute> apply_route_policy_entry(Gateway& gateway,
                                                            const RoutePolicyEntry& entry);
[[nodiscard]] Result<RoutePolicyEntry> find_route_policy_source(
    const RoutePolicyDocument& document, ChannelId source);

}  // namespace lattice
