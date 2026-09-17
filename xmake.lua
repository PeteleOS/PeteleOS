--[[
PeteleOS: root xmake.lua, time write: 2026/07/26
This file uses the Apache-2.0 license
--]]

--[[
This file does ONLY project-wide orchestration.
NOTE ON LOCATION: this file lives at the repository ROOT. OS calls the
whole source tree "src" (OpenBSD convention, as in /usr/src), but there is
no literal src/ subdirectory in this repository -- tools/, include/,
libraries/, kernel/, resources/, userland/, gui/, installer/, tests/,
configs/ and docs/ already live directly at the repo root.
--]]

-- Require a reasonably recent xmake so every API used across this project
-- (custom toolchains, mode.releasedbg/minsizerel, LTO/sanitizer policies,
-- etc.) is guaranteed to exist. Bump this if you rely on something newer.
set_xmakever("3.0.9")


-- PROJECT IDENTITY
set_project("PeteleOS")

-- Placeholder version -- replace with real versioning scheme once
-- one is decided; this only needs to be *some* valid semver for now.
set_version("0.1.0", {build = "%Y%m%d"})


-- GLOBAL POLICIES
-- We are cross-compiling an entire OS: a flag that xmake silently drops
-- because it "looks unsupported" is a much worse failure mode here than a
-- loud build error, so auto-ignoring is turned off project-wide.
set_policy("check.auto_ignore_flags", false)

-- Stream compiler/linker warnings as they happen instead of only at the end.
set_policy("build.warning", true)

-- Object-file build cache (ccache-style). No add_requires()
-- package dependencies, so the separate package-lock policies do not
-- apply here.
set_policy("build.ccache", true)


-- BUILD MODES
-- Required: debug, release. Extended: releasedbg, minsizerel. Per-mode
-- optimize/symbols levels come from these built-in rules; nothing here
-- overrides them -- that stays out of the root file entirely.
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.minsizerel")


-- DIRECTORIES: build / install / package / cache
-- Keep every generated artifact under build/, never inside the source tree.
set_config("builddir", "$(projectdir)/build")

-- Configurable install root; override any time with `xmake f --installdir=`.
set_config("installdir", "$(projectdir)/build/install")

-- xmake's own package cache/install locations are normally read from the
-- XMAKE_PKG_CACHEDIR / XMAKE_PKG_INSTALLDIR environment variables at
-- process start-up. No add_requires() package dependencies
-- (see tools/compiler.lua), so these paths are never actually consulted --
-- and as of xmake 3.0.9, os.setenv() is no longer callable at description
-- scope at all (confirmed: it is not in the interpreter-scope os module,
-- core/sandbox/modules/interpreter/os.lua -- only script-scope callbacks
-- like on_load can call it, via the separate, broader sandbox os module).
-- If a future revision of this project DOES start using add_requires(),
-- point XMAKE_PKG_CACHEDIR / XMAKE_PKG_INSTALLDIR at project-local folders
-- from the calling shell/CI instead (this can no longer be done from
-- inside xmake.lua itself).


-- SHARED INCLUDE PATH
-- include/ holds the headers shared across kernel, libraries and userland
-- (per README.md).
add_includedirs("$(projectdir)/include")


-- SUBMODULES
-- Fixed dependency order mandated by the build spec -- do not reorder
-- without a clear technical reason. "tools" is loaded first because it now
-- owns every toolchain/option/compiler/rule declaration that every other
-- module depends on. Each module is only pulled in once its OWN xmake.lua
-- actually exists, so this file keeps working while modules are converted
-- from BSD Make one at a time instead of all at once.
local modules = {
    "tools",
    "include",
    "libraries",
    "kernel",
    "resources",
    "userland",
    "gui",
    "installer",
    "tests",
    "configs",
    "docs",
}

for _, mod in ipairs(modules) do
    local modfile = path.join(os.scriptdir(), mod, "xmake.lua")
    if os.isfile(modfile) then
        includes(mod)
    else
        print(string.format("os: skipping '%s' (no xmake.lua there yet)", mod))
    end
end

