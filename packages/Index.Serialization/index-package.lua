-- Index.Serialization — JSON, encrypted-JSON, and zip helpers extracted from
-- Index-ScriptCore. Pure managed code (System.Text.Json + System.IO.Compression
-- + System.Security.Cryptography); no native side, no engine link.
--
-- This package exists primarily to validate the C# layer of the engine package
-- system. It demonstrates extracting a self-contained subsystem out of the core
-- managed assembly into its own redistributable Pkg.<Name> .NET DLL.

return {
    name = "Index.Serialization",
    version = "0.1.0",
    description = "JSON / encrypted JSON / ZIP helpers under the Index.Serialization namespace.",

    layers = {
        csharp = {
            sources = {
                "Source/**.cs",
            },
        },
    },

    dependencies = {},
}
