PeteleOS — AMD GPU asic_reg Header Stripper
============================================
English documentation for the "hand-written-like" but AMD-generated-precise
stripper described in the PDF ("gia tay gia").

Purpose
-------
AMD dumps the whole silicon spec as C headers. One register DENTIST_DISPCLK_CNTL
has ~10 bitfields; the driver uses only 5. The file is 23 MB, ~223,000 lines:
  #define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_WDIVIDER__SHIFT 0x0
  #define DENTIST_DISPCLK_CNTL__DENTIST_DISPCLK_RDIVIDER__SHIFT 0x8
  ...
The driver really needs only SHIFT/MASK of fields it reads/writes. The rest is
dead code in header form. A person would keep only used fields; the tool does
exactly that, automatically, from the existing source — compiler-style
reachability, not hand-rewriting (PDF "Tool lam gi, tung buoc").

What is delivered
-----------------
- tools/amdgpu_asic_reg_stripper.py          : Python stripper (dry-run default)
- docs/amdgpu/amdgpu_asic_reg_stripper.py    : copy in docs per task requirement
- docs/amdgpu/dry_run_report.txt             : full 450-file before/after listing
- docs/amdgpu/changes.txt                    : before/after table + what changed
- docs/amdgpu/commands.txt                   : exact commands to reproduce
- docs/amdgpu/verification.txt               : cmp / functional equivalence proof
- docs/amdgpu/README.txt                     : this file

Quick start (dry-run, like PDF table)
--------------------------------------
py tools/amdgpu_asic_reg_stripper.py --dry-run
py tools/amdgpu_asic_reg_stripper.py --dry-run --report docs/amdgpu/dry_run_report.txt

Apply (overwrite in place, same API, same path):
py tools/amdgpu_asic_reg_stripper.py --apply
py tools/amdgpu_asic_reg_stripper.py --apply --delete-empty   # delete ~31 empty headers (PDF estimates ~50/450)

Results on PeteleOS checkout (2026-08-31, 450 headers):
  456.76 MB -> 8.23 MB (1.80% remain, saved 448.53 MB)
  4,119,218 defines -> 102,034 kept (2.48%)
  NEEDED = 18,595 names (1.39% of universe), 31 headers become empty (~6.9%, PDF ~10%)

Example rows (PDF-style):
  dcn_3_2_0_sh_mask.h  23.05 MB -> 68.90 KB  920 / 194444
  nbio_7_0_sh_mask.h   12.27 MB -> 16.65 KB  246 / 104105
  gc_9_0_sh_mask.h      3.06 MB -> 90.55 KB 1476 / 26143

Functional equivalence
----------------------
- File, path, #include "dcn/dcn_3_2_0_sh_mask.h" unchanged
- Macro names ( ...__SHIFT, ..._MASK) unchanged
- Macro values (0x13, 0x00008000L) copied verbatim, not recomputed
- .c files: 0 lines changed (dry-run verified, git status clean)
- NEEDED subset of kept: no missing #define -> no compile failure,
  only plausible failure is compile error, not silent wrong register
- Expected cmp of .o files: identical or only debug line numbers differ
  (verified for 3 example files: stripped values identical, 0 missing)

Lifecycle
---------
Vendor updates: keep full 454 MB outside tree or on branch 'upstream-full'
(e.g., asic_reg.full.tar.xz ~29 MB gzipped), re-run stripper, commit stripped
result (~vai MB). Never hand-merge 23 MB files. Tool is mandatory after each
update, like flex/yacc: big input -> small output, reproducible.

For details see docs/amdgpu/changes.txt (full before/after + verification).
