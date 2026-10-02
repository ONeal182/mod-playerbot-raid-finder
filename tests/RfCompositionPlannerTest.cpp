/*
 * mod-playerbot-raid-finder — composition planner unit tests.
 */

#include "RfCompositionPlanner.h"

#include "gtest/gtest.h"

using namespace RaidFinder;

namespace
{
    constexpr std::uint8_t T = RF_ROLE_TANK;
    constexpr std::uint8_t H = RF_ROLE_HEALER;
    constexpr std::uint8_t D = RF_ROLE_DPS;

    RfRoleCounts Counts(std::uint32_t t, std::uint32_t h, std::uint32_t d)
    {
        RfRoleCounts c;
        c.tank = t;
        c.healer = h;
        c.dps = d;
        return c;
    }

    RfCandidate Cand(std::uint32_t id, std::uint8_t roles, std::uint32_t score = 100, bool ranged = false)
    {
        RfCandidate c;
        c.id = id;
        c.roles = roles;
        c.score = score;
        c.ranged = ranged;
        return c;
    }

    // Adds `n` single-role candidates starting at id `base`.
    void AddMany(RfPlanInput& in, std::uint32_t base, std::uint32_t n, std::uint8_t roles)
    {
        for (std::uint32_t i = 0; i < n; ++i)
            in.candidates.push_back(Cand(base + i, roles));
    }
}

TEST(RfCompositionPlanner, Basic10Man)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    AddMany(in, 100, 3, T);
    AddMany(in, 200, 3, H);
    AddMany(in, 300, 8, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(r.selected.size(), 10u);
    EXPECT_EQ(r.assigned, Counts(2, 2, 6));
    EXPECT_EQ(r.missing.Total(), 0u);
}

TEST(RfCompositionPlanner, OneRealDps)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    in.filled = Counts(0, 0, 1);
    AddMany(in, 100, 2, T);
    AddMany(in, 200, 2, H);
    AddMany(in, 300, 10, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(r.selected.size(), 9u);
    EXPECT_EQ(r.assigned, Counts(2, 2, 5));
}

TEST(RfCompositionPlanner, SeveralRealPlayers)
{
    RfPlanInput in;
    in.required = Counts(2, 5, 18);
    in.filled = Counts(1, 2, 3);
    AddMany(in, 100, 5, T);
    AddMany(in, 200, 5, H);
    AddMany(in, 300, 30, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(r.assigned, Counts(1, 3, 15));
    EXPECT_EQ(r.selected.size(), 19u);
}

TEST(RfCompositionPlanner, TankHealHybridGoesToHealWhenOnlyHealerSource)
{
    // Warrior = Tank, Paladin = Tank+Heal, Mage = DPS. Need 1/1/1.
    RfPlanInput in;
    in.required = Counts(1, 1, 1);
    in.candidates = { Cand(1, T | H), Cand(2, T), Cand(3, D) }; // hybrid listed first on purpose

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.RoleOf(2), T);
    EXPECT_EQ(r.RoleOf(1), H);
    EXPECT_EQ(r.RoleOf(3), D);
}

TEST(RfCompositionPlanner, TankDpsHybrid)
{
    // Feral druid (Tank+DPS) must tank when no pure tank exists.
    RfPlanInput in;
    in.required = Counts(1, 0, 2);
    in.candidates = { Cand(1, T | D), Cand(2, D), Cand(3, D) };

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.RoleOf(1), T);
    EXPECT_EQ(r.RoleOf(2), D);
    EXPECT_EQ(r.RoleOf(3), D);
}

TEST(RfCompositionPlanner, HealDpsHybrid)
{
    RfPlanInput in;
    in.required = Counts(0, 1, 1);
    in.candidates = { Cand(1, H | D), Cand(2, D) };

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.RoleOf(1), H);
    EXPECT_EQ(r.RoleOf(2), D);
}

