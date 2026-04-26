struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float3 col : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target
{
    return float4(input.col, 1.0f);
}
