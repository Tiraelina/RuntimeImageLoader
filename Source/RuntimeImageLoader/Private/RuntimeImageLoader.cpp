// Copyright 2022 Peter Leontev. All Rights Reserved.

#include "RuntimeImageLoader.h"
#include "Subsystems/SubsystemBlueprintLibrary.h"
#include "UObject/WeakObjectPtr.h"
#include "HAL/Platform.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuntimeImageLoader, Log, All);

void URuntimeImageLoader::Initialize(FSubsystemCollectionBase& Collection)
{
    InitializeImageReader();
}

void URuntimeImageLoader::Deinitialize()
{
    ImageReader->Deinitialize();
    ImageReader = nullptr;
}

bool URuntimeImageLoader::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::PIE || WorldType == EWorldType::Game;
}

void URuntimeImageLoader::LoadImageAsync(const FString& ImageFilename, const FTransformImageParams& TransformParams, UTexture2D*& OutTexture, bool& bSuccess, FString& OutError, FLatentActionInfo LatentInfo, UObject* WorldContextObject /*= nullptr*/)
{
    if (!IsValid(WorldContextObject))
    {
        return;
    }

    FLoadImageRequest Request;
    {
        Request.Params.ImageFilename = ImageFilename;
        Request.Params.TransformParams = TransformParams;

        Request.OnRequestCompleted.BindLambda(
            [this, &OutTexture, &bSuccess, &OutError, LatentInfo](const FImageReadResult& ReadResult)
            {
                FWeakObjectPtr CallbackTargetPtr = LatentInfo.CallbackTarget;
                if (UObject* CallbackTarget = CallbackTargetPtr.Get())
                {
                    UFunction* ExecutionFunction = CallbackTarget->FindFunction(LatentInfo.ExecutionFunction);
                    if (IsValid(ExecutionFunction))
                    {
                        int32 Linkage = LatentInfo.Linkage;

#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
                        // Make sure the texture was not destroyed by GC 
                        if (ReadResult.OutError.IsEmpty())
                        {
                            ensure(IsValid(ReadResult.OutTexture));
                        }
#endif

                        if (!ReadResult.OutError.IsEmpty())
                        {
                            UE_LOG(LogRuntimeImageLoader, Error, TEXT("Failed to load image. Error: %s"), *ReadResult.OutError);
                        }

                        bSuccess = ReadResult.OutError.IsEmpty();
                        OutTexture = ReadResult.OutTexture;
                        OutError = ReadResult.OutError;

                        if (Linkage != -1)
                        {
                            CallbackTarget->ProcessEvent(ExecutionFunction, &Linkage);
                        }
                    }
                }
            }
        );
    }

    Requests.Enqueue(Request);
}

void URuntimeImageLoader::LoadHDRIAsync(const FString& ImageFilename, const FTransformImageParams& TransformParams, UTextureCube*& OutTextureCube, bool& bSuccess, FString& OutError, FLatentActionInfo LatentInfo, UObject* WorldContextObject /*= nullptr*/)
{
    if (!IsValid(WorldContextObject))
    {
        return;
    }

    // TODO: loading cubemaps is not supported on Android platform! Use at your own risk!
    // TODO: opportunity for a pull request!
#if PLATFORM_ANDROID
    OutError = TEXT("Loading cubemaps is not supported on Android platform! Please build plugin from source to change this behaviour..");
    bSuccess = false;
    UE_LOG(LogRuntimeImageLoader, Warning, TEXT("%s"), *OutError);
    return;
#endif

    FLoadImageRequest Request;
    {
        Request.Params.ImageFilename = ImageFilename;
        Request.Params.TransformParams = TransformParams;

        Request.OnRequestCompleted.BindLambda(
            [this, &OutTextureCube, &bSuccess, &OutError, LatentInfo](const FImageReadResult& ReadResult)
            {
                FWeakObjectPtr CallbackTargetPtr = LatentInfo.CallbackTarget;
                if (UObject* CallbackTarget = CallbackTargetPtr.Get())
                {
                    UFunction* ExecutionFunction = CallbackTarget->FindFunction(LatentInfo.ExecutionFunction);
                    if (IsValid(ExecutionFunction))
                    {
                        int32 Linkage = LatentInfo.Linkage;

#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
                        // Make sure the texture was not destroyed by GC 
                        if (ReadResult.OutError.IsEmpty())
                        {
                            ensure(IsValid(ReadResult.OutTextureCube));
                        }
#endif

                        if (!ReadResult.OutError.IsEmpty())
                        {
                            UE_LOG(LogRuntimeImageLoader, Error, TEXT("Failed to load image. Error: %s"), *ReadResult.OutError);
                        }

                        bSuccess = ReadResult.OutError.IsEmpty();
                        OutTextureCube = ReadResult.OutTextureCube;
                        OutError = ReadResult.OutError;

                        if (Linkage != -1)
                        {
                            CallbackTarget->ProcessEvent(ExecutionFunction, &Linkage);
                        }
                    }
                }
            }
        );
    }

    Requests.Enqueue(Request);
}

void URuntimeImageLoader::LoadImageSync(const FString& ImageFilename, const FTransformImageParams& TransformParams, UTexture2D*& OutTexture, bool& bSuccess, FString& OutError)
{
    FImageReadRequest ReadRequest;
    {
        ReadRequest.ImageFilename = ImageFilename;
        ReadRequest.TransformParams = TransformParams;
    }

    ImageReader->BlockTillAllRequestsFinished();
    ImageReader->AddRequest(ReadRequest);
    ImageReader->BlockTillAllRequestsFinished();

    FImageReadResult ReadResult;
    ImageReader->GetResult(ReadResult);

    bSuccess = ReadResult.OutError.IsEmpty();
    OutTexture = ReadResult.OutTexture;
    OutError = ReadResult.OutError;
}

void URuntimeImageLoader::CancelAll()
{
    check (IsInGameThread());

    // TODO: Cancelling http request leads to crash on Android!
    // TODO: opportunity for a pull request!
#if PLATFORM_ANDROID
    UE_LOG(LogRuntimeImageLoader, Warning, TEXT("Cancelling http requests is not supported on Android platform!"));
    return;
#endif

    Requests.Empty();
    ActiveRequest.Invalidate();

    ImageReader->Clear();
}

void URuntimeImageLoader::Tick(float DeltaTime)
{
    ensure(IsValid(ImageReader));
    
    if (!ActiveRequest.IsRequestValid() && !Requests.IsEmpty())
    {
        Requests.Dequeue(ActiveRequest);

        FImageReadRequest ReadRequest(ActiveRequest.Params);

        ImageReader->AddRequest(ReadRequest);
        ImageReader->Trigger();
    }

    if (ActiveRequest.IsRequestValid() && ImageReader->IsWorkCompleted())
    {
        FImageReadResult ReadResult;
        ImageReader->GetResult(ReadResult);

        ensure(ActiveRequest.OnRequestCompleted.IsBound());

        ActiveRequest.OnRequestCompleted.Execute(ReadResult);

        ActiveRequest.Invalidate();
    }
}

TStatId URuntimeImageLoader::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(URuntimeImageLoader, STATGROUP_Tickables);
}

bool URuntimeImageLoader::IsAllowedToTick() const
{
    return !IsTemplate();
}

URuntimeImageReader* URuntimeImageLoader::InitializeImageReader()
{
    if (!IsValid(ImageReader))
    {
        ImageReader = NewObject<URuntimeImageReader>(this);
        ImageReader->Initialize();
    }

    ensure(IsValid(ImageReader));
    return ImageReader;
}
