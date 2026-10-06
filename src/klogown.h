/* Whether a hit in the kernel message buffer sits on a line this payload wrote itself.
 *
 * ps5tm_log() mirrors every entry into the kernel's message buffer, and a few of the entries carry text a stranger
 * chose: the name of a package, a path, the word of a refused request. The readers of that buffer look for messages of
 * the console (the pad changing hands, a battery level, a memory report) by searching for their wording anywhere in
 * the buffer; a title called "SetControllerFocus(0x7)" would, written into one of our own lines, pass for the console
 * saying so. Our own lines carry the tag the kernel puts in front of everything a process writes (main() names the
 * process "ps5tm.elf"), after the priority in angle brackets:
 *
 *     <118>[ps5tm.elf] [WARN] code: text
 *
 * so a hit on such a line is ours and no message of the console. (micbutton.c has its own copy of this, from before.) */
#ifndef PS5TM_KLOGOWN_H
#define PS5TM_KLOGOWN_H

#include <string.h>

#define PS5TM_KLOG_SELF_TAG "[ps5tm.elf]"

static inline int
ps5tm_klog_own_line(const char *buf, const char *hit) {
  const char *ls = hit;
  for(int back = 0; ls > buf && ls[-1] != '\n'; back++) {
    if(back > 1024) return 0;       /* no line of ours is this long: not one of ours */
    ls--;
  }
  if(*ls == '<') {                  /* the priority tag: "<118>" */
    const char *q = ls + 1;
    while(*q >= '0' && *q <= '9') q++;
    if(q > ls + 1 && *q == '>') ls = q + 1;
  }
  return !strncmp(ls, PS5TM_KLOG_SELF_TAG, sizeof(PS5TM_KLOG_SELF_TAG) - 1);
}

#endif
