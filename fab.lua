local opt_arch = fab.option("arch", { "x86_64", "riscv64" }) or "x86_64"
local opt_build_type = fab.option("buildtype", { "debug", "release" }) or "debug"

local c = require("lang_c")
local asm = require("lang_nasm")
local linker = require("ld")

local clang = c.get_clang()
assert(clang ~= nil, "No clang compiler found")

local nasm = asm.get_nasm()
assert(nasm ~= nil, "No nasm found")

local ld = linker.get_linker("ld.lld")
assert(ld ~= nil, "No ld.lld found")

local prekernel_protocol = fab.git(
    "prekernel-protocol",
    "https://github.com/vcvtph2ps/lunar-prekernel",
    "e08a8f57cb5fe8a0e350534ecf8195508e869966"
)

local flanterm = fab.git(
    "flanterm",
    "https://github.com/vcvtph2ps/Flanterm.git",
    "ef09b0cebee940a89d21ccf17e193782f440a856"
)

local function get_kernel_objs(kernel_flags)
    local kernel_sources = sources(fab.glob("kernel/src/**/*.c", "!kernel/src/arch/**"))
    table.extend(kernel_sources, sources(fab.glob(path("kernel/src/arch", opt_arch, "**/*.c"))))

    if opt_arch == "x86_64" then
        table.extend(kernel_sources, sources(fab.glob("kernel/src/arch/x86_64/**/*.asm")))
    elseif opt_arch == "riscv64" then
        table.extend(kernel_sources, sources(fab.glob("kernel/src/arch/riscv64/**/*.S")))
    end

    local kernel_include_dirs = {
        c.include_dir("kernel/include"),
        c.include_dir("third_party"),
    }
    table.insert(kernel_include_dirs,
        c.include_dir(path(fab.build_dir(), prekernel_protocol.path, "pre_kernel", "public")))
    table.insert(kernel_include_dirs, c.include_dir(path(fab.build_dir(), flanterm.path, "src")))

    local generators = {
        c = function(sources) return clang:generate(sources, kernel_flags, kernel_include_dirs) end
    }

    if opt_arch == "x86_64" then
        local nasm_flags = { "-f", "elf64", "-Werror" }
        generators.asm = function(sources) return nasm:generate(sources, nasm_flags) end
    elseif opt_arch == "riscv64" then
        generators.S = function(sources) return clang:generate(sources, kernel_flags, kernel_include_dirs) end
    end

    return generate(kernel_sources, generators)
end

local c_flags = {
    "-std=gnu23",
    "-ffreestanding",

    "-fno-strict-aliasing",
    "-Wimplicit-fallthrough",
    "-Wmissing-field-initializers",

    "-fdiagnostics-color=always",
}

-- Flags
if opt_build_type == "release" then
    table.extend(c_flags, {
        "-O3",
        "-flto",
    })
elseif opt_build_type == "debug" then
    table.extend(c_flags, {
        "-O0",
        "-g",
        "-fno-lto",
        "-fno-omit-frame-pointer",
    })
end

if opt_arch == "x86_64" then
    table.extend(c_flags, {
        "--target=x86_64-none-elf",
        "-mno-red-zone",
        "-mgeneral-regs-only",
        "-mabi=sysv",
        "-mcmodel=kernel",
        "-D__ARCH_X86_64__"
    })
elseif opt_arch == "riscv64" then
    table.extend(c_flags, {
        "--target=riscv64-none-elf",
        "-march=rv64gc",
        "-mabi=lp64d",
        "-mcmodel=medany",
        "-D__ARCH_RISCV64__"
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
table.extend(flanterm_sources, sources(fab.glob("src/*.c", { relative_to = flanterm.path })))
local flanterm_objects = generate(flanterm_sources, {
    c = function(sources)
        return clang:generate(sources, c_flags,
            c.include_dir(path(fab.build_dir(), flanterm.path, "src")))
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
end

table.extend(objects, get_kernel_objs(kernel_flags))

local linker_script = fab.def_source("support/" .. opt_arch .. ".lds")
local kernel = ld:link("kernel.elf", objects, {
    "-znoexecstack"
}, linker_script)

return {
    install = {
        ["kernel.elf"] = kernel
    }
}
