-- TestPkg.Hello — the smallest possible Index package, used as a
-- smoke-test for the cloud registry's download + verify + install flow.
-- Pure-C# layer, no native side, no dependencies.

return {
    name        = "TestPkg.Hello",
    version     = "0.1.0",
    description = "Smoke-test package for the Index registry.",

    layers = {
        csharp = {
            sources = {
                "Source/**.cs",
            },
        },
    },

    dependencies = {},
}
