<?php
// SPDX-License-Identifier: GPL-3.0-only
// The cloud build relay (docs/WEB.md, "Cloud builds without a GitHub account"): the public web builder sends
// {turnstile, commit, config, name} here; this checks the Cloudflare Turnstile token (not a robot), then opens the
// cloud-build issue with a token that can only write issues, so .github/workflows/cloud-build.yml builds it as if the
// form had been sent. The body is the form's (### Commit, ### Configuration); tools/cloud_build.py reads it and checks
// the commit and the configuration as for any issue: nothing here is trusted by the workflow.
//
// Install (PHP 7.4+ with curl): this file and cloud-relay-config.php (it holds the tokens) outside the web root, nginx
// running this one path through PHP; a data directory PHP can write (ledger.json). See cloud-relay-config.example.php.
//
//   php cloud-build.php body < req.json   the issue body of a request (tests/cloud_build_test.py)

const MAX_CONFIG = 18000;                  // tools/cloud_build.py MAX_BODY is 20000 for the whole body
const MAX_REQUEST = MAX_CONFIG + 2000;     // the JSON around it
const SHA_RE = '/^([0-9a-f]{40}|[0-9a-f]{64})$/';

/** the issue body of a request: the form's headings, the configuration fenced as the form renders it */
function relay_body(string $commit, string $config): string {
  // (it starts with "### Commit": the workflow's trigger when the label comes late; "### Sent by" is not read)
  return "### Commit\n\n" . ($commit === '' ? '_No response_' : $commit) . "\n\n### Configuration\n\n```text\n"
    . rtrim($config) . "\n```\n\n### Sent by\n\nthe web builder (no GitHub account)\n";
}

/** the firmware's characters (web/builder/logic.js firmwareName), 16 at most */
function firmware_name($name): string {
  return substr(preg_replace('/[^A-Za-z0-9 +._-]/', '', is_string($name) ? $name : ''), 0, 16);
}

/** the request's commit and configuration, or the reason it is refused */
function check_request($req): array {
  if (!is_array($req)) return ['error' => 'not a request'];
  $commit = $req['commit'] ?? '';
  $config = $req['config'] ?? null;
  if (!is_string($commit) || ($commit !== '' && !preg_match(SHA_RE, $commit))) return ['error' => 'the commit must be a full SHA or empty'];
  if (!is_string($config) || trim($config) === '') return ['error' => 'no configuration'];
  if (strlen($config) > MAX_CONFIG) return ['error' => 'the configuration is too long'];
  if (strpos($config, '```') !== false || preg_match('/^###/m', $config)) return ['error' => 'not a builder configuration'];
  return ['commit' => $commit, 'config' => str_replace("\r\n", "\n", $config), 'name' => firmware_name($req['name'] ?? '')];
}

function answer(int $status, array $out): void {
  http_response_code($status);
  header('Content-Type: application/json');
  echo json_encode($out);
  exit;
}

function load_config(): array {
  foreach ([__DIR__ . '/../cloud-relay-config.php', __DIR__ . '/cloud-relay-config.php'] as $f) {
    if (is_file($f)) return require $f;
  }
  error_log('cloud relay: no cloud-relay-config.php');
  answer(500, ['error' => 'the relay is not set up']);
}

/** the ledger (data_dir/ledger.json), locked for the whole request: one request at a time, so a flood waits in line.
    ['ip' => {address => [request times]}, 'polls' => {address => [GET times]}, 'builds' => {address => [build times]},
    'sent' => {sha256(address, commit, config) => [time, issue URL]}, 'queued' => {address => [time, issue URL, issue
    number]}, 'issues' => {issue number => time} (the relay's: the only ones ?status= answers for), 'status' => {issue
    number => [time, answer]} (the cache)], an address being sha256(salt . IP); entries older than a day dropped */
function ledger_open(array $cfg) {
  $file = rtrim($cfg['data_dir'], '/') . '/ledger.json';
  $fh = fopen($file, 'c+');
  if (!$fh || !flock($fh, LOCK_EX)) { error_log("cloud relay: cannot lock $file"); answer(503, ['error' => 'the relay is busy']); }
  $l = json_decode(stream_get_contents($fh) ?: '{}', true);
  $l = is_array($l) ? $l : [];
  $now = time();
  $fresh = fn($t) => is_int($t) && $t > $now - 86400;
  foreach (['ip', 'polls', 'builds'] as $k) {
    $l[$k] = array_filter(array_map(fn($ts) => array_values(array_filter((array)$ts, $fresh)), (array)($l[$k] ?? [])));
  }
  $l['queued'] = array_filter((array)($l['queued'] ?? []), fn($e) => is_array($e) && $fresh($e[0] ?? null));
  $l['sent'] = array_filter((array)($l['sent'] ?? []), fn($e) => is_array($e) && $fresh($e[0] ?? null));
  $l['issues'] = array_filter((array)($l['issues'] ?? []), $fresh);
  $l['status'] = array_filter((array)($l['status'] ?? []), fn($e) => is_array($e) && is_int($e[0] ?? null) && $e[0] > $now - 600);
  return [$fh, $l];
}

