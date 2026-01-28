#include "object3d.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
    float32_t shininess;
};

ConstantBuffer<Material> gMaterial : register(b0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

struct PointLight
{
    float32_t4 color;
    float32_t3 posirion;
    float intensity;
 
};

struct Camera
{
    float32_t3 worldPosition;
};

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);



PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 transformedUV = mul(float32_t4(input.texcoord,0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler,transformedUV.xy);
    if (gMaterial.enableLighting != 0)
    {
        float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        
        float3 N = normalize(input.normal);
        float3 L = normalize(-gDirectionalLight.direction);
        float32_t3 PointLightDirection = normalize(input.worldPosition - gPointLight.posirion);
        //float32_t3 reflectLight = reflect(L, N);
        //float RdotE = dot(reflectLight, toEye);
        //float specularPow = pow(saturate(RdotE), 70);
        //float specularPow = pow(saturate(RdotE), gMaterial.shininess);
        
        float NdotL = (dot(N, L));
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);

        // 拡散反射
        float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        float32_t3 pointLightDiffuse = gMaterial.color.rgb * textureColor.rgb * gPointLight.color.rgb * saturate(dot(N, -PointLightDirection)) * gPointLight.intensity;
        
        
        // 鏡面反射
        float32_t3 halfVector = normalize(L + toEye);
        float NDotH = dot(N,halfVector);
        float specularPow = pow(saturate(NDotH), gMaterial.shininess);
        
        
        float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);
        float32_t3 pointLightSpecular = gPointLight.color.rgb * gPointLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);
        
        // 拡散反射と鏡面反射を合成
        output.color.rgb = diffuse + specular + pointLightDiffuse + pointLightSpecular;
        
        output.color.a = gMaterial.color.a * textureColor.a;

        
        }
    else
    {
        output.color = gMaterial.color * textureColor;

    }
    return output;
}



