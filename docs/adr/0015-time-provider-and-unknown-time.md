# ADR-0015: A time provider, and an explicit "time unknown" state

- **Status:** Accepted
- **Date:** 2026-07-29

## Context

The clock app was moved out of v0.2 into v0.4 precisely so this decision would be made when
something needed it (roadmap, "The clock moved to v0.4"). Now something does.

The constraint is C-9: **the Pi has no real-time clock.** At power-on the system clock holds
whatever `fake-hwclock` last wrote — the time of the previous shutdown, which can be days old and
is confidently wrong. It becomes correct only after `systemd-timesyncd` reaches an NTP server,
which requires WiFi, which is what the rest of v0.4 is about. On a fresh unit the first boot has no
network at all, so the very first thing a clock has to be able to say is that it does not know.

Two project rules bear on the shape of the answer:

- **FR-26** — apps perform no I/O and read no clock of their own; anything from outside arrives
  through an object handed in at construction.
- **NFR-17** — no abstraction without a second real implementation or three duplications. One
  interface with one real implementation would normally fail that test; the roadmap accepted the
  exception in advance ("needs a time provider in the platform, FR-26, fake for tests per NFR-17"),
  and the reason is that a clock is the one dependency that cannot be tested without being
  replaced. A test that waits for midnight is not a test.

## Decision

**`os/clock.h` defines `TimeProvider`**, with two implementations: `SystemTimeProvider` on the
device and on the host, and a fake in the tests.

```cpp
struct LocalTime            // broken down in the configured zone
{
    int year, month, day, hour, minute, second, weekday;
};

class TimeProvider
{
public:
    virtual bool synchronized() const = 0;   // false until NTP has been reached
    virtual LocalTime now() const = 0;       // meaningless while !synchronized()
    virtual void setTimeZone(std::string_view tz) = 0;
};
```

**Synchronisation is read from `/run/systemd/timesync/synchronized`.** `systemd-timesyncd` creates
that file the moment it has accepted a server's time, and `systemd-time-wait-sync` exists for
exactly this purpose — so this is the documented signal, not a heuristic. It costs one `stat()`,
needs no process spawn and no D-Bus, and it lives on a tmpfs, so it is absent again after every
reboot, which is correct behaviour rather than a limitation.

**The check is rate-limited to once every two seconds**, not once per frame. Sixty `stat()` calls a
second to learn a fact that changes twice in a device's lifetime is waste of exactly the kind C-3
warns about, and the panel's refresh thread is the thing being protected.

**On a host without `/run/systemd/timesync`** the provider falls back to a plausibility check — a
system clock past 2025 counts as synchronised. That keeps the simulator usable on any development
machine, and it is a fallback rather than the primary rule because on the device the wrong answer
here is a clock that displays yesterday with confidence.

**The time zone is a tzdata name** (`Europe/Vienna`), stored in the settings section and applied
with `setenv("TZ", …)` plus `tzset()`, after which `localtime_r` does the work. Names rather than a
fixed UTC offset, because an offset is wrong for half the year and nobody wants to re-set their
clock in March.

**The settings app offers a short curated list of zones**, not all of tzdata. Stepping an encoder
through 600 entries is not a user interface; a dozen covers the units that will exist, and the
configuration page is where an arbitrary zone belongs once someone needs one.

## Consequences

- **The clock is honest before it is useful.** Until the flag appears, the app shows that the time
  is unknown, and it says why in a form a non-technical user can act on — the device needs WiFi. It
  never displays a time it is not entitled to display.
- **The app is fully testable.** The fake provider makes "unknown at boot", "correct after sync",
  "midnight rollover" and "zone change" ordinary unit tests with no waiting and no flakiness — the
  same property that made the Pomodoro's counting testable by taking `dt` as an argument.
- **`setenv`/`tzset` mutate process-global state**, which is worth naming because MatrixOS now has a
  second thread ([ADR-0012](0012-own-http-server.md)). The rule that keeps it safe is narrow and
  enforced by where the code lives: time formatting happens only on the render thread, and the HTTP
  thread never calls `localtime_r`. If the configuration page ever needs to display a local
  timestamp, it formats from UTC or this decision gets revisited.
- **An invalid zone name silently means UTC**, because that is what glibc does with an unknown `TZ`.
  The curated list makes this unreachable from the panel; a hand-edited `settings.conf` can still
  produce it, and the fallback behaviour is the store's usual one — a wrong value degrades to a
  defensible default rather than to a failure.
- **We do not run NTP ourselves and do not choose a poll interval.** `systemd-timesyncd` owns that,
  with its own backoff. The device only asks whether it has succeeded. That keeps the network off
  our render thread entirely, which is what the Q-2 note about timing was pointing at.
- **A device that never gets WiFi has a clock that never works**, permanently showing the unknown
  state. That is the correct behaviour and it is also a design constraint on the setup flow: the
  clock is the default app of a fresh unit, so the unknown screen is the first thing many users
  will see, and it has to look intentional rather than broken.

## Alternatives considered

- **Read the clock directly in the app** — one line instead of an interface, and it breaks FR-26,
  makes the app untestable, and scatters the "do we know the time?" question across every future
  app that shows a timestamp.
- **`timedatectl show -p NTPSynchronized`** — the same fact through a child process, several
  milliseconds and a fork, on a device that renders every 16.6 ms. The file it reports on is right
  there.
- **D-Bus to `org.freedesktop.timedate1`** — typed and event-driven, and it wants a D-Bus library
  cross-compiled for one boolean.
- **Assume the time is right if the year is plausible** — kept only as the host fallback. On the
  device it would answer "yes" to a `fake-hwclock` value from three days ago, which is precisely the
  failure C-9 exists to prevent.
- **Adding a hardware RTC module (DS3231)** — the real fix for C-9, at the cost of two more GPIO
  pins, a component in every unit, and a battery that eventually dies in someone's living room. The
  network is already a prerequisite for the device's other apps, so the clock depending on it costs
  nothing extra.
- **A fixed UTC offset instead of a zone name** — smaller and simpler until the last Sunday in
  March, when every device shows the wrong hour until someone changes a setting they do not know
  exists.
