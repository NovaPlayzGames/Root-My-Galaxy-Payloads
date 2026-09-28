# ksuload — self-contained KernelSU LKM loader

Replaces the `ksud` late-load artifact for targets where the upstream ksud's
embedded module is unsafe (e.g. live-text-patching builds panic under Samsung
EL2/RKP/DEFEX).

The root helper bind-mounts this binary over `/system/bin/logcat` in a private
mount namespace and execs it as `logcat late-load ...`. argv is ignored; the
loader:

1. drops `kptr_restrict` to 0 for live kallsyms;
2. parses the embedded `.ko` symtab and pre-binds every undefined import to its
   kallsyms address (`st_shndx=SHN_ABS`, `st_value=addr`) — the standard manual
   relocation trick, required because SELinux internals (`policydb_*`,
   `avtab_*`, `avc_*`) are unexported;
3. writes the patched image to a memfd and calls `finit_module`;
4. exits 0 on success or when kernelsu is already loaded.

## Rebuild

```sh
python3 - <<'PY'
data = open('path/to/kernelsu.ko','rb').read()
with open('mod_blob.inc','w') as f:
    f.write('static const unsigned char mod_blob[] = {')
    for i in range(0,len(data),16):
        f.write(''.join('%d,'%b for b in data[i:i+16]))
    f.write('};')
PY
clang --target=aarch64-linux-android31 -static -Oz -o ksuload ksuload.c
```

For `e3q-S928USQS6DZH3` the embedded module is
`kernelsu/android14-6.1_kernelsu-e3q-S928BXXS6DZF2-kdp.ko` (no-patch-text e3q
build) with `vermagic` byte-patched `abS928BXXS6DZF2` -> `abS928USQS6DZH3`.

`KSULOAD_DRYRUN=1` resolves imports and reports coverage without loading.
