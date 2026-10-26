#pragma once

// OpenSky rations its API by daily credits rather than by rate, so the refresh
// interval and the allowance are two views of one number. Both the firmware and
// the configuration panel do this arithmetic, and they must agree on it: the
// panel promises the user a credit cost that the firmware then has to keep.
namespace OpenSkyBudget
{
    constexpr long CreditsPerDayAnonymous = 400;
    constexpr long CreditsPerDayAuthenticated = 4000;

    // Fetching an access token spends credits of its own, and the token lasts
    // 29 minutes, so a handful is held back for the refreshes.
    constexpr long ReservedCredits = 3;

    constexpr long SecondsPerDay = 24L * 60L * 60L;

    // Below this the feed has nothing new to say - OpenSky resolves positions to
    // five seconds for authenticated users and ten for everyone else.
    constexpr long MinimumIntervalSeconds = 5;

    constexpr long LargestIntervalSeconds = 3600;

    constexpr long Credits(bool authenticated)
    {
        return (authenticated ? CreditsPerDayAuthenticated : CreditsPerDayAnonymous) - ReservedCredits;
    }

    // The fastest refresh that still lasts a full day on the whole allowance.
    // Rounded up, because truncating the division buys a shorter interval than
    // the allowance covers: 86400 / 3997 is 21.6 s, and 21 s spends 4115.
    constexpr long AutomaticIntervalSeconds(bool authenticated)
    {
        const long credits = Credits(authenticated);

        return (SecondsPerDay + credits - 1) / credits;
    }
}
