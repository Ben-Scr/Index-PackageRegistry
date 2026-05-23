using System;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Index.Cryptography;

// High-level user-facing crypto API. One-shot encrypt/decrypt for arbitrary
// data, available either with a raw byte[] key or a password.
//
// Quick start:
//
//   string secret  = Crypto.EncryptString("Hello world", "my-password");
//   string plain   = Crypto.DecryptString(secret, "my-password");
//
//   byte[] key     = Crypto.GenerateKey();          // 32-byte random AES-256
//   byte[] cipher  = Crypto.Encrypt(payload, key);
//   byte[] payload = Crypto.Decrypt(cipher, key);
//
//   // Anything System.Text.Json can serialise round-trips through Encrypt<T>:
//   var save = new PlayerSave { Score = 42, Name = "Ben" };
//   string blob = Crypto.EncryptToString(save, "password");
//   var loaded  = Crypto.DecryptFromString<PlayerSave>(blob, "password");
//
// Output formats:
//   Encrypt*        → byte[]  (raw packed AES-GCM payload)
//   EncryptToString → string  (Base64 of the same payload, safe for files / JSON)
//
// Wire format:
//   key-based:      [Aes packed = version|iv|tag|ciphertext]
//   password-based: [version=2][salt:16][iv:12][tag:16][ciphertext:N]
//
// The two formats are distinguishable by their leading version byte (0x01 for
// raw-key, 0x02 for password-based), so attempting to Decrypt with the wrong
// API throws a clear CryptographicException at the version check.
public static class Crypto
{
    internal const byte PasswordFormatVersion = 0x02;
    internal const int  PasswordHeaderSize    = 1 + PasswordKey.SaltSize + Aes.IvSize + Aes.TagSize;

    private static readonly JsonSerializerOptions s_JsonOptions = new()
    {
        IncludeFields = true,
        PropertyNameCaseInsensitive = true,
        WriteIndented = false,
    };

    // --------------------------------------------------------------------
    // Key generation
    // --------------------------------------------------------------------

    public static byte[] GenerateKey(KeySize size = KeySize.Bits256)
        => Aes.GenerateKey(size);

    // --------------------------------------------------------------------
    // Raw-bytes / raw-key encrypt/decrypt
    // --------------------------------------------------------------------

    public static byte[] Encrypt(byte[] plaintext, byte[] key)
    {
        if (plaintext == null) throw new ArgumentNullException(nameof(plaintext));
        if (key == null)       throw new ArgumentNullException(nameof(key));
        return Aes.Encrypt(plaintext, key);
    }

    public static byte[] Decrypt(byte[] ciphertext, byte[] key)
    {
        if (ciphertext == null) throw new ArgumentNullException(nameof(ciphertext));
        if (key == null)        throw new ArgumentNullException(nameof(key));
        return Aes.Decrypt(ciphertext, key);
    }

    public static string EncryptToString(byte[] plaintext, byte[] key)
        => Convert.ToBase64String(Encrypt(plaintext, key));

    public static byte[] DecryptFromString(string ciphertextBase64, byte[] key)
    {
        if (ciphertextBase64 == null) throw new ArgumentNullException(nameof(ciphertextBase64));
        return Decrypt(DecodeBase64(ciphertextBase64), key);
    }

    // --------------------------------------------------------------------
    // Raw-bytes / password encrypt/decrypt
    // --------------------------------------------------------------------

    public static byte[] Encrypt(byte[] plaintext, string password)
    {
        if (plaintext == null) throw new ArgumentNullException(nameof(plaintext));
        if (password == null)  throw new ArgumentNullException(nameof(password));

        byte[] salt = PasswordKey.GenerateSalt();
        byte[] key  = PasswordKey.DeriveKey(password, salt);
        try
        {
            return PackPasswordBlob(plaintext, key, salt);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(key);
        }
    }

    public static byte[] Decrypt(byte[] ciphertext, string password)
    {
        if (ciphertext == null) throw new ArgumentNullException(nameof(ciphertext));
        if (password == null)   throw new ArgumentNullException(nameof(password));
        return UnpackPasswordBlob(ciphertext, password);
    }

    public static string EncryptToString(byte[] plaintext, string password)
        => Convert.ToBase64String(Encrypt(plaintext, password));

    public static byte[] DecryptFromString(string ciphertextBase64, string password)
    {
        if (ciphertextBase64 == null) throw new ArgumentNullException(nameof(ciphertextBase64));
        return Decrypt(DecodeBase64(ciphertextBase64), password);
    }

    // --------------------------------------------------------------------
    // String plaintext convenience overloads (UTF-8)
    // --------------------------------------------------------------------

    public static string EncryptString(string plaintext, byte[] key)
    {
        if (plaintext == null) throw new ArgumentNullException(nameof(plaintext));
        return EncryptToString(Encoding.UTF8.GetBytes(plaintext), key);
    }

