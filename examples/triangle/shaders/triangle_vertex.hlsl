struct Vertex
{
    float2 pos;
    float3 col;
};

ByteAddressBuffer Vertices : register(t0);
ByteAddressBuffer Indices : register(t1);

cbuffer DrawParams : register(b1)
{
    uint FirstIndex;
    int VertexOffset;
    uint IndexFormat;
    uint DrawParamsPadding;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float3 col : COLOR0;
};

Vertex LoadVertex(uint vertexId)
{
    uint offset = vertexId * 20;

    Vertex vertex;
    vertex.pos = asfloat(Vertices.Load2(offset + 0));
    vertex.col = asfloat(Vertices.Load3(offset + 8));
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
    Vertex vertex = LoadVertex(vertexId);

    PS_INPUT output;
    output.pos = float4(vertex.pos, 0.0f, 1.0f);
    output.col = vertex.col;
    return output;
}
