// Copyright 2022 Eidos-Montreal / Eidos-Sherbrooke

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http ://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "VdbPrincipledComponent.h"

#include "VdbCommon.h"
#include "VdbVolumeStatic.h"
#include "VdbVolumeSequence.h"
#include "VdbAssetComponent.h"
#include "VdbSequenceComponent.h"
#include "Rendering/VdbPrincipledRendering.h"
#include "Rendering/VdbPrincipledSceneProxy.h"

#include "RendererInterface.h"

#define LOCTEXT_NAMESPACE "VdbPrincipledComponent"

UVdbPrincipledComponent::UVdbPrincipledComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UVdbPrincipledComponent::~UVdbPrincipledComponent() {}

void UVdbPrincipledComponent::SetVdbAssets(UVdbAssetComponent* Comp)
{
	VdbAssets = Comp;
	Comp->OnFrameChanged.AddUObject(this, &UVdbPrincipledComponent::UpdateSceneProxy);
}

FPrimitiveSceneProxy* UVdbPrincipledComponent::CreateSceneProxy()
{
	if (!VdbAssets->DensityVolume || !VdbAssets->DensityVolume->IsValid() || VdbAssets->DensityVolume->IsVectorGrid())
		return nullptr;

	return new FVdbPrincipledSceneProxy(VdbAssets, this);
}

FBoxSphereBounds UVdbPrincipledComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (VdbAssets->DensityVolume != nullptr)
	{
		FBoxSphereBounds VdbBounds(VdbAssets->DensityVolume->GetGlobalBounds());
		return VdbBounds.TransformBy(LocalToWorld);
	}
	else
	{
		return Super::CalcBounds(LocalToWorld);
	}
}

void UVdbPrincipledComponent::UpdateSceneProxy(uint32 FrameIndex)
{
	FVdbPrincipledSceneProxy* VdbSceneProxy = static_cast<FVdbPrincipledSceneProxy*>(SceneProxy);
	if (VdbSceneProxy == nullptr)
	{
		return;
	}

	UVdbVolumeSequence* DensitySequence = (UVdbVolumeSequence*)VdbAssets->DensityVolume;
	const FVolumeRenderInfos* RenderInfosDensity = DensitySequence->GetRenderInfos(FrameIndex);

	UVdbVolumeSequence* TemperatureSequence = (UVdbVolumeSequence*)VdbAssets->TemperatureVolume;
	const FVolumeRenderInfos* RenderInfosTemperature = TemperatureSequence ? TemperatureSequence->GetRenderInfos(FrameIndex) : nullptr;

	UVdbVolumeSequence* ColorSequence = (UVdbVolumeSequence*)VdbAssets->ColorVolume;
	const FVolumeRenderInfos* RenderInfosColor = ColorSequence ? ColorSequence->GetRenderInfos(FrameIndex) : nullptr;

	if (RenderInfosDensity)
	{
		ENQUEUE_RENDER_COMMAND(UploadVdbGpuData)(
			[this,
			VdbSceneProxy,
			IndexMin = RenderInfosDensity->GetIndexMin(),
			IndexSize = RenderInfosDensity->GetIndexSize(),
			IndexToLocal = RenderInfosDensity->GetIndexToLocal(),
			DensityBuffer = RenderInfosDensity->GetRenderResource(),
			TemperatureBuffer = RenderInfosTemperature ? RenderInfosTemperature->GetRenderResource() : nullptr,
			ColorBuffer = RenderInfosColor ? RenderInfosColor->GetRenderResource() : nullptr]
		(FRHICommandList& RHICmdList)
		{
			VdbSceneProxy->Update(IndexToLocal, IndexMin, IndexSize, DensityBuffer, TemperatureBuffer, ColorBuffer);
		});

		if (VdbAssets->FloatVolume1 || VdbAssets->FloatVolume2 || VdbAssets->FloatVolume3 || VdbAssets->FloatVolume4 ||
			VdbAssets->VectorVolume1 || VdbAssets->VectorVolume2 || VdbAssets->VectorVolume3 || VdbAssets->VectorVolume4)
		{
			auto FillValue = [FrameIndex](UVdbVolumeBase* Base, FVdbRenderBuffer*& Buffer)
			{
				UVdbVolumeSequence* Seq = Cast<UVdbVolumeSequence>(Base);
				Buffer = Seq ? Seq->GetRenderInfos(FrameIndex)->GetRenderResource() : nullptr;
			};
			TStaticArray<FVdbRenderBuffer*, NUM_EXTRA_VDBS> Buffers;
			FillValue(VdbAssets->FloatVolume1, Buffers[0]);
			FillValue(VdbAssets->FloatVolume2, Buffers[1]);
			FillValue(VdbAssets->FloatVolume3, Buffers[2]);
			FillValue(VdbAssets->FloatVolume4, Buffers[3]);
			FillValue(VdbAssets->VectorVolume1, Buffers[4]);
			FillValue(VdbAssets->VectorVolume2, Buffers[5]);
			FillValue(VdbAssets->VectorVolume3, Buffers[6]);
			FillValue(VdbAssets->VectorVolume4, Buffers[7]);

			ENQUEUE_RENDER_COMMAND(UploadVdbGpuData)(
				[this, VdbSceneProxy, Buffers](FRHICommandList& RHICmdList)
				{
					VdbSceneProxy->UpdateExtraBuffers(Buffers);
				});
		}
	}
}


template<typename T>
void UVdbPrincipledComponent::SetAttribute(T& Attribute, const T& NewValue)
{
	if (AreDynamicDataChangesAllowed() && Attribute != NewValue)
	{
		Attribute = NewValue;
		MarkRenderStateDirty();
	}
}
template void UVdbPrincipledComponent::SetAttribute<float>(float& Attribute, const float& NewValue);
template void UVdbPrincipledComponent::SetAttribute<FLinearColor>(FLinearColor& Attribute, const FLinearColor& NewValue);

#undef LOCTEXT_NAMESPACE