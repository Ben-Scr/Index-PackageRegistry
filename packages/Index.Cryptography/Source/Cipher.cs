using System;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Index.Cryptography;

// Stateful cipher that holds an AES key for repeated encryption / decryption.
// Use this when you'd otherwise call Crypto.Encrypt(..., key) many times in
// a loop — it avoids re-validating the key length on every call.
//
// Replaces the old `AesEncryptor`. Differences:
//   - Authenticated (AES-GCM) instead of raw AES-CBC.
//   - IDisposable: zeroes the in-memory key on Dispose so it doesn't linger.
//   - Same packed format as Crypto.Encrypt — produced data is interchangeable.
//
// The key is owned by the cipher: Cipher.Create() generates a fresh random key,
// and the byte[] passed to the byte[] constructor is copied internally.
public sealed class Cipher : IDisposable
{
    private byte[]? m_Key;

    public Cipher(KeySize size = KeySize.Bits256)
    {
        m_Key = RandomNumberGenerator.GetBytes(size.Bytes());
    }

    public Cipher(byte[] key)
    {
        if (key == null) throw new ArgumentNullException(nameof(key));
        Aes.ValidateKey(key);
        m_Key = (byte[])key.Clone();
    }

    public KeySize Size => KeySizeExtensions.FromKeyLength(EnsureKey().Length);

    // Returns a copy of the key. The cipher's internal copy is preserved so
    // it can keep encrypting; the returned bytes are the caller's to manage
    // (consider zeroing them once finished — CryptographicOperations.ZeroMemory).
    public byte[] ExportKey() => (byte[])EnsureKey().Clone();

    public byte[] Encrypt(byte[] plaintext)
    {
        if (plaintext == null) throw new ArgumentNullException(nameof(plaintext));
        return Aes.Encrypt(plaintext, EnsureKey());
    }

    public byte[] Decrypt(byte[] ciphertext)
    {
        if (ciphertext == null) throw new ArgumentNullException(nameof(ciphertext));
        return Aes.Decrypt(ciphertext, EnsureKey());
    }

    public string EncryptToString(byte[] plaintext)
        => Convert.ToBase64String(Encrypt(plaintext));

    public byte[] DecryptFromString(string ciphertextBase64)
    {
        if (ciphertextBase64 == null) throw new ArgumentNullException(nameof(ciphertextBase64));
        return Decrypt(Convert.FromBase64String(ciphertextBase64));
    }

    public string EncryptString(string plaintext)
    {
        if (plaintext == null) throw new ArgumentNullException(nameof(plaintext));
        return EncryptToString(Encoding.UTF8.GetBytes(plaintext));
    }

    public string DecryptString(string ciphertextBase64)
        => Encoding.UTF8.GetString(DecryptFromString(ciphertextBase64));

    public byte[] Encrypt<T>(T value, JsonSerializerOptions? options = null)
        => Aes.Encrypt(JsonSerializer.SerializeToUtf8Bytes(value, options), EnsureKey());

    public T? Decrypt<T>(byte[] ciphertext, JsonSerializerOptions? options = null)
    {
        byte[] payload = Decrypt(ciphertext);
        return JsonSerializer.Deserialize<T>(payload, options);
    }

    public string EncryptToString<T>(T value, JsonSerializerOptions? options = null)
        => Convert.ToBase64String(Encrypt(value, options));

    public T? DecryptFromString<T>(string ciphertextBase64, JsonSerializerOptions? options = null)
        => Decrypt<T>(Convert.FromBase64String(ciphertextBase64), options);

    public void Dispose()
    {
        if (m_Key != null)
        {
            CryptographicOperations.ZeroMemory(m_Key);
            m_Key = null;
        }
    }

    private byte[] EnsureKey()
    {
        if (m_Key == null) throw new ObjectDisposedException(nameof(Cipher));
        return m_Key;
    }
}
