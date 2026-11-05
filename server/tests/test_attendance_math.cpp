#include <gtest/gtest.h>

#include "attendance/attendance_math.h"

// SRS-F-018 / AC-018 / TC-018: percentage = present / conducted * 100, rounded
// to two decimals; present > conducted is rejected.

namespace {

using srs::attendance::CheckTally;
using srs::attendance::Percentage;
using srs::attendance::TallyCheck;

TEST(AttendanceMath, ExactValues) {
    EXPECT_DOUBLE_EQ(*Percentage(34, 40), 85.00);
    EXPECT_DOUBLE_EQ(*Percentage(40, 40), 100.00);
    EXPECT_DOUBLE_EQ(*Percentage(0, 40), 0.00);
    EXPECT_DOUBLE_EQ(*Percentage(3, 4), 75.00);
}

TEST(AttendanceMath, RoundsToTwoDecimalsHalfUp) {
    EXPECT_DOUBLE_EQ(*Percentage(101, 120), 84.17);  // api.md's own example
    EXPECT_DOUBLE_EQ(*Percentage(2, 3), 66.67);      // 66.666… rounds up
    EXPECT_DOUBLE_EQ(*Percentage(1, 3), 33.33);      // 33.333… rounds down
    EXPECT_DOUBLE_EQ(*Percentage(1, 8), 12.50);      // exact half-percent
    EXPECT_DOUBLE_EQ(*Percentage(1, 6), 16.67);      // 16.666…
    EXPECT_DOUBLE_EQ(*Percentage(1, 7), 14.29);      // 14.2857…
    EXPECT_DOUBLE_EQ(*Percentage(5, 9), 55.56);      // 55.555…
}

TEST(AttendanceMath, PresentExceedingConductedIsRejected) {
    EXPECT_EQ(CheckTally(41, 40), TallyCheck::kPresentExceedsConducted);
    EXPECT_FALSE(Percentage(41, 40).has_value());
    EXPECT_FALSE(Percentage(1, 0).has_value());
}

TEST(AttendanceMath, NegativeCountsAreRejected) {
    EXPECT_EQ(CheckTally(-1, 4), TallyCheck::kNegative);
    EXPECT_EQ(CheckTally(1, -4), TallyCheck::kNegative);
    EXPECT_FALSE(Percentage(-1, 4).has_value());
}

TEST(AttendanceMath, NothingConductedHasNoPercentage) {
    EXPECT_EQ(CheckTally(0, 0), TallyCheck::kOk);
    EXPECT_FALSE(Percentage(0, 0).has_value());
}

TEST(AttendanceMath, LargeCountsDoNotOverflow) {
    EXPECT_DOUBLE_EQ(*Percentage(999999, 1000000), 100.00);
    EXPECT_DOUBLE_EQ(*Percentage(123456, 1000000), 12.35);
}

}  // namespace
