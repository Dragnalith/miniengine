cbuffer DrawConstants : register(b0)
{
    row_major float4x4 ProjectionMatrix;
    uint TextureIndex;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

SamplerState TextureSampler : register(s0);
Texture2D Textures[1024] : register(t2);

float4 main(PS_INPUT input) : SV_Target
{
    return input.col * Textures[TextureIndex].Sample(TextureSampler, input.uv);
}