    public static string EncryptString(string plaintext, string password)
    {
        if (plaintext == null) throw new ArgumentNullException(nameof(plaintext));
        return EncryptToString(Encoding.UTF8.GetBytes(plaintext), password);
    }

    public static string DecryptString(string ciphertextBase64, byte[] key)
        => Encoding.UTF8.GetString(DecryptFromString(ciphertextBase64, key));

    public static string DecryptString(string ciphertextBase64, string password)
        => Encoding.UTF8.GetString(DecryptFromString(ciphertextBase64, password));

    // --------------------------------------------------------------------
    // Generic Encrypt<T> / Decrypt<T> — anything System.Text.Json handles
    // --------------------------------------------------------------------

    public static byte[] Encrypt<T>(T value, byte[] key, JsonSerializerOptions? options = null)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, options ?? s_JsonOptions);
        return Aes.Encrypt(payload, key);
    }

    public static byte[] Encrypt<T>(T value, string password, JsonSerializerOptions? options = null)
    {
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(value, options ?? s_JsonOptions);
        return Encrypt(payload, password);
    }

    public static T? Decrypt<T>(byte[] ciphertext, byte[] key, JsonSerializerOptions? options = null)
    {
        byte[] payload = Decrypt(ciphertext, key);
        return JsonSerializer.Deserialize<T>(payload, options ?? s_JsonOptions);
    }

    public static T? Decrypt<T>(byte[] ciphertext, string password, JsonSerializerOptions? options = null)
    {
        byte[] payload = Decrypt(ciphertext, password);
        return JsonSerializer.Deserialize<T>(payload, options ?? s_JsonOptions);
    }

    public static string EncryptToString<T>(T value, byte[] key, JsonSerializerOptions? options = null)
        => Convert.ToBase64String(Encrypt(value, key, options));

    public static string EncryptToString<T>(T value, string password, JsonSerializerOptions? options = null)
        => Convert.ToBase64String(Encrypt(value, password, options));

    public static T? DecryptFromString<T>(string ciphertextBase64, byte[] key, JsonSerializerOptions? options = null)
        => Decrypt<T>(DecodeBase64(ciphertextBase64), key, options);

    public static T? DecryptFromString<T>(string ciphertextBase64, string password, JsonSerializerOptions? options = null)
        => Decrypt<T>(DecodeBase64(ciphertextBase64), password, options);

    // --------------------------------------------------------------------
    // Internals
    // --------------------------------------------------------------------

    private static byte[] PackPasswordBlob(byte[] plaintext, byte[] key, byte[] salt)
    {
        byte[] packed = new byte[PasswordHeaderSize + plaintext.Length];
        packed[0] = PasswordFormatVersion;

        Span<byte> saltSpan = packed.AsSpan(1, PasswordKey.SaltSize);
        Span<byte> ivSpan   = packed.AsSpan(1 + PasswordKey.SaltSize, Aes.IvSize);
        Span<byte> tagSpan  = packed.AsSpan(1 + PasswordKey.SaltSize + Aes.IvSize, Aes.TagSize);
        Span<byte> ctSpan   = packed.AsSpan(PasswordHeaderSize);

        salt.CopyTo(saltSpan);
        RandomNumberGenerator.Fill(ivSpan);

        using var gcm = new AesGcm(key, Aes.TagSize);
        gcm.Encrypt(ivSpan, plaintext, ctSpan, tagSpan);
        return packed;
    }

    private static byte[] UnpackPasswordBlob(byte[] packed, string password)
    {
        if (packed.Length < PasswordHeaderSize)
            throw new CryptographicException(
                $"Ciphertext is too short ({packed.Length} bytes; need at least {PasswordHeaderSize}).");

        byte version = packed[0];
        if (version != PasswordFormatVersion)
            throw new CryptographicException(
                $"Unknown Index.Cryptography password format version 0x{version:X2}. " +
                $"(Are you decrypting a raw-key payload with a password? Use Decrypt(ciphertext, key) instead.)");

        ReadOnlySpan<byte> salt = packed.AsSpan(1, PasswordKey.SaltSize);
        ReadOnlySpan<byte> iv   = packed.AsSpan(1 + PasswordKey.SaltSize, Aes.IvSize);
        ReadOnlySpan<byte> tag  = packed.AsSpan(1 + PasswordKey.SaltSize + Aes.IvSize, Aes.TagSize);
        ReadOnlySpan<byte> ct   = packed.AsSpan(PasswordHeaderSize);

        byte[] key = PasswordKey.DeriveKey(password, salt);
        try
        {
            byte[] plaintext = new byte[ct.Length];
            using var gcm = new AesGcm(key, Aes.TagSize);
            gcm.Decrypt(iv, ct, tag, plaintext);
            return plaintext;
        }
        finally
        {
            CryptographicOperations.ZeroMemory(key);
        }
    }

    private static byte[] DecodeBase64(string s)
    {
        try
        {
            return Convert.FromBase64String(s);
        }
        catch (FormatException ex)
        {
            throw new CryptographicException("Ciphertext is not valid Base64.", ex);
        }
    }
}
