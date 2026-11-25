local opt_arch = fab.option("arch", { "x86_64", "riscv64" }) or "x86_64"
local opt_build_type = fab.option("buildtype", { "debug", "release" }) or "debug"

local c = require("lang_c")
local linker = require("ld")

local clang = c.get_clang()
assert(clang ~= nil, "No clang compiler found")

local ld = linker.get_linker("ld.lld")
assert(ld ~= nil, "No ld.lld found")

local python = fab.which("python3")
assert(python ~= nil, "No python3 found")

local prekernel_protocol = fab.dependency("prekernel-protocol")
local flanterm = fab.dependency("flanterm")
local uacpi = fab.dependency("uacpi")
local zstd = fab.dependency("zstd")
local libfdt = opt_arch == "riscv64" and fab.dependency("libfdt") or nil

local zstd_gen_rule = fab.define_rule(
    "gen_zstd_decoder",
    python.path:gsub("@", "@@") .. " @IN@ " .. tostring(zstd) .. " @OUT@",
    "Generating freestanding zstd decoder @OUT@"
)

local zstd_gen_script = fab.resolve_source(fab.project_dir / "scripts/zstd/gen-decoder.py")
local zstd_decoder = zstd_gen_rule:build("zstddeclib.c", { zstd_gen_script }, {}, {
    fab.resolve_source(zstd / "lib/common/zstd_deps.h"),
    fab.resolve_source(zstd / "lib/common/xxhash.h"),
    fab.resolve_source(zstd / "build/single_file_libs/zstddeclib-in.c"),
    fab.resolve_source(zstd / "build/single_file_libs/combine.py"),
})

-- @brief Return a copy of `flags` with `extra` appended.
local function with_flags(flags, extra)
    local result = {}
    table.extend(result, flags)
    table.extend(result, extra)
    return result
end

local function source_glob(pattern)
    return sources(fab.project_dir:glob(pattern))
end

local base_flags = {
    "-std=gnu23",
    "-ffreestanding",
    "-fno-strict-aliasing",
    "-Wimplicit-fallthrough",
    "-Wmissing-field-initializers",
    "-fdiagnostics-color=always",
    "-DUACPI_OVERRIDE_CONFIG",
}

if opt_build_type == "debug" then
    table.extend(base_flags, { "-Og", "-g", "-fno-lto", "-fno-omit-frame-pointer", "-D__DEBUG__" })
else
    table.extend(base_flags, { "-O3", "-flto", "-g", "-D__RELEASE__" })
end

local target_flags
if opt_arch == "riscv64" then
    target_flags = {
        "--target=riscv64-none-elf",
        "-march=rv64imac_zicsr_zifencei_zihintpause",
        "-mabi=lp64",
        "-mcmodel=medlow",
    }
else
    target_flags = {
        "--target=x86_64-none-elf",
        "-mno-red-zone",
        "-mgeneral-regs-only",
        "-mabi=sysv",
        "-mcmodel=kernel",
    }
end

local warning_flags = {
    "-Wall",
    "-Wextra",
    "-Wvla",
    "-Werror",
    "-Wno-language-extension-token",
    "-Wno-gnu-zero-variadic-macro-arguments",
    "-Wno-gnu-statement-expression-from-macro-expansion",
    "-Wno-extra-semi",
    "-Wno-error=unused-function",
    "-Wmissing-prototypes",
    "-Wdocumentation",
    "-Wmissing-noreturn",
}

local kernel_base_flags = with_flags(base_flags, target_flags)
local kernel_flags = with_flags(kernel_base_flags, warning_flags)

if opt_build_type == "debug" then
    table.extend(kernel_flags, { "-fsanitize=undefined", "-fstack-protector-all" })
else
    table.extend(kernel_flags, { "-fstack-protector-strong" })
end

local flanterm_flags = with_flags(kernel_base_flags, { "-DFLANTERM_FB_DISABLE_BUMP_ALLOC" })
local zstd_kernel_flags = with_flags(kernel_base_flags, {
    "-Os",
    "-g0",
    "-fomit-frame-pointer",
    "-fno-sanitize=undefined",
    "-fno-stack-protector",
})

local function include_dirs(extra)
    local result = {
        c.include_dir(fab.project_dir / "kernel/include"),
        c.include_dir(fab.project_dir / "third_party"),
        c.include_dir(prekernel_protocol / "pre_kernel/public"),
        c.include_dir(flanterm / "src"),
        c.include_dir(uacpi / "include"),
        c.include_dir(zstd / "lib"),
    }
    if libfdt ~= nil then
        table.extend(result, { c.include_dir(libfdt / "libfdt"), c.include_dir(libfdt) })
    end
    table.extend(result, extra)
    return result
end

local kernel_include_dirs = include_dirs({})

local function compile_sources(srcs, flags, directories, prefix)
    local objects = {}
    for _, source in ipairs(srcs) do
        local name = prefix .. generator_artifact_name(source) .. ".o"
        table.insert(objects, clang:compile_object(name, source, directories, flags, name .. ".d"))
    end
    return objects
end

local common_sources = {}
for _, pattern in ipairs({
    "kernel/src/common/**/*.c",
    "kernel/src/fdt/**/*.c",
    "kernel/src/init/**/*.c",
    "kernel/src/lib/**/*.c",
    "kernel/src/memory/**/*.c",
    "kernel/src/runtime/**/*.c",
}) do
    table.extend(common_sources, source_glob(pattern))
end

local arch_sources = source_glob("kernel/src/arch/" .. opt_arch .. "/**/*.c")
local arch_asm_sources = source_glob("kernel/src/arch/" .. opt_arch .. "/**/*.S")
local uacpi_sources = sources(uacpi:glob("source/*.c"))
local flanterm_sources = sources(flanterm:glob("src/*.c"))

local kernel_sources = {}
table.extend(kernel_sources, common_sources)
table.extend(kernel_sources, arch_sources)
table.extend(kernel_sources, arch_asm_sources)
table.extend(kernel_sources, uacpi_sources)

local objects = compile_sources(kernel_sources, kernel_flags, kernel_include_dirs, "")
table.extend(objects, compile_sources(flanterm_sources, flanterm_flags, { c.include_dir(flanterm / "src") }, ""))
table.insert(objects,
    clang:compile_object("zstddeclib.o", zstd_decoder, kernel_include_dirs, zstd_kernel_flags, "zstddeclib.o.d"))

local kernel = ld:link("kernel.elf", objects, {
    "-znoexecstack",
}, fab.resolve_source(fab.project_dir / ("support/" .. opt_arch .. ".lds")))

return {
    ["kernel.elf"] = kernel,
}
