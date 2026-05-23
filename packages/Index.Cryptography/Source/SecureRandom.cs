using System;
using System.Security.Cryptography;
using System.Text;

namespace Index.Cryptography;

// Cryptographically secure random number generator.
//
// Differences from a non-secure RNG (Index.Random) you should be aware of:
//   - All ranged methods are bias-free. The BCL's RandomNumberGenerator.GetInt32
//     handles rejection sampling internally, so NextInt(0, 7) is uniform even
//     though 7 doesn't divide 2^31. The old RandomSecure used `value % range`,
//     which is biased.
//   - There is no seed. By design — a seedable secure RNG is a contradiction.
//   - Thread-safe: the underlying RandomNumberGenerator is process-global and
//     thread-safe, so SecureRandom instances can be shared freely.
public sealed class SecureRandom
{
    public static readonly SecureRandom Shared = new SecureRandom();

    private const string DefaultCharset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    public bool NextBool() => NextByte() < 128;

    public byte NextByte()
    {
        Span<byte> buf = stackalloc byte[1];
        RandomNumberGenerator.Fill(buf);
        return buf[0];
    }

    public byte NextByte(byte maxExclusive)
    {
        if (maxExclusive == 0) throw new ArgumentOutOfRangeException(nameof(maxExclusive));
        return (byte)RandomNumberGenerator.GetInt32(maxExclusive);
    }

    public byte NextByte(byte minInclusive, byte maxExclusive)
    {
        if (minInclusive >= maxExclusive)
            throw new ArgumentOutOfRangeException(
                nameof(minInclusive),
                $"min ({minInclusive}) must be < max ({maxExclusive}).");
        return (byte)RandomNumberGenerator.GetInt32(minInclusive, maxExclusive);
    }

    public int NextInt() => RandomNumberGenerator.GetInt32(0, int.MaxValue);

    public int NextInt(int maxExclusive)
    {
        if (maxExclusive <= 0) throw new ArgumentOutOfRangeException(nameof(maxExclusive));
        return RandomNumberGenerator.GetInt32(maxExclusive);
    }

    public int NextInt(int minInclusive, int maxExclusive)
    {
        if (minInclusive >= maxExclusive)
            throw new ArgumentOutOfRangeException(
                nameof(minInclusive),
                $"min ({minInclusive}) must be < max ({maxExclusive}).");
        return RandomNumberGenerator.GetInt32(minInclusive, maxExclusive);
    }

    public long NextLong()
    {
        Span<byte> buf = stackalloc byte[8];
        RandomNumberGenerator.Fill(buf);
        return BitConverter.ToInt64(buf) & long.MaxValue;
    }

    public long NextLong(long maxExclusive)
    {
        if (maxExclusive <= 0) throw new ArgumentOutOfRangeException(nameof(maxExclusive));
        return NextLongRange(0, maxExclusive);
    }

    public long NextLong(long minInclusive, long maxExclusive)
    {
        if (minInclusive >= maxExclusive)
            throw new ArgumentOutOfRangeException(
                nameof(minInclusive),
                $"min ({minInclusive}) must be < max ({maxExclusive}).");
        return NextLongRange(minInclusive, maxExclusive);
    }

    // Unbiased [0, 1) using 53 bits of entropy — same precision pattern as
    // System.Random.NextDouble.
    public double NextDouble()
    {
        Span<byte> buf = stackalloc byte[8];
        RandomNumberGenerator.Fill(buf);
        ulong bits = BitConverter.ToUInt64(buf) >> 11;       // 53 random bits
        return bits * (1.0 / (1UL << 53));
    }

    public double NextDouble(double maxExclusive)
    {
        if (maxExclusive <= 0.0) throw new ArgumentOutOfRangeException(nameof(maxExclusive));
        return NextDouble() * maxExclusive;
    }

    public double NextDouble(double minInclusive, double maxExclusive)
    {
        if (minInclusive >= maxExclusive)
            throw new ArgumentOutOfRangeException(
                nameof(minInclusive),
                $"min ({minInclusive}) must be < max ({maxExclusive}).");
        return minInclusive + NextDouble() * (maxExclusive - minInclusive);
    }

