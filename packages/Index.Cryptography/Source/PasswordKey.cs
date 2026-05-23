using System;
using System.Security.Cryptography;
using System.Text;

namespace Index.Cryptography;

// PBKDF2-SHA256 password-based key derivation.
//
// Defaults:
//   - 600,000 iterations (OWASP 2023 recommendation for PBKDF2-SHA256).
//   - 16-byte random salt per derivation.
//
// If you need more resistance to GPU/ASIC attackers, derive a long-lived
// key offline with a higher iteration count and feed it to Cipher / Crypto
// directly instead of re-deriving from password every call.
public static class PasswordKey
{
    public const int DefaultIterations = 600_000;
    public const int SaltSize = 16;

    public static byte[] GenerateSalt()
        => RandomNumberGenerator.GetBytes(SaltSize);

    public static byte[] DeriveKey(
        string password,
        ReadOnlySpan<byte> salt,
        KeySize keySize = KeySize.Bits256,
        int iterations = DefaultIterations)
    {
        if (password == null) throw new ArgumentNullException(nameof(password));
        if (salt.Length == 0) throw new ArgumentException("salt must be non-empty.", nameof(salt));
        if (iterations < 1)  throw new ArgumentOutOfRangeException(nameof(iterations));

        return Rfc2898DeriveBytes.Pbkdf2(
            Encoding.UTF8.GetBytes(password),
            salt,
            iterations,
            HashAlgorithmName.SHA256,
            keySize.Bytes());
    }
}
