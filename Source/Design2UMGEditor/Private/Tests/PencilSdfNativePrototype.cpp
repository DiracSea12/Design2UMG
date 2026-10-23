// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Tests/PencilSdfNativePrototype.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/Image.h"
#include "Components/RetainerBox.h"
#include "Design2UMGMaterialAccess.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialShared.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#if WITH_AUTOMATION_WORKER && PLATFORM_WINDOWS && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 7
namespace
{
constexpr int32 Extent = 128;
// Pencil 0.6.77 uses 5 fractional coordinate bits, sigma=Mc(6)=3 and Scharr gradients.
// The source identities and verbatim programs are frozen with this test's evidence.
const TCHAR* InitCode = TEXT(R"(
float2 p = UV * 128.0;
float a = Texture2DSampleLevel(Alpha, AlphaSampler, UV, 0).a;
float4 aa;
float2 offsets[4] = {float2(1,0),float2(0,1),float2(-1,0),float2(0,-1)};
[unroll] for(int k=0;k<4;k++) {
    float2 q = UV + offsets[k]/128.0;
    aa[k] = all(q>=0)&&all(q<1) ? Texture2DSampleLevel(Alpha,AlphaSampler,saturate(q),0).a : 0;
}
float4 edge = float4((a-.5)*(aa.x-.5)<0,(a-.5)*(aa.y-.5)<0,(a-.5)*(aa.z-.5)<0,(a-.5)*(aa.w-.5)<0);
float count = dot(edge,edge);
if(count==0) return float4(1,1,1,1);
float4 x = (.5-a)/(aa-a+(1-edge))*(edge/count);
int2 fixedPoint = (int2)((p+x.xy-x.zw)*32.0+.5);
return float4((fixedPoint.x>>8)&255,fixedPoint.x&255,(fixedPoint.y>>8)&255,fixedPoint.y&255)/255.0;
)");
// Intermediate reads address exact texels; alpha capture resampling retains its native sampler.
const TCHAR* JumpCode = TEXT(R"(
float2 p=UV*128.0;
float4 closest=1; float best=1e20;
for(int i=-1;i<=1;i++) for(int j=-1;j<=1;j++) {
    float2 q=clamp((p+float2(i,j)*Jump)/128.0,.5/128.0,127.5/128.0);
    float4 c=Input.Load(int3(int2(q*128.0),0));
    uint2 hi=(uint2)(c.xz*255.0+.5),lo=(uint2)(c.yw*255.0+.5);
    float2 d=(float2)((hi<<8)|lo)/32.0-p;
    float distance=dot(d,d);
    if(distance<best){best=distance;closest=c;}
}
return closest;
)");
const TCHAR* DistanceCode = TEXT(R"(
float4 c=Input.Load(int3(clamp(int2(UV*128.0),0,127),0));
uint2 hi=(uint2)(c.xz*255.0+.5),lo=(uint2)(c.yw*255.0+.5);
float2 closest=(float2)((hi<<8)|lo)/32.0;
float sign=Texture2DSampleLevel(Alpha,AlphaSampler,UV,0).a>=.5 ? 1 : -1;
return float4(sign*length(closest-UV*128.0)*Frame.r/128.0,0,0,1);
)");
const TCHAR* BlurCode = TEXT(R"(
float distance=0,weight=0;
// The separable sigma=3 Gaussian uses its +/-3 sigma support, with Clamp edges.
for(int i=-9;i<=9;i++) {
    float w=exp(-float(i*i)/18.0);
    float2 q=clamp(UV+Axis.xy*float(i)/128.0,.5/128.0,127.5/128.0);
    distance+=w*Input.Load(int3(int2(q*128.0),0)).r;weight+=w;
}
return float4(distance/weight,0,0,1);
)");
const TCHAR* GradientCode = TEXT(R"(
float d[9];int k=0;
for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++) {
    float2 q=clamp(UV+float2(x,y)/128.0,.5/128.0,127.5/128.0);
    d[k++]=Input.Load(int3(int2(q*128.0),0)).r;
}
float gx=(10.0/32.0)*(d[5]-d[3])+(3.0/32.0)*(d[2]-d[0]+d[8]-d[6]);
float gy=(10.0/32.0)*(d[1]-d[7])+(3.0/32.0)*(d[0]-d[6]+d[2]-d[8]);
return float4(d[4],gx,gy,1);
)");

template<class T> T* Expression(UMaterial* Material)
{
    T* Result=NewObject<T>(Material);
    Design2UMGMaterialAccess::AddExpression(*Material,*Result);
    return Result;
}

