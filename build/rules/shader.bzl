"""Rules for compiling one HLSL source file into one shader stage blob."""

load("//build/rules:providers.bzl", "AssetSpecInfo", "validate_relative_path")
load("//build/config:gpuapi.bzl", "GpuApiInfo")

_SHADER_TOOLCHAIN = "//build/rules:shader_toolchain_type"

ShaderStageInfo = provider(
    doc = "Compiled shader stage metadata.",
    fields = {
        "entry": "Shader entry point.",
        "file": "Compiled shader output File.",
        "format": "Output format selected by the shader toolchain.",
        "stage": "Shader stage.",
    },
)

def _dirname(path):
    slash = path.rfind("/")
    if slash < 0:
        return ""
    return path[:slash]

def _profile(stage, shader_model):
    prefix = {
        "vertex": "vs",
        "pixel": "ps",
    }[stage]
    return "%s_%s" % (prefix, shader_model)

def _asset_dest(ctx, out):
    if ctx.attr.asset_dest:
        validate_relative_path(ctx.attr.asset_dest, "shader asset dest")
        return ctx.attr.asset_dest

    dest = "shaders/" + out.basename
    validate_relative_path(dest, "shader asset dest")
    return dest

def _shader_stage_impl(ctx):
    toolchain = ctx.toolchains[_SHADER_TOOLCHAIN].shader
    gpuapi = ctx.attr._gpuapi[GpuApiInfo].value
    output_format = "spirv" if gpuapi == "Vulkan" else toolchain.format

    if ctx.attr.out:
        validate_relative_path(ctx.attr.out, "shader output")
        out = ctx.actions.declare_file(ctx.attr.out)
    else:
        out = ctx.actions.declare_file(ctx.label.name + toolchain.extension)

    src_dir = _dirname(ctx.file.src.path)
    args = ctx.actions.args()
    args.add("-nologo")
    args.add_all(toolchain.default_copts)
    if gpuapi == "Vulkan":
        args.add("-spirv")
        args.add("-fvk-use-dx-layout")
        args.add_all([
            "-fvk-bind-register", "b0", "0", "0", "0",
            "-fvk-bind-register", "b1", "0", "1", "0",
            "-fvk-bind-register", "t0", "0", "2", "0",
            "-fvk-bind-register", "t1", "0", "3", "0",
            "-fvk-bind-register", "s0", "0", "0", "1",
            "-fvk-bind-register", "t2", "0", "1", "1",
        ])
    args.add("-E", ctx.attr.entry)
    args.add("-T", _profile(ctx.attr.stage, ctx.attr.shader_model))
    args.add("-Fo", out)
    if src_dir:
        args.add("-I", src_dir)
    args.add_all(ctx.attr.defines, before_each = "-D")
    args.add_all(ctx.attr.copts)
    args.add(ctx.file.src)

    ctx.actions.run(
        executable = toolchain.compiler,
        arguments = [args],
        inputs = depset([ctx.file.src] + ctx.files.deps),
        tools = toolchain.compiler_files,
        outputs = [out],
        mnemonic = "ShaderCompile",
        progress_message = "Compiling %s shader %s to %s" % (
            ctx.attr.stage,
            ctx.file.src.short_path,
            output_format.upper(),
        ),
    )

    return [
        DefaultInfo(
            files = depset([out]),
            runfiles = ctx.runfiles(files = [out]),
        ),
        ShaderStageInfo(
            entry = ctx.attr.entry,
            file = out,
            format = output_format,
            stage = ctx.attr.stage,
        ),
        AssetSpecInfo(entries = depset([
            struct(file = out, dest = _asset_dest(ctx, out)),
        ])),
    ]

shader_stage = rule(
    implementation = _shader_stage_impl,
    attrs = {
        "asset_dest": attr.string(default = ""),
        "copts": attr.string_list(default = []),
        "defines": attr.string_list(default = []),
        "deps": attr.label_list(allow_files = True),
        "entry": attr.string(default = "main"),
        "out": attr.string(default = ""),
        "shader_model": attr.string(default = "6_0"),
        "src": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "stage": attr.string(
            mandatory = True,
            values = ["vertex", "pixel"],
        ),
        "_gpuapi": attr.label(
            default = Label("//build/config:gpuapi"),
        ),
    },
    toolchains = [_SHADER_TOOLCHAIN],
)
