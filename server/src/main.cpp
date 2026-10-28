// Vendored headers, exposed via srs_deps (server/third_party). Quoted rather
// than angled so they resolve through the include path, not the system.
#include "httplib.h"
#include "nlohmann/json.hpp"

#include <pqxx/pqxx>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <regex>
#include <string>

using json = nlohmann::json;

static std::string env_or(const char* key, const std::string& fallback) {
    const char* value = std::getenv(key);
    return value && *value ? value : fallback;
}

// Prefer the standard PORT variable used by PaaS/container runtimes.
// Keep SRS_PORT as a backward-compatible fallback for local development.
static int PortFromEnv() {
    const char* raw = std::getenv("PORT");
    if (raw == nullptr || *raw == '\0') {
        raw = std::getenv("SRS_PORT");
    }
    if (raw == nullptr || *raw == '\0') {
        return 8080;
    }

    try {
        const int port = std::stoi(raw);
        if (port <= 0 || port > 65535) {
            std::cerr << "PORT out of range, using 8080\n";
            return 8080;
        }
        return port;
    } catch (const std::exception&) {
        std::cerr << "PORT is not a number, using 8080\n";
        return 8080;
    }
}

static json error_json(const std::string& message) {
    return json{{"error", message}};
}

struct Auth {
    bool authenticated{false};
    bool is_admin{false};
    std::string role;
};

static Auth authenticate(const httplib::Request& req) {
    if (!req.has_header("Authorization")) return {};
    const std::string header = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (header.rfind(prefix, 0) != 0) return {};
    const std::string token = header.substr(prefix.size());

    if (token == env_or("ADMIN_TOKEN", "local-admin-token-change-me"))
        return {true, true, "Administrator"};
    if (token == env_or("TEACHER_TOKEN", "local-teacher-token-change-me"))
        return {true, false, "Teacher"};
    if (token == env_or("STUDENT_TOKEN", "local-student-token-change-me"))
        return {true, false, "Student"};
    return {};
}

static bool require_auth(const httplib::Request& req, httplib::Response& res, Auth& auth) {
    auth = authenticate(req);
    if (!auth.authenticated) {
        res.status = 401;
        res.set_content(error_json("Missing, invalid, or expired bearer token").dump(), "application/json");
        return false;
    }
    return true;
}

static bool require_admin(const httplib::Request& req, httplib::Response& res, Auth& auth) {
    if (!require_auth(req, res, auth)) return false;
    if (!auth.is_admin) {
        res.status = 403;
        res.set_content(error_json("Administrator role required").dump(), "application/json");
        return false;
    }
    return true;
}

static std::optional<long long> parse_id(const httplib::Request& req) {
    try {
        const auto raw = req.matches[1].str();
        if (raw.empty() || raw.size() > 18) return std::nullopt;
        std::size_t pos = 0;
        long long id = std::stoll(raw, &pos);
        if (pos != raw.size() || id <= 0) return std::nullopt;
        return id;
    } catch (...) {
        return std::nullopt;
    }
}

static json student_json(const pqxx::row& row) {
    json j;
    j["id"] = row["id"].as<long long>();
    j["roll_no"] = row["roll_no"].as<std::string>();
    j["name"] = row["name"].as<std::string>();
    j["date_of_birth"] = row["date_of_birth"].is_null() ? "" : row["date_of_birth"].as<std::string>();
    j["department"] = row["department"].as<std::string>();
    j["semester"] = row["semester"].as<int>();
    j["contact_details"] = row["contact_details"].as<std::string>();
    return j;
}

static bool has_nonempty_string(const json& body, const char* key) {
    return body.contains(key) && body[key].is_string() && !body[key].get<std::string>().empty()
        && body[key].get<std::string>().find_first_not_of(" \t\r\n") != std::string::npos;
}

