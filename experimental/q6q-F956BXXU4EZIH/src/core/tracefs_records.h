#ifndef TRACEFS_RECORDS_H
#define TRACEFS_RECORDS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Linux ring_buffer.h format, shared by the stock 4.19 and 6.1 kernels.
 * memcpy avoids unaligned/aliasing loads. All lengths are bounded by the
 * returned raw page before event fields are inspected.
 */
static int tracefs_scan_page(const unsigned char *buf, size_t n,
                            uint16_t wanted_id, uint64_t caller_off,
                            uint64_t image_base, uint64_t max_slide,
                            uint64_t slide_alignment, uint64_t *out) {
  if (!buf || !out || n < 20 || !slide_alignment ||
      (slide_alignment & (slide_alignment - 1))) return 0;
  uint64_t commit;
  memcpy(&commit, buf + 8, sizeof(commit));
  size_t end = (size_t)(commit & 0xfff) + 16;
  if (end > n) end = n;
  for (size_t hdr = 16; hdr + 4 <= end;) {
    uint32_t header, length;
    memcpy(&header, buf + hdr, sizeof(header));
    unsigned int type = header & 31;
    size_t payload = hdr + 4;
    if (type >= 30) { // TIME_EXTEND and TIME_STAMP are both 8 bytes.
      if (end - hdr < 8) break;
      hdr += 8;
      continue;
    }
    if (type == 0 || type == 29) {
      if (end - hdr < 8 || (type == 29 && (header >> 5) == 0)) break;
      memcpy(&length, buf + hdr + 4, sizeof(length));
      if (length < 4 || length > end - hdr - 4) break;
      payload += 4; // array[0] is the length word, not event data.
    } else {
      length = type * 4;
      if (length > end - hdr - 4) break;
    }
    size_t next = hdr + 4 + length;
    if (type != 29 && next - payload >= 24) {
      uint16_t id;
      uint64_t caller;
      memcpy(&id, buf + payload, sizeof(id));
      memcpy(&caller, buf + payload + 16, sizeof(caller));
      if (id == wanted_id && caller >= 0xffff000000000000ULL && caller >= caller_off) {
        uint64_t base = caller - caller_off;
        if (base >= image_base && base - image_base <= max_slide &&
            ((base - image_base) & (slide_alignment - 1)) == 0) {
          *out = base;
          return 1;
        }
      }
    }
    hdr = next;
  }
  return 0;
}

#endif
