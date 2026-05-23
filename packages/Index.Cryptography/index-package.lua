-- Index.Cryptography — authenticated encryption (AES-GCM), password-based key
-- derivation (PBKDF2-SHA256), hashing/HMAC, and a bias-free cryptographically
-- secure RNG. Pure managed C# on top of System.Security.Cryptography +
-- System.Text.Json; no native side, no engine link.
--
-- Replaces the old Index.Cryptography namespace that lived inside
-- Index-ScriptCore. The previous implementation used unauthenticated AES-CBC
-- (vulnerable to padding-oracle attacks), PBKDF2 with too few iterations, and
-- a modulo-biased RNG. This package fixes all three and adds a generic
-- Crypto.Encrypt<T>/Decrypt<T> surface so any JSON-serialisable value can
-- round-trip through a string or byte[].

return {
    name        = "Index.Cryptography",
    version     = "0.1.0",
    description = "Authenticated AES-GCM encryption, PBKDF2 password keys, hashing/HMAC, and a bias-free secure RNG.",

    layers = {
        csharp = {
            sources = {
                "Source/**.cs",
            },
        },
    },

    dependencies = {},
}
