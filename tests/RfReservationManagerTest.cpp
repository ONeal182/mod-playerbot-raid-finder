/*
 * mod-playerbot-raid-finder — reservation manager unit tests.
 */

#include "RfReservationManager.h"

#include "gtest/gtest.h"

using namespace RaidFinder;

TEST(RfReservationManager, Reserve)
{
    RfReservationManager m;
    EXPECT_TRUE(m.Reserve(1001, 1));
    EXPECT_TRUE(m.IsReserved(1001));
    EXPECT_EQ(m.OwnerOf(1001), 1u);
    EXPECT_TRUE(m.Reserve(1001, 1)); // same session: idempotent
    EXPECT_EQ(m.Count(), 1u);
}

TEST(RfReservationManager, DuplicateReserveRejected)
{
    RfReservationManager m;
    EXPECT_TRUE(m.Reserve(1001, 1));
    EXPECT_FALSE(m.Reserve(1001, 2));
    EXPECT_EQ(m.OwnerOf(1001), 1u);
}

TEST(RfReservationManager, InvalidKeysRejected)
{
    RfReservationManager m;
    EXPECT_FALSE(m.Reserve(0, 1));
    EXPECT_FALSE(m.Reserve(5, 0));
    EXPECT_EQ(m.Count(), 0u);
}

TEST(RfReservationManager, Release)
{
    RfReservationManager m;
    m.Reserve(1001, 1);
    EXPECT_FALSE(m.Release(1001, 2)); // not the owner
    EXPECT_TRUE(m.IsReserved(1001));
    EXPECT_TRUE(m.Release(1001, 1));
    EXPECT_FALSE(m.IsReserved(1001));
    EXPECT_TRUE(m.Reserve(1001, 2));  // free again
}

TEST(RfReservationManager, DoubleReleaseSafe)
{
    RfReservationManager m;
    m.Reserve(1001, 1);
    EXPECT_TRUE(m.Release(1001, 1));
    EXPECT_FALSE(m.Release(1001, 1));
    EXPECT_EQ(m.Count(), 0u);
}

TEST(RfReservationManager, ReleaseSessionIdempotent)
{
    RfReservationManager m;
    for (RfRawGuid g = 1; g <= 9; ++g)
        m.Reserve(g, 7);
    m.Reserve(100, 8);

    EXPECT_EQ(m.BotsOf(7).size(), 9u);
    EXPECT_EQ(m.ReleaseSession(7), 9u);
    EXPECT_EQ(m.ReleaseSession(7), 0u);
    EXPECT_EQ(m.Count(), 1u);
    EXPECT_EQ(m.OwnerOf(100), 8u);
}
