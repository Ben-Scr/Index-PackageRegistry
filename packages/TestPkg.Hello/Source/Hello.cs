// TestPkg.Hello — a smoke-test package for the Index cloud registry.
// Successful installation means the package downloaded, sha256-verified,
// extracted into <project>/Packages/TestPkg.Hello/, was added to the
// project allow-list, and got compiled by the next premake regen.

namespace TestPkg.Hello;

public static class Greeter
{
    public static string Greet(string name) => $"Hello from TestPkg.Hello, {name}!";
}