int main() {
    const std::string db_url = env_or("DATABASE_URL",
        "postgresql://srs_user:change-this-local-password@localhost:5432/student_record_system");
    const std::string bind = env_or("SRS_BIND", "127.0.0.1");
    const int port = PortFromEnv();

    httplib::Server server;
    server.set_payload_max_length(1024 * 1024);

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"status", "ok"}}.dump(), "application/json");
    });

    server.Get("/api/students", [&](const httplib::Request& req, httplib::Response& res) {
        Auth auth;
        if (!require_auth(req, res, auth)) return;
        try {
            pqxx::connection conn(db_url);
            pqxx::read_transaction tx(conn);
            auto rows = tx.exec(
                "SELECT id, roll_no, name, date_of_birth::text AS date_of_birth, department, semester, contact_details "
                "FROM students ORDER BY roll_no");
            json output = json::array();
            for (const auto& row : rows) output.push_back(student_json(row));
            res.set_content(output.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "GET students failed: " << e.what() << '\n';
            res.status = 500;
            res.set_content(error_json("Internal server error").dump(), "application/json");
        }
    });

    server.Get(R"(/api/students/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        Auth auth;
        if (!require_auth(req, res, auth)) return;
        auto id = parse_id(req);
        if (!id) { res.status = 400; res.set_content(error_json("Invalid student id").dump(), "application/json"); return; }
        try {
            pqxx::connection conn(db_url);
            pqxx::read_transaction tx(conn);
            auto row = tx.exec_params(
                "SELECT id, roll_no, name, date_of_birth::text AS date_of_birth, department, semester, contact_details "
                "FROM students WHERE id = $1", *id);
            if (row.empty()) { res.status = 404; res.set_content(error_json("Student not found").dump(), "application/json"); return; }
            res.set_content(student_json(row[0]).dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "GET student failed: " << e.what() << '\n';
            res.status = 500; res.set_content(error_json("Internal server error").dump(), "application/json");
        }
    });

    server.Put(R"(/api/students/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        Auth auth;
        if (!require_admin(req, res, auth)) return;
        auto id = parse_id(req);
        if (!id) { res.status = 400; res.set_content(error_json("Invalid student id").dump(), "application/json"); return; }

        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400; res.set_content(error_json("Request body must be valid JSON").dump(), "application/json"); return; }

        if (body.contains("roll_no")) {
            res.status = 400;
            res.set_content(error_json("roll_no is immutable and must not be included in an update").dump(), "application/json");
            return;
        }
        const char* required[] = {"name", "contact_details", "department", "semester"};
        for (const char* key : required) {
            if (!body.contains(key)) {
                res.status = 400;
                res.set_content(error_json(std::string("Missing required field: ") + key).dump(), "application/json");
                return;
            }
        }
        if (!has_nonempty_string(body, "name") || !has_nonempty_string(body, "contact_details") ||
            !has_nonempty_string(body, "department") || !body["semester"].is_number_integer()) {
            res.status = 400;
            res.set_content(error_json("name, contact_details and department must be non-empty strings; semester must be an integer").dump(), "application/json");
            return;
        }
        const int semester = body["semester"].get<int>();
        if (semester < 1 || semester > 12) {
            res.status = 400; res.set_content(error_json("semester must be between 1 and 12").dump(), "application/json"); return;
        }
        const std::string name = body["name"].get<std::string>();
        const std::string contact = body["contact_details"].get<std::string>();
        const std::string department = body["department"].get<std::string>();

        try {
            pqxx::connection conn(db_url);
            pqxx::work tx(conn);
            auto rows = tx.exec_params(
                "UPDATE students SET name=$1, contact_details=$2, department=$3, semester=$4, updated_at=now() "
                "WHERE id=$5 RETURNING id, roll_no, name, date_of_birth::text AS date_of_birth, department, semester, contact_details",
                name, contact, department, semester, *id);
            if (rows.empty()) {
                tx.abort();
                res.status = 404; res.set_content(error_json("Student not found").dump(), "application/json"); return;
            }
            json result = student_json(rows[0]);
            tx.commit();
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "PUT student failed: " << e.what() << '\n';
            res.status = 500; res.set_content(error_json("Internal server error").dump(), "application/json");
        }
    });

    server.Delete(R"(/api/students/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        Auth auth;
        if (!require_admin(req, res, auth)) return;
        auto id = parse_id(req);
        if (!id) { res.status = 400; res.set_content(error_json("Invalid student id").dump(), "application/json"); return; }
        try {
            pqxx::connection conn(db_url);
            pqxx::work tx(conn);
            auto rows = tx.exec_params("DELETE FROM students WHERE id=$1 RETURNING roll_no", *id);
            if (rows.empty()) {
                tx.abort();
                res.status = 404; res.set_content(error_json("Student not found").dump(), "application/json"); return;
            }
            const std::string roll_no = rows[0]["roll_no"].as<std::string>();
            tx.commit();
            res.set_content(json{{"message", "Student deleted; dependent grades and attendance were cascaded"},
                                  {"roll_no", roll_no}}.dump(), "application/json");
        } catch (const std::exception& e) {
            std::cerr << "DELETE student failed: " << e.what() << '\n';
            res.status = 500; res.set_content(error_json("Internal server error").dump(), "application/json");
        }
    });

    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            res.set_content(error_json("Route not found").dump(), "application/json");
        }
    });

    std::cout << "srs_server listening on port " << port << '\n' << std::flush;
    std::cout << "SRS API listening on http://" << bind << ':' << port << '\n';
    std::cout << "Demo bearer tokens are configured through ADMIN_TOKEN / TEACHER_TOKEN / STUDENT_TOKEN.\n";
    if (!server.listen(bind.c_str(), port)) {
        std::cerr << "Could not start server. Check port and configuration.\n";
        return 1;
    }
    return 0;
}