function ledger_save($fh, array $l): void {
  ftruncate($fh, 0);
  rewind($fh);
  fwrite($fh, json_encode($l));
  fflush($fh);
}

/** the times in the last $secs seconds */
function within(array $times, int $secs): int {
  $now = time();
  return count(array_filter($times, fn($t) => $t > $now - $secs));
}

/** a GitHub API call (the lock is held meanwhile: 15 s at most) -> [HTTP status, the JSON answer]; 0 = no answer */
function github(array $cfg, string $path, ?array $post = null): array {
  $ch = curl_init(($cfg['api'] ?? 'https://api.github.com') . "/repos/{$cfg['repo']}/$path");
  curl_setopt_array($ch, [
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_TIMEOUT => 15,
    CURLOPT_HTTPHEADER => ['Authorization: Bearer ' . $cfg['token'], 'Accept: application/vnd.github+json',
      'X-GitHub-Api-Version: 2022-11-28', 'User-Agent: optimist-cloud-relay', 'Content-Type: application/json'],
  ] + ($post === null ? [] : [CURLOPT_POST => true, CURLOPT_POSTFIELDS => json_encode($post)]));
  $res = curl_exec($ch);
  $code = (int)curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
  if ($code === 0) error_log('cloud relay: GitHub did not answer: ' . curl_error($ch));
  $out = json_decode((string)$res, true);
  return [$code, is_array($out) ? $out : []];
}

/** POST the issue -> [its page, its number]; the GitHub error is logged, not shown */
function open_issue(array $cfg, string $title, string $body): array {
  [$code, $out] = github($cfg, 'issues', ['title' => $title, 'body' => $body, 'labels' => ['cloud-build']]);
  $url = $code === 201 && is_string($out['html_url'] ?? null) ? $out['html_url'] : '';
  if ($url === '' || !is_int($out['number'] ?? null)) {
    error_log("cloud relay: GitHub answered $code " . substr(json_encode($out), 0, 300));
    answer(502, ['error' => 'GitHub did not take the request: try again later']);
  }
  return [$url, $out['number']];
}

/** Cloudflare's answer on the token: a pass for this site and action, from this address */
function not_a_robot(array $cfg, string $token): bool {
  if ($token === '' || strlen($token) > 2048) return false;
  $ch = curl_init(($cfg['turnstile_api'] ?? 'https://challenges.cloudflare.com') . '/turnstile/v0/siteverify');
  curl_setopt_array($ch, [CURLOPT_RETURNTRANSFER => true, CURLOPT_TIMEOUT => 10, CURLOPT_POST => true,
    CURLOPT_POSTFIELDS => http_build_query(['secret' => $cfg['turnstile_secret'], 'response' => $token, 'remoteip' => $_SERVER['REMOTE_ADDR'] ?? ''])]);
  $out = json_decode((string)curl_exec($ch), true);
  if (!is_array($out)) { error_log('cloud relay: Turnstile did not answer: ' . curl_error($ch)); return false; }
  return ($out['success'] ?? false) === true && ($out['action'] ?? '') === 'cloud-build'
    && in_array($out['hostname'] ?? '', $cfg['turnstile_hostnames'], true);
}

/** this address's last build is still queued or building: its issue is open (the workflow closes it when the build is
    done, refused or failed) and younger than queue_max_seconds (a workflow error leaves it open). No answer from GitHub = queued */
function still_queued(array $cfg, ?array $q): bool {
  if (!is_array($q) || !is_int($q[0] ?? null) || !is_int($q[2] ?? null) || $q[0] <= time() - $cfg['queue_max_seconds']) return false;
  [$code, $out] = github($cfg, "issues/{$q[2]}");
  return $code !== 200 || ($out['state'] ?? '') === 'open';
}

/** a build's progress, from the workflow's own comments (tools/cloud_build.py marker): the last marker of the
    github-actions bot (anyone can comment on an issue: a marker of anybody else is ignored), else "queued".
    -> {issue, url, stage, id?, key?, run?, reason?} */
