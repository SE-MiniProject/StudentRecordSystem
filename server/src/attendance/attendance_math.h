#pragma once

#include <optional>

namespace srs::attendance {

// What is wrong, if anything, with a (present, conducted) pair.
enum class TallyCheck {
    kOk,
    kNegative,                  // either count below zero
    kPresentExceedsConducted,   // AC-018: present > conducted is rejected
};

// Validates one tally before any percentage is computed from it. A pair that
// fails is rejected outright — never clamped to 100% — because a percentage over
// 100 can only come from bad data, and showing it would hide the bug.
TallyCheck CheckTally(long long present, long long conducted);

// present / conducted * 100, rounded half-up to two decimals (SRS-F-018).
//
// Computed in integer arithmetic, so 101/120 is exactly 84.17 and 2/3 is
// exactly 66.67 rather than whatever a double happens to round to.
//
// nullopt when the tally fails CheckTally, or when conducted is 0: no sessions
// held is "no percentage", not 0% (which would flag a student at risk for a
// course that has not started) and not a division by zero.
std::optional<double> Percentage(long long present, long long conducted);

}  // namespace srs::attendance
