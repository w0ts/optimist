<?php
// SPDX-License-Identifier: GPL-3.0-only
// The cloud build relay's settings (cloud-build.php, docs/WEB.md). Copy to cloud-relay-config.php one directory above
// the web root's cloud-build.php (or next to it), fill in, and keep it out of git: it holds the token.
return [
  // a fine-grained token: Repository access = only w0ts/optimist, Permissions = Issues: Read and write, nothing else.
  // Its account is the issues' author: a maintainer's, which the workflow's per-author limit does not count.
  'token' => 'github_pat_...',
  'repo' => 'w0ts/optimist',
  // the pages allowed to call the relay (the browser's Origin header)
  'origins' => ['https://w0ts.github.io'],
  // Cloudflare Turnstile (dash.cloudflare.com > Turnstile, free): the widget's secret key; its site key goes in
  // web/builder/logic.js TURNSTILE_SITEKEY. The hostnames the widget may be solved on:
  'turnstile_secret' => '0x...',
  'turnstile_hostnames' => ['w0ts.github.io'],
  // flooding, per address (sha256(salt . IP)): every request counts (per_ip_per_hour); a build still queued blocks the
  // next; a gap between two builds and a daily number; all addresses together have one; the same request again answers
  // its first issue
  'per_ip_per_hour' => 20,
  'min_gap_seconds' => 120,
  // one build queued per address: refused while its issue is open, for this long at most (a failed run leaves it open)
  'queue_max_seconds' => 3600,
  'per_ip_per_day' => 100,
  'all_per_day' => 1000,
  // the page's checks (GET ?status= every 20 s while a build runs, ?refs): counted apart, cached
  'polls_per_ip_per_hour' => 400,
  'status_cache_seconds' => 15,
  'refs_cache_seconds' => 300,
  // a random string of yours: the addresses are kept only as sha256(salt . address)
  'salt' => 'change-me',
  // a directory PHP can write, outside the web root (ledger.json)
  'data_dir' => __DIR__ . '/cloud-relay-data',
];