function build_status(array $cfg, int $n): array {
  [$c1, $iss] = github($cfg, "issues/$n");
  [$c2, $comments] = github($cfg, "issues/$n/comments?per_page=100");
  if ($c1 !== 200 || $c2 !== 200) answer(502, ['error' => 'GitHub did not answer: try again in a minute']);
  $out = ['issue' => $n, 'url' => is_string($iss['html_url'] ?? null) ? $iss['html_url'] : '', 'stage' => 'queued'];
  foreach ($comments as $c) {
    $body = is_array($c) && is_string($c['body'] ?? null) ? $c['body'] : '';
    if (($c['user']['login'] ?? '') !== 'github-actions[bot]' || ($c['user']['type'] ?? '') !== 'Bot') continue;
    if (!preg_match('/<!-- optimist-cloud (\{[^<>]{1,600}\}) -->\s*$/', $body, $m)) continue;
    $mk = json_decode($m[1], true);
    if (!is_array($mk) || !in_array($mk['stage'] ?? '', ['building', 'done', 'refused', 'failed', 'error'], true)) continue;
    $out = ['issue' => $n, 'url' => $out['url'], 'stage' => $mk['stage']];
    if (preg_match('/^[0-9a-f]{64}$/', $mk['key'] ?? '')) $out += ['id' => substr($mk['key'], 0, 16), 'key' => $mk['key']];
    if (preg_match('#^https://github\.com/[A-Za-z0-9._/-]+$#', $mk['run'] ?? '')) $out['run'] = $mk['run'];
    if (in_array($mk['stage'], ['refused', 'failed', 'error'], true)) {
      // (the comment's text, plain: the page shows it as text, never as HTML)
      $out['reason'] = mb_substr(trim(preg_replace('/<!--.*?-->/s', '', $body)), 0, 800);
    }
  }
  return $out;
}

/** the branches and tags a build can start from, at most refs_cache_seconds old (data_dir/refs.json); the cloud-builds
    branch and release tag (the builds themselves, not a source) left out */
function refs(array $cfg): array {
  $file = rtrim($cfg['data_dir'], '/') . '/refs.json';
  $old = is_file($file) ? json_decode((string)file_get_contents($file), true) : null;
  if (is_array($old) && ($old['t'] ?? 0) > time() - ($cfg['refs_cache_seconds'] ?? 300)) return $old['refs'];
  $list = function (string $what) use ($cfg, $old): array {
    [$code, $out] = github($cfg, "$what?per_page=100");
    if ($code !== 200) {
      if (is_array($old)) return $old['refs'][$what];
      answer(502, ['error' => 'GitHub did not answer: try again in a minute']);
    }
    $refs = [];
    foreach ($out as $r) {
      $name = $r['name'] ?? '';
      $sha = $r['commit']['sha'] ?? '';
      if (is_string($name) && $name !== 'cloud-builds' && preg_match('/^[A-Za-z0-9._\/-]{1,100}$/', $name)
        && is_string($sha) && preg_match('/^[0-9a-f]{40}$/', $sha)) {
        $refs[] = ['name' => $name, 'sha' => $sha];
      }
    }
    return $refs;
  };
  $refs = ['branches' => $list('branches'), 'tags' => $list('tags')];
  file_put_contents($file, json_encode(['t' => time(), 'refs' => $refs]));
  return $refs;
}

/** GET ?status=<issue> (a build's progress) or ?refs (where a build can start from): counted apart from the POSTs */
function serve_get(array $cfg, string $ip): void {
  [$fh, $l] = ledger_open($cfg);
  $now = time();
  $l['polls'][$ip] = array_merge($l['polls'][$ip] ?? [], [$now]);
  ledger_save($fh, $l);
  if (within($l['polls'][$ip], 3600) > ($cfg['polls_per_ip_per_hour'] ?? 400)) answer(429, ['error' => 'too many checks from this network: try again later']);
  if (isset($_GET['refs'])) answer(200, refs($cfg));
  $n = filter_var($_GET['status'] ?? '', FILTER_VALIDATE_INT, ['options' => ['min_range' => 1]]);
  if ($n === false || !isset($l['issues'][$n])) answer(404, ['error' => 'not a build this service sent']);
  $hit = $l['status'][$n] ?? null;
  if (is_array($hit) && $hit[0] > $now - ($cfg['status_cache_seconds'] ?? 15)) answer(200, $hit[1]);
  $st = build_status($cfg, $n);
  $l['status'][$n] = [$now, $st];
  ledger_save($fh, $l);
  answer(200, $st);
}

/** an issue's number from its URL (the relay's own: https://github.com/<repo>/issues/<n>) */
function issue_number(string $url): int {
  return preg_match('#/issues/(\d+)$#', $url, $m) ? (int)$m[1] : 0;
}

