using System;
using System.Security.Cryptography;

namespace Index.Cryptography;

// Low-level AES-GCM byte helpers. Thin wrappers over System.Security.Cryptography
// .AesGcm that pack the IV + tag with the ciphertext into one self-describing
// blob so callers don't have to track three byte arrays per encryption.
//
// Packed layout (V1):
//
//   [version:1][iv:12][tag:16][ciphertext:N]
//
// The version byte lets us evolve the format later (e.g. switch tag size or
// nonce size) without breaking existing data — Decrypt rejects unknown
// versions instead of silently misparsing.
//
// Why AES-GCM and not AES-CBC?
//   GCM is *authenticated*: tampering with the ciphertext (or using the wrong
//   key) raises CryptographicException at decrypt time. CBC silently
//   "succeeds" with garbage plaintext, which is a security disaster (padding
//   oracle attacks). The old AesHelper in this engine used CBC — never use it
//   for new code.
public static class Aes
{
    internal const byte FormatVersion = 0x01;
    internal const int  IvSize     = 12;   // 96-bit nonce, GCM-recommended
    internal const int  TagSize    = 16;   // 128-bit tag, GCM max
    internal const int  HeaderSize = 1 + IvSize + TagSize;

    public static byte[] Encrypt(ReadOnlySpan<byte> plaintext, ReadOnlySpan<byte> key)
    {
        ValidateKey(key);

        byte[] packed = new byte[HeaderSize + plaintext.Length];
        packed[0] = FormatVersion;
        Span<byte> iv         = packed.AsSpan(1, IvSize);
        Span<byte> tag        = packed.AsSpan(1 + IvSize, TagSize);
        Span<byte> ciphertext = packed.AsSpan(HeaderSize);

        RandomNumberGenerator.Fill(iv);

        using var gcm = new AesGcm(key, TagSize);
        gcm.Encrypt(iv, plaintext, ciphertext, tag);
        return packed;
    }

    public static byte[] Decrypt(ReadOnlySpan<byte> packed, ReadOnlySpan<byte> key)
    {
        ValidateKey(key);
        if (packed.Length < HeaderSize)
            throw new CryptographicException(
                $"Ciphertext is too short ({packed.Length} bytes; need at least {HeaderSize}).");

        byte version = packed[0];
        if (version != FormatVersion)
            throw new CryptographicException(
                $"Unknown Index.Cryptography format version 0x{version:X2}. " +
                $"This binary supports up to 0x{FormatVersion:X2}.");

        ReadOnlySpan<byte> iv         = packed.Slice(1, IvSize);
        ReadOnlySpan<byte> tag        = packed.Slice(1 + IvSize, TagSize);
        ReadOnlySpan<byte> ciphertext = packed.Slice(HeaderSize);

        byte[] plaintext = new byte[ciphertext.Length];
        using var gcm = new AesGcm(key, TagSize);
        // Throws CryptographicException on tag mismatch (wrong key OR tampered).
        gcm.Decrypt(iv, ciphertext, tag, plaintext);
        return plaintext;
    }

    internal static void ValidateKey(ReadOnlySpan<byte> key)
    {
        if (!KeySizeExtensions.IsValidKeyLength(key.Length))
            throw new ArgumentException(
                $"AES key must be 16, 24, or 32 bytes (got {key.Length}).",
                nameof(key));
    }

    public static byte[] GenerateKey(KeySize size = KeySize.Bits256)
        => RandomNumberGenerator.GetBytes(size.Bytes());
}
