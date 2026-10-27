#pragma once

#include <string>

#include "httplib.h"

namespace srs {

// Builds the one JSON error envelope every non-2xx response uses, so a client
// never has to parse two formats:
//
//   { "error": "<code>", "message": "<human text>", "requestId": "<echoed>" }
//
// plus an optional "fields" map for 400s, where the client needs to know which
// field failed. `request_id` is echoed verbatim from the X-Request-Id header; an
// absent header yields an empty string, which is the contract's behaviour.
//
// This lives in one place because the shape is shared infrastructure: the global
// error handler and every route that authors its own 4xx must produce an
// identical envelope, and hand-copying the keys is how the two drift apart.
std::string ErrorEnvelope(const std::string& code, const std::string& message,
                          const std::string& request_id,
                          const std::string& field = std::string(),
                          const std::string& field_problem = std::string());

// Fills `res` with `status` and the envelope. `field` is optional and, when set,
// adds { "fields": { "<field>": "<field_problem>" } }.
void SendError(httplib::Response& res, int status, const std::string& code,
               const std::string& message, const std::string& request_id,
               const std::string& field = std::string(),
               const std::string& field_problem = std::string());

}  // namespace srs
