using System.Runtime.InteropServices;

namespace Index.Tilemap2D;

// Tile — managed mirror of the native Tile struct.
//
// Layout: ulong (asset GUID) + 4 floats (RGBA) = 24 bytes, sequential.
// Must stay byte-for-byte identical to the C++ definition in
// `include/Tilemap2DComponent.hpp`. `[StructLayout(Sequential, Pack = 4)]`
// matches the natural alignment of those fields on x64 — UUID is 8-byte
// aligned, floats are 4-byte aligned, no padding.
//
// `TextureGuid` holds the asset reference by GUID, not by path. Renaming
// or moving the texture asset doesn't break the tile. The `Texture`
// helper property does the GUID → managed `Index.Texture` resolution
// lazily; storage stays a raw ulong so spans of Tile remain blittable
// for ReadOnlySpan-based batch APIs.
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct Tile
{
    public ulong TextureGuid;
    public float R;
    public float G;
    public float B;
    public float A;

    public Tile(ulong textureGuid, Color color)
    {
        TextureGuid = textureGuid;
        R = color.R;
        G = color.G;
        B = color.B;
        A = color.A;
    }

    public Color Color
    {
        get => new(R, G, B, A);
        set { R = value.R; G = value.G; B = value.B; A = value.A; }
    }

    public Texture? Texture
    {
        get => Texture.FromAssetUUID(TextureGuid);
        set => TextureGuid = value?.UUID ?? 0;
    }
}
