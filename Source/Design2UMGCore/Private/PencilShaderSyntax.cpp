// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilShaderSyntax.h"
#include "DesignShaderData.h"
#include "Misc/Paths.h"
#include <algorithm>
#include <cctype>
#include <sstream>
namespace PencilShaderSyntax
{
FString ResolveStringUniform(const FDesignShaderData& Shader, const FUniform& Uniform)
{
    if (const auto* Value = Shader.Uniforms.Find(UTF8_TO_TCHAR(Uniform.Name.c_str())))
        if (Value->Kind == FDesignShaderValue::EKind::String)
            return Value->String;
    FString Value = FString(UTF8_TO_TCHAR(Uniform.DefaultValue.c_str())).TrimQuotes();
    // Authored defaults are relative to the shader; explicit overrides already carry source paths.
    if (Uniform.Type == "sampler2D" && !Value.IsEmpty() && !Value.StartsWith(TEXT("pencil:")) &&
        !Value.Contains(TEXT("://")) && FPaths::IsRelative(Value))
    {
        Value = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Shader.Url), Value);
        FPaths::CollapseRelativeDirectories(Value);
    }
    return Value;
}

namespace
{
struct FToken {std::string Text;size_t Begin,End;};
bool Name(char C){return std::isalnum(static_cast<unsigned char>(C))||C=='_';}
std::vector<FToken> Tokens(const std::string& S)
{
    std::vector<FToken> R;
    for(size_t I=0;I<S.size();)
    {
        if(std::isspace(static_cast<unsigned char>(S[I]))){++I;continue;}
        if(S.compare(I,2,"//")==0){I=S.find('\n',I);if(I==std::string::npos)break;continue;}
        if(S.compare(I,2,"/*")==0){auto E=S.find("*/",I+2);I=E==std::string::npos?S.size():E+2;continue;}
        if(S[I]=='#'){auto E=S.find('\n',I);I=E==std::string::npos?S.size():E+1;continue;}
        size_t B=I++;
        if(Name(S[B]))while(I<S.size()&&Name(S[I]))++I;
        R.push_back({S.substr(B,I-B),B,I});
    }
    return R;
}
size_t Close(const std::vector<FToken>& T,size_t I,const std::string& L,const std::string& R)
{
    int Depth=0;
    for(;I<T.size();++I){if(T[I].Text==L)++Depth;if(T[I].Text==R&&--Depth==0)return I;}
    return T.size();
}
struct FEdit {size_t Begin,End;std::string Text;};
std::string Apply(std::string S,std::vector<FEdit> E)
{
    std::sort(E.begin(),E.end(),[](const FEdit& A,const FEdit& B){return A.Begin>B.Begin;});
    for(const auto& X:E)S.replace(X.Begin,X.End-X.Begin,X.Text);
    return S;
}
std::string Trim(std::string S)
{
    auto B=S.find_first_not_of(" \t\r\n");if(B==std::string::npos)return {};
    return S.substr(B,S.find_last_not_of(" \t\r\n")-B+1);
}
std::string Directive(const std::string& Comment,const std::string& Key)
{
    auto B=Comment.find("@"+Key);if(B==std::string::npos)return {};
    B+=Key.size()+1;auto E=Comment.find('@',B);return Trim(Comment.substr(B,E-B));
}
}
bool PrepareSource(const std::string& Source,FProgram& Out,std::string& Error)
{
    Out={};auto T=Tokens(Source);std::vector<FEdit> Edits;size_t UniformEnd=0;
    bool HasFloatPrecision = false;
    for (size_t I = 0; I + 3 < T.size(); ++I)
        if (T[I].Text == "precision" && T[I + 2].Text == "float" && T[I + 3].Text == ";")
            HasFloatPrecision = true;
    if (!HasFloatPrecision && !T.empty())
    {
        // The first token follows leading version/extension directives and comments.
        // Restore authored line numbers after supplying the ES fragment float environment.
        const size_t Begin = T.front().Begin;
        const size_t Line = 1 + std::count(Source.begin(), Source.begin() + Begin, '\n');
        Edits.push_back({Begin, Begin, "precision highp float;\n#line " + std::to_string(Line) + "\n"});
    }
    for(size_t I=0;I<T.size();++I)
    {
        if(T[I].Text=="gl_FragCoord")Out.UsesCoordinates=true;
        if(T[I].Text.rfind("gl_",0)==0&&T[I].Text!="gl_FragCoord"&&T[I].Text!="gl_FragColor")
        {Error="Pencil shader builtin is not supported: "+T[I].Text;return false;}
        if(T[I].Text!="uniform")continue;
        size_t J=I+1;if(J<T.size()&&(T[J].Text=="highp"||T[J].Text=="mediump"||T[J].Text=="lowp"))++J;
        if(J+2>=T.size()){Error="Incomplete shader uniform declaration";return false;}
        std::vector<std::string> Names;size_t Last=J+1;
        for(;;)
        {
            if(Last+1>=T.size()){Error="Incomplete shader uniform declaration";return false;}
            Names.push_back(T[Last].Text);++Last;
            if(T[Last].Text==";")break;
            if(T[Last].Text!=","){Error="Shader uniform arrays are not implemented";return false;}
            ++Last;
        }
        FUniform U;U.Type=T[J].Text;U.Name=Names[0];
        const std::vector<std::string> Allowed={"float","int","bool","vec2","vec3","vec4","ivec2","ivec3","ivec4","bvec2","bvec3","bvec4","sampler2D"};
        if(std::find(Allowed.begin(),Allowed.end(),U.Type)==Allowed.end()){Error="Shader uniform type is not implemented: "+U.Type;return false;}
        auto E=Source.rfind("*/",T[I].Begin);std::string Comment;
        if(E!=std::string::npos&&Trim(Source.substr(E+2,T[I].Begin-E-2)).empty())
        {auto B=Source.rfind("/*",E);if(B!=std::string::npos)Comment=Source.substr(B+2,E-B-2);}
        std::istringstream Lines(Comment);std::string Line,Clean;
        while(std::getline(Lines,Line)){Line=Trim(Line);if(!Line.empty()&&Line[0]=='*')Line=Trim(Line.substr(1));Clean+=Line+"\n";}
        Comment=Clean;
        U.Time=Comment.find("@time")!=std::string::npos;U.Resolution=Comment.find("@resolution")!=std::string::npos;
        U.Mouse=Comment.find("@mouse")!=std::string::npos;
        U.Sdf=Comment.find("@sdf")!=std::string::npos;
        const bool MipmapAnnotation=Comment.find("@mipmap")!=std::string::npos;
        if(MipmapAnnotation)
        {
            const std::string Value=Directive(Comment,"mipmap");
            if(Value!=""&&Value!="true"&&Value!="false"){Error="Shader @mipmap expects true or false";return false;}
            U.Mipmap=Value!="false";
        }
        if((U.Sdf||MipmapAnnotation)&&U.Type!="sampler2D"){Error="Shader SDF/mipmap annotations require sampler2D";return false;}
        if(U.Sdf&&(!Directive(Comment,"sdf").empty()||MipmapAnnotation||Comment.find("@default")!=std::string::npos))
        {Error="Shader @sdf takes no arguments, default, or mipmap annotation";return false;}
        U.DefaultValue=Directive(Comment,"default");
        if((U.Time&&U.Type!="float")||((U.Resolution||U.Mouse)&&U.Type!="vec2"&&U.Type!="ivec2")||int(U.Time)+int(U.Resolution)+int(U.Mouse)>1){Error="Invalid shader time/resolution declaration";return false;}
        for(const auto& UniformName:Names){auto Copy=U;Copy.Name=UniformName;Out.Uniforms.push_back(Copy);}
        UniformEnd=T[Last].End;I=Last;
    }
    // Resolve only parsed textureSize call expressions; identifiers/comments are untouched.
    std::vector<std::string> Sizes;
    for(size_t I=0;I<T.size();++I)if(T[I].Text=="textureSize"&&I+2<T.size()&&T[I+1].Text=="(")
    {
        size_t E=Close(T,I+1,"(",")");if(E==T.size()){Error="Unclosed textureSize call";return false;}
        const auto It=std::find_if(Out.Uniforms.begin(),Out.Uniforms.end(),[&](const FUniform& U){return U.Name==T[I+2].Text&&U.Type=="sampler2D";});
        if(It==Out.Uniforms.end()||I+3>E||(T[I+3].Text!=")"&&T[I+3].Text!=","))
        {Error="textureSize requires a declared sampler2D uniform";return false;}
        if(std::find(Sizes.begin(),Sizes.end(),It->Name)==Sizes.end())Sizes.push_back(It->Name);
        auto N=std::find(Sizes.begin(),Sizes.end(),It->Name)-Sizes.begin();
        const std::string Argument=T[I+3].Text==","?Source.substr(T[I+3].End,T[E].Begin-T[I+3].End):"";
        Edits.push_back({T[I].Begin,T[E].End,"D2UTextureSize"+std::to_string(N)+"("+Argument+")"});I=E;
    }
    std::string Added;
    for (size_t I = 0; I < Sizes.size(); ++I)
    {
        FUniform Size;
        Size.Type = "ivec2";
        Size.Name = "D2UTextureSizeValue" + std::to_string(I);
        Size.SizeOf = Sizes[I];
        Out.Uniforms.push_back(Size);
        FUniform Levels;
        Levels.Type = "int";
        Levels.Name = "D2UTextureLevels" + std::to_string(I);
        Levels.SizeOf = Sizes[I];
        Out.Uniforms.push_back(Levels);
        const std::string Function = "D2UTextureSize" + std::to_string(I);
        // Native binding supplies the available mip count, including non-mipmapped textures.
        Added += "\nuniform ivec2 " + Size.Name + ";\nuniform int " + Levels.Name + ";\n" +
            "ivec2 " + Function + "(){return " + Size.Name + ";}\n" +
            "ivec2 " + Function + "(int lod){return ivec2(max(floor(vec2(" + Size.Name +
            ")/exp2(clamp(float(lod),0.0,float(" + Levels.Name + "-1)))),vec2(1.0)));}\n";
    }
    if(!Added.empty())Edits.push_back({UniformEnd,UniformEnd,Added});
    Out.Source=Apply(Source,Edits);return true;
}
bool WrapHlsl(const std::string& Hlsl,FProgram& Program,std::string& Error)
{
    auto B=Hlsl.find("// INITIAL HLSL BEGIN"),E=Hlsl.find("// INITIAL HLSL END");
    if(B==std::string::npos||E==std::string::npos){Error="ANGLE initial HLSL markers are missing";return false;}
    B+=std::string("// INITIAL HLSL BEGIN").size();std::string S=Hlsl.substr(B,E-B);
    for(const auto& Hole:{std::string("@@ PIXEL OUTPUT @@"),std::string("@@ PIXEL MAIN PARAMETERS @@"),std::string("@@ MAIN PROLOGUE @@")})
    {auto P=S.find(Hole);if(P!=std::string::npos)S.erase(P,Hole.size());}
    auto T=Tokens(S);std::vector<FEdit> Edits;std::string Initialize;
    std::string TextureArray,SamplerArray;
    bool FoundMain=false,HasFragmentCoordinates=false;
    for(size_t I=0;I<T.size();)
    {
        if(T[I].Text=="@"){++I;continue;}
        if(T[I].Text=="cbuffer")
        {
            size_t J=I;while(J<T.size()&&T[J].Text!="{")++J;J=Close(T,J,"{","}");
            if(J+1>=T.size()){Error="Invalid ANGLE constant buffer";return false;}
            Edits.push_back({T[I].Begin,T[J+1].End,{}});I=J+2;continue;
        }
        size_t End=I;int Parentheses=0;
        while(End<T.size())
        {
            if(T[End].Text=="(")++Parentheses;if(T[End].Text==")")--Parentheses;
            if(Parentheses==0&&(T[End].Text==";"||T[End].Text=="{"))break;++End;
        }
        if(End==T.size())break;
        if(T[End].Text=="{"&&End>0&&T[End-1].Text==")")
        {
            size_t Open=I;while(Open<End&&T[Open].Text!="(")++Open;
            size_t Last=Close(T,End,"{","}");if(Last==T.size()){Error="Invalid ANGLE function body";return false;}
            if(Open>I&&T[Open-1].Text=="main")
            {
                Edits.push_back({T[I].Begin,T[End].End,"void Run(){"});FoundMain=true;
                for(size_t J=End+1;J+4<Last;++J)if(T[J].Text=="return"&&T[J+1].Text=="generateOutput"&&T[J+2].Text=="("&&T[J+3].Text==")"&&T[J+4].Text==";")
                    Edits.push_back({T[J].Begin,T[J+4].End,"return;"});
            }
            I=Last+1;continue;
        }
        if(T[End].Text=="{")
        { // A global array initializer, not a function.
            End=Close(T,End,"{","}");if(End+1>=T.size()){Error="Invalid ANGLE initializer";return false;}++End;
        }
        size_t Type=I;bool Uniform=false,Static=false,Const=false;
        while(Type<End&&(T[Type].Text=="uniform"||T[Type].Text=="static"||T[Type].Text=="const"))
        {Uniform|=T[Type].Text=="uniform";Static|=T[Type].Text=="static";Const|=T[Type].Text=="const";++Type;}
        size_t NameIndex=Type+1;
        if(NameIndex<End&&T[NameIndex].Text=="<")NameIndex=Close(T,NameIndex,"<",">")+1;
        if(NameIndex>=End){Error="Unsupported ANGLE global declaration";return false;}
        const std::string Name=T[NameIndex].Text,Kind=T[Type].Text;
        // ANGLE can discard a source builtin; only assign fields retained in the actual state.
        HasFragmentCoordinates |= Name=="gl_FragCoord";
        if(Uniform)Edits.push_back({T[I].Begin,T[Type].Begin,{}});
        if(Kind=="Texture2D")TextureArray=Name;
        if(Kind=="SamplerState")SamplerArray=Name;
        size_t Equal=End,Colon=End;
        for(size_t J=NameIndex+1;J<End;++J){if(T[J].Text=="="&&Equal==End)Equal=J;if(T[J].Text==":")Colon=J;}
        if(Colon<End)Edits.push_back({T[Colon].Begin,T[End].Begin,{}});
        for(auto& U:Program.Uniforms)if(Name=="_"+U.Name)
        {
            U.MappedName=Name;
            if(U.Type=="sampler2D")
            {if(Equal==End){Error="ANGLE sampler index is missing";return false;}U.TextureIndex=std::stoi(T[Equal+1].Text);}
        }
        // Custom.Code is function-local: globals become instance fields initialized in their own member scope.
        if(Static||Const)
        {
            Edits.push_back({T[I].Begin,T[Type].Begin,{}});
            bool Array=NameIndex+1<End&&T[NameIndex+1].Text=="[";
            if(Array&&Const)
            {
                if(Equal==End){Error="ANGLE constant array initializer is missing";return false;}
                const std::string LocalName="D2UInit_"+Name;
                std::string Declaration=S.substr(T[Type].Begin,T[End].End-T[Type].Begin);
                Declaration.replace(T[NameIndex].Begin-T[Type].Begin,Name.size(),LocalName);
                Initialize+="{"+Declaration+"\n"+Name+"="+LocalName+";}\n";
            }
            else if(Array)
            {
                if(Name!="gl_Color"){Error="Mutable global array initialization is not implemented";return false;}
                Initialize+=Name+"[0]=0;\n";
            }
            else
            {
                std::string Value=Equal<End?S.substr(T[Equal+1].Begin,T[End].Begin-T[Equal+1].Begin):"0";
                if(Equal+1<End&&T[Equal+1].Text=="{")
                    Value=Kind+"("+S.substr(T[Equal+1].End,T[End-1].Begin-T[Equal+1].End)+")";
                Initialize+=Name+"="+Value+";\n";
            }
            if(Equal<End)Edits.push_back({T[Equal].Begin,T[End].Begin,{}});
        }
        I=End+1;
    }
    if(!FoundMain){Error="ANGLE fragment entry is missing";return false;}
    for(const auto& Hole:{std::string("@@ PIXEL OUTPUT @@"),std::string("@@ MAIN PROLOGUE @@")})
    {auto P=S.find(Hole);if(P!=std::string::npos)Edits.push_back({P,P+Hole.size(),{}});}
    S=Apply(S,Edits);
    if(S.find("@@")!=std::string::npos){Error="Unconsumed ANGLE backend slot";return false;}
    // Driver-dependent builtins are not silently assigned fabricated values.
    for(const auto& Token:Tokens(S))if(Token.Text.rfind("dx_",0)==0){Error="ANGLE driver builtin is not implemented: "+Token.Text;return false;}
    // Pencil's shader space has an upward Y axis; Slate texture space has a downward Y axis.
    // Intercept ANGLE's actual texture operations, including calls from user helper functions.
    T=Tokens(S);Edits.clear();std::vector<std::string> Sampling;
    for(size_t I=0;I+5<T.size();++I)
    {
        if(T[I].Text!=TextureArray||T[I+1].Text!="[")continue;
        size_t IndexEnd=Close(T,I+1,"[","]");
        if(IndexEnd+3>=T.size()||T[IndexEnd+1].Text!="."||T[IndexEnd+3].Text!="(")continue;
        const std::string Mode=T[IndexEnd+2].Text;
        if(Mode!="Sample"&&Mode!="SampleBias"&&Mode!="SampleLevel"&&Mode!="SampleGrad")
        {Error="ANGLE texture operation is not implemented: "+Mode;return false;}
        size_t Last=Close(T,IndexEnd+3,"(",")"),First=IndexEnd+4;
        if(Last>=T.size()||First+2>=Last||T[First].Text!=SamplerArray||T[First+1].Text!="[")
        {Error="ANGLE texture sampler binding is invalid";return false;}
        size_t SamplerEnd=Close(T,First+1,"[","]");
        if(SamplerEnd+1>=Last||T[SamplerEnd+1].Text!=",")
        {Error="ANGLE texture coordinate argument is missing";return false;}
        std::string Index=S.substr(T[I+1].End,T[IndexEnd].Begin-T[I+1].End);
        std::string Arguments=S.substr(T[SamplerEnd+1].End,T[Last].Begin-T[SamplerEnd+1].End);
        Edits.push_back({T[I].Begin,T[Last].End,"D2U"+Mode+"("+Index+","+Arguments+")"});
        if(std::find(Sampling.begin(),Sampling.end(),Mode)==Sampling.end())Sampling.push_back(Mode);
        I=Last;
    }
    S=Apply(S,Edits);
    int TextureCount=0;
    for(const auto& U:Program.Uniforms)TextureCount=std::max(TextureCount,U.TextureIndex+1);
    if(TextureCount)
    {
        S+="\nfloat D2UBackgroundFlags["+std::to_string(TextureCount)+"];\n";
        S+="\nfloat D2UObjectFlags["+std::to_string(TextureCount)+"];float D2USdfFlags["+std::to_string(TextureCount)+"];float D2UMipmapFlags["+std::to_string(TextureCount)+"];\n";
        S+="float2 D2UResolution;float4 D2UDrawRect;float D2UFillShape;float4 D2UBackgroundU;float4 D2UBackgroundV;\n";
        S += "float2 D2UTextureUV(uint i,float2 uv){"
             "float2 p=float2(uv.x,1-uv.y);"
             "if(D2UBackgroundFlags[i]!=0)return float2(dot(float3(p,1),D2UBackgroundU.xyz),dot(float3(p,1),D2UBackgroundV.xyz));"
             "if(D2UObjectFlags[i]==0&&D2USdfFlags[i]==0)return p;"
             "float2 mapped=(D2USdfFlags[i]!=0&&D2UFillShape!=0)?p:(p*D2UResolution-D2UDrawRect.xy)/D2UDrawRect.zw;"
             "if(D2USdfFlags[i]!=0){uint width,height;" + TextureArray +
             "[i].GetDimensions(width,height);float2 halfTexel=0.5/float2(width,height);"
             "return clamp(mapped,halfTexel,1-halfTexel);}return mapped;}\n";
        S+="float2 D2UTextureScale(uint i){if((D2UObjectFlags[i]==0&&D2USdfFlags[i]==0)||(D2USdfFlags[i]!=0&&D2UFillShape!=0))return float2(1,-1);return float2(1,-1)*D2UResolution/D2UDrawRect.zw;}\n";
        S+="float2 D2UTextureDerivative(uint i,float2 d){if(D2UBackgroundFlags[i]!=0){float2 p=float2(d.x,-d.y);return float2(dot(p,D2UBackgroundU.xy),dot(p,D2UBackgroundV.xy));}return d*D2UTextureScale(i);}\n";
        S+="float D2UCoverage(uint i,float2 uv){float2 p=D2UTextureUV(i,uv);if(D2UBackgroundFlags[i]!=0)return float(all(p>=0)&&all(p<=1));return 1-D2UObjectFlags[i]+D2UObjectFlags[i]*float(all(p>=0)&&all(p<=1));}\n";
        for(const auto& Mode:Sampling)
        {
            const bool Grad=Mode=="SampleGrad",Extra=Mode=="SampleBias"||Mode=="SampleLevel";
            std::string Params=Grad?",float2 dx,float2 dy":Extra?",float extra":"";
            std::string Args=Grad?",D2UTextureDerivative(i,dx),D2UTextureDerivative(i,dy)":Extra?",extra":"";
            S+="float4 D2U"+Mode+"(uint i,float2 uv"+Params+"){if(D2UMipmapFlags[i]==0)return "+TextureArray+"[i].SampleLevel("+SamplerArray+"[i],D2UTextureUV(i,uv),0)*D2UCoverage(i,uv);return "+TextureArray+"[i]."+Mode+"("+SamplerArray+"[i],D2UTextureUV(i,uv)"+Args+")*D2UCoverage(i,uv);}\n";
        }
    }
    std::string Bindings;
    for(size_t I=0;I<Program.Uniforms.size();++I)
    {
        const auto& U=Program.Uniforms[I];if(U.MappedName.empty())continue;
        const std::string Input="Input"+std::to_string(I);
        if(U.Type=="sampler2D")
        {
            if(TextureArray.empty()||SamplerArray.empty()||U.TextureIndex<0){Error="ANGLE sampler resources are missing";return false;}
            const auto Index = std::to_string(U.TextureIndex);
            Bindings += "State.D2UMipmapFlags[" + Index + "]=" + (U.Mipmap ? "1" : "0") + ";\n";
            Bindings += "State.D2UObjectFlags[" + Index + "]=" + Input + "IsObject;\n";
        Bindings += "State.D2UBackgroundFlags[" + Index + "]=" + Input + "IsBackground;\n";
            Bindings += "State.D2USdfFlags[" + Index + "]=" + (U.Sdf ? "1" : "0") + ";\n";
            Bindings += "State." + TextureArray + "[" + Index + "]=" + Input + ";\nState." + SamplerArray + "[" + Index + "]=" + Input + "Sampler;\n";
        }
        else
        {
            std::string Cast;
            if(U.Type=="bool")Cast="bool";else if(U.Type=="int")Cast="int";
            else if(U.Type.rfind("ivec",0)==0)Cast="int"+U.Type.substr(4);
            else if(U.Type.rfind("bvec",0)==0)Cast="bool"+U.Type.substr(4);
            Bindings+="State."+U.MappedName+"="+(Cast.empty()?Input:Cast+"("+Input+")")+";\n";
        }
    }
    if(TextureCount)Bindings+="State.D2UResolution=Resolution;State.D2UDrawRect=DrawRect;State.D2UFillShape=FillShape;State.D2UBackgroundU=BackgroundU;State.D2UBackgroundV=BackgroundV;\n";
    std::string FragmentCoordinates;
    if(HasFragmentCoordinates)
        FragmentCoordinates="float2 NodePosition=DrawRect.xy+UV*DrawRect.zw;\nState.gl_FragCoord=float4(NodePosition.x,Resolution.y-NodePosition.y,0,1);\n";
    Program.Code="struct FPencilShader {\n"+S+"\nvoid D2UInitialize(){\n"+Initialize+"}\n};\nFPencilShader State;\n"+Bindings+"State.D2UInitialize();\n"+FragmentCoordinates+"State.Run();\nreturn State.gl_Color[0]*(Opacity*Mask.Sample(MaskSampler,UV).a);\n";
    return true;
}
}
