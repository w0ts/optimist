// SPDX-License-Identifier: GPL-3.0-only
// A reference Link peer built on Ableton's own library (github.com/Ableton/link, GPL-2.0-or-later), for
// tools/link/interop.sh: it prints the session as it sees it, so ours (link_host) can be compared with it.
//   ref_peer [-bpm B] [-for S] [-at S:tempo=B] [-at S:play=0|1] [-q Q]
// Every 20 ms one line: "R host_us micro_beats bpm playing peers" (host_us: Link's clock, mach_absolute_time).
// Build: c++ -std=c++17 -O2 -I<link>/include -I<link>/modules/asio-standalone/asio/include ref_peer.cpp
#include <ableton/Link.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

struct Ev { double at; bool tempo; double v; bool done; };

int main(int argc, char** argv)
{
  double bpm = 120, secs = 20, q = 4;
  std::vector<Ev> evs;
  for (int i = 1; i + 1 < argc; i += 2)
  {
    if (!strcmp(argv[i], "-bpm")) bpm = atof(argv[i + 1]);
    else if (!strcmp(argv[i], "-for")) secs = atof(argv[i + 1]);
    else if (!strcmp(argv[i], "-q")) q = atof(argv[i + 1]);
    else if (!strcmp(argv[i], "-at"))
    {
      const char* a = argv[i + 1];
      const char* c = strchr(a, ':');
      if (!c) return 2;
      Ev e{atof(a), !strncmp(c + 1, "tempo=", 6), atof(strchr(c, '=') + 1), false};
      evs.push_back(e);
    }
  }
  ableton::Link link(bpm);
  link.enableStartStopSync(true);
  link.enable(true);
  const auto t0 = link.clock().micros();
  while (true)
  {
    const auto now = link.clock().micros();
    const double el = (now - t0).count() / 1e6;
    if (el > secs) break;
    for (auto& e : evs)
    {
      if (e.done || el < e.at) continue;
      e.done = true;
      auto s = link.captureAppSessionState();
      if (e.tempo)
        s.setTempo(e.v, now);
      else
        s.setIsPlayingAndRequestBeatAtTime(e.v != 0, now, 0., q);
      link.commitAppSessionState(s);
    }
    auto s = link.captureAppSessionState();
    // the session's own beats (phase-true): beatAtTime with quantum 1 is the client timeline's beat; print the
    // phase against q too, which is what peers agree on
    const double b = s.beatAtTime(now, q);
    const double ph = s.phaseAtTime(now, q);
    printf("R %lld %lld %.6f %d %zu %.6f\n", (long long)now.count(), (long long)llround(b * 1e6), s.tempo(),
           s.isPlaying() ? 1 : 0, link.numPeers(), ph);
    fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  link.enable(false);
  return 0;
}
