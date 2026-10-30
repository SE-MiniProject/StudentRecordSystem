#include "students/student_routes.h"

#include <optional>
#include <string>
#include <vector>

#include "api/error_response.h"
#include "auth/rbac.h"
#include "auth/session_store.h"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "students/student_store.h"
#include "validation/validation.h"

namespace srs {
namespace {

using json = nlohmann::json;

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 200;

// The wire contract spells fields camelCase (api.md "Students"); the validation
// layer and the database use snake_case. Translation happens here, at the
// boundary, so neither side has to know about the other's spelling.
json StudentToJson(const Student& student) {
    return json{
        {"rollNo", student.roll_no},
        {"fullName", student.full_name},
        {"dateOfBirth", student.date_of_birth},
        {"department", student.department},
        {"semester", student.semester},
        {"contact", student.contact},
    };
}

// Reads a string field, treating a wrong JSON type as absent so validation
// reports it rather than the JSON library throwing.
std::string StringField(const json& body, const char* key) {
    if (!body.contains(key) || !body[key].is_string()) return std::string();
    return body[key].get<std::string>();
}

int IntField(const json& body, const char* key, int fallback) {
    if (!body.contains(key) || !body[key].is_number_integer()) return fallback;
    return body[key].get<int>();
}

// A 400 carrying every failed field, not just the first. The shared SendError
// takes one field, which would make the server report problems one at a time
// and force the client to submit repeatedly to discover them all.
void SendFieldErrors(httplib::Response& res, const validation::FieldErrors& errors,
                     const httplib::Request& req) {
    json fields = json::object();
    for (const auto& error : errors.errors) {
        fields[error.field] = error.problem;
    }
    json body{
        {"error", {{"code", "validation_failed"},
                   {"message", "one or more fields are invalid"},
                   {"fields", fields},
                   {"requestId", req.get_header_value("X-Request-Id")}}},
    };
    res.status = 400;
    res.set_content(body.dump(), "application/json");
}

// The query string is parsed by hand rather than through a helper because every
// value needs the same "absent or unparseable means default" treatment, and a
// malformed limit must not silently become a different page size.
int IntParam(const httplib::Request& req, const char* name, int fallback) {
    if (!req.has_param(name)) return fallback;
    try {
        return std::stoi(req.get_param_value(name));
    } catch (const std::exception&) {
        return fallback;
    }
}

// Roll numbers appear in a URL path, so a request for one is a lookup, not a
// validation failure: an unknown value is 404, handled by the store.
std::string PathRollNo(const httplib::Request& req) { return req.matches[1].str(); }

}  // namespace

void RegisterStudentRoutes(httplib::Server& server, StudentStore& students,
                           SessionStore& sessions) {
    const RbacGuard guard(sessions);

    // GET /api/students — Admin and Teacher, paginated (api.md line 88).
    server.Get("/api/students", [&students, guard](const httplib::Request& req,
                                                   httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin, Role::kTeacher}, &who), req,
                         res)) {
            return;
        }

        StudentQuery query;
        query.search = req.has_param("search") ? req.get_param_value("search") : std::string();
        query.semester = IntParam(req, "semester", 0);
        query.limit = IntParam(req, "limit", kDefaultLimit);
        query.offset = IntParam(req, "offset", 0);

        // The contract is explicit that limit above 200 is a client error, not a
        // silent clamp: a client asking for 1000 has a bug worth reporting.
        if (query.limit <= 0 || query.limit > kMaxLimit) {
            SendError(res, 400, "invalid_limit", "limit must be between 1 and 200",
                      req.get_header_value("X-Request-Id"));
            return;
        }
        if (query.offset < 0) {
            SendError(res, 400, "invalid_offset", "offset must not be negative",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        const auto page = students.List(query);
        if (!page.has_value()) {
            SendError(res, 500, "internal_error", "could not read students",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        json items = json::array();
        for (const auto& student : page->items) items.push_back(StudentToJson(student));
        res.status = 200;
        res.set_content(json{{"items", items},
                             {"total", page->total},
                             {"limit", page->limit},
                             {"offset", page->offset}}
                            .dump(),
                        "application/json");
    });

    // GET /api/students/{rollNo} — Admin, Teacher, or the student themselves.
    server.Get(R"(/api/students/([^/]+))", [&students, guard](const httplib::Request& req,
                                                             httplib::Response& res) {
        const std::string roll_no = PathRollNo(req);

        // Ownership is resolved before the guard runs because the guard takes the
        // owner's username as an input. An unknown roll number has no owner, so
        // only the role check can pass, and the 404 below is then reached by an
        // Admin or Teacher rather than by a student fishing for existence.
        const std::string owner = students.OwnerOf(roll_no);

        Principal who;
        if (!GuardAllows(
                guard.RequireRoleOrOwnership(req, {Role::kAdmin, Role::kTeacher}, owner, &who),
                req, res)) {
            return;
        }

        const auto student = students.Find(roll_no);
        if (!student.has_value()) {
            SendError(res, 404, "not_found", "no student with that roll number",
                      req.get_header_value("X-Request-Id"));
            return;
        }
        res.status = 200;
        res.set_content(StudentToJson(*student).dump(), "application/json");
    });

    // POST /api/students — Admin only (SRS-20).
    server.Post("/api/students", [&students, guard](const httplib::Request& req,
                                                    httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) {
            return;
        }

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            SendError(res, 400, "invalid_json", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        validation::StudentFields fields;
        fields.roll_no = StringField(body, "rollNo");
        fields.full_name = StringField(body, "fullName");
        fields.date_of_birth = StringField(body, "dateOfBirth");
        fields.department = StringField(body, "department");
        fields.contact = StringField(body, "contact");
        fields.semester = IntField(body, "semester", validation::StudentFields::kAbsentSemester);

        const auto errors = validation::ValidateStudentCreate(fields);
        if (!errors.ok()) {
            SendFieldErrors(res, errors, req);
            return;
        }

        Student student;
        student.roll_no = fields.roll_no;
        student.full_name = fields.full_name;
        student.date_of_birth = fields.date_of_birth;
        student.department = fields.department;
        student.contact = fields.contact;
        student.semester = fields.semester;

        switch (students.Create(student)) {
            case CreateStudentResult::kOk:
                res.status = 201;
                res.set_content(StudentToJson(student).dump(), "application/json");
                return;
            case CreateStudentResult::kDuplicateRollNo:
                // 409, not 400: the request is well-formed and the client's only
                // possible fix is a different roll number.
                SendError(res, 409, "duplicate_roll_no", "a student with that roll number exists",
                          req.get_header_value("X-Request-Id"));
                return;
            case CreateStudentResult::kError:
                SendError(res, 500, "internal_error", "could not create student",
                          req.get_header_value("X-Request-Id"));
                return;
        }
    });

    // PUT /api/students/{rollNo} — Admin only (SRS-25 / SRS-17).
    server.Put(R"(/api/students/([^/]+))", [&students, guard](const httplib::Request& req,
                                                             httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) {
            return;
        }
        const std::string roll_no = PathRollNo(req);

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            SendError(res, 400, "invalid_json", "request body must be valid JSON",
                      req.get_header_value("X-Request-Id"));
            return;
        }

        // The contract says a roll number in the body is ignored, not rejected:
        // a client that echoes the object it read can still update it.
        validation::StudentFields fields;
        fields.full_name = StringField(body, "fullName");
        fields.department = StringField(body, "department");
        fields.contact = StringField(body, "contact");
        fields.semester = IntField(body, "semester", validation::StudentFields::kAbsentSemester);

        const auto errors = validation::ValidateStudentUpdate(fields);
        if (!errors.ok()) {
            SendFieldErrors(res, errors, req);
            return;
        }

        Student changes;
        changes.full_name = fields.full_name;
        changes.department = fields.department;
        changes.contact = fields.contact;
        changes.semester = fields.semester == validation::StudentFields::kAbsentSemester
                               ? 0
                               : fields.semester;

        switch (students.Update(roll_no, changes)) {
            case UpdateStudentResult::kOk: {
                const auto stored = students.Find(roll_no);
                res.status = 200;
                res.set_content(stored.has_value() ? StudentToJson(*stored).dump() : "{}",
                                "application/json");
                return;
            }
            case UpdateStudentResult::kNotFound:
                SendError(res, 404, "not_found", "no student with that roll number",
                          req.get_header_value("X-Request-Id"));
                return;
            case UpdateStudentResult::kError:
                SendError(res, 500, "internal_error", "could not update student",
                          req.get_header_value("X-Request-Id"));
                return;
        }
    });

    // DELETE /api/students/{rollNo} — Admin only, cascading (SRS-26).
    server.Delete(R"(/api/students/([^/]+))", [&students, guard](const httplib::Request& req,
                                                                httplib::Response& res) {
        Principal who;
        if (!GuardAllows(guard.RequireRole(req, {Role::kAdmin}, &who), req, res)) {
            return;
        }
        if (students.Delete(PathRollNo(req))) {
            res.status = 204;  // no body, per the contract
            return;
        }
        SendError(res, 404, "not_found", "no student with that roll number",
                  req.get_header_value("X-Request-Id"));
    });
}

}  // namespace srs