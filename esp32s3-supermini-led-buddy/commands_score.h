#pragma once
#include "commands_table.h"

// ---------- Matcher ----------
//
// scoreCommand() walks kCommandTable (see commands_table.h) and returns the
// highest matching keyword's score, defaulting to 3 ("cautious orange") for
// unknown or empty input. Matching is case-insensitive and word-boundary
// aware so multi-token keywords like "rm -rf" land naturally while bare
// "rm" does NOT catch "warm" or "lrm".

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
//
// Loop ordering matters for performance: the table is laid out 5 -> 1, so
//   - `e.score > best` skips lower-priority entries once a higher score has
//     matched (e.g. after matching a 4, all 1/2/3/4 entries are skipped),
//   - and `break` on best == 5 exits as soon as any destructive keyword hits.
// Correctness is independent of order — the loop picks the MAX across all
// matches — but the layout makes the common case fast.
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
