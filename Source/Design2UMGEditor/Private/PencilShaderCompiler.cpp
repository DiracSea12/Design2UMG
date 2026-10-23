// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilShaderCompiler.h"
#include "HAL/PlatformProcess.h"
#include "WebBrowserModule.h"
#if PLATFORM_WINDOWS
namespace
{
using P=void*;using I=int32;using U=uint32;
struct FAngle
{
    void* EGL=nullptr;
    void* GLES=nullptr;
    P Display=nullptr,Context=nullptr,Surface=nullptr,PreviousDisplay=nullptr,PreviousContext=nullptr,PreviousDraw=nullptr,PreviousRead=nullptr;
#define API(Name,Ret,...) Ret(__stdcall* Name)(__VA_ARGS__)=nullptr
    API(eglGetProcAddress,P,const char*);API(eglInitialize,U,P,I*,I*);API(eglChooseConfig,U,P,const I*,P*,I,I*);
    API(eglCreatePbufferSurface,P,P,P,const I*);API(eglBindAPI,U,U);API(eglCreateContext,P,P,P,P,const I*);
    API(eglMakeCurrent,U,P,P,P,P);API(eglDestroyContext,U,P,P);API(eglDestroySurface,U,P,P);API(eglTerminate,U,P);API(eglGetError,I);
    API(eglGetCurrentDisplay,P);API(eglGetCurrentContext,P);API(eglGetCurrentSurface,P,I);
    API(glGetError,U);API(glCreateShader,U,U);API(glShaderSource,void,U,I,const char*const*,const I*);API(glCompileShader,void,U);
    API(glGetShaderiv,void,U,U,I*);API(glGetShaderInfoLog,void,U,I,I*,char*);
    API(glGetTranslatedShaderSourceANGLE,void,U,I,I*,char*);API(glDeleteShader,void,U);
#undef API
    bool Open(FString& Error)
    {
        // The engine module owns CEF initialization and selects its matching ANGLE libraries.
        IWebBrowserModule::Get();
        GLES=FPlatformProcess::GetDllHandle(TEXT("libGLESv2.dll"));
        EGL=FPlatformProcess::GetDllHandle(TEXT("libEGL.dll"));
        if(!EGL||!GLES){Error=TEXT("Pencil shader requires the engine's x64 CEF EGL and GLES libraries");return false;}
#define LOAD(Lib,Name) Name=reinterpret_cast<decltype(Name)>(FPlatformProcess::GetDllExport(Lib,TEXT(#Name)));if(!Name){Error=TEXT("Missing ANGLE entry: ") TEXT(#Name);return false;}
        LOAD(EGL,eglGetProcAddress);LOAD(EGL,eglInitialize);LOAD(EGL,eglChooseConfig);LOAD(EGL,eglCreatePbufferSurface);
        LOAD(EGL,eglBindAPI);LOAD(EGL,eglCreateContext);LOAD(EGL,eglMakeCurrent);LOAD(EGL,eglDestroyContext);LOAD(EGL,eglDestroySurface);LOAD(EGL,eglTerminate);LOAD(EGL,eglGetError);
        LOAD(EGL,eglGetCurrentDisplay);LOAD(EGL,eglGetCurrentContext);LOAD(EGL,eglGetCurrentSurface);
#undef LOAD
        using FDisplay=P(__stdcall*)(U,P,const I*);
        auto Platform=reinterpret_cast<FDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if(!Platform){Error=TEXT("ANGLE platform display API is missing");return false;}
        PreviousDisplay=eglGetCurrentDisplay();PreviousContext=eglGetCurrentContext();PreviousDraw=eglGetCurrentSurface(0x3059);PreviousRead=eglGetCurrentSurface(0x305a);
        const I Attributes[]={0x3203,0x3208,0x3209,0x320b,0x3038};
        Display=Platform(0x3202,nullptr,Attributes);I Major=0,Minor=0;
        if(!Display||!eglInitialize(Display,&Major,&Minor)){Error=TEXT("ANGLE WARP initialization failed");return false;}
        const I ConfigAttrs[]={0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038};
        P Config=nullptr;I Count=0;
        if(!eglChooseConfig(Display,ConfigAttrs,&Config,1,&Count)||Count==0){Error=TEXT("ANGLE pbuffer config is missing");return false;}
        const I SurfaceAttrs[]={0x3057,8,0x3056,8,0x3038};Surface=eglCreatePbufferSurface(Display,Config,SurfaceAttrs);
        const I ContextAttrs[]={0x3098,2,0x3038};eglBindAPI(0x30a0);Context=eglCreateContext(Display,Config,nullptr,ContextAttrs);
        if(!Surface||!Context||!eglMakeCurrent(Display,Surface,Surface,Context)){Error=TEXT("ANGLE offscreen context creation failed");return false;}
        // EGL owns the client dispatch; a separately loaded GLES DLL may have a different thread context.
#define LOAD_GL(Name) Name=reinterpret_cast<decltype(Name)>(eglGetProcAddress(#Name));if(!Name){Error=TEXT("Missing ANGLE GL entry: ") TEXT(#Name);return false;}
        LOAD_GL(glGetError);LOAD_GL(glCreateShader);LOAD_GL(glShaderSource);LOAD_GL(glCompileShader);LOAD_GL(glGetShaderiv);
        LOAD_GL(glGetShaderInfoLog);LOAD_GL(glGetTranslatedShaderSourceANGLE);LOAD_GL(glDeleteShader);
#undef LOAD_GL
        return true;
    }
    ~FAngle()
    {
        if(Display&&eglMakeCurrent)
        {
            eglMakeCurrent(Display,nullptr,nullptr,nullptr);
            if(Context)eglDestroyContext(Display,Context);
            if(Surface)eglDestroySurface(Display,Surface);
            if(PreviousDisplay)eglMakeCurrent(PreviousDisplay,PreviousDraw,PreviousRead,PreviousContext);
            if(PreviousDisplay!=Display)eglTerminate(Display);
        }
        if(EGL)FPlatformProcess::FreeDllHandle(EGL);
        if(GLES)FPlatformProcess::FreeDllHandle(GLES);
    }
};
}
#endif
bool PencilShaderCompiler::Compile(const FString& Source,PencilShaderSyntax::FProgram& Out,FString& Error)
{
    std::string Message;
    if(!PencilShaderSyntax::PrepareSource(TCHAR_TO_UTF8(*Source),Out,Message)){Error=UTF8_TO_TCHAR(Message.c_str());return false;}
#if PLATFORM_WINDOWS
    FAngle Angle;if(!Angle.Open(Error))return false;
    const U Shader=Angle.glCreateShader(0x8b30);
    if(!Shader)
    {
        Error=FString::Printf(TEXT("Pencil GLSL shader creation failed: GL=0x%x EGL=0x%x"),
            Angle.glGetError(),Angle.eglGetError());return false;
    }
    const char* Bytes=Out.Source.c_str();const I Length=static_cast<I>(Out.Source.size());
    Angle.glShaderSource(Shader,1,&Bytes,&Length);Angle.glCompileShader(Shader);
    I Compiled=0,Size=0;Angle.glGetShaderiv(Shader,0x8b81,&Compiled);
    if(!Compiled)
    {
        Angle.glGetShaderiv(Shader,0x8b84,&Size);TArray<ANSICHAR> Log;Log.SetNumZeroed(FMath::Max(Size,1));
        Angle.glGetShaderInfoLog(Shader,Log.Num(),nullptr,Log.GetData());const U CompileError=Angle.glGetError();Angle.glDeleteShader(Shader);
        Error=FString::Printf(TEXT("Pencil GLSL compilation failed: %s [GL=0x%x]"),UTF8_TO_TCHAR(Log.GetData()),CompileError);return false;
    }
    Angle.glGetShaderiv(Shader,0x93a0,&Size);TArray<ANSICHAR> Hlsl;Hlsl.SetNumZeroed(FMath::Max(Size,1));
    Angle.glGetTranslatedShaderSourceANGLE(Shader,Hlsl.Num(),nullptr,Hlsl.GetData());Angle.glDeleteShader(Shader);
    if(!PencilShaderSyntax::WrapHlsl(Hlsl.GetData(),Out,Message)){Error=UTF8_TO_TCHAR(Message.c_str());return false;}
    return true;
#else
    Error=TEXT("Pencil shader translation currently requires engine ANGLE on Windows x64");return false;
#endif
}
