# Branch Coverage Report

**Execution date:** 2026-11-15  
**Scope:** C++ source under `server/src`, measured with GCC coverage data and gcovr 8.8 after the 203/203 CTest run.

**Raw branch coverage:** 2,753 / 5,412 (**50.9%**).

For context, gcovr reports **2,401 / 3,230 (74.3%)** when excluding branches it classifies as unreachable and exception branches. This filtered number is supplementary; the raw result remains the primary metric. The per-file table uses gcovr's integer percentages.

Reproduce raw coverage with: `gcovr --root . --filter 'server/src/' --txt --txt-metric branch build`.
The supplementary filtered figure uses `--exclude-unreachable-branches --exclude-throw-branches`.

## Raw branch report

```text
------------------------------------------------------------------------------
                           GCC Code Coverage Report
Directory: .
------------------------------------------------------------------------------
File                                      Branches    Taken  Cover   Missing
------------------------------------------------------------------------------
server/src/accounts/account_routes.cpp         506      234    46%   25,26,29,35,36,38,42,43,44,45,51,54,57,59,61,62,65,66,68,69,70,73,76,81,82,83,87,88,89,90,93,95,99,102,103,104,107,108,111,112,115,118,121,123,128,129,130,135,136,137,138,139,146,147,148,152,154,158,163,164,167,168,174,178,179,182,183,186
server/src/accounts/account_store.h              0        0    --%
server/src/accounts/pg_account_store.cpp
                                               125       73    58%   31,32,33,34,45,50,52,53,55,66,67,68,69,73,77,78,80,82,93,94,95,96,97,98,99,103,118,120,122,130,131,134,136,145,148,150
server/src/api/error_response.cpp               88       33    37%   17,19,21,22,28,29
server/src/api/router.cpp                      116       39    33%   39,40,43,44,65,66,69,70,73,110,121,122,126,127,128,132,134,135
server/src/attendance/attendance_math.cpp
                                                10       10   100%
server/src/attendance/attendance_routes.cpp
                                               612      303    49%   24,34,40,41,42,43,44,45,46,54,55,56,57,58,59,62,63,66,67,68,71,72,73,76,79,82,85,88,94,96,99,102,103,105,108,109,110,113,116,117,119,122,123,125,126,127,128,131,132,135,138,139,141,144,145,146,147,149,151,154,158,159,164,166,169,171,172,173
server/src/attendance/attendance_store.h
                                                 0        0    --%
server/src/attendance/pg_attendance_store.cpp
                                                94       57    60%   49,53,54,62,69,72,74,76,84,86,88,91,92,93,94,95,96,99,124,127,129
server/src/auth/bearer_token.cpp                18       13    72%   18,34,39,42
server/src/auth/default_session_store.cpp
                                                26        6    23%   38,39,42,44
server/src/auth/default_user_store.cpp          28        7    25%   35,47,48,51,53
server/src/auth/login_route.cpp                220       99    45%   37,40,44,45,49,52,59,63,68,69,72,79,84,85,91,92,94,102,104,108,109,113,116,126,127,128,129
server/src/auth/logout_route.cpp                30       18    60%   14,18,24,26,28,37,38
server/src/auth/password_hash.cpp               72       50    69%   24,26,27,45,52,64,67,71,78,79,83,86,107,110,118,121
server/src/auth/pg_session_store.cpp            46       26    56%   24,27,28,34,50,51,57,58,60,68,71,72,78,87,88,90
server/src/auth/pg_user_store.cpp              162       97    59%   26,30,36,37,38,39,40,41,42,44,66,67,69,70,77,80,89,91,93,96,98,99,114,115,116,120,132,138,142,143,144,156,157,158,163,164,165,169,175,179,180,182,183,184
server/src/auth/rbac.cpp                        62       44    71%   31,47,52,66,74,79,84,97,103,117,118,122,125
server/src/auth/session_store.h                  0        0    --%
server/src/auth/user_store.h                     0        0    --%
server/src/grades/grade_routes.cpp             514      246    47%   30,31,34,51,55,58,63,64,65,69,70,72,73,74,75,79,80,81,82,85,86,87,88,89,92,96,97,98,99,100,104,105,108,109,110,111,115,116,117,118,121,122,125,126,129,130,133,135,137,140,145,146,147,151,153,154,158,159,164,165,166
server/src/grades/grade_store.h                  0        0    --%
server/src/grades/pg_grade_store.cpp            96       55    57%   65,67,69,72,73,74,75,76,77,80,88,91,92,93,94,97,98,101,102,104,127,130,132
server/src/profile/pg_profile_store.cpp         52       29    55%   18,24,27,28,29,30,32,52,55,57
server/src/profile/profile_routes.cpp          108       49    45%   14,16,18,24,26,27,34,38,39,41,42
server/src/profile/profile_store.h               0        0    --%
server/src/records/record_access.cpp            82       46    56%   18,20,28,29,30,34,35,36,48,50,55,58,59,60
server/src/students/pg_student_store.cpp
                                               223      124    55%   21,22,23,24,25,26,27,28,62,70,73,74,75,78,79,82,84,85,86,95,98,99,100,102,103,104,105,107,115,116,117,118,120,121,129,131,133,134,144,150,152,156,160,161,163,174,189,191,194,196,204,209,211,214,216,224,225,228,230
server/src/students/student_routes.cpp         668      318    47%   34,35,54,56,62,63,65,66,72,74,75,88,91,94,100,101,102,103,108,109,113,114,118,120,121,125,126,128,131,132,133,134,137,139,145,148,149,154,156,157,161,165,168,175,176,177,182,183,184,185,186,187,189,191,196,197,198,199,200,203,206,211,212,215,216,219,222,225,228,233,234,235,242,243,244,245,247,249,254,255,256,261,263,265,266,270,271,274,275,278,281,284,287,291,292
server/src/students/student_store.h              0        0    --%
server/src/timetable/pg_timetable_store.cpp
                                               171       89    52%   43,45,47,50,51,52,53,54,55,56,57,58,59,60,61,64,72,76,84,86,97,99,110,111,118,120,121,122,123,124,125,126,127,128,129,130,131,133,134,135,162,165,167
server/src/timetable/timetable_routes.cpp
                                               791      385    48%   35,36,40,42,43,46,47,52,53,54,55,58,59,62,66,70,71,77,83,85,86,87,96,99,100,101,107,108,109,117,120,123,127,128,129,130,135,136,137,138,139,140,141,142,143,144,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,165,170,173,176,177,178,181,182,183,186,187,190,193,196,201,204,205,208,210,211,220,223,224,229,231,236,239,242,248,252,255,256,261,265,266,270,272,273,278,280
server/src/timetable/timetable_store.h           0        0    --%
server/src/validation/validation.cpp           306      196    64%   14,30,41,47,57,66,76,79,80,89,95,96,121,126,133,140,148,153,166,175,176,182,183,187,188,189,192,194,197,208,209,210,211,242,245,246,247,254,255,262,265,267,268,279,281,282,283,288,289
server/src/validation/validation.h              10        4    40%   46
server/src/web/static_files.cpp                176      103    58%   38,39,41,42,49,52,54,55,57,58,69,77,82,88,95,96,98,103,107,109,110,128,131,132,134,146,147,156,157,158
------------------------------------------------------------------------------
TOTAL                                         5412     2753    50%
------------------------------------------------------------------------------
```

