#pragma once
#include <QString>

// The account a signed-in user is acting as. Parsed once at the login boundary so
// screens compare a Role rather than re-testing raw strings.
enum class Role { Admin, Teacher, Student };

// Returns Student for anything unrecognised; the server remains the authority on
// access, so an unknown role must not be treated as more privileged than the least.
Role roleFromString(const QString& value);

struct Session {
    QString username;
    QString fullName;
    Role role = Role::Student;
    QString token;

    bool isValid() const { return !token.isEmpty() && !username.isEmpty(); }
};