TEST(RfCompositionPlanner, InsufficientHealer)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    AddMany(in, 100, 2, T);
    AddMany(in, 200, 1, H);
    AddMany(in, 300, 10, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_FALSE(r.complete);
    EXPECT_EQ(r.missing, Counts(0, 1, 0));
    EXPECT_EQ(r.assigned, Counts(2, 1, 6));
}

TEST(RfCompositionPlanner, InsufficientTank)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    AddMany(in, 200, 4, H);
    AddMany(in, 300, 10, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_FALSE(r.complete);
    EXPECT_EQ(r.missing, Counts(2, 0, 0));
}

TEST(RfCompositionPlanner, ExactPool)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    AddMany(in, 100, 2, T);
    AddMany(in, 200, 2, H);
    AddMany(in, 300, 6, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(r.selected.size(), 10u);
}

TEST(RfCompositionPlanner, OversizedPoolPrefersHigherScore)
{
    RfPlanInput in;
    in.required = Counts(1, 0, 0);
    in.candidates = { Cand(1, T, 150), Cand(2, T, 260), Cand(3, T, 200) };
    for (std::uint32_t i = 0; i < 500; ++i)
        in.candidates.push_back(Cand(1000 + i, D, i));

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    ASSERT_TRUE(r.complete);
    ASSERT_EQ(r.selected.size(), 1u);
    EXPECT_EQ(r.selected[0].id, 2u);
}

TEST(RfCompositionPlanner, LeastFlexibleSelection)
{
    // Plenty of pure DPS: hybrids must not take DPS seats, they stay free.
    RfPlanInput in;
    in.required = Counts(0, 0, 3);
    in.candidates = { Cand(1, T | D, 999), Cand(2, H | D, 999), Cand(3, D, 10), Cand(4, D, 10),
                      Cand(5, D, 10) };

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.RoleOf(1), RF_ROLE_NONE);
    EXPECT_EQ(r.RoleOf(2), RF_ROLE_NONE);
    EXPECT_EQ(r.RoleOf(3), D);
}

TEST(RfCompositionPlanner, NoValidComposition)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_FALSE(r.complete);
    EXPECT_TRUE(r.selected.empty());
    EXPECT_EQ(r.missing, Counts(2, 2, 6));
}

TEST(RfCompositionPlanner, MaxSelectedCap)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    in.maxSelected = 4;
    AddMany(in, 100, 2, T);
    AddMany(in, 200, 2, H);
    AddMany(in, 300, 6, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_FALSE(r.complete);
    EXPECT_EQ(r.selected.size(), 4u);
    EXPECT_EQ(r.missing.Total(), 6u);
}

TEST(RfCompositionPlanner, NoRoleBalanceFillsSeats)
{
    RfPlanInput in;
    in.required = Counts(2, 2, 6);
    in.requireRoleBalance = false;
    AddMany(in, 300, 12, D);

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    EXPECT_TRUE(r.complete);
    EXPECT_EQ(r.selected.size(), 10u);
}

TEST(RfCompositionPlanner, RangedMeleeInterleave)
{
    RfPlanInput in;
    in.required = Counts(0, 0, 4);
    for (std::uint32_t i = 0; i < 4; ++i)
        in.candidates.push_back(Cand(10 + i, D, 300, true));   // ranged, higher score
    for (std::uint32_t i = 0; i < 4; ++i)
        in.candidates.push_back(Cand(20 + i, D, 200, false));  // melee

    RfPlanResult r = RfCompositionPlanner::Plan(in);
    ASSERT_TRUE(r.complete);
    int ranged = 0;
    for (RfAssignment const& a : r.selected)
        ranged += a.id < 20 ? 1 : 0;
    EXPECT_EQ(ranged, 2);
}

TEST(RfCompositionPlanner, ParseRole)
{
    EXPECT_EQ(ParseRole("Tank"), T);
    EXPECT_EQ(ParseRole("HEAL"), H);
    EXPECT_EQ(ParseRole("healer"), H);
    EXPECT_EQ(ParseRole("dps"), D);
    EXPECT_EQ(ParseRole("bogus"), RF_ROLE_NONE);
}
