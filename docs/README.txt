PeteleOS Architecture Documentation -- Entry Point
==================================================

PURPOSE
This directory (docs/) is the architectural knowledge base for PeteleOS.
It maps the repository hierarchically:

  Repository (Level 0)
    -> Subsystem (Level 1)
      -> Component (Level 2)
        -> Module (Level 3)
          -> File (Level 4)
            -> Symbol (Level 5)
              -> Dependency (Level 6)
                -> Control / Data / Runtime Flow (Level 7)

It is intended for developers and AI coding agents that need to understand
WHERE a responsibility lives, HOW components interact, WHY dependencies exist,
WHEN initialization happens, and WHERE safe vs high-risk modification
boundaries are.

LANGUAGE AND FORMAT
All documentation files are English, plain .txt. Source identifiers, paths,
function names, macros and types are preserved verbatim and never translated.

HIERARCHY AND READING ORDER
Recommended order for a newcomer:

  1. README.txt                 -- this file
  2. 00_repository_overview.txt -- Level 0, what PeteleOS is
  3. 01_architecture.txt        -- system-wide layering and boundaries
  4. 02_source_tree.txt         -- directory-by-directory inventory
  5. 03_dependency_model.txt    -- inter-subsystem dependency matrix
  6. 04_build_system.txt        -- xmake/Make, generation, ordering
  7. 05_boot_and_initialization.txt
  8. 06_runtime_flows.txt
  9. 07_data_flow.txt
  10. 16_architecture_support.txt -- amd64 / arm64 / riscv64
  11. 20_openbsd_relationship.txt
  12. 19_generated_code.txt
  13. subsystem files in docs/subsystems/ for the area being changed
  14. 08..15 topic files (memory, FS, device, net, userland, gui, ...)
  15. 21_refactoring_map.txt    -- risk map before refactoring
  16. 22_code_navigation.txt    -- AI-agent workflow

FILE INDEX
  docs/README.txt
  docs/00_repository_overview.txt
  docs/01_architecture.txt
  docs/02_source_tree.txt
  docs/03_dependency_model.txt
  docs/04_build_system.txt
  docs/05_boot_and_initialization.txt
  docs/06_runtime_flows.txt
  docs/07_data_flow.txt
  docs/08_memory_and_process_model.txt
  docs/09_filesystem_and_storage.txt
  docs/10_device_and_driver_model.txt
  docs/11_networking.txt
  docs/12_userland.txt
  docs/13_gui.txt
  docs/14_installer.txt
  docs/15_configuration.txt
  docs/16_architecture_support.txt
  docs/17_testing.txt
  docs/18_resources.txt
  docs/19_generated_code.txt
  docs/20_openbsd_relationship.txt
  docs/21_refactoring_map.txt
  docs/22_code_navigation.txt
  docs/subsystems/kernel.txt
  docs/subsystems/libraries.txt
  docs/subsystems/include.txt
  docs/subsystems/userland.txt
  docs/subsystems/gui.txt
  docs/subsystems/installer.txt
  docs/subsystems/configs.txt
  docs/subsystems/tools.txt
  docs/subsystems/resources.txt
  docs/subsystems/tests.txt

TERMINOLOGY
  Subsystem  -- top-level directory (kernel/, userland/, libraries/, ...)
  Component  -- coherent functional area inside a subsystem (e.g. UVM)
  Module     -- group of source files with a clear responsibility
  Symbol     -- function, struct, macro, variable, enum
  MI         -- machine-independent (shared across architectures)
  MD         -- machine-dependent (architecture-specific)
  GENERIC    -- default kernel configuration (kernel/core/conf/GENERIC)
  files.*    -- per-architecture file lists consumed by config(8)

SOURCE REFERENCE CONVENTION
  Paths are repository-relative: kernel/core/kern/init_main.c, not absolute.
  Where a line range is verified it is listed; otherwise only the file is
  cited. Do not invent line numbers.

VERIFICATION DISCIPLINE
  VERIFIED FACT    -- observed directly in source / build files / headers
  INFERRED         -- deduced from structure, marked INFERRED FROM SOURCE STRUCTURE
  UNKNOWN / NOT VERIFIED -- cannot be confirmed from current evidence

MAINTENANCE RULES
  - When source changes, review the affected doc file(s).
  - Remove obsolete information; do not leave stale paths.
  - Add new modules to 02_source_tree.txt and the relevant subsystem file.
  - Update dependency changes in 03_dependency_model.txt.
  - Update build changes in 04_build_system.txt.
  - Update generated-code changes in 19_generated_code.txt.
  - Update runtime-flow changes in 05/06/07.

NO SOURCE CODE IS MODIFIED BY THIS DOCUMENTATION TASK.
All changes are confined to docs/.