    public float NextFloat() => (float)NextDouble();

    public float NextFloat(float maxExclusive)
    {
        if (maxExclusive <= 0f) throw new ArgumentOutOfRangeException(nameof(maxExclusive));
        return (float)(NextDouble() * maxExclusive);
    }

    public float NextFloat(float minInclusive, float maxExclusive)
    {
        if (minInclusive >= maxExclusive)
            throw new ArgumentOutOfRangeException(
                nameof(minInclusive),
                $"min ({minInclusive}) must be < max ({maxExclusive}).");
        return (float)(minInclusive + NextDouble() * (maxExclusive - minInclusive));
    }

    public string NextString(int length = 16, string? charset = null)
    {
        if (length < 0) throw new ArgumentOutOfRangeException(nameof(length));
        if (length == 0) return string.Empty;

        charset ??= DefaultCharset;
        if (charset.Length == 0)
            throw new ArgumentException("charset must contain at least one character.", nameof(charset));

        return string.Create(length, charset, static (span, cs) =>
        {
            int csLen = cs.Length;
            for (int i = 0; i < span.Length; i++)
                span[i] = cs[RandomNumberGenerator.GetInt32(csLen)];
        });
    }

    public byte[] NextBytes(int length)
    {
        if (length < 0) throw new ArgumentOutOfRangeException(nameof(length));
        if (length == 0) return Array.Empty<byte>();
        byte[] result = new byte[length];
        RandomNumberGenerator.Fill(result);
        return result;
    }

    public void FillBytes(Span<byte> buffer) => RandomNumberGenerator.Fill(buffer);

    public T Next<T>() where T : IComparable<T>
    {
        if (typeof(T) == typeof(int))    return (T)(object)NextInt();
        if (typeof(T) == typeof(long))   return (T)(object)NextLong();
        if (typeof(T) == typeof(byte))   return (T)(object)NextByte();
        if (typeof(T) == typeof(float))  return (T)(object)NextFloat();
        if (typeof(T) == typeof(double)) return (T)(object)NextDouble();
        if (typeof(T) == typeof(bool))   return (T)(object)NextBool();
        if (typeof(T) == typeof(string)) return (T)(object)NextString();
        throw new NotSupportedException($"SecureRandom.Next<{typeof(T).Name}> is not supported.");
    }

    public T Next<T>(T min, T max) where T : IComparable<T>
    {
        if (typeof(T) == typeof(int))    return (T)(object)NextInt(Convert.ToInt32(min), Convert.ToInt32(max));
        if (typeof(T) == typeof(long))   return (T)(object)NextLong(Convert.ToInt64(min), Convert.ToInt64(max));
        if (typeof(T) == typeof(byte))   return (T)(object)NextByte(Convert.ToByte(min), Convert.ToByte(max));
        if (typeof(T) == typeof(float))  return (T)(object)NextFloat(Convert.ToSingle(min), Convert.ToSingle(max));
        if (typeof(T) == typeof(double)) return (T)(object)NextDouble(Convert.ToDouble(min), Convert.ToDouble(max));
        throw new NotSupportedException($"SecureRandom.Next<{typeof(T).Name}>(min,max) is not supported.");
    }

    private static long NextLongRange(long minInclusive, long maxExclusive)
    {
        // Rejection sampling so we don't introduce modulo bias. The mask trims
        // the random ulong down to the smallest 2^k that covers the range; on
        // average this rejects <2 samples regardless of range size.
        ulong range = (ulong)(maxExclusive - minInclusive);
        if (range == 1) return minInclusive;

        ulong mask = range - 1;
        mask |= mask >> 1;
        mask |= mask >> 2;
        mask |= mask >> 4;
        mask |= mask >> 8;
        mask |= mask >> 16;
        mask |= mask >> 32;

        Span<byte> buf = stackalloc byte[8];
        while (true)
        {
            RandomNumberGenerator.Fill(buf);
            ulong sample = BitConverter.ToUInt64(buf) & mask;
            if (sample < range) return minInclusive + (long)sample;
        }
    }
}
