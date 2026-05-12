local opt_arch = fab.option("arch", { "x86_64" }) or "x86_64"
local opt_bootloader = fab.option("bootloader", { "limine", "tartarus" }) or "tartarus"
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

-- Deps
-- local freestanding_c_headers = fab.git(
--     "freestanding-c-headers",
--     "https://github.com/osdev0/freestnd-c-hdrs-0bsd.git",
--     "097259a"
-- )
-- table.insert(include_dirs, c.include_dir(path(fab.build_dir(), freestanding_c_headers.path, opt_arch .. "/include")))

local limine_protocol = fab.git(
    "limine_protocol",
    "https://github.com/Limine-Bootloader/limine-protocol.git",
    "5b9d13e"
)

local tartarus_protocol = fab.git(
    "tartarus",
    "https://github.com/elysium-os/tartarus-bootloader.git",
    "d1ecb3dd137ecfb08ac15b6b8fc5233a9daefd97"
)


local function get_common_objs(kernel_flags)
    local common_sources = sources(fab.glob("common/src/**/*.c", "!common/src/arch/**"))
    table.extend(common_sources, sources(fab.glob(path("common/src/arch", opt_arch, "**/*.c"))))

    if opt_arch == "x86_64" then
        table.extend(common_sources, sources(fab.glob("common/src/arch/x86_64/**/*.asm")))
    end

    local common_include_dirs = {
        c.include_dir(path("common/include/arch/", opt_arch)),
        c.include_dir("common/include"),
        c.include_dir(path("common/include/arch/", opt_arch)),
    }

    table.insert(common_include_dirs, c.include_dir(path(fab.build_dir(), limine_protocol.path, "include")))
    table.insert(common_include_dirs, c.include_dir(path(fab.build_dir(), tartarus_protocol.path)))

    local generators = {
        c = function(sources) return clang:generate(sources, kernel_flags, common_include_dirs) end
    }

    if opt_arch == "x86_64" then
        local nasm_flags = { "-f", "elf64", "-Werror" }
        generators.asm = function(sources) return nasm:generate(sources, nasm_flags) end
    end

    return generate(common_sources, generators)
end


local function get_prekernel_objs(kernel_flags)
    local pre_kernel_sources = sources(fab.glob("pre_kernel/src/**/*.c", "!pre_kernel/src/arch/**"))
    table.extend(pre_kernel_sources, sources(fab.glob(path("pre_kernel/src/arch", opt_arch, "**/*.c"))))

    if opt_arch == "x86_64" then
        table.extend(pre_kernel_sources, sources(fab.glob("pre_kernel/src/arch/x86_64/**/*.asm")))
    end

    local pre_kernel_include_dirs = {
        c.include_dir(path("pre_kernel/include/arch/", opt_arch)),
        c.include_dir("pre_kernel/include"),
        c.include_dir("common/include"),
        c.include_dir(path("common/include/arch/", opt_arch)),
        c.include_dir("pre_kernel/public")
    }

    table.insert(pre_kernel_include_dirs, c.include_dir(path(fab.build_dir(), limine_protocol.path, "include")))
    table.insert(pre_kernel_include_dirs, c.include_dir(path(fab.build_dir(), tartarus_protocol.path)))

    local generators = {
        c = function(sources) return clang:generate(sources, kernel_flags, pre_kernel_include_dirs) end
    }

    if opt_arch == "x86_64" then
        local nasm_flags = { "-f", "elf64", "-Werror" }
        generators.asm = function(sources) return nasm:generate(sources, nasm_flags) end
    end

    return generate(pre_kernel_sources, generators)
end


local function get_kernel_objs(kernel_flags)
    local kernel_sources = sources(fab.glob("kernel/src/**/*.c", "!kernel/src/arch/**"))
    table.extend(kernel_sources, sources(fab.glob(path("kernel/src/arch", opt_arch, "**/*.c"))))

    if opt_arch == "x86_64" then
        table.extend(kernel_sources, sources(fab.glob("kernel/src/arch/x86_64/**/*.asm")))
    end

    local kernel_include_dirs = {
        c.include_dir(path("kernel/include/arch/", opt_arch)),
        c.include_dir("kernel/include"),
        c.include_dir("common/include"),
        c.include_dir(path("common/include/arch/", opt_arch)),
        c.include_dir("pre_kernel/public")
    }

    local generators = {
        c = function(sources) return clang:generate(sources, kernel_flags, kernel_include_dirs) end
    }

    if opt_arch == "x86_64" then
        local nasm_flags = { "-f", "elf64", "-Werror" }
        generators.asm = function(sources) return nasm:generate(sources, nasm_flags) end
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
    "-DLIMINE_API_REVISION=6"
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

    if opt_bootloader == "limine" then
        table.insert(c_flags, "-D__BOOTLOADER_LIMINE__")
    elseif opt_bootloader == "tartarus" then
        table.insert(c_flags, "-D__BOOTLOADER_TARTARUS__")
    end
end

local objects = {}

local kernel_flags = {}
table.extend(kernel_flags, c_flags)
table.extend(kernel_flags, {
    "-Wall",
    "-Wextra",
    "-Wvla",
    "-Werror",
    "-Wno-error=unused-function"
})

local linker_script

if opt_arch == "x86_64" then
    linker_script = fab.def_source("support/x86_64-" .. opt_bootloader .. ".lds")
end

local common_objs = get_common_objs(kernel_flags)
local prekernel_objs = get_prekernel_objs(kernel_flags)
-- if opt_build_type == "debug" then
--     table.extend(kernel_flags, {
--         "-fsanitize=undefined",
--         "-fstack-protector-all"
--     })
-- end

local kernel_objs = get_kernel_objs(kernel_flags)

table.extend(objects, common_objs)
table.extend(objects, prekernel_objs)
table.extend(objects, kernel_objs)

local kernel = ld:link("kernel.elf", objects, {
    "-znoexecstack"
}, linker_script)

return {
    install = {
        ["kernel.elf"] = kernel
    }
}
