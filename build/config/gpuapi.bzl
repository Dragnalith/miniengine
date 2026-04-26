"""Build setting for selecting the active graphics API."""

GpuApiInfo = provider(
    doc = "Selected graphics API build setting value.",
    fields = {
        "value": "Selected graphics API.",
    },
)

def _gpuapi_impl(ctx):
    value = ctx.build_setting_value
    if value not in ["DX12", "Vulkan"]:
        fail("gpuapi must be one of: DX12, Vulkan")
    return [GpuApiInfo(value = value)]

gpuapi = rule(
    implementation = _gpuapi_impl,
    build_setting = config.string(flag = True),
)
