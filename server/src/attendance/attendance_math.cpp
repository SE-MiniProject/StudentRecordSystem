#include "attendance/attendance_math.h"

namespace srs::attendance {

TallyCheck CheckTally(long long present, long long conducted) {
    if (present < 0 || conducted < 0) return TallyCheck::kNegative;
    if (present > conducted) return TallyCheck::kPresentExceedsConducted;
    return TallyCheck::kOk;
}

std::optional<double> Percentage(long long present, long long conducted) {
    if (CheckTally(present, conducted) != TallyCheck::kOk) return std::nullopt;
    if (conducted == 0) return std::nullopt;

    // Hundredths of a percent, rounded half-up: (present * 10000) / conducted,
    // plus one half. Doubling numerator and denominator keeps it all integer:
    //   round(p * 10000 / c) == (2 * p * 10000 + c) / (2 * c)
    const long long hundredths = (2 * present * 10000 + conducted) / (2 * conducted);
    return static_cast<double>(hundredths) / 100.0;
}

}  // namespace srs::attendance
