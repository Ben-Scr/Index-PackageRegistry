namespace Index.Cryptography;

public enum KeySize
{
    Bits128 = 16,
    Bits192 = 24,
    Bits256 = 32,
}

public static class KeySizeExtensions
{
    public static int Bytes(this KeySize size) => (int)size;
    public static int Bits(this KeySize size) => (int)size * 8;

    public static bool IsValidKeyLength(int byteLength)
        => byteLength == 16 || byteLength == 24 || byteLength == 32;

    public static KeySize FromKeyLength(int byteLength) => byteLength switch
    {
        16 => KeySize.Bits128,
        24 => KeySize.Bits192,
        32 => KeySize.Bits256,
        _ => throw new System.ArgumentException(
            $"AES key length must be 16, 24, or 32 bytes (got {byteLength}).",
            nameof(byteLength)),
    };
}
