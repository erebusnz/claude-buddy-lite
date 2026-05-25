#pragma once
#include <Arduino.h>

// Score 1 = safe / read-only, 5 = destructive / irreversible.
// Multi-token entries (e.g. "rm -rf") are matched as a whole phrase with
// word boundaries on both ends, so "rm -rf" matches "sudo rm -rf /" but
// keyword "rm" does NOT match "lrm" or "warm".
struct CommandEntry {
  const char *keyword;
  uint8_t score;
};

static const CommandEntry kCommandTable[] = {
  // 5 — destructive / irreversible
  {"rm -rf",            5},
  {"rm -fr",            5},
  {"rm -r",             5},
  {"rm",                5},
  {"mkfs",              5},
  {"dd",                5},
  {"shutdown",          5},
  {"reboot",            5},
  {"halt",              5},
  {"git push --force",  5},
  {"git push -f",       5},
  {"git push --force-with-lease", 5},
  {"git reset --hard",  5},
  {"git clean -fd",     5},
  {"git clean -f",      5},
  {"git branch -D",     5},
  {"drop table",        5},
  {"drop database",     5},
  {"truncate",          5},
  {"shred",             5},
  {":(){",              5}, // fork bomb hint

  // 4 — mutates system / remote state
  {"sudo",              4},
  {"chmod",             4},
  {"chown",             4},
  {"kill",              4},
  {"killall",           4},
  {"pkill",             4},
  {"systemctl",         4},
  {"service",           4},
  {"apt",               4},
  {"apt-get",           4},
  {"yum",               4},
  {"dnf",               4},
  {"pacman",            4},
  {"git push",          4},
  {"git reset",         4},
  {"git rebase",        4},
  {"npm uninstall",     4},
  {"pip uninstall",     4},
  {"mv",                4},

  // 3 — writes locally, contained
  {"cp",                3},
  {"ln",                3},
  {"curl",              3},
  {"wget",              3},
  {"git commit",        3},
  {"git checkout",      3},
  {"git merge",         3},
  {"git pull",          3},
  {"git stash",         3},
  {"npm install",       3},
  {"pip install",       3},
  {"brew",              3},
  {"docker",            3},

  // 2 — runs code locally
  {"python",            2},
  {"python3",           2},
  {"node",              2},
  {"bash",              2},
  {"sh",                2},
  {"make",              2},
  {"mkdir",             2},
  {"touch",             2},

  // 1 — read-only / inspect
  {"ls",                1},
  {"cat",               1},
  {"head",              1},
  {"tail",              1},
  {"less",              1},
  {"more",              1},
  {"grep",              1},
  {"rg",                1},
  {"find",              1},
  {"pwd",               1},
  {"echo",              1},
  {"which",             1},
  {"whoami",            1},
  {"date",              1},
  {"stat",              1},
  {"file",              1},
  {"wc",                1},
  {"git status",        1},
  {"git log",           1},
  {"git diff",          1},
  {"git show",          1},
  {"git branch",        1},
};

static inline bool commandIsWordChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static inline char commandToLower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Case-insensitive word-boundary search. A boundary is start/end of string
// or any non-word char (space, dash, slash, pipe, ...), which lets multi-token
// keywords like "rm -rf" match naturally while preventing "rm" from hitting
// "warm" or "lrm".
static bool commandContains(const char *hay, const char *needle) {
  const size_t nlen = strlen(needle);
  if (nlen == 0 || !hay) return false;
  for (const char *p = hay; *p; ++p) {
    if (p != hay && commandIsWordChar(*(p - 1))) continue;
    size_t i = 0;
    while (i < nlen && p[i] && commandToLower(p[i]) == commandToLower(needle[i])) ++i;
    if (i != nlen) continue;
    const char after = p[nlen];
    if (after == 0 || !commandIsWordChar(after)) return true;
  }
  return false;
}

// Returns 1..5. Unknown / empty command falls back to 3 (medium) so an
// unrecognized prompt still leans cautious-orange rather than safe-yellow.
static uint8_t scoreCommand(const char *cmd) {
  if (!cmd || !*cmd) return 3;
  uint8_t best = 0;
  for (const auto &e : kCommandTable) {
    if (e.score > best && commandContains(cmd, e.keyword)) {
      best = e.score;
      if (best == 5) break; // can't get worse
    }
  }
  return best ? best : 3;
}