function cli(array $argv): void {
  if (($argv[1] ?? '') === 'body') {
    $r = check_request(json_decode(stream_get_contents(STDIN), true));
    if (isset($r['error'])) { fwrite(STDERR, $r['error'] . "\n"); exit(1); }
    echo relay_body($r['commit'], $r['config']);
    return;
  }
  fwrite(STDERR, "usage: php cloud-build.php body < request.json\n");
  exit(2);
}

function serve(): void {
  header('X-Content-Type-Options: nosniff');
  header('Cache-Control: no-store');
  $cfg = load_config();
  $origin = $_SERVER['HTTP_ORIGIN'] ?? '';
  if (!in_array($origin, $cfg['origins'], true)) answer(403, ['error' => 'not from the builder']);
  header("Access-Control-Allow-Origin: $origin");
  header('Access-Control-Allow-Methods: GET, POST');
  header('Access-Control-Allow-Headers: Content-Type');
  header('Access-Control-Max-Age: 600');
  header('Vary: Origin');
  if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }
  $ip = hash('sha256', $cfg['salt'] . ($_SERVER['REMOTE_ADDR'] ?? ''));
  if ($_SERVER['REQUEST_METHOD'] === 'GET') serve_get($cfg, $ip);
  if ($_SERVER['REQUEST_METHOD'] !== 'POST') answer(405, ['error' => 'GET or POST only']);
  // (JSON only: a plain form post from another site cannot reach here without the CORS preflight)
  if (stripos($_SERVER['CONTENT_TYPE'] ?? '', 'application/json') !== 0) answer(415, ['error' => 'JSON only']);
  if ((int)($_SERVER['CONTENT_LENGTH'] ?? 0) > MAX_REQUEST) answer(413, ['error' => 'the request is too long']);

  [$fh, $l] = ledger_open($cfg);
  $now = time();
  // every request counts for its address, whatever it holds: flooding stops here
  $l['ip'][$ip] = array_merge($l['ip'][$ip] ?? [], [$now]);
  $over_ip = within($l['ip'][$ip], 3600) > $cfg['per_ip_per_hour'];
  ledger_save($fh, $l);
  if ($over_ip) answer(429, ['error' => 'too many requests from this network: try again in an hour']);

  $req = json_decode((string)file_get_contents('php://input', false, null, 0, MAX_REQUEST), true);
  $r = check_request($req);
  if (isset($r['error'])) answer(400, ['error' => $r['error']]);

  // the same request again from this address (a second click, a reload): its issue, no new one
  $digest = hash('sha256', $ip . "\0" . $r['commit'] . "\0" . $r['config']);
  if (isset($l['sent'][$digest])) {
    $url = $l['sent'][$digest][1];
    answer(200, ['issue' => $url, 'number' => issue_number($url), 'again' => true]);
  }
  // one build queued per address: a new one only when the last is built (or refused)
  if (still_queued($cfg, $l['queued'][$ip] ?? null)) {
    answer(429, ['error' => 'your last cloud build is not finished yet: wait for it', 'issue' => $l['queued'][$ip][1], 'number' => $l['queued'][$ip][2]]);
  }
  unset($l['queued'][$ip]);
  $mine = $l['builds'][$ip] ?? [];
  if ($mine && max($mine) > $now - $cfg['min_gap_seconds']) answer(429, ['error' => 'one cloud build at a time: try again in a few minutes']);
  if (count($mine) >= $cfg['per_ip_per_day']) answer(429, ['error' => 'too many cloud builds today from this network: try again tomorrow']);
  if (array_sum(array_map('count', $l['builds'])) >= $cfg['all_per_day']) answer(429, ['error' => 'too many cloud builds today: try again tomorrow']);
  // last, the robot check (a call to Cloudflare): only for a request that would be built
  $token = is_array($req) && is_string($req['turnstile'] ?? null) ? $req['turnstile'] : '';
  if (!not_a_robot($cfg, $token)) answer(403, ['error' => 'the robot check failed: reload the page and try again']);

  $title = 'Cloud build: ' . ($r['name'] !== '' ? $r['name'] : 'custom');
  [$url, $number] = open_issue($cfg, $title, relay_body($r['commit'], $r['config']));
  $l['builds'][$ip] = array_merge($mine, [$now]);
  $l['queued'][$ip] = [$now, $url, $number];
  $l['sent'][$digest] = [$now, $url];
  $l['issues'][$number] = $now;
  ledger_save($fh, $l);
  answer(201, ['issue' => $url, 'number' => $number]);
}

if (PHP_SAPI === 'cli') cli($argv);
else serve();