UMaterial* Material(const FString& Code,bool UserInterface,UTexture* Placeholder,FAutomationTestBase& Test)
{
    UMaterial* Result=NewObject<UMaterial>(GetTransientPackage());
    Result->MaterialDomain=UserInterface?MD_UI:MD_Surface;
    Result->BlendMode=UserInterface?BLEND_Translucent:BLEND_AlphaComposite;
    Result->SetShadingModel(MSM_Unlit);
    Result->bAllowNegativeEmissiveColor=true;
    auto* Custom=Expression<UMaterialExpressionCustom>(Result);
    Custom->Code=Code;Custom->OutputType=CMOT_Float4;
    const auto Input=[&](FName Name,UMaterialExpression* Value,int32 Output=0)
    {FCustomInput Item;Item.InputName=Name;Item.Input.Expression=Value;Item.Input.OutputIndex=Output;Custom->Inputs.Add(Item);};
    Input(TEXT("UV"),Expression<UMaterialExpressionTextureCoordinate>(Result));
    for(FName Name:{FName(TEXT("Input")),FName(TEXT("Alpha"))})
    {
        auto* Texture=Expression<UMaterialExpressionTextureObjectParameter>(Result);
        Texture->ParameterName=Name;Texture->Texture=Placeholder;
        Texture->SamplerType=Placeholder->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;
        Input(Name,Texture);
    }
    for(FName Name:{FName(TEXT("Frame")),FName(TEXT("Axis")),FName(TEXT("Counters"))})
    {
        auto* Vector=Expression<UMaterialExpressionVectorParameter>(Result);
        Vector->ParameterName=Name;
        Vector->DefaultValue=Name==TEXT("Frame")?FLinearColor(Extent,Extent,0,0):FLinearColor::Transparent;
        Input(Name,Vector,5);
    }
    for(FName Name:{FName(TEXT("Jump")),FName(TEXT("Radius"))})
    {
        auto* Scalar=Expression<UMaterialExpressionScalarParameter>(Result);
        Scalar->ParameterName=Name;Scalar->DefaultValue=Name==TEXT("Radius")?.2f:1.f;
        Input(Name,Scalar);
    }
    auto* RGB=Expression<UMaterialExpressionComponentMask>(Result);
    RGB->Input.Expression=Custom;RGB->R=RGB->G=RGB->B=true;
    auto* Alpha=Expression<UMaterialExpressionComponentMask>(Result);
    Alpha->Input.Expression=Custom;Alpha->A=true;
    Result->GetExpressionInputForProperty(MP_EmissiveColor)->Expression=RGB;
    if(UserInterface)Result->GetExpressionInputForProperty(MP_Opacity)->Expression=Alpha;
    else
    {
        auto* Invert=Expression<UMaterialExpressionOneMinus>(Result);Invert->Input.Expression=Alpha;
        Result->GetExpressionInputForProperty(MP_Opacity)->Expression=Invert;
    }
    Result->PreEditChange(nullptr);Result->PostEditChange();
    Design2UMGMaterialAccess::CacheShadersForRendering(*Result);
    Design2UMGMaterialAccess::WaitForCompilation(*Result);
    const FMaterialResource* Resource=Result->GetMaterialResource(GMaxRHIShaderPlatform);
    if(!Resource||!Resource->GetCompileErrors().IsEmpty()||!Resource->GetGameThreadShaderMap()||!Resource->GetGameThreadShaderMap()->IsValidForRendering())
    {
        Test.AddError(Resource?FString::Join(Resource->GetCompileErrors(),TEXT("; ")):TEXT("SDF prototype material has no resource"));
        return nullptr;
    }
    return Result;
}

