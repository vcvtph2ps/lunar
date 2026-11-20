local opt_arch = fab.option("arch", { "x86_64", "riscv64" }) or "x86_64"
local opt_build_type = fab.option("buildtype", { "debug", "release" }) or "debug"

local c = require("lang_c")
local asm = require("lang_nasm")
local linker = require("ld")

local project_dir = fab.project_dir

local clang = c.get_clang()
assert(clang ~= nil, "No clang compiler found")

local ld = linker.get_linker("ld.lld")
assert(ld ~= nil, "No ld.lld found")

local prekernel_protocol = fab.dependency("prekernel-protocol")
local flanterm = fab.dependency("flanterm")
local uacpi = fab.dependency("uacpi")
local zstd = fab.dependency("zstd")

local python = fab.which("python3")
assert(python ~= nil, "No python3 found")

local zstd_gen_rule = fab.define_rule(
    "gen_zstd_decoder",
    python.path:gsub("@", "@@") .. " @IN@ " .. tostring(zstd) .. " @OUT@",
    "Generating freestanding zstd decoder @OUT@"
)
local zstd_gen_script = fab.resolve_source(project_dir / "scripts/zstd/gen-decoder.py")
local zstd_decoder = zstd_gen_rule:build("zstddeclib.c", { zstd_gen_script }, {}, {
    fab.resolve_source(zstd / "lib/common/zstd_deps.h"),
    fab.resolve_source(zstd / "lib/common/xxhash.h"),
    fab.resolve_source(zstd / "build/single_file_libs/zstddeclib-in.c"),
    fab.resolve_source(zstd / "build/single_file_libs/combine.py"),
})

local function get_kernel_objs(kernel_flags, zstd_flags)
    local kernel_sources = sources(project_dir:glob("kernel/src/**/*.c", "!kernel/src/arch/**"))
    table.extend(kernel_sources, sources(project_dir:glob("kernel/src/arch/" .. opt_arch .. "/**/*.c")))
    table.extend(kernel_sources, sources(project_dir:glob("kernel/src/arch/" .. opt_arch .. "/**/*.S")))
    table.extend(kernel_sources, sources(uacpi:glob("source/*.c")))

    local include_dirs = {
        c.include_dir(project_dir / "kernel/include"),
        c.include_dir(project_dir / "third_party"),
        c.include_dir(zstd / "lib"),
    }
    table.insert(include_dirs, c.include_dir(prekernel_protocol / "pre_kernel/public"))
    table.insert(include_dirs, c.include_dir(flanterm / "src"))
    table.insert(include_dirs, c.include_dir(uacpi / "include"))

    if opt_arch == "riscv64" then
        local libfdt = fab.dependency("libfdt")
        -- we need to include libfdt c files to override libfdt_env.h :/
        table.insert(include_dirs, c.include_dir(libfdt / "libfdt"))
        table.insert(include_dirs, c.include_dir(libfdt))
    end


    local generators = {
        c = function(sources) return clang:generate(sources, kernel_flags, include_dirs) end
    }

    generators.S = function(sources) return clang:generate(sources, kernel_flags, include_dirs) end

    local objects = generate(kernel_sources, generators)
    table.insert(objects,
        clang:compile_object("zstddeclib.o", zstd_decoder, include_dirs, zstd_flags, "zstddeclib.o.d"))
    return objects
end

local c_flags = {
    "-std=gnu23",
    "-ffreestanding",

    "-fno-strict-aliasing",
    "-Wimplicit-fallthrough",
    "-Wmissing-field-initializers",

    "-fdiagnostics-color=always",
    "-DUACPI_OVERRIDE_CONFIG"
}

-- Flags
if opt_build_type == "release" then
    table.extend(c_flags, {
        "-O3",
        "-flto",
        "-g",
        "-D__RELEASE__"
    })
elseif opt_build_type == "debug" then
    table.extend(c_flags, {
        "-Og",
        "-g",
        "-fno-lto",
        "-fno-omit-frame-pointer",
        "-D__DEBUG__"
    })
end

if opt_arch == "x86_64" then
    table.extend(c_flags, {
        "--target=x86_64-none-elf",
        "-mno-red-zone",
        "-mgeneral-regs-only",
        "-mabi=sysv",
        "-mcmodel=kernel"
    })
elseif opt_arch == "riscv64" then
    table.extend(c_flags, {
        "--target=riscv64-none-elf",
        "-march=rv64imac_zicsr_zifencei_zihintpause",
        "-mabi=lp64",
        "-mcmodel=medlow"
    })
end

local objects = {}

local other_flags = {}
table.extend(other_flags, c_flags)
table.extend(other_flags, {
    "-Wall",
    "-Wextra",
})

local flanterm_sources = {}
table.extend(flanterm_sources, sources(flanterm:glob("src/*.c")))
local flanterm_objects = generate(flanterm_sources, {
    c = function(sources)
        return clang:generate(sources, c_flags, c.include_dir(flanterm / "src"))
    end
})
table.extend(objects, flanterm_objects)

local kernel_flags = {}
table.extend(kernel_flags, c_flags)
table.extend(kernel_flags, {
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
})

if opt_build_type == "debug" then
    table.extend(kernel_flags, {
        "-fsanitize=undefined",
        "-fstack-protector-all"
    })
elseif opt_build_type == "release" then
    table.extend(kernel_flags, {
        "-fstack-protector-strong"
    })
end

local zstd_flags = {}
table.extend(zstd_flags, c_flags)
table.extend(zstd_flags, {
    "-Os",
    "-g0",
    "-fomit-frame-pointer",
    "-fno-sanitize=undefined",
    "-fno-stack-protector",
})

table.extend(objects, get_kernel_objs(kernel_flags, zstd_flags))

local linker_script = fab.resolve_source(project_dir / ("support/" .. opt_arch .. ".lds"))
local kernel = ld:link("kernel.elf", objects, {
    "-znoexecstack"
}, linker_script)

return {
    ["kernel.elf"] = kernel
}
