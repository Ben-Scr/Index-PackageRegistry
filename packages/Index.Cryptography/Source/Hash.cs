using System;
using System.Security.Cryptography;
using System.Text;

namespace Index.Cryptography;

// Hash & MAC primitives. Inputs accept byte[] or string (UTF-8 encoded);
// outputs come in raw byte[] or lowercase hex form.
public static class Hash
{
    public static byte[] Sha256(byte[] data)
    {
        if (data == null) throw new ArgumentNullException(nameof(data));
        return SHA256.HashData(data);
    }

    public static byte[] Sha256(ReadOnlySpan<byte> data)
    {
        byte[] result = new byte[32];
        SHA256.HashData(data, result);
        return result;
    }

    public static byte[] Sha256(string text)
    {
        if (text == null) throw new ArgumentNullException(nameof(text));
        return SHA256.HashData(Encoding.UTF8.GetBytes(text));
    }

    public static string Sha256Hex(byte[] data) => ToHex(Sha256(data));
    public static string Sha256Hex(string text) => ToHex(Sha256(text));

    public static byte[] Sha512(byte[] data)
    {
        if (data == null) throw new ArgumentNullException(nameof(data));
        return SHA512.HashData(data);
    }

    public static byte[] Sha512(ReadOnlySpan<byte> data)
    {
        byte[] result = new byte[64];
        SHA512.HashData(data, result);
        return result;
    }

    public static byte[] Sha512(string text)
    {
        if (text == null) throw new ArgumentNullException(nameof(text));
        return SHA512.HashData(Encoding.UTF8.GetBytes(text));
    }

    public static string Sha512Hex(byte[] data) => ToHex(Sha512(data));
    public static string Sha512Hex(string text) => ToHex(Sha512(text));

    public static byte[] HmacSha256(byte[] data, byte[] key)
    {
        if (data == null) throw new ArgumentNullException(nameof(data));
        if (key == null)  throw new ArgumentNullException(nameof(key));
        return HMACSHA256.HashData(key, data);
    }

    public static byte[] HmacSha512(byte[] data, byte[] key)
    {
        if (data == null) throw new ArgumentNullException(nameof(data));
        if (key == null)  throw new ArgumentNullException(nameof(key));
        return HMACSHA512.HashData(key, data);
    }

    // Constant-time compare. Use this whenever you compare a secret (MAC,
    // hash, token) against a user-supplied value — a regular `==` short-
    // circuits and leaks length/prefix information through timing.
    public static bool FixedTimeEquals(ReadOnlySpan<byte> a, ReadOnlySpan<byte> b)
        => CryptographicOperations.FixedTimeEquals(a, b);

    public static string ToHex(byte[] bytes)
    {
        if (bytes == null) throw new ArgumentNullException(nameof(bytes));
        return Convert.ToHexString(bytes).ToLowerInvariant();
    }
}