// Test-time graph construction only. No C++ function is called by the generated runtime graph.
struct FPaintGraph
{
    UEdGraph* Graph;
    FAutomationTestBase& Test;
    bool Valid=true;
    UEdGraphPin* Exec=nullptr;
    template<class T> T* Add()
    {auto* Node=NewObject<T>(Graph);Graph->AddNode(Node,false,false);Node->CreateNewGuid();return Node;}
    void Link(UEdGraphPin* A,UEdGraphPin* B)
    {Valid&=A&&B&&CastChecked<UEdGraphSchema_K2>(Graph->GetSchema())->TryCreateConnection(A,B);}
    UK2Node_CallFunction* Call(UClass* Owner,FName Name)
    {
        UFunction* Function=Owner->FindFunctionByName(Name);
        if(!Function){Test.AddError(TEXT("Missing native function ")+Name.ToString());Valid=false;return nullptr;}
        auto* Node=Add<UK2Node_CallFunction>();Node->SetFromFunction(Function);Node->AllocateDefaultPins();
        if(UEdGraphPin* Input=Node->GetExecPin()){Link(Exec,Input);Exec=Node->GetThenPin();}
        return Node;
    }
    UEdGraphPin* Get(FName Name)
    {auto* Node=Add<UK2Node_VariableGet>();Node->VariableReference.SetSelfMember(Name);Node->AllocateDefaultPins();return Node->FindPin(Name);}
    UEdGraphPin* Self()
    {auto* Node=Add<UK2Node_Self>();Node->AllocateDefaultPins();return Node->FindPin(UEdGraphSchema_K2::PN_Self);}
    void Set(FName Name,UEdGraphPin* Value)
    {
        auto* Node=Add<UK2Node_VariableSet>();Node->VariableReference.SetSelfMember(Name);Node->AllocateDefaultPins();
        Link(Exec,Node->GetExecPin());if(Value)Link(Value,Node->FindPin(Name));Exec=Node->GetThenPin();
    }
    void Scalar(UEdGraphPin* MID,FName Name,UEdGraphPin* Value,const FString& Literal=FString())
    {
        auto* Node=Call(UMaterialInstanceDynamic::StaticClass(),TEXT("SetScalarParameterValue"));
        Link(MID,Node->FindPin(UEdGraphSchema_K2::PN_Self));Default(Node,TEXT("ParameterName"),Name.ToString());
        if(Value)Link(Value,Node->FindPin(TEXT("Value")));else Default(Node,TEXT("Value"),Literal);
    }
    void Texture(UEdGraphPin* MID,FName Name,UEdGraphPin* Value)
    {
        auto* Node=Call(UMaterialInstanceDynamic::StaticClass(),TEXT("SetTextureParameterValue"));
        Link(MID,Node->FindPin(UEdGraphSchema_K2::PN_Self));Default(Node,TEXT("ParameterName"),Name.ToString());Link(Value,Node->FindPin(TEXT("Value")));
    }
    UEdGraphPin* Return(UK2Node_CallFunction* Node){return Node?Node->GetReturnValuePin():nullptr;}
    void Default(UK2Node_CallFunction* Node,FName Name,const FString& Value)
    {
        auto* Pin=Node?Node->FindPin(Name):nullptr;
        if(!Pin){Valid=false;return;}
        CastChecked<UEdGraphSchema_K2>(Graph->GetSchema())->TrySetDefaultValue(*Pin,Value);
    }
    UEdGraphPin* Effect(UEdGraphPin* Retainer)
    {auto* Node=Call(URetainerBox::StaticClass(),TEXT("GetEffectMaterial"));Link(Retainer,Node->FindPin(UEdGraphSchema_K2::PN_Self));return Return(Node);}
    UEdGraphPin* ReadVector(UEdGraphPin* MID,FName Name)
    {auto* Node=Call(UMaterialInstanceDynamic::StaticClass(),TEXT("K2_GetVectorParameterValue"));Link(MID,Node->FindPin(UEdGraphSchema_K2::PN_Self));Default(Node,TEXT("ParameterName"),Name.ToString());return Return(Node);}
    void Vector(UEdGraphPin* MID,FName Name,UEdGraphPin* Value)
    {auto* Node=Call(UMaterialInstanceDynamic::StaticClass(),TEXT("SetVectorParameterValue"));Link(MID,Node->FindPin(UEdGraphSchema_K2::PN_Self));Link(Value,Node->FindPin(TEXT("Value")));Default(Node,TEXT("ParameterName"),Name.ToString());}
    UEdGraphPin* Part(UEdGraphPin* Value,FName Component)
    {auto* Node=Call(UKismetMathLibrary::StaticClass(),TEXT("BreakColor"));Link(Value,Node->FindPin(TEXT("InColor")));return Node->FindPin(Component);}
    UEdGraphPin* Color(UEdGraphPin* R,UEdGraphPin* G,UEdGraphPin* B)
    {
        auto* Node=Call(UKismetMathLibrary::StaticClass(),TEXT("MakeColor"));
        for(auto Pair:{TPair<FName,UEdGraphPin*>(TEXT("R"),R),{TEXT("G"),G},{TEXT("B"),B}})
            if(Pair.Value)Link(Pair.Value,Node->FindPin(Pair.Key));else Default(Node,Pair.Key,TEXT("0"));
        Default(Node,TEXT("A"),TEXT("0"));return Return(Node);
    }
    void Counter(UEdGraphPin* MID,FName Channel)
    {
        auto* Previous=ReadVector(MID,TEXT("Counters"));
        auto* AddValue=Call(UKismetMathLibrary::StaticClass(),TEXT("Add_DoubleDouble"));
        Link(Part(Previous,Channel),AddValue->FindPin(TEXT("A")));Default(AddValue,TEXT("B"),TEXT("1"));
        auto* R=Channel==TEXT("R")?Return(AddValue):Part(Previous,TEXT("R"));
        auto* G=Channel==TEXT("G")?Return(AddValue):Part(Previous,TEXT("G"));
        Vector(MID,TEXT("Counters"),Color(R,G,Part(Previous,TEXT("B"))));
    }
    void Draw(FName Pass,FName Target)
    {
        auto* Self=Add<UK2Node_Self>();Self->AllocateDefaultPins();
        auto* Clear=Call(UKismetRenderingLibrary::StaticClass(),TEXT("ClearRenderTarget2D"));
        Link(Self->FindPin(UEdGraphSchema_K2::PN_Self),Clear->FindPin(TEXT("WorldContextObject")));
        Link(Get(Target),Clear->FindPin(TEXT("TextureRenderTarget")));Default(Clear,TEXT("ClearColor"),TEXT("(R=0,G=0,B=0,A=1)"));
        auto* Paint=Call(UKismetRenderingLibrary::StaticClass(),TEXT("DrawMaterialToRenderTarget"));
        Link(Self->FindPin(UEdGraphSchema_K2::PN_Self),Paint->FindPin(TEXT("WorldContextObject")));
        Link(Get(Target),Paint->FindPin(TEXT("TextureRenderTarget")));Link(Get(Pass),Paint->FindPin(TEXT("Material")));
    }
};

const TArray<FName> TargetNames={TEXT("Coords0"),TEXT("Coords1"),TEXT("Distance"),TEXT("Blur"),TEXT("Sdf")};
TArray<FName> PassNames()
{
    TArray<FName> Result={TEXT("Init")};
    for(int32 I=0;I<8;++I)Result.Add(*FString::Printf(TEXT("Jump%d"),I));
    Result.Append({TEXT("DistancePass"),TEXT("BlurX"),TEXT("BlurY"),TEXT("Gradient")});return Result;
}

