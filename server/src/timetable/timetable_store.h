#pragma once

#include <optional>
#include <string>
#include <vector>

namespace srs {

// One weekly slot as GET /api/timetable returns it.
struct TimetableSlot {
    long long id = 0;
    int day_of_week = 0;         // ISO: 1 = Monday … 7 = Sunday
    std::string start_time;      // "HH:MM"
    std::string end_time;        // "HH:MM"
    std::string course_code;
    std::string course_name;
    std::string teacher_username;
    std::string teacher_name;
    std::string department;
    int semester = 0;
    std::string room;
};

// What one timetable read is scoped to. Built by the route from the
// server-resolved caller; every non-empty field is an AND filter.
struct TimetableQuery {
    std::string teacher_username;  // '' = any teacher
    std::string department;        // '' = any department
    int semester = 0;              // 0  = any semester
};

// BE lane owns this interface; DB lane owns the scoped query behind it.
class TimetableStore {
public:
    virtual ~TimetableStore() = default;

    // Slots matching `query`, ordered by weekday then start time. nullopt only
    // on a storage failure.
    virtual std::optional<std::vector<TimetableSlot>> List(const TimetableQuery& query) = 0;
};

TimetableStore& DefaultTimetableStore();

}  // namespace srs
