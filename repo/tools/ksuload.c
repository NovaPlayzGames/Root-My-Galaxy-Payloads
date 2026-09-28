/*
 * ksuload.c — self-contained KernelSU LKM loader for the Root My Galaxy
 * helper flow.
 *
 * The helper (su_daemon run_kernelsu_late_load) bind-mounts this binary over
 * /system/bin/logcat inside a private mount namespace and execs it with
 * "late-load" argv.  argv is ignored; we load the embedded no-patch-text
 * kernelsu.ko via kallsyms-resolved symbol pre-binding + finit_module, the
 * same technique ksud's manual loader uses for unexported symbols.
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "mod_blob.inc"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001
#endif

static void out(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) write(STDERR_FILENO, buf, (size_t)n);
}

/* --- tiny symbol hashmap: name -> address --- */
#define KSYM_BUCKETS (1u << 18)
static char **ksym_names;
static uint64_t *ksym_addrs;

static uint32_t khash(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
  return (uint32_t)h;
}

static void ksym_put(const char *name, uint64_t addr) {
  uint32_t i = khash(name) & (KSYM_BUCKETS - 1);
  while (ksym_names[i]) {
    if (strcmp(ksym_names[i], name) == 0) return; /* keep first (text) */
    i = (i + 1) & (KSYM_BUCKETS - 1);
  }
  ksym_names[i] = strdup(name);
  ksym_addrs[i] = addr;
}

static uint64_t ksym_get(const char *name) {
  uint32_t i = khash(name) & (KSYM_BUCKETS - 1);
  while (ksym_names[i]) {
    if (strcmp(ksym_names[i], name) == 0) return ksym_addrs[i];
    i = (i + 1) & (KSYM_BUCKETS - 1);
  }
  return 0;
}

static int module_loaded(void) {
  int f = open("/proc/modules", O_RDONLY);
  if (f < 0) return 0;
  char buf[8192];
  int found = 0;
  ssize_t n;
  while ((n = read(f, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = 0;
    if (strstr(buf, "kernelsu")) { found = 1; break; }
  }
  close(f);
  return found;
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  out("[*] ksuload: self-contained KernelSU loader starting\n");

  int dryrun = getenv("KSULOAD_DRYRUN") != NULL;
  if (module_loaded() && !dryrun) {
    out("[+] kernelsu already loaded\n");
    return 0;
  }

  /* live kallsyms need real addresses */
  int f = open("/proc/sys/kernel/kptr_restrict", O_WRONLY);
  if (f >= 0) { write(f, "0", 1); close(f); }

  /* parse /proc/kallsyms into the hashmap */
  ksym_names = calloc(KSYM_BUCKETS, sizeof(char *));
  ksym_addrs = calloc(KSYM_BUCKETS, sizeof(uint64_t));
  if (!ksym_names || !ksym_addrs) { out("[-] oom\n"); return 2; }

  FILE *ks = fopen("/proc/kallsyms", "r");
  if (!ks) { out("[-] kallsyms: %s\n", strerror(errno)); return 3; }
  char line[512];
  unsigned long long addr;
  char type, name[256];
  unsigned n_syms = 0;
  while (fgets(line, sizeof(line), ks)) {
    if (sscanf(line, "%llx %c %255s", &addr, &type, name) == 3 && addr) {
      ksym_put(name, addr);
      n_syms++;
    }
  }
  fclose(ks);
  out("[*] kallsyms: %u symbols\n", n_syms);

  /* make a private mutable copy of the module image */
  size_t sz = sizeof(mod_blob);
  uint8_t *img = malloc(sz);
  if (!img) { out("[-] oom img\n"); return 4; }
  memcpy(img, mod_blob, sz);

  Elf64_Ehdr *eh = (Elf64_Ehdr *)img;
  if (memcmp(eh->e_ident, ELFMAG, 4) || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
      eh->e_machine != EM_AARCH64 || eh->e_type != ET_REL) {
    out("[-] not an aarch64 relocatable\n");
    return 5;
  }
  Elf64_Shdr *sh = (Elf64_Shdr *)(img + eh->e_shoff);

  Elf64_Shdr *symtab = NULL, *strtab = NULL;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (sh[i].sh_type == SHT_SYMTAB) { symtab = &sh[i]; strtab = &sh[symtab->sh_link]; break; }
  }
  if (!symtab) { out("[-] no symtab\n"); return 6; }

  Elf64_Sym *syms = (Elf64_Sym *)(img + symtab->sh_offset);
  const char *strs = (const char *)(img + strtab->sh_offset);
  int cnt = symtab->sh_size / sizeof(Elf64_Sym);

  unsigned resolved = 0, missing = 0;
  for (int i = 0; i < cnt; i++) {
    if (syms[i].st_shndx != SHN_UNDEF || syms[i].st_name == 0) continue;
    const char *sn = strs + syms[i].st_name;
    uint64_t a = ksym_get(sn);
    if (!a) {
      if (ELF64_ST_BIND(syms[i].st_info) == STB_WEAK) continue; /* weak undef = 0, fine */
      missing++;
      if (missing <= 15) out("[-] unresolved: %s\n", sn);
      continue;
    }
    syms[i].st_shndx = SHN_ABS;
    syms[i].st_value = a;
    resolved++;
  }
  out("[*] pre-resolved %u imports, %u missing\n", resolved, missing);
  if (missing) { out("[-] refusing load with unresolved imports\n"); return 7; }

  if (dryrun) {
    out("[*] dryrun: imports resolved, skipping finit_module\n");
    return 0;
  }

  int mfd = memfd_create("kernelsu", MFD_CLOEXEC);
  if (mfd < 0) { out("[-] memfd: %s\n", strerror(errno)); return 8; }
  size_t off = 0;
  while (off < sz) {
    ssize_t w = write(mfd, img + off, sz - off);
    if (w <= 0) { out("[-] memfd write: %s\n", strerror(errno)); return 9; }
    off += w;
  }
  free(img);

  if (syscall(SYS_finit_module, mfd, "", 0) != 0) {
    out("[-] finit_module: %s (%d)\n", strerror(errno), errno);
    return 10;
  }
  close(mfd);

  if (!module_loaded()) {
    out("[-] module absent after load\n");
    return 11;
  }
  out("[+] kernelsu module loaded\n");
  return 0;
}