UWidgetBlueprint* SourceBlueprint(UMaterial* Source,FAutomationTestBase& Test)
{
    auto* Result=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(),GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UWidgetBlueprint::StaticClass(),TEXT("SdfSourceProbe")),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
    if(!Result)return nullptr;
    auto* Image=Result->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("Source"));
    Image->bIsVariable=true;Image->SetBrushFromMaterial(Source);Result->WidgetTree->RootWidget=Image;
    FEdGraphPinType RadiusType;RadiusType.PinCategory=UEdGraphSchema_K2::PC_Real;RadiusType.PinSubCategory=UEdGraphSchema_K2::PC_Float;
    if(!FBlueprintEditorUtils::AddMemberVariable(Result,TEXT("TargetRadius"),RadiusType,TEXT("0.2")))return nullptr;
    auto* Graph=FBlueprintEditorUtils::CreateNewGraph(Result,TEXT("OnPaint"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Result,Graph,false,UUserWidget::StaticClass());
    UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* ReturnNode=nullptr;
    for(UEdGraphNode* Node:Graph->Nodes){if(auto* E=Cast<UK2Node_FunctionEntry>(Node))Entry=E;if(auto* R=Cast<UK2Node_FunctionResult>(Node))ReturnNode=R;}
    if(!Entry||!ReturnNode){Test.AddError(TEXT("Source OnPaint has no native entry/result"));return nullptr;}
    FPaintGraph Paint{Graph,Test};Paint.Exec=Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    auto* MID=Paint.Call(UImage::StaticClass(),TEXT("GetDynamicMaterial"));Paint.Link(Paint.Get(TEXT("Source")),MID->FindPin(UEdGraphSchema_K2::PN_Self));
    Paint.Scalar(Paint.Return(MID),TEXT("Radius"),Paint.Get(TEXT("TargetRadius")));
    Paint.Link(Paint.Exec,ReturnNode->FindPin(UEdGraphSchema_K2::PN_Execute));
    if(Entry->FindPin(TEXT("Context"))&&ReturnNode->FindPin(TEXT("Context")))Paint.Link(Entry->FindPin(TEXT("Context")),ReturnNode->FindPin(TEXT("Context")));
    if(!Paint.Valid){Test.AddError(TEXT("Source OnPaint has a failed pin connection"));return nullptr;}
    FKismetEditorUtilities::CompileBlueprint(Result);
    if(Result->Status!=BS_UpToDate){Test.AddError(TEXT("Source OnPaint blueprint has compilation errors or warnings"));return nullptr;}
    UFunction* Function=Result->GeneratedClass->FindFunctionByName(TEXT("OnPaint"));
    if(!Function||Function->GetOuterUClass()!=Result->GeneratedClass||!Function->HasAnyFunctionFlags(FUNC_Const))return nullptr;
    return Result;
}

bool Lifecycle(UEdGraph* Events,const TArray<UMaterial*>& Materials,FAutomationTestBase& Test)
{
    const auto Event=[&](FName Name)
    {
        FPaintGraph Graph{Events,Test};auto* Node=Graph.Add<UK2Node_Event>();
        Node->EventReference.SetExternalMember(Name,UUserWidget::StaticClass());Node->bOverrideFunction=true;Node->AllocateDefaultPins();
        return Node;
    };
    FPaintGraph Init{Events,Test};Init.Exec=Event(TEXT("PreConstruct"))->GetThenPin();
    for(int32 I=0;I<TargetNames.Num();++I)
    {
        // PreConstruct may run again on the same instance; replace only its owned resources.
        auto* Release=Init.Call(UKismetRenderingLibrary::StaticClass(),TEXT("ReleaseRenderTarget2D"));Init.Link(Init.Get(TargetNames[I]),Release->FindPin(TEXT("TextureRenderTarget")));
        auto* Create=Init.Call(UKismetRenderingLibrary::StaticClass(),TEXT("CreateRenderTarget2D"));
        Init.Link(Init.Self(),Create->FindPin(TEXT("WorldContextObject")));
        Init.Default(Create,TEXT("Width"),TEXT("128"));Init.Default(Create,TEXT("Height"),TEXT("128"));
        Init.Default(Create,TEXT("Format"),(I==2||I==3)?TEXT("RTF_R16f"):TEXT("RTF_RGBA16f"));
        Init.Default(Create,TEXT("ClearColor"),TEXT("(R=0,G=0,B=0,A=1)"));Init.Set(TargetNames[I],Init.Return(Create));
    }
    const auto Names=PassNames();
    for(int32 I=0;I<Names.Num();++I)
    {
        auto* Create=Init.Call(UKismetMaterialLibrary::StaticClass(),TEXT("CreateDynamicMaterialInstance"));
        Init.Link(Init.Self(),Create->FindPin(TEXT("WorldContextObject")));Create->FindPin(TEXT("Parent"))->DefaultObject=Materials[I];
        Init.Set(Names[I],Init.Return(Create));
    }
    for(int32 I=0;I<8;++I)
    {
        Init.Scalar(Init.Get(Names[I+1]),TEXT("Jump"),nullptr,FString::FromInt(128>>I));
        Init.Texture(Init.Get(Names[I+1]),TEXT("Input"),Init.Get(TargetNames[I%2]));
    }
    Init.Texture(Init.Get(TEXT("DistancePass")),TEXT("Input"),Init.Get(TEXT("Coords0")));
    for(auto Pair:{TPair<FName,FName>(TEXT("BlurX"),TEXT("Distance")),{TEXT("BlurY"),TEXT("Blur")},{TEXT("Gradient"),TEXT("Distance")}})
        Init.Texture(Init.Get(Pair.Key),TEXT("Input"),Init.Get(Pair.Value));
    for(auto Pair:{TPair<FName,FString>(TEXT("BlurX"),TEXT("(R=1,G=0,B=0,A=0)")),{TEXT("BlurY"),TEXT("(R=0,G=1,B=0,A=0)")}})
    {
        auto* Axis=Init.Call(UMaterialInstanceDynamic::StaticClass(),TEXT("SetVectorParameterValue"));
        Init.Link(Init.Get(Pair.Key),Axis->FindPin(UEdGraphSchema_K2::PN_Self));Init.Default(Axis,TEXT("ParameterName"),TEXT("Axis"));Init.Default(Axis,TEXT("Value"),Pair.Value);
    }
    Init.Texture(Init.Effect(Init.Get(TEXT("Capture"))),TEXT("Input"),Init.Get(TEXT("Sdf")));
    FPaintGraph Destroy{Events,Test};Destroy.Exec=Event(TEXT("Destruct"))->GetThenPin();
    for(FName Name:TargetNames)
    {
        auto* Release=Destroy.Call(UKismetRenderingLibrary::StaticClass(),TEXT("ReleaseRenderTarget2D"));Destroy.Link(Destroy.Get(Name),Release->FindPin(TEXT("TextureRenderTarget")));Destroy.Set(Name,nullptr);
    }
    for(FName Name:Names)Destroy.Set(Name,nullptr);
    return Init.Valid&&Destroy.Valid;
}

