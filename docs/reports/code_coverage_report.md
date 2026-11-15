# Code Coverage Report

**Execution date:** 2026-11-15  
**Scope:** C++ source under `server/src`, measured with GCC coverage data and gcovr 8.8 after the 203/203 CTest run.

**Line coverage:** 1,820 / 1,923 (**94.6%**). The per-file table uses gcovr's integer percentages.

Reproduce with: `gcovr --root . --filter 'server/src/' --txt --txt-metric line build`.
Configure the instrumented build with `cmake -S server -B build -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-Wno-error=deprecated-declarations --coverage' -DCMAKE_EXE_LINKER_FLAGS=--coverage`, then build and run CTest as recorded in [unit_test_report.md](unit_test_report.md).

```text
------------------------------------------------------------------------------
                           GCC Code Coverage Report
Directory: .
------------------------------------------------------------------------------
File                                         Lines     Exec  Cover   Missing
------------------------------------------------------------------------------
server/src/accounts/account_routes.cpp         120      120   100%
server/src/accounts/account_store.h              2        1    50%   47
server/src/accounts/pg_account_store.cpp
                                                83       81    97%   36,117
server/src/api/error_response.cpp               12       12   100%
server/src/api/router.cpp                       55       39    70%   115,119-123,126-129,132,134-136,138-139
server/src/attendance/attendance_math.cpp
                                                 9        9   100%
server/src/attendance/attendance_routes.cpp
                                               122      122   100%
server/src/attendance/attendance_store.h
                                                 2        1    50%   35
server/src/attendance/pg_attendance_store.cpp
                                                60       59    98%   61
server/src/auth/bearer_token.cpp                18       17    94%   19
server/src/auth/default_session_store.cpp
                                                14       12    85%   39-40
server/src/auth/default_user_store.cpp          18       16    88%   48-49
server/src/auth/login_route.cpp                 61       60    98%   41
server/src/auth/logout_route.cpp                18       18   100%
server/src/auth/password_hash.cpp               58       53    91%   30,65,84,108,119
server/src/auth/pg_session_store.cpp            40       36    90%   25,29,69,73
server/src/auth/pg_user_store.cpp               93       91    97%   158,165
server/src/auth/rbac.cpp                        70       66    94%   41,80-81,128
server/src/auth/session_store.h                  2        1    50%   27
server/src/auth/user_store.h                     2        1    50%   70
server/src/grades/grade_routes.cpp             109      108    99%   27
server/src/grades/grade_store.h                  2        1    50%   47
server/src/grades/pg_grade_store.cpp            53       53   100%
server/src/profile/pg_profile_store.cpp         28       28   100%
server/src/profile/profile_routes.cpp           22       22   100%
server/src/profile/profile_store.h               2        1    50%   26
server/src/records/record_access.cpp            34       31    91%   29-31
server/src/students/pg_student_store.cpp
                                               138      134    97%   30,165,225-226
server/src/students/student_routes.cpp         179      171    95%   214-217,273-276
server/src/students/student_store.h              2        1    50%   62
server/src/timetable/pg_timetable_store.cpp
                                                88       83    94%   96,108,134-136
server/src/timetable/timetable_routes.cpp
                                               188      176    93%   128-132,163,185-188,266-267
server/src/timetable/timetable_store.h           2        1    50%   42
server/src/validation/validation.cpp           141      123    87%   33-34,42,56-57,75-77,79-81,84,200,214,248-249,272,293
server/src/validation/validation.h               4        4   100%
server/src/web/static_files.cpp                 72       68    94%   135-136,158-159
------------------------------------------------------------------------------
TOTAL                                         1923     1820    94%
------------------------------------------------------------------------------
```
