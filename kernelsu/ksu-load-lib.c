/*
 * ksu-load: load a Samsung KernelSU .ko whose undefined symbols were trimmed
 * from the kernel export table (CONFIG_TRIM_UNUSED_KSYMS + MODULE_FORCE_LOAD=n).
 *
 * Instead of forcing the module in, each undefined symbol is resolved against
 * /proc/kallsyms and rewritten in the module's .symtab as an absolute
 * (SHN_ABS) symbol. The kernel then applies the module's own relocations
 * against those absolute addresses, so no relocated symbol stays unresolved
 * and the manual __versions (CRC) contract is not needed.
 *
 * Requires root with /proc/kallsyms addresses visible
 * (kptr_restrict is set to 0 by this tool when it can).
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define KSYM_BUCKETS (1u << 19)

struct ksym {
  const char *name;
  unsigned long long addr;
};

static struct ksym *ksyms;
static unsigned ksym_used;

static unsigned long ksym_hash(const char *s) {
  unsigned long h = 5381;
  while (*s) {
    h = (h * 33) ^ (unsigned char)*s++;
  }
  return h;
}

static void ksym_put(const char *name, unsigned long long addr) {
  unsigned i = ksym_hash(name) & (KSYM_BUCKETS - 1);
  for (unsigned probe = 0; probe < KSYM_BUCKETS; probe++) {
    unsigned slot = (i + probe) & (KSYM_BUCKETS - 1);
    if (!ksyms[slot].name) {
      ksyms[slot].name = name;
      ksyms[slot].addr = addr;
      ksym_used++;
      return;
    }
    if (strcmp(ksyms[slot].name, name) == 0) {
      return; /* first definition wins */
    }
  }
}

static unsigned long long ksym_get(const char *name) {
  unsigned i = ksym_hash(name) & (KSYM_BUCKETS - 1);
  for (unsigned probe = 0; probe < KSYM_BUCKETS; probe++) {
    unsigned slot = (i + probe) & (KSYM_BUCKETS - 1);
    if (!ksyms[slot].name) {
      return 0;
    }
    if (strcmp(ksyms[slot].name, name) == 0) {
      return ksyms[slot].addr;
    }
  }
  return 0;
}

static int load_kallsyms(void) {
  ksyms = calloc(KSYM_BUCKETS, sizeof(*ksyms));
  if (!ksyms) {
    return 0;
  }
  FILE *fp = fopen("/proc/kallsyms", "r");
  if (!fp) {
    perror("open /proc/kallsyms");
    return 0;
  }
  char line[512];
  unsigned long long addr;
  char type;
  char name[256];
  while (fgets(line, sizeof(line), fp)) {
    if (sscanf(line, "%llx %c %255s", &addr, &type, name) != 3) {
      continue;
    }
    (void)type;
    ksym_put(strdup(name), addr);
  }
  fclose(fp);
  fprintf(stderr, "[ksu-load] kallsyms entries: %u\n", ksym_used);
  return 1;
}

static void reveal_kallsyms(void) {
  int fd = open("/proc/sys/kernel/kptr_restrict", O_WRONLY);
  if (fd >= 0) {
    if (write(fd, "0", 1) != 1) {
      fprintf(stderr, "[ksu-load] kptr_restrict write failed errno=%d\n", errno);
    }
    close(fd);
  }
}

static int patch_module(const char *in_path, const char *out_path,
                        unsigned char **image_out, size_t *size_out) {
  int fd = open(in_path, O_RDONLY);
  if (fd < 0) {
    perror("open module");
    return 0;
  }
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    perror("stat module");
    close(fd);
    return 0;
  }
  size_t size = (size_t)st.st_size;
  unsigned char *image = malloc(size);
  if (!image) {
    close(fd);
    return 0;
  }
  size_t got = 0;
  while (got < size) {
    ssize_t n = read(fd, image + got, size - got);
    if (n <= 0) {
      fprintf(stderr, "[ksu-load] short read\n");
      free(image);
      close(fd);
      return 0;
    }
    got += (size_t)n;
  }
  close(fd);

  Elf64_Ehdr *eh = (Elf64_Ehdr *)image;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
      eh->e_ident[EI_CLASS] != ELFCLASS64 ||
      eh->e_machine != EM_AARCH64) {
    fprintf(stderr, "[ksu-load] not an aarch64 ELF64 module\n");
    free(image);
    return 0;
  }

  Elf64_Shdr *sh = (Elf64_Shdr *)(image + eh->e_shoff);
  const char *shstr = (const char *)(image + sh[eh->e_shstrndx].sh_offset);
  Elf64_Sym *symtab = NULL;
  const char *strtab = NULL;
  size_t nsyms = 0;

  for (unsigned i = 0; i < eh->e_shnum; i++) {
    if (sh[i].sh_type != SHT_SYMTAB) {
      continue;
    }
    symtab = (Elf64_Sym *)(image + sh[i].sh_offset);
    nsyms = sh[i].sh_size / sizeof(Elf64_Sym);
    if (sh[i].sh_link < eh->e_shnum) {
      strtab = (const char *)(image + sh[sh[i].sh_link].sh_offset);
    }
    break;
  }
  if (!symtab || !strtab) {
    fprintf(stderr, "[ksu-load] no .symtab/.strtab\n");
    free(image);
    return 0;
  }
  (void)shstr;

  unsigned patched = 0;
  unsigned missing = 0;
  for (size_t i = 0; i < nsyms; i++) {
    Elf64_Sym *s = &symtab[i];
    if (s->st_shndx != SHN_UNDEF || s->st_name == 0) {
      continue;
    }
    const char *name = strtab + s->st_name;
    if (!name[0]) {
      continue;
    }
    unsigned long long addr = ksym_get(name);
    if (!addr) {
      fprintf(stderr, "[ksu-load] unresolved: %s\n", name);
      missing++;
      continue;
    }
    s->st_value = addr;
    s->st_shndx = SHN_ABS;
    patched++;
  }
  fprintf(stderr, "[ksu-load] resolved=%u unresolved=%u\n", patched, missing);
  if (missing) {
    free(image);
    return 0;
  }

  int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (out >= 0) {
    ssize_t n = write(out, image, size);
    if (n != (ssize_t)size) {
      fprintf(stderr, "[ksu-load] patched write short\n");
    }
    close(out);
  }

  *image_out = image;
  *size_out = size;
  return 1;
}

static int ksu_load_run(const char *module_path, const char *out_path) {

  reveal_kallsyms();
  if (!load_kallsyms()) {
    return 1;
  }

  unsigned char *image = NULL;
  size_t size = 0;
  if (!patch_module(module_path, out_path, &image, &size)) {
    return 1;
  }

  errno = 0;
  long ret = syscall(SYS_init_module, image, size, "");
  fprintf(stderr, "[ksu-load] init_module ret=%ld errno=%d (%s)\n", ret, errno,
          strerror(errno));
  return ret == 0 ? 0 : 1;
}

/* DEFEX only allowlists /system/bin executables, so this loader is used as
 * LD_PRELOAD into an allowlisted binary (e.g. /system/bin/id) and runs from
 * the constructor. Module path comes from KSU_MODULE. */
__attribute__((constructor)) static void ksu_load_ctor(void) {
  const char *module = getenv("KSU_MODULE");
  if (!module || !*module) {
    module = "/data/local/tmp/kernelsu-rmg.ko";
  }
  const char *out = getenv("KSU_PATCHED");
  if (!out || !*out) {
    out = "/data/local/tmp/ksu-loaded.ko";
  }
  ksu_load_run(module, out);
}