UWidgetBlueprint* Blueprint(UWidgetBlueprint* Inner,UMaterial* Effect,const TArray<UMaterial*>& Materials,FAutomationTestBase& Test)
{
    auto* Result=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(),GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(),UWidgetBlueprint::StaticClass(),TEXT("SdfPaintProbe")),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
    if(!Result)return nullptr;
    auto* Retainer=Result->WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(),TEXT("Capture"));
    Retainer->bIsVariable=true;Retainer->SetEffectMaterial(Effect);Retainer->SetTextureParameter(TEXT("Alpha"));
    Result->WidgetTree->RootWidget=Retainer;
    auto* Child=Result->WidgetTree->ConstructWidget<UUserWidget>(TSubclassOf<UUserWidget>(Inner->GeneratedClass.Get()),TEXT("Inner"));
    Child->bIsVariable=true;Retainer->SetContent(Child);
    for(bool IsTarget:{true,false})for(FName Name:IsTarget?TargetNames:PassNames())
    {
        FEdGraphPinType Type;Type.PinCategory=UEdGraphSchema_K2::PC_Object;
        Type.PinSubCategoryObject=IsTarget?UTextureRenderTarget2D::StaticClass():UMaterialInstanceDynamic::StaticClass();
        if(!FBlueprintEditorUtils::AddMemberVariable(Result,Name,Type))return nullptr;
    }
    auto* Graph=FBlueprintEditorUtils::CreateNewGraph(Result,TEXT("OnPaint"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(Result,Graph,false,UUserWidget::StaticClass());
    UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* ReturnNode=nullptr;
    for(UEdGraphNode* Node:Graph->Nodes){if(auto* E=Cast<UK2Node_FunctionEntry>(Node))Entry=E;if(auto* R=Cast<UK2Node_FunctionResult>(Node))ReturnNode=R;}
    if(!Entry||!ReturnNode){Test.AddError(TEXT("OnPaint override has no native entry/result"));return nullptr;}
    FPaintGraph Paint{Graph,Test};Paint.Exec=Entry->FindPin(UEdGraphSchema_K2::PN_Then);
    auto* MID=Paint.Effect(Paint.Get(TEXT("Capture")));
    // The fixture's user OnPaint chain precedes the generated work.
    Paint.Counter(MID,TEXT("R"));
    auto* Geometry=Paint.Call(UWidget::StaticClass(),TEXT("GetCachedGeometry"));Paint.Link(Paint.Get(TEXT("Capture")),Geometry->FindPin(UEdGraphSchema_K2::PN_Self));
    auto* Size=Paint.Call(USlateBlueprintLibrary::StaticClass(),TEXT("GetLocalSize"));Paint.Link(Paint.Return(Geometry),Size->FindPin(TEXT("Geometry")));
    auto* Split=Paint.Call(UKismetMathLibrary::StaticClass(),TEXT("BreakVector2D"));Paint.Link(Paint.Return(Size),Split->FindPin(TEXT("InVec")));
    auto* Frame=Paint.Color(Split->FindPin(TEXT("X")),Split->FindPin(TEXT("Y")),nullptr);
    Paint.Vector(MID,TEXT("Frame"),Frame);Paint.Vector(Paint.Get(TEXT("DistancePass")),TEXT("Frame"),Frame);
    auto* Alpha=Paint.Call(UMaterialInstanceDynamic::StaticClass(),TEXT("K2_GetTextureParameterValue"));
    Paint.Link(MID,Alpha->FindPin(UEdGraphSchema_K2::PN_Self));Paint.Default(Alpha,TEXT("ParameterName"),TEXT("Alpha"));
    for(FName Pass:{FName(TEXT("Init")),FName(TEXT("DistancePass"))})
    {
        auto* Bind=Paint.Call(UMaterialInstanceDynamic::StaticClass(),TEXT("SetTextureParameterValue"));
        Paint.Link(Paint.Get(Pass),Bind->FindPin(UEdGraphSchema_K2::PN_Self));Paint.Default(Bind,TEXT("ParameterName"),TEXT("Alpha"));Paint.Link(Paint.Return(Alpha),Bind->FindPin(TEXT("Value")));
    }
    Paint.Draw(TEXT("Init"),TEXT("Coords0"));
    for(int32 I=0;I<8;++I)Paint.Draw(*FString::Printf(TEXT("Jump%d"),I),I%2?TEXT("Coords0"):TEXT("Coords1"));
    Paint.Draw(TEXT("DistancePass"),TEXT("Distance"));Paint.Draw(TEXT("BlurX"),TEXT("Blur"));Paint.Draw(TEXT("BlurY"),TEXT("Distance"));Paint.Draw(TEXT("Gradient"),TEXT("Sdf"));
    Paint.Link(Paint.Exec,ReturnNode->FindPin(UEdGraphSchema_K2::PN_Execute));
    if(Entry->FindPin(TEXT("Context"))&&ReturnNode->FindPin(TEXT("Context")))Paint.Link(Entry->FindPin(TEXT("Context")),ReturnNode->FindPin(TEXT("Context")));
    auto* Events=FBlueprintEditorUtils::CreateNewGraph(Result,TEXT("EventGraph"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());FBlueprintEditorUtils::AddUbergraphPage(Result,Events);
    FPaintGraph User{Events,Test};auto* Tick=User.Add<UK2Node_Event>();Tick->EventReference.SetExternalMember(TEXT("Tick"),UUserWidget::StaticClass());Tick->bOverrideFunction=true;Tick->AllocateDefaultPins();User.Exec=Tick->GetThenPin();User.Counter(User.Effect(User.Get(TEXT("Capture"))),TEXT("G"));
    Test.TestEqual(TEXT("User Tick retains its own execution chain"),Tick->GetThenPin()->LinkedTo.Num(),1);
    const bool LifecycleValid=Lifecycle(Events,Materials,Test);
    if(!Paint.Valid||!User.Valid||!LifecycleValid){Test.AddError(TEXT("Native SDF graph has a failed pin connection"));return nullptr;}
    FKismetEditorUtilities::CompileBlueprint(Result);
    if(Result->Status!=BS_UpToDate){Test.AddError(TEXT("Native SDF OnPaint blueprint has compilation errors or warnings"));return nullptr;}
    UFunction* Function=Result->GeneratedClass->FindFunctionByName(TEXT("OnPaint"));
    if(!Function||Function->GetOuterUClass()!=Result->GeneratedClass||!Function->HasAnyFunctionFlags(FUNC_Const))return nullptr;
    return Result;
}

struct FInstance
{
    UUserWidget* Widget=nullptr;
    UMaterialInstanceDynamic* Effect=nullptr;
    UMaterialInstanceDynamic* Source=nullptr;
    TArray<UTextureRenderTarget2D*> Targets;
    TArray<UMaterialInstanceDynamic*> Passes;
};

UObject* Object(UUserWidget* Widget,FName Name)
{
    FObjectPropertyBase* Property=FindFProperty<FObjectPropertyBase>(Widget->GetClass(),Name);
    return Property?Property->GetObjectPropertyValue_InContainer(Widget):nullptr;
}

bool Observe(FInstance& Instance,UUserWidget* Widget,FAutomationTestBase& Test)
{
    Instance.Widget=Widget;
    for(FName Name:TargetNames)
    {
        auto* Target=Cast<UTextureRenderTarget2D>(Object(Widget,Name));
        if(!Target||!Target->GetResource()){Test.AddError(TEXT("PreConstruct did not initialize SDF RT: ")+Name.ToString());return false;}
        Instance.Targets.Add(Target);
    }
    for(FName Name:PassNames())
    {
        auto* MID=Cast<UMaterialInstanceDynamic>(Object(Widget,Name));
        if(!MID){Test.AddError(TEXT("PreConstruct did not initialize SDF MID: ")+Name.ToString());return false;}
        Test.TestTrue(TEXT("PreConstruct MID belongs to its actual editor-world instance"),MID->GetOuter()==Widget);
        Instance.Passes.Add(MID);
    }
    auto* Retainer=Cast<URetainerBox>(Widget->WidgetTree->FindWidget(TEXT("Capture")));
    Instance.Effect=Retainer?Retainer->GetEffectMaterial():nullptr;
    if(!Instance.Effect){Test.AddError(TEXT("Native retainer effect is unavailable after PreConstruct"));return false;}
    if(auto* Content=Cast<UUserWidget>(Widget->WidgetTree->FindWidget(TEXT("Inner"))))
        if(auto* Source=Cast<UImage>(Content->WidgetTree->FindWidget(TEXT("Source"))))Instance.Source=Source->GetDynamicMaterial();
    return true;
}
}

bool PencilSdfNativePrototype::Run(FAutomationTestBase& Test)
{
    auto* Deferred=IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.DeferRetainedRenderingRenderThread"));
    if(!Deferred||Deferred->GetInt()!=0){Test.AddError(TEXT("This prototype requires the unchanged non-deferred Windows Retainer mode"));return false;}
    UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    if(!World){Test.AddError(TEXT("SDF prototype has no real editor World"));return false;}
    TArray<TStrongObjectPtr<UObject>> Keep;
    auto* Placeholder=UKismetRenderingLibrary::CreateRenderTarget2D(World,Extent,Extent,RTF_RGBA16f,FLinearColor(0,0,0,1));
    if(!Placeholder){Test.AddError(TEXT("SDF placeholder RT creation failed"));return false;}
    Keep.Emplace(Placeholder);
    TArray<UMaterial*> Programs;
    for(const TCHAR* Code:{InitCode,JumpCode,DistanceCode,BlurCode,GradientCode})
    {
        auto* Pass=Material(Code,false,Placeholder,Test);if(!Pass){UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);return false;}
        Keep.Emplace(Pass);Programs.Add(Pass);
    }
    TArray<UMaterial*> Materials={Programs[0]};
    for(int32 I=0;I<8;++I)Materials.Add(Programs[1]);
    Materials.Append({Programs[2],Programs[3],Programs[3],Programs[4]});
    auto* Source=Material(TEXT("return float4(1,1,1,length(UV-.5)<Radius?1:0);"),true,Placeholder,Test);
    auto* Effect=Material(TEXT("float d=Input.Load(int3(clamp(int2(UV*128.0),0,127),0)).r;float3 source=Texture2DSampleLevel(Alpha,AlphaSampler,UV,0).rgb;return float4(source+Counters.r*1e-20+Counters.g*1e-20+Frame.r*1e-20,d>=0?1:0);"),true,Placeholder,Test);
    if(!Source||!Effect){UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);return false;}
    Keep.Emplace(Source);Keep.Emplace(Effect);
    auto* SourceOwner=SourceBlueprint(Source,Test);if(!SourceOwner){UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);return false;}Keep.Emplace(SourceOwner);
    auto* Inner=Blueprint(SourceOwner,Effect,Materials,Test);if(!Inner){UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);return false;}Keep.Emplace(Inner);
    auto* Outer=Blueprint(Inner,Effect,Materials,Test);if(!Outer){UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);return false;}Keep.Emplace(Outer);
    TArray<FInstance> Instances;TArray<UUserWidget*> Roots;TArray<TStrongObjectPtr<UUserWidget>> KeepRoots;TArray<TSharedPtr<SWidget>> SlateRoots;
    auto* Output=UKismetRenderingLibrary::CreateRenderTarget2D(World,Extent,Extent,RTF_RGBA16f);Keep.Emplace(Output);
    if(!Output){UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);Test.AddError(TEXT("SDF output RT creation failed"));return false;}
    bool Valid=true;
    for(int32 I=0;I<2;++I)
    {
        auto* Widget=CreateWidget<UUserWidget>(World,TSubclassOf<UUserWidget>(Outer->GeneratedClass.Get()));if(!Widget){Valid=false;break;}
        KeepRoots.Emplace(Widget);Roots.Add(Widget);
        auto* Child=Cast<UUserWidget>(Widget->WidgetTree->FindWidget(TEXT("Inner")));
        if(!Child){Valid=false;break;}
        if(I==0)
        {
            const auto Flags=EWidgetDesignFlags::Designing|EWidgetDesignFlags::ExecutePreConstruct;
            Widget->SetDesignerFlags(Flags);Child->SetDesignerFlags(Flags);
            CastChecked<UUserWidget>(Child->WidgetTree->FindWidget(TEXT("Inner")))->SetDesignerFlags(Flags);
        }
        SlateRoots.Add(Widget->TakeWidget());
        const int32 Parent=Instances.AddDefaulted(),Descendant=Instances.AddDefaulted();
        if(!Observe(Instances[Parent],Widget,Test)||!Observe(Instances[Descendant],Child,Test)){Valid=false;break;}
    }
    if(Valid)
    {
        Test.TestTrue(TEXT("Native Retainers create independent MIDs from the same UMaterial asset"),Instances[0].Effect!=Instances[2].Effect&&Instances[1].Effect!=Instances[3].Effect);
        Test.TestTrue(TEXT("Source and pass MIDs are per instance"),Instances[1].Source!=Instances[3].Source&&Instances[1].Passes[0]!=Instances[3].Passes[0]);
        Test.TestTrue(TEXT("SDF outputs are per instance"),Instances[0].Targets[4]!=Instances[2].Targets[4]);
        for(const FInstance& Instance:Instances)
        {
            Test.TestTrue(TEXT("Native instance has the real editor World"),Instance.Widget->GetWorld()==World);
            for(const UTextureRenderTarget2D* Target:Instance.Targets)Test.TestTrue(TEXT("Each SDF RT belongs to its native instance"),Target->GetOuter()==Instance.Widget);
        }
        Test.TestTrue(TEXT("Both nested instances participate in Designing preview"),Instances[0].Widget->IsDesignTime()&&Instances[1].Widget->IsDesignTime());
        FWidgetRenderer Renderer(true);
        const auto Draw=[&](int32 Index,int32 Size)
        {
            for(int32 N=Index*2;N<Index*2+2;++N)CastChecked<URetainerBox>(Instances[N].Widget->WidgetTree->FindWidget(TEXT("Capture")))->RequestRender();
            const double Start=FPlatformTime::Seconds();
            Renderer.DrawWidget(Output,SlateRoots[Index].ToSharedRef(),FVector2D(Size,Size),1.f/60.f,false);
            const double CPU=FPlatformTime::Seconds()-Start;
            const double ReadStart=FPlatformTime::Seconds();
            const FLinearColor Parent=UKismetRenderingLibrary::ReadRenderTargetRawPixel(World,Instances[Index*2].Targets[4],100,64,false);
            const FLinearColor Child=UKismetRenderingLibrary::ReadRenderTargetRawPixel(World,Instances[Index*2+1].Targets[4],100,64,false);
            Test.AddInfo(FString::Printf(TEXT("SDF %d size=%d parent=(%.4f,%.4f,%.4f) child=(%.4f,%.4f,%.4f) CPU DrawWidget wall=%.3fms blocking readback=%.3fms GPU=unknown"),Index,Size,Parent.R,Parent.G,Parent.B,Child.R,Child.G,Child.B,CPU*1000,(FPlatformTime::Seconds()-ReadStart)*1000));
            return TPair<FLinearColor,FLinearColor>(Parent,Child);
        };
        auto A=Draw(0,Extent);auto B=Draw(1,Extent);
        Test.TestTrue(TEXT("Initial dynamic and nested distances are outside"),A.Key.R<0&&A.Value.R<0&&B.Key.R<0&&B.Value.R<0);
        const FLinearColor Other=B.Key;
        auto* SourceInstance=CastChecked<UUserWidget>(Instances[1].Widget->WidgetTree->FindWidget(TEXT("Inner")));
        auto* Radius=FindFProperty<FFloatProperty>(SourceInstance->GetClass(),TEXT("TargetRadius"));
        if(!Radius){Test.AddError(TEXT("Source blueprint has no target radius property"));Valid=false;}
        else Radius->SetPropertyValue_InContainer(SourceInstance,.4f);
        Test.TestEqual(TEXT("Source material is unchanged before its native OnPaint"),Instances[1].Source->K2_GetScalarParameterValue(TEXT("Radius")),.2f);
        A=Draw(0,Extent);
        Test.TestEqual(TEXT("Source OnPaint applied the target radius before capture"),Instances[1].Source->K2_GetScalarParameterValue(TEXT("Radius")),.4f);
        Test.TestTrue(TEXT("Changed source reaches its SDF and ancestor SDF in the same paint"),A.Value.R>0&&A.Key.R>0);
        Test.TestTrue(TEXT("Both true distance fields carry their right-side Scharr gradient"),A.Value.G<0&&A.Key.G<0);
        const FLinearColor Untouched=UKismetRenderingLibrary::ReadRenderTargetRawPixel(World,Instances[2].Targets[4],100,64,false);
        Test.TestEqual(TEXT("Other instance's stored SDF is unchanged"),Untouched,Other);
        B=Draw(1,Extent);Test.TestTrue(TEXT("Other instance remains outside after repaint"),B.Key.R<0&&B.Value.R<0);
        const auto Resized=Draw(0,96);
        const FLinearColor Frame=Instances[0].Effect->K2_GetVectorParameterValue(TEXT("Frame"));
        Test.TestTrue(TEXT("Designing OnPaint uses actual resized geometry"),FMath::IsNearlyEqual(Frame.R,96.f)&&FMath::IsNearlyEqual(Frame.G,96.f));
        Test.TestTrue(TEXT("Current layout scale reaches both distance passes in the same paint"),Resized.Key.R>0&&Resized.Key.R<A.Key.R&&Resized.Value.R>0&&Resized.Value.R<A.Value.R);
        Test.TestTrue(TEXT("User OnPaint remains active in Designing"),Instances[0].Effect->K2_GetVectorParameterValue(TEXT("Counters")).R>=3);
        Test.TestEqual(TEXT("Designing does not run the user's Tick"),Instances[0].Effect->K2_GetVectorParameterValue(TEXT("Counters")).G,0.f);
        Test.AddInfo(TEXT("Private SDF RTs: four subtrees x (3 RGBA16f + 2 R16f) x 128 squared = 1835008 base bytes; native Retainer/output RTs and RHI overhead are additional, not measured VRAM."));
    }
    else Test.AddError(TEXT("SDF native instance initialization failed"));
    TArray<TWeakObjectPtr<UTextureRenderTarget2D>> DesignerTargets;
    if(Instances.Num()==4)
        for(int32 I=0;I<2;++I)for(auto* Target:Instances[I].Targets)DesignerTargets.Add(Target);
    SlateRoots.Reset();
    for(UUserWidget* Root:Roots)Root->ReleaseSlateResources(true);
    FlushRenderingCommands();
    if(Instances.Num()==4)
    {
        for(int32 I=2;I<4;++I)for(auto* Target:Instances[I].Targets)
            Test.TestNull(TEXT("Native Destruct releases non-Designing instance RT resources"),Target->GetResource());
        for(const auto& Target:DesignerTargets)
            Test.TestTrue(TEXT("Designer skips Destruct and retains RT resource before GC"),Target.IsValid()&&Target->GetResource()!=nullptr);
        for(FName Name:TargetNames)
            Test.TestNull(TEXT("Native Destruct clears its RT member"),Object(Instances[2].Widget,Name));
    }
    KeepRoots.Reset();Roots.Reset();Instances.Reset();
    CollectGarbage(RF_NoFlags,true);
    for(const auto& Target:DesignerTargets)
        Test.TestFalse(TEXT("Designer RT weak reference is invalid after native GC"),Target.IsValid());
    UKismetRenderingLibrary::ReleaseRenderTarget2D(Output);UKismetRenderingLibrary::ReleaseRenderTarget2D(Placeholder);
    Test.AddInfo(TEXT("Native PreConstruct owns RT/MID initialization; source OnPaint updates before its capture, SDF OnPaint after it. Non-Designing Destruct releases resources; Designer RT objects are observed through native GC, not instantaneous VRAM telemetry."));
    return Valid&&!Test.HasAnyErrors();
}
#endif
