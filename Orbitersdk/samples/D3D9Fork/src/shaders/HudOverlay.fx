// AtmoFX fork: conformal HUD markers (see HudOverlay.h). Additive glow; bright where visible, faint where hidden.

float4x4 gVP;
float4   gCol;

float4 HudVS(float3 p : POSITION) : POSITION { return mul(float4(p, 1.0), gVP); }
float4 VisPS() : COLOR { return float4(gCol.rgb * 0.9, 1.0); }
float4 HidPS() : COLOR { return float4(gCol.rgb * 0.22, 1.0); }

technique Visible
{
	pass P0
	{
		VertexShader = compile vs_3_0 HudVS();
		PixelShader = compile ps_3_0 VisPS();
		ZEnable = true; ZWriteEnable = false; ZFunc = LessEqual;
		CullMode = None; AlphaBlendEnable = true; SrcBlend = One; DestBlend = One;
		StencilEnable = false; AlphaTestEnable = false; FogEnable = false;
	}
}

technique Hidden
{
	pass P0
	{
		VertexShader = compile vs_3_0 HudVS();
		PixelShader = compile ps_3_0 HidPS();
		ZEnable = true; ZWriteEnable = false; ZFunc = Greater;
		CullMode = None; AlphaBlendEnable = true; SrcBlend = One; DestBlend = One;
		StencilEnable = false; AlphaTestEnable = false; FogEnable = false;
	}
}
