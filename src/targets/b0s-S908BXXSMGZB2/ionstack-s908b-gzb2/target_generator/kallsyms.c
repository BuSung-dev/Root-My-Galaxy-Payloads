#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

struct cheese_kallsyms_lookup {
  const void* kernel_data;
  size_t kernel_length;
  const int* kallsyms_offsets;
  uint64_t kallsyms_relative_base;
  unsigned int kallsyms_num_syms;
  const uint8_t* kallsyms_names;
  const char* kallsyms_token_table;
  const uint16_t* kallsyms_token_index;
  char** decompressed_names;
  uint64_t text_base;

  // Internal state
  uint8_t endian; // 0=unknown, 1=little, 2=big
  uint16_t* built_token_index;
  bool token_index_is_built;
};

uint64_t cheese_kallsyms_lookup(struct cheese_kallsyms_lookup* kallsyms_lookup,
                                const char* name);

static void* memmem_custom(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen) {
    if (needlelen == 0) return (void*)haystack;
    if (haystacklen < needlelen) return NULL;
    const uint8_t* h = haystack;
    const uint8_t* n = needle;
    for (size_t i = 0; i <= haystacklen - needlelen; i++) {
        if (h[i] == n[0] && memcmp(&h[i], n, needlelen) == 0) {
            return (void*)&h[i];
        }
    }
    return NULL;
}

static void* memmem_last(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen) {
    if (needlelen == 0) return (void*)haystack;
    if (haystacklen < needlelen) return NULL;
    const uint8_t* h = haystack;
    const uint8_t* n = needle;
    for (size_t i = haystacklen - needlelen; i != (size_t)-1; i--) {
        if (h[i] == n[0] && memcmp(&h[i], n, needlelen) == 0) {
            return (void*)&h[i];
        }
    }
    return NULL;
}

static void* align_up(const void* p, size_t align) {
    uintptr_t addr = (uintptr_t)p;
    if (addr % align == 0) return (void*)p;
    return (void*)((addr + align - 1) & ~(align - 1));
}

static void* align_down(const void* p, size_t align) {
    uintptr_t addr = (uintptr_t)p;
    return (void*)(addr & ~(align - 1));
}

static size_t align_offset(size_t offset, size_t align) {
    if (offset % align == 0) return offset;
    return (offset + align - 1) & ~(align - 1);
}

static uint16_t read_u16(const uint8_t* p, uint8_t e) {
    if (e == 2) return (p[0] << 8) | p[1];
    return (p[1] << 8) | p[0];
}

