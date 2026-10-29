#include "session/Session.h"

Role roleFromString(const QString& value) {
    if (value == QLatin1String("admin")) return Role::Admin;
    if (value == QLatin1String("teacher")) return Role::Teacher;
    return Role::Student;
}
