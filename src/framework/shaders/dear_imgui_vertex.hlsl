cbuffer DrawConstants : register(b0)
{
    row_major float4x4 ProjectionMatrix;
    uint TextureIndex;
};

cbuffer DrawParams : register(b1)
{
    uint FirstIndex;
    int VertexOffset;
    uint IndexFormat;
    uint DrawParamsPadding;
};

struct ImDrawVert
{
    float2 pos;
    float2 uv;
    uint col;
};

ByteAddressBuffer Vertices : register(t0);
ByteAddressBuffer Indices : register(t1);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

float4 UnpackColor(uint color)
{
    return float4(
        (color & 0xff) / 255.0f,
        ((color >> 8) & 0xff) / 255.0f,
        ((color >> 16) & 0xff) / 255.0f,
        ((color >> 24) & 0xff) / 255.0f);
}

ImDrawVert LoadVertex(uint vertexId)
{
    uint offset = vertexId * 20;

    ImDrawVert vertex;
    vertex.pos = asfloat(Vertices.Load2(offset + 0));
    vertex.uv = asfloat(Vertices.Load2(offset + 8));
    vertex.col = Vertices.Load(offset + 16);
    return vertex;
}

uint LoadIndex16(uint indexNumber)
{
    uint alignedIndex = indexNumber & ~1u;
    uint packed = Indices.Load(alignedIndex * 2u);
    return (indexNumber & 1u) == 0u ? (packed & 0xffffu) : (packed >> 16);
}

uint LoadIndex(uint indexSlot)
{
    uint indexNumber = FirstIndex + indexSlot;
    if (IndexFormat == 1u)
        return Indices.Load(indexNumber * 4u);
    return LoadIndex16(indexNumber);
}

PS_INPUT main(uint indexSlot : SV_VertexID)
{
    uint vertexId = uint(int(LoadIndex(indexSlot)) + VertexOffset);
    ImDrawVert input = LoadVertex(vertexId);

    PS_INPUT output;
    output.pos = mul(float4(input.pos.xy, 0.0f, 1.0f), ProjectionMatrix);
    output.col = UnpackColor(input.col);
    output.uv = input.uv;
    return output;
}
