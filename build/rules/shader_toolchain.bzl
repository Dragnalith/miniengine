"""Toolchain definition for offline shader compilation."""

ShaderToolchainInfo = provider(
    doc = "Describes a shader compiler configuration.",
    fields = {
        "compiler": "Executable File for the shader compiler.",
        "compiler_files": "Depset of files required by the compiler at execution time.",
        "default_copts": "Default compiler options for this target format.",
        "extension": "Output file extension, including the leading dot.",
        "format": "Output format name, such as dxil or spirv.",
    },
)

def _shader_toolchain_impl(ctx):
    compiler_files = depset(
        [ctx.executable.compiler] + ctx.files.compiler_files,
    )

    return [
        platform_common.ToolchainInfo(
            shader = ShaderToolchainInfo(
                compiler = ctx.executable.compiler,
                compiler_files = compiler_files,
                default_copts = ctx.attr.default_copts,
                extension = ctx.attr.extension,
                format = ctx.attr.format,
            ),
        ),
    ]

shader_toolchain = rule(
    implementation = _shader_toolchain_impl,
    attrs = {
        "compiler": attr.label(
            executable = True,
            allow_files = True,
            cfg = "exec",
            mandatory = True,
        ),
        "compiler_files": attr.label_list(
            allow_files = True,
            cfg = "exec",
        ),
        "default_copts": attr.string_list(default = []),
        "extension": attr.string(mandatory = True),
        "format": attr.string(
            mandatory = True,
            values = ["dxil", "spirv"],
        ),
    },
)