## Excluding unreachable and exception branches

```text
------------------------------------------------------------------------------
                           GCC Code Coverage Report
Directory: .
------------------------------------------------------------------------------
File                                      Branches    Taken  Cover   Missing
------------------------------------------------------------------------------
server/src/accounts/account_routes.cpp         249      184    73%   25,29,42,66,68,70,81,99,103,115,128,158,164,167,186
server/src/accounts/account_store.h              0        0    --%
server/src/accounts/pg_account_store.cpp
                                                89       67    75%   31,32,33,34,55,67,69,80,94,96,118,130,136,145,148,150
server/src/api/error_response.cpp               15       11    73%   17,19
server/src/api/router.cpp                       44       23    52%   39,121,122,126,127,128,132,134,135
server/src/attendance/attendance_math.cpp
                                                10       10   100%
server/src/attendance/attendance_routes.cpp
                                               394      277    70%   40,41,42,55,56,68,72,119,126,127,146,166,171,173
server/src/attendance/attendance_store.h
                                                 0        0    --%
server/src/attendance/pg_attendance_store.cpp
                                                69       53    76%   76,91,92,93,94,95,99,124,127,129
server/src/auth/bearer_token.cpp                14       13    92%   18
server/src/auth/default_session_store.cpp
                                                23        6    26%   38,39,42,44
server/src/auth/default_user_store.cpp          25        7    28%   35,47,48,51,53
server/src/auth/login_route.cpp                139       89    64%   37,40,49,68,129
server/src/auth/logout_route.cpp                18       18   100%
server/src/auth/password_hash.cpp               54       50    92%   64,83,107,118
server/src/auth/pg_session_store.cpp            32       24    75%   24,28,57,58,60,68,72,90
server/src/auth/pg_user_store.cpp              114       89    78%   36,37,38,39,40,41,42,44,116,120,142,143,144,157,158,164,165,179,183,184
server/src/auth/rbac.cpp                        47       44    93%   31,79,118
server/src/auth/session_store.h                  0        0    --%
server/src/auth/user_store.h                     0        0    --%
server/src/grades/grade_routes.cpp             319      221    69%   30,34,63,85,96,104,105,109,116,133,164,166
server/src/grades/grade_store.h                  0        0    --%
server/src/grades/pg_grade_store.cpp            73       53    72%   72,73,74,75,76,80,92,93,97,101,104,127,130,132
server/src/profile/pg_profile_store.cpp         42       28    66%   27,28,29,30,32,52,55,57
server/src/profile/profile_routes.cpp           79       49    62%   39,42
server/src/profile/profile_store.h               0        0    --%
server/src/records/record_access.cpp            55       45    81%   28,29,30,55,58
server/src/students/pg_student_store.cpp
                                               145      117    80%   21,22,23,25,26,27,28,107,121,133,134,163,196,216,224,225,228,230
server/src/students/student_routes.cpp         331      257    77%   34,63,75,100,131,134,175,203,215,216,219,233,261,265,274,275,278
server/src/students/student_store.h              0        0    --%
server/src/timetable/pg_timetable_store.cpp
                                               126       87    69%   50,51,52,53,54,55,56,57,58,59,60,64,111,120,121,122,123,124,125,126,127,128,129,130,133,134,135,162,165,167
server/src/timetable/timetable_routes.cpp
                                               354      284    80%   35,52,58,66,70,71,77,83,85,86,87,99,108,128,129,130,141,143,147,149,151,153,155,157,161,162,163,170,186,187,190,208,236,265,266
server/src/timetable/timetable_store.h           0        0    --%
server/src/validation/validation.cpp           244      196    80%   14,30,41,47,57,66,76,79,80,89,95,96,175,176,182,242,245,246,247,267,281,282,288
server/src/validation/validation.h               0        0    --%
server/src/web/static_files.cpp                126       99    78%   39,42,49,52,55,58,95,103,132,134,157,158
------------------------------------------------------------------------------
TOTAL                                         3230     2401    74%
------------------------------------------------------------------------------
```
