#include "api/error_response.h"

#include <string>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace srs {

std::string ErrorEnvelope(const std::string& code, const std::string& message,
                          const std::string& request_id, const std::string& field,
                          const std::string& field_problem) {
    nlohmann::json body{
        {"error", code},
        {"message", message},
        {"requestId", request_id},
    };
    if (!field.empty()) {
        body["fields"] = {{field, field_problem}};
    }
    return body.dump();
}

void SendError(httplib::Response& res, int status, const std::string& code,
               const std::string& message, const std::string& request_id,
               const std::string& field, const std::string& field_problem) {
    res.status = status;
    res.set_content(ErrorEnvelope(code, message, request_id, field, field_problem),
                    "application/json");
}

}  // namespace srs