static uint32_t read_u32(const uint8_t* p, uint8_t e) {
    if (e == 2) return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    return (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}

static uint64_t read_u64(const uint8_t* p, uint8_t e) {
    uint64_t v = 0;
    if (e == 2) {
        for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    } else {
        for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (i * 8);
    }
    return v;
}

static int32_t read_i32(const uint8_t* p, uint8_t e) {
    return (int32_t)read_u32(p, e);
}

static bool check_relative_base(uint64_t rb) {
    return ((rb & 0xffff000000000000ULL) == 0xffff000000000000ULL ||
            (rb & 0xffffffff00000000ULL) == 0xc000000000000000ULL);
}

static size_t decompress_string(uint8_t* p, const char* kallsyms_token_table,
                                const uint16_t* kallsyms_token_index,
                                char* output, uint8_t endian, bool is_built) {
  uint8_t count = *p;
  size_t output_length = 0;
  char* s = output;
  for (int i = 0; i < count; i++) {
    uint16_t tok_off;
    if (is_built) {
      tok_off = kallsyms_token_index[p[i + 1]];
    } else {
      tok_off = read_u16((const uint8_t*)&kallsyms_token_index[p[i + 1]], endian);
    }
    const char* token = kallsyms_token_table + tok_off;
    size_t token_length = strlen(token);
    output_length += token_length;
    if (s) {
      strcpy(s, token);
      s += token_length;
    }
  }
  if (s) {
    *s = 0;
  }
  return output_length;
}

int cheese_create_kallsyms_lookup(
    struct cheese_kallsyms_lookup* kallsyms_lookup, void* kernel_data,
    size_t kernel_length) {

  memset(kallsyms_lookup, 0, sizeof(*kallsyms_lookup));
  kallsyms_lookup->kernel_data = kernel_data;
  kallsyms_lookup->kernel_length = kernel_length;

  uint8_t* data = kernel_data;
  size_t size = kernel_length;

  // 1. Find token index and token table
  uint8_t* token_index_ptr = NULL;
  uint8_t* token_table_ptr = NULL;
  uint8_t* p;

  for (long i = 0; i < size - 512; i += 2) {
      // Check LE
      if (read_u16(data + i, 1) == 0) {
          bool possible = true;
          for (int j = 0; j < 255; j++) {
             uint16_t v1 = read_u16(data + i + j*2, 1);
             uint16_t v2 = read_u16(data + i + (j+1)*2, 1);
             if (v2 <= v1 || (v2 - v1) < 2 || (v2 - v1) > 128) { possible = false; break; }
          }
          if (possible) {
              // Try to find token table
              uint16_t last_idx = read_u16(data + i + 255*2, 1);
              // Search backwards for table start.
              // Optimization: T is likely aligned or close.
              // Let's scan backwards from index_ptr - last_idx - 512 to index_ptr.
              uint8_t* search_limit = data + i - last_idx - 4096;
              if (search_limit < data) search_limit = data;

              uint16_t idx1 = read_u16(data + i + 2, 1);
              uint16_t idx2 = read_u16(data + i + 4, 1);
              uint16_t idx3 = read_u16(data + i + 6, 1);

              for (uint8_t* t = data + i - last_idx; t > search_limit; t--) {
                  if (t[idx1 - 1] == 0 && t[idx2 - 1] == 0 && t[idx3 - 1] == 0) {
                      // Verify more?
                      bool match = true;
                      for(int k=1; k<10; k++) {
                          uint16_t idx = read_u16(data + i + k*2, 1);
                          if (t[idx-1] != 0) { match = false; break; }
                      }
                      if (match) {
                          token_index_ptr = data + i;
                          token_table_ptr = t;
                          kallsyms_lookup->endian = 1;
                          goto token_found;
                      }
                  }
              }
          }
      }
  }

  if (!token_index_ptr) {
      return 1;
  }

token_found:
  kallsyms_lookup->kallsyms_token_table = (char*)token_table_ptr;
  kallsyms_lookup->kallsyms_token_index = (uint16_t*)token_index_ptr;
  kallsyms_lookup->token_index_is_built = false;

  uint8_t* token_table = token_table_ptr;
  uint8_t* token_index = token_index_ptr;
  p = (uint8_t*)token_table;

  // Find markers
  uint8_t* markers = NULL;
  p = (uint8_t*)align_down(token_table, 4);
  uint8_t* limit = p - 1024 * 1024;
  if (limit < data) limit = data;

  while (p > limit) {
    p -= 4;
    if (*(uint32_t*)p == 0) {
      uint8_t* m = p;
      uint32_t v1_le = read_u32(m + 4, 1);
      uint32_t v2_le = read_u32(m + 8, 1);
      uint32_t v3_le = read_u32(m + 12, 1);

      bool match_le = (v1_le > 0x200 && v1_le < 0x40000 &&
                       v2_le > v1_le && (v2_le - v1_le) > 500 && (v2_le - v1_le) < 20000 &&
                       v3_le > v2_le && (v3_le - v2_le) > 500 && (v3_le - v2_le) < 20000);

      uint32_t v1_be = read_u32(m + 4, 2);
      uint32_t v2_be = read_u32(m + 8, 2);
      uint32_t v3_be = read_u32(m + 12, 2);

      bool match_be = (v1_be > 0x200 && v1_be < 0x40000 &&
                       v2_be > v1_be && (v2_be - v1_be) > 500 && (v2_be - v1_be) < 20000 &&
                       v3_be > v2_be && (v3_be - v2_be) > 500 && (v3_be - v2_be) < 20000);

      if (match_le && (kallsyms_lookup->endian == 0 || kallsyms_lookup->endian == 1)) {
        if (kallsyms_lookup->endian == 0) kallsyms_lookup->endian = 1;
        markers = p;
        break;
      }
      if (match_be && (kallsyms_lookup->endian == 0 || kallsyms_lookup->endian == 2)) {
        if (kallsyms_lookup->endian == 0) kallsyms_lookup->endian = 2;
        markers = p;
        break;
      }
    }
  }

  if (kallsyms_lookup->endian == 0) {
      kallsyms_lookup->endian = 1; // default little
  }

  // Find names and metadata
  uint8_t* search_end = (uint8_t*)align_down(token_table, 4);
  while (search_end > data && *(search_end-1) == 0) search_end--;

  p = (uint8_t*)align_down(token_table, 8);
  limit = p - 30 * 1024 * 1024;
  if (limit < data) limit = data;

  for (; p > limit; p -= 4) {
    uint32_t ns = read_u32(p, kallsyms_lookup->endian);
    if (ns < 20000 || ns > 2000000) continue;

    if (search_end < (p + 8) || (search_end - (p + 8)) < (ptrdiff_t)ns) {
         continue;
    }

    uint8_t* name_ptr = p + 8;
    if (*name_ptr == 0) {
        // Heuristic: First symbol name cannot be empty
        continue;
    }
    uint8_t* curr = name_ptr;
    bool ok = true;
    for (uint32_t i = 0; i < ns; i++) {
      if (curr >= token_table) { ok = false; break; }
      uint8_t len = *curr++;
      uint32_t count = len;
      if (len & 0x80) {
        if (curr >= token_table) { ok = false; break; }
        count = (len & 0x7f) | (*curr++ << 7);
      }
      curr += count;
    }

    if (!ok || curr > token_table || curr <= name_ptr) {
        continue;
    }

    // Try OLD layout
    if (p - 8 >= data) {
      uint64_t rb = read_u64(p - 8, kallsyms_lookup->endian);
      if (check_relative_base(rb)) {
        kallsyms_lookup->kallsyms_num_syms = ns;
        kallsyms_lookup->kallsyms_names = name_ptr;
        kallsyms_lookup->kallsyms_relative_base = rb;
        size_t offsets_size = ((ns * 4) + 7) & ~7;
        kallsyms_lookup->kallsyms_offsets = (int*)(p - 8 - offsets_size);
        goto found;
      }
    }

    // Try NEW layout (6.4+)
    if (token_index_ptr) {
      uint8_t* idx_end = token_index_ptr + 512;
      size_t idx_end_offset = idx_end - data;
      size_t offsets_start_offset = align_offset(idx_end_offset, 4);
      uint8_t* offsets_ptr = data + offsets_start_offset;

      if (offsets_ptr < data + size) {
        size_t offsets_len = ns * 4;
        uint8_t* offsets_end = offsets_ptr + offsets_len;
        size_t offsets_end_offset = offsets_end - data;
        size_t rb_offset = align_offset(offsets_end_offset, 8);
        uint8_t* rb_ptr = data + rb_offset;

        if (rb_ptr + 8 <= data + size) {
          uint64_t rb = read_u64(rb_ptr, kallsyms_lookup->endian);
          if (check_relative_base(rb)) {
            kallsyms_lookup->kallsyms_num_syms = ns;
            kallsyms_lookup->kallsyms_names = name_ptr;
            kallsyms_lookup->kallsyms_relative_base = rb;
            kallsyms_lookup->kallsyms_offsets = (int*)offsets_ptr;
            goto found;
          }
        }
      }
    }
  }

  return 1;

found:
  // Decompress all names
  kallsyms_lookup->decompressed_names = malloc(kallsyms_lookup->kallsyms_num_syms * sizeof(char*));

  p = (uint8_t*)kallsyms_lookup->kallsyms_names;
  for (uint32_t i = 0; i < kallsyms_lookup->kallsyms_num_syms; i++) {
    uint8_t entry_token_count = *p;
    size_t length = decompress_string(p, kallsyms_lookup->kallsyms_token_table,
                                      kallsyms_lookup->kallsyms_token_index, NULL,
                                      kallsyms_lookup->endian, kallsyms_lookup->token_index_is_built);
    char* s = malloc(length + 1);
    decompress_string(p, kallsyms_lookup->kallsyms_token_table,
                      kallsyms_lookup->kallsyms_token_index, s,
                      kallsyms_lookup->endian, kallsyms_lookup->token_index_is_built);
    kallsyms_lookup->decompressed_names[i] = s;
    p += entry_token_count + 1;
  }

  uint64_t efi_header_end_addr = cheese_kallsyms_lookup(kallsyms_lookup, "efi_header_end");
  if (efi_header_end_addr) {
    kallsyms_lookup->text_base = efi_header_end_addr - 0x10000;
  } else {
    uint64_t text_addr = cheese_kallsyms_lookup(kallsyms_lookup, "_text");
    if (!text_addr) {
      return 1;
    }
    kallsyms_lookup->text_base = text_addr;
  }
  return 0;
}

uint64_t cheese_kallsyms_lookup(struct cheese_kallsyms_lookup* kallsyms_lookup,
                                const char* name) {
  for (uint32_t i = 0; i < kallsyms_lookup->kallsyms_num_syms; i++) {
    if (strcmp(kallsyms_lookup->decompressed_names[i] + 1, name) == 0) {
      int32_t offset = read_i32((uint8_t*)&kallsyms_lookup->kallsyms_offsets[i], kallsyms_lookup->endian);
      if (offset < 0) {
        return kallsyms_lookup->kallsyms_relative_base - 1 - offset;
      } else {
        return kallsyms_lookup->kallsyms_relative_base + offset;
      }
    }
  }
  return 0;
}

unsigned char init_cred_start_bytes_bin[] = {
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x01, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00};

uint64_t cheese_lookup_init_cred(
    struct cheese_kallsyms_lookup* kallsyms_lookup) {
  void* p =
      memmem_last(kallsyms_lookup->kernel_data, kallsyms_lookup->kernel_length,
                  init_cred_start_bytes_bin, sizeof(init_cred_start_bytes_bin));
  if (!p) {
    return 0;
  }
  return kallsyms_lookup->text_base + (p - kallsyms_lookup->kernel_data);
}

uint64_t cheese_decode_adrp(uint32_t instr, uint64_t pc) {
  uint32_t immhi = (instr >> 5) & ((1 << 19) - 1);
  uint32_t immlo = (instr >> 29) & 0b11;
  int64_t extended = ((int32_t)(immhi << 2 | immlo)) << 11 >> 11;
  int64_t off = extended << 12;
  return (pc & ~((1 << 12) - 1)) + off;
}

uint64_t cheese_lookup_selinux_state(
    struct cheese_kallsyms_lookup* kallsyms_lookup) {
  uint64_t sel_read_policy_addr =
      cheese_kallsyms_lookup(kallsyms_lookup, "sel_read_policy");
  if (!sel_read_policy_addr) {
    return 0;
  }

  uint64_t text_base = kallsyms_lookup->text_base;
  uint64_t sel_read_policy_off = sel_read_policy_addr - text_base;
  const uint32_t* instrs = kallsyms_lookup->kernel_data + sel_read_policy_off;
  uint64_t found_addr = 0;
  for (int i = 0; i < 0x100; i++) {
    uint32_t instr = instrs[i];
#define BL_MASK (0b111111 << 26)
#define BL_INST (0b100101 << 26)
#define ADRP_X0_MASK ((0b10011111 << 24) | (0b11111))
#define ADRP_X0_INST (0b10010000 << 24)
#define ADD_X0_MASK ((0b1111111111 << 22) | (0b1111111111))
#define ADD_X0_INST (0b1001000100 << 22)
    if ((instr & BL_MASK) == BL_INST) {
      return found_addr;
    } else if ((instr & ADRP_X0_MASK) == ADRP_X0_INST) {
      found_addr = cheese_decode_adrp(
          instr, sel_read_policy_addr + i * sizeof(uint32_t));
    } else if ((instr & ADD_X0_MASK) == ADD_X0_INST) {
      uint32_t imm = (instr >> 10) & ((1 << 12) - 1);
      found_addr += imm;
    }
  }

  return 0;
}

#ifndef KALLSYMS_LOOKUP_INCLUDE

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "kernel.dump";
  FILE* f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  off_t file_length = ftell(f);
  fseek(f, 0, SEEK_SET);
  void* kernel_data = malloc(file_length);
  fread(kernel_data, 1, file_length, f);
  fclose(f);
  struct cheese_kallsyms_lookup kallsyms_lookup;
  if (cheese_create_kallsyms_lookup(&kallsyms_lookup, kernel_data,
                                    file_length)) {
    return 1;
  }

  printf("writing kallsyms.txt...\n");
  FILE* out = fopen("kallsyms.txt", "w");
  if (!out) {
      perror("fopen kallsyms.txt");
      return 1;
  }

  for (unsigned int i = 0; i < kallsyms_lookup.kallsyms_num_syms; i++) {
      int32_t offset = read_i32((uint8_t*)&kallsyms_lookup.kallsyms_offsets[i], kallsyms_lookup.endian);
      uint64_t addr;
      if (offset < 0) {
        addr = kallsyms_lookup.kallsyms_relative_base - 1 - offset;
      } else {
        addr = kallsyms_lookup.kallsyms_relative_base + offset;
      }
      char type = kallsyms_lookup.decompressed_names[i][0];
      char* name = kallsyms_lookup.decompressed_names[i] + 1;
      fprintf(out, "%llx %c %s\n", (unsigned long long)addr, type, name);
  }
  fclose(out);
  printf("wrote kallsyms.txt\n");
}

#endif
