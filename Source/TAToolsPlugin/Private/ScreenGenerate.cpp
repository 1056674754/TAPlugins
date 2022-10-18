// Fill out your copyright notice in the Description page of Project Settings.


#include "ScreenGenerate.h"
#include "Misc/Paths.h"
#include  <stdio.h>

#if PLATFORM_WINDOWS
#include  <direct.h>
#endif
#include "Uobject/Object.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Camera/CameraTypes.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "FoliageType.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Canvas.h"

#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/ximgproc.hpp"
#include "opencv2/core.hpp"
#include "uefast_line_detector.h"
#include "MinVolumeSphere3.h"
#include "MinVolumeBox3.h"

#include "PolygonProcess.h"
#include "Async/Async.h"
#include "Math/TransformCalculus3D.h"

//????ObjectTransform??????
#define ObjectTransformType_Forward 0 //????????????x??, ???????, ????????????
#define ObjectTransformType_Up 2 //????????????Z????·????????. ????????
#define ObjectTransformType_ForwardFoliage_OverTarget 1 //???????????Z????0 Z?????????(0,0,1) ????????淨???Forward
#define debug = 0

using namespace std;
using namespace cv;
using namespace ximgproc;
using namespace UE::Geometry;

//ScreenProcess::ScreenProcess(ASceneCapture2D* InSceneCapture, TArray<UStaticMesh*> InInputMeshs, FVector BoxExtent, FVector Center)
//	: SceneCapture(InSceneCapture)
//	, Meshs(InInputMeshs)
//	, BoxExtent(BoxExtent)
//	, Center(Center)
//{
//}
FConsiderMesh::FConsiderMesh(UStaticMesh* StaticMesh)
	: StaticMesh(StaticMesh)
{
	OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(StaticMesh->GetMeshDescription(0), *OriginalMesh);
}

FConsiderMeshY::FConsiderMeshY(UStaticMesh* StaticMesh)
	:FConsiderMesh(StaticMesh)
{
	//StaticMesh = StaticMesh;
}


//void FConsiderMesh::ProcessMesh()
//{
//
//}

bool UScreenGenerate::BoxAreaFoliageGenerate(ASceneCapture2D* SceneCapture, TArray<AActor*> PickActors, TArray<UStaticMesh*> InputMeshs, UTexture2D* ReferenceTextrue, float BlockGenerate, float DivideScale)
{
	FVector Center, BoxExtent;
	UGameplayStatics::GetActorArrayBounds(PickActors, true, Center, BoxExtent);
	
	if (!SceneCapture || BoxExtent.X < 10 || InputMeshs.Num() == 0)
	{
		return false;
	}
	//TSharedPtr<OpenAssetProcess, ESPMode::ThreadSafe> OpenAsset = MakeShareable(new OpenAssetProcess());
	TSharedPtr<ScreenProcess, ESPMode::ThreadSafe> ColorData = MakeShareable(new ProcessFoliage());
	TSharedPtr<SceneCalculate, ESPMode::ThreadSafe> SceneCalculator = MakeShareable(new SceneCalculate(ColorData));
	ColorData->SceneCapture = SceneCapture;
	ColorData->Meshs = InputMeshs;
	ColorData->BoxExtent = BoxExtent;
	ColorData->Center = Center;
	ColorData->PickActors = PickActors;
	ColorData->OutTexture = ReferenceTextrue;
	ColorData->Block = BlockGenerate;
	ColorData->DivdeSize = DivideScale;
	ColorData->FCalDelegate.AddThreadSafeSP(ColorData.ToSharedRef(), &ScreenProcess::CalculateStaticMesh);
	ColorData->FScenceCaptureCalDelegate.AddThreadSafeSP(SceneCalculator.ToSharedRef(), &SceneCalculate::Calculate);
	SceneCalculator->Calculate();

	return true;
}

bool UScreenGenerate::BoxAreaOpenAssetGenerate(ASceneCapture2D* SceneCapture, TArray<AActor*> PickActors, TArray<UStaticMesh*> InputMeshs, UTexture2D* ReferenceTextrue, float BlockGenerate, float DivideScale)
{
	FVector Center, BoxExtent;
	UGameplayStatics::GetActorArrayBounds(PickActors, true, Center, BoxExtent);

	if (!SceneCapture || BoxExtent.X < 10 || InputMeshs.Num() == 0)
	{
		return false;
	}
	TSharedPtr<ScreenProcess, ESPMode::ThreadSafe> ColorData = MakeShareable(new ProcessOpenAsset());
	TSharedPtr<SceneCalculate, ESPMode::ThreadSafe> SceneCalculator = MakeShareable(new SceneCalculate(ColorData));
	ColorData->SceneCapture = SceneCapture;
	ColorData->Meshs = InputMeshs;
	ColorData->BoxExtent = BoxExtent;
	ColorData->Center = Center;
	ColorData->PickActors = PickActors;
	ColorData->OutTexture = ReferenceTextrue;
	ColorData->Block = BlockGenerate;
	ColorData->Switch = 1;
	ColorData->DivdeSize = DivideScale;
	ColorData->ObjectScale = FVector::OneVector * float(FMath::FRandRange(1.1, 3.1));

	ASceneCaptureContainter* SceneCaptureContainter = nullptr;
	TArray<AActor*> FindOutActors;
	UGameplayStatics::GetAllActorsOfClass(GWorld, ASceneCaptureContainter::StaticClass(), FindOutActors);
	//ASceneCaptureContainter* SceneCaptureContainter = nullptr;
	if (FindOutActors.Num() == 0)
	{
		FActorSpawnParameters Params;
		SceneCaptureContainter = GWorld->SpawnActor<ASceneCaptureContainter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	else
	{
		SceneCaptureContainter = Cast<ASceneCaptureContainter>(FindOutActors[0]);
	}

	TMap<UStaticMesh*, TArray<FVector>> MeshOpenVertexMap = SceneCaptureContainter->MeshOpenVertexMap;
	for (UStaticMesh* StaticMesh : InputMeshs)
	{
		if (MeshOpenVertexMap.Find(StaticMesh))
		{
			continue;
		}
		TArray<FVector> Vertices;
		TSharedPtr<FDynamicMesh3> OriginalMesh = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(StaticMesh->GetMeshDescription(0), *OriginalMesh);


		//initialize topology
		TUniquePtr<FBasicTopologyFindPosition> Topology = MakeUnique<FBasicTopologyFindPosition>(OriginalMesh.Get(), false);
		Topology->GetVerticesPosition(Vertices);
		MeshOpenVertexMap.Add(StaticMesh, Vertices);
	}
	SceneCaptureContainter->MeshOpenVertexMap = MeshOpenVertexMap;
	ColorData->MeshOpenVertexMap = MeshOpenVertexMap;
	ColorData->FCalDelegate.AddThreadSafeSP(ColorData.ToSharedRef(), &ScreenProcess::CalculateStaticMesh);
	ColorData->FScenceCaptureCalDelegate.AddThreadSafeSP(SceneCalculator.ToSharedRef(), &SceneCalculate::Calculate);
	SceneCalculator->Calculate();

	return true;
}

bool UScreenGenerate::BoxAreaLineFoliageGenerate(ASceneCapture2D* SceneCapture, TArray<AActor*> PickActors,
                                                 TArray<UStaticMesh*> InputMeshs, FVector SourceCenter, FVector SourceExtent,
                                                 float BlockGenerate, float DivideScale, bool DebugImg)
{

	FVector Center, BoxExtent;
	UGameplayStatics::GetActorArrayBounds(PickActors, true, Center, BoxExtent);

	if (!SceneCapture || BoxExtent.X < 10 || InputMeshs.Num() == 0)
	{
		return false;
	}
	TSharedPtr<ScreenProcess, ESPMode::ThreadSafe> ColorData = MakeShareable(new ProcessLineFoliage());
	TSharedPtr<SceneCalculate, ESPMode::ThreadSafe> SceneCalculator = MakeShareable(new SceneCalculate(ColorData));
	ColorData->SceneCapture = SceneCapture;
	ColorData->Meshs = InputMeshs;
	ColorData->BoxExtent = BoxExtent;
	ColorData->Center = Center;
	ColorData->PickActors = PickActors;
	//ColorData->OutTexture = ReferenceTextrue;
	ColorData->Block = BlockGenerate;
	ColorData->DivdeSize = DivideScale;
	ColorData->DebugImg = DebugImg;

	ColorData->Switch = 1;
	ColorData->FCalDelegate.AddThreadSafeSP(ColorData.ToSharedRef(), &ScreenProcess::CalculateStaticMesh);
	ColorData->FScenceCaptureCalDelegate.AddThreadSafeSP(SceneCalculator.ToSharedRef(), &SceneCalculate::Calculate);
	SceneCalculator->Calculate();

	return true;
}

//????????????????????????. ?????????????A????B  B?????A???????. ???????о??????. ????????????????.
void SceneCalculate::Calculate()
{
	if (ColorData->Setup())
	{
		auto AsyncTast = new FAutoDeleteAsyncTask<FAsyncTasksTemplate<ScreenProcess>>(ColorData);
		AsyncTast->StartBackgroundTask();
	}
}


bool ScreenProcess::Setup()
{	
	CaptureSize = DivdeSize;
	TArray<AActor*> FindOutActors;
	UGameplayStatics::GetAllActorsOfClass(World, ASceneCaptureContainter::StaticClass(), FindOutActors);
	//ASceneCaptureContainter* SceneCaptureContainter = nullptr;
	if (FindOutActors.Num() == 0)
	{
		FActorSpawnParameters Params;
		SceneCaptureContainter = World->SpawnActor<ASceneCaptureContainter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);

		//SceneCaptureContainter->GenerateBlockBox();
	}
	else
	{
		SceneCaptureContainter = Cast<ASceneCaptureContainter>(FindOutActors[0]);
	}
	//???????????????????????.
	SceneCaptureContainter->PickActors = PickActors;
	SceneCaptureContainter->DivdeSize = DivdeSize;
	SceneCaptureContainter->Block = Block;
	//????????transform
	CaptureTransforms = SceneCaptureContainter->CaptureTransforms;
	
	if (CaptureTransforms.Num() == 0)
	{
		SceneCaptureContainter->GenerateTransforms();
	}
	if(CaptureTransforms.Num() == 0)
	{
		return false;
	}
	
	CurrentTransform = SceneCaptureContainter->CaptureTransforms.Pop();
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes{ UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldDynamic), UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic) };
	TArray<AActor*> ActorsToIgnore;
	bool OverlapPickActor = false;
	FHitResult OutHit;
	//?????????????????. ?????????????????????. ????е???????????????????.
	if (UKismetSystemLibrary::BoxTraceSingleForObjects(World, CurrentTransform.GetLocation(),
	                                                   CurrentTransform.GetLocation(), FVector(0, DivdeSize, DivdeSize),
	                                                   CurrentTransform.GetRotation().Rotator(), ObjectTypes, true,
	                                                   ActorsToIgnore, EDrawDebugTrace::None, OutHit, true))
	{
		return false;
	}
	//?????????б??????Transform???????. ???????????д????.
	CamIndex = -1;
	for(int32 i = 0; i < SceneCaptureContainter->StoreCaptureTransforms.Num(); i++)
	{
		FTransform Transform = SceneCaptureContainter->StoreCaptureTransforms[i];
		if(CurrentTransform.GetLocation() == Transform.GetLocation() &&CurrentTransform.GetRotation() == Transform.GetRotation())
		{
			CamIndex = i+1;
			break;
		}
	}
	SceneCapture->SetActorTransform(CurrentTransform);
	//DynamicMeshData
	DynamicMeshData = SceneCaptureContainter->DynamicMeshData;
	for (UStaticMesh* Mesh : Meshs)
	{
		if (DynamicMeshData.Find(Mesh))
		{
			continue;
		}
		TSharedPtr<FDynamicMesh3> OriginalMesh = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(Mesh->GetMeshDescription(0), *OriginalMesh);
		DynamicMeshData.Add(Mesh, OriginalMesh);
	}
	SceneCaptureContainter->DynamicMeshData = DynamicMeshData;
	//????ж??SceneCapture????????????Count??????
	SceneCaptureCount += 1;
	TArray<FName> Tags;
	Tags.Add(FName(TEXT("%d"), SceneCaptureCount));
	//
	if (SceneCapture->GetCaptureComponent2D()->TextureTarget)
	{
		RTSize = SceneCapture->GetCaptureComponent2D()->TextureTarget->SizeX;
	}
	else
	{
		RTSize = 128;
	}	Width = RTSize;
	Height = RTSize;
	//CaptureSize *= 1.5;
	PixelSize = CaptureSize / RTSize;
	RandomScale = FMath::FRandRange(.8, 1.2);

	SceneCapture->GetCaptureComponent2D()->OrthoWidth = CaptureSize;
	DirRight = SceneCapture->GetActorRightVector();
	DirUp = SceneCapture->GetActorUpVector();
	Forward = SceneCapture->GetActorForwardVector();
	CurrentTransform = SceneCapture->GetActorTransform();
	Root = CurrentTransform.GetLocation() - DirRight * CaptureSize / 2 + DirUp * CaptureSize / 2;


	return true;
}

void ScreenProcess::CalculateStaticMesh()
{
	CacheImg();
	ProcessImg();
}

void ScreenProcess::CacheImg()
{
	ColorArray2D.Empty();
	SortIdx.Empty();
	DepthArray.Empty();
	for (int i = 0; i < Height; i++)
	{
		TArray<ColorCheck> ColumnArray;
		for (int n = 0; n < Width; n++)
		{
			ColorCheck HitColor;
			float ScreenDepthMax = 10000;
			FVector Start = Root + PixelSize / 2 - i * DirUp * PixelSize + n * DirRight * PixelSize;
			FVector End = Start + Forward * ScreenDepthMax;
			TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes{ UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic) };
			TArray<AActor*> ActorsToIgnore;
			FHitResult OutHit;
			bool Hit = UKismetSystemLibrary::LineTraceSingle(SceneCapture->GetWorld(), Start, End, ETraceTypeQuery::TraceTypeQuery1, true, ActorsToIgnore, EDrawDebugTrace::None, OutHit, true);
			if (Hit)
			{
				HitColor.Color = FLinearColor(0, 0, 0, OutHit.Distance);
				HitColor.TraceActor = OutHit.GetActor();
				HitColor.Normal = OutHit.Normal;
				HitColor.WorldPos = OutHit.Location;
				HitColor.Item = OutHit.Item;
				HitColor.Unchecked = false;
				if (PickActors.Find(HitColor.TraceActor)>=0 && OutHit.Distance > 100)
				{
					HitColor.Unchecked = true;
				}
				float Dot = FVector::DotProduct(-Forward, OutHit.Normal);
				SortIdx.Add(FVector2i(i, n), Dot);
				//DepthArray
			}
			ColumnArray.Add(HitColor);
		}
		ColorArray2D.Add(ColumnArray);
	}
	
	SortIdx.ValueSort([](float A, float B) { return A > B; });
	
}

//????????????
bool ScreenProcess::ProcessImgFoliage()
{
	int32 index = FMath::RandRange(0, Meshs.Num() - 1);
	CurrenStaticMesh = Meshs[index];
	FBoxSphereBounds Bounds = CurrenStaticMesh->GetBounds();
	FVector2D XZBound = FVector2D(Bounds.BoxExtent.X, Bounds.BoxExtent.Z);
	ErodeBoundPixels = XZBound / PixelSize * RandomScale;
	//??????overlap????. ??????????????????????. ???????????.
	ErodeNumPixels = FMath::Min(ErodeBoundPixels.X, ErodeBoundPixels.Y);

	OtherSides.Empty();
	//CaptureTransforms.Empty();
	//???????. ????????????ж?????????????????.
	//????????????????????????н????????????.?????????????俪????. ??????????????SortIdx???趨.
	for (TTuple<FVector2i, float>& Pair : SortIdx)
	{
		int32 i = Pair.Key.X;
		int32 n = Pair.Key.Y;
		if (ColorArray2D[i][n].Color.A <= 0 || ColorArray2D[i][n].Unchecked == false)
		{
			continue;
		}
		//???????????????
		PickPixelArray.Empty();
		FVector2i SearchIndex = FVector2i(i, n);
		if (!CheckNormalImg(SearchIndex, .7))
		{
			continue;
		}

		FVector2i MaxPixel, MinPixel;
		FVector2i PixelBound = PixelBox(MaxPixel, MinPixel);
		//???????????С???????
		if (PixelBound.X - ErodeNumPixels <= 0 || PixelBound.Y - ErodeNumPixels <= 0)
		{
			continue;
		}
		
		//?????????????(??????????????????????????????????????ó????ε????)
		TArray<FVector2i> PickPixelArrayErode = ScreenErode();
		
		//?????????????????????????????????.
		if (PickPixelArrayErode.Num() == 0)
		{
			continue;
		}

		for (int32 c = 0; c < PickPixelArrayErode.Num(); c++)
		{
			PickPixelArrayErode.Swap(c, FMath::RandRange(0, PickPixelArrayErode.Num() - 1));
		}
		//???5?? ???????????;
		for (int32 c = 0; c < FMath::Min(PickPixelArrayErode.Num(), 5); c++)
		{
			
			//FVector2i PickPixel = PickPixelArrayErode[FMath::RandRange(0, PickPixelArrayErode.Num() - 1)];
			FVector2i PickPixel = PickPixelArrayErode[c];

			//???????????????????????. 
			//float TargetZ = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].Color.A;
			//????涨?????????1050??500???????????????????
			//if (TargetZ > 1050 || TargetZ < 500)
			//{
			//	continue;
			//}

			//??????????Transform, ??1????????????????Forward
			ObjectTransform = CreateObjectTransform(PickPixel, ObjectTransformType_ForwardFoliage_OverTarget);

			//??????????????, ??????峬?????????????false
			Mat ObjectImg;
			if (!RasterizeMesh(CurrenStaticMesh, ObjectTransform, ObjectImg))
			{
				continue;
			}
			Mat PickPixelImg = Array2DToMatBinarization(PickPixelArray);
			TArray<FVector2i> Test = MatBinarizationToVector2D(ObjectImg);

			//????overlap
			if (OverlapImgCheck(PickPixelImg, ObjectImg))
			{
				continue;
			}
			FMeshData MeshData;
			MeshData.Mesh = CurrenStaticMesh;
			MeshData.Transform = ObjectTransform;
			MeshData.Count = SceneCaptureCount;
			SceneCaptureContainter->MeshData.Add(MeshData);
			//FScenceCaptureCalDelegate.Broadcast();
			//SceneCaptureComponent->ComponentTags.Add(FName(TEXT("Destory")));
			
			//???????????????
			AsyncTask(ENamedThreads::GameThread, [&]()
				{
					SpawnStaticMesh();

				});
			break;
		}
	}
	return true;
}

bool ProcessFoliage::ProcessImg()
{
	int32 index = FMath::RandRange(0, Meshs.Num() - 1);
	CurrenStaticMesh = Meshs[index];
	FBoxSphereBounds Bounds = CurrenStaticMesh->GetBounds();
	FVector2D XZBound = FVector2D(Bounds.BoxExtent.X, Bounds.BoxExtent.Z);
	ErodeBoundPixels = XZBound / PixelSize * RandomScale;
	//??????overlap????. ??????????????????????. ???????????.
	ErodeNumPixels = FMath::Min(ErodeBoundPixels.X, ErodeBoundPixels.Y);

	OtherSides.Empty();
	//CaptureTransforms.Empty();
	//???????. ????????????ж?????????????????.
	//????????????????????????н????????????.?????????????俪????. ??????????????SortIdx???趨.
	for (TTuple<FVector2i, float>& Pair : SortIdx)
	{
		int32 i = Pair.Key.X;
		int32 n = Pair.Key.Y;
		if (ColorArray2D[i][n].Color.A <= 0 || ColorArray2D[i][n].Unchecked == false)
		{
			continue;
		}
		//???????????????
		PickPixelArray.Empty();
		FVector2i SearchIndex = FVector2i(i, n);
		if (!CheckNormalImg(SearchIndex, .7))
		{
			continue;
		}

		FVector2i MaxPixel, MinPixel;
		FVector2i PixelBound = PixelBox(MaxPixel, MinPixel);
		//???????????С???????
		if (PixelBound.X - ErodeNumPixels <= 0 || PixelBound.Y - ErodeNumPixels <= 0)
		{
			continue;
		}

		//?????????????(??????????????????????????????????????ó????ε????)
		TArray<FVector2i> PickPixelArrayErode = ScreenErode();

		//?????????????????????????????????.
		if (PickPixelArrayErode.Num() == 0)
		{
			continue;
		}

		for (int32 c = 0; c < PickPixelArrayErode.Num(); c++)
		{
			PickPixelArrayErode.Swap(c, FMath::RandRange(0, PickPixelArrayErode.Num() - 1));
		}
		//???5?? ???????????;
		for (int32 c = 0; c < FMath::Min(PickPixelArrayErode.Num(), 5); c++)
		{

			//FVector2i PickPixel = PickPixelArrayErode[FMath::RandRange(0, PickPixelArrayErode.Num() - 1)];
			FVector2i PickPixel = PickPixelArrayErode[c];

			//???????????????????????. 
			//float TargetZ = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].Color.A;
			//????涨?????????1050??500???????????????????
			//if (TargetZ > 1050 || TargetZ < 500)
			//{
			//	continue;
			//}

			//??????????Transform, ??1????????????????Forward
			ObjectTransform = CreateObjectTransform(PickPixel, ObjectTransformType_ForwardFoliage_OverTarget);

			//??????????????, ??????峬?????????????false
			Mat ObjectImg;
			if (!RasterizeMesh(CurrenStaticMesh, ObjectTransform, ObjectImg))
			{
				continue;
			}
			Mat PickPixelImg = Array2DToMatBinarization(PickPixelArray);
			TArray<FVector2i> Test = MatBinarizationToVector2D(ObjectImg);

			//????overlap
			if (OverlapImgCheck(PickPixelImg, ObjectImg))
			{
				continue;
			}
			FMeshData MeshData;
			MeshData.Mesh = CurrenStaticMesh;
			MeshData.Transform = ObjectTransform;
			MeshData.Count = SceneCaptureCount;
			SceneCaptureContainter->MeshData.Add(MeshData);
			//FScenceCaptureCalDelegate.Broadcast();
			//SceneCaptureComponent->ComponentTags.Add(FName(TEXT("Destory")));

			//???????????????
			AsyncTask(ENamedThreads::GameThread, [&]()
				{
					SpawnStaticMesh();

				});
			break;
		}
	}
	return true;
}

bool ProcessLineFoliage::ProcessImg()
{
	Mat Img(Height, Width, CV_8UC1, Scalar::all(0));
	LineImg = Img;
	OutReferenceImg = Img;
	//SceneCaptureContainter->ReferenceImg = Img;
	int32 index = FMath::RandRange(0, Meshs.Num() - 1);
	CurrenStaticMesh = Meshs[index];
	FBoxSphereBounds Bounds = CurrenStaticMesh->GetBounds();
	FVector2D XZBound = FVector2D(Bounds.BoxExtent.X, Bounds.BoxExtent.Z);
	ErodeBoundPixels = XZBound / PixelSize * RandomScale;
	//??????overlap????. ??????????????????????. ???????????.
	ErodeNumPixels = FMath::Min(ErodeBoundPixels.X, ErodeBoundPixels.Y);

	OtherSides.Empty();
	//???????. ????????????ж?????????????????.
	//????????????????????????н????????????.?????????????俪????. ??????????????SortIdx???趨.
	for (TTuple<FVector2i, float>& Pair : SortIdx)
	{
		int32 i = Pair.Key.X;
		int32 n = Pair.Key.Y;
		if (ColorArray2D[i][n].Color.A <= 0 || ColorArray2D[i][n].Unchecked == false)
		{
			continue;
		}
		//???????????????
		PickPixelArray.Empty();
		FVector2i SearchIndex = FVector2i(i, n);
		if (!CheckNormalImg(SearchIndex, .7))
		{
			continue;
		}
		LineDetection();
	}
	
	OutReferenceTArray = MatBinarizationToVector2D(OutReferenceImg);
	//?й???PickPixelArraya??????
	SceneCaptureContainter->ReferenceImg1 = Array2DToMatBinarization(PickPixelArray);
	//?й?????????????
	SceneCaptureContainter->ReferenceImg2 = OutReferenceImg;
	AsyncTask(ENamedThreads::GameThread, [&]()
		{
			SpawnStaticMesh();
			if(DebugImg)
			{
				
				TArray<AActor*> ContainerActors;
				UGameplayStatics::GetAllActorsOfClass(GWorld, ASceneCaptureContainter::StaticClass(), ContainerActors);
				if(ContainerActors.Num() > 0)
				{
					ASceneCaptureContainter* SceneCaptureContainter = Cast<ASceneCaptureContainter>(ContainerActors[0]);
					DrawDebugTexture(SceneCaptureContainter->ReferenceImg1, 1);
					DrawDebugTexture(SceneCaptureContainter->ReferenceImg2, 2);
											
				}
			}
			

		});
	return true;
}

bool ProcessOpenAsset::ProcessImg()
{
	//?????????????????????????????. ??????????????????????????.
	int32 index = FMath::RandRange(0, Meshs.Num() - 1);
	CurrenStaticMesh = Meshs[index];
	FBoxSphereBounds Bounds = CurrenStaticMesh->GetBounds();
	FVector2D XZBound = FVector2D(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
	ErodeBoundPixels = XZBound / PixelSize * .8;
	//??????overlap????. ??????????????????????. ???????????.
	ErodeNumPixels = FMath::Min(ErodeBoundPixels.X, ErodeBoundPixels.Y);

	OtherSides.Empty();
	//CaptureTransforms.Empty();
	//???????. ????????????ж?????????????????.
	//????????????????????????н????????????.?????????????俪????. ??????????????SortIdx???趨.
	for (TTuple<FVector2i, float>& Pair : SortIdx)
	{
		int32 i = Pair.Key.X;
		int32 n = Pair.Key.Y;
		bool BoundingPixel = (FMath::Abs(i - Height/2) >= Height/2 - ErodeNumPixels) || (FMath::Abs(n - Width) >= Height / 2 - ErodeNumPixels);
		if (ColorArray2D[i][n].Color.A <= 0 || ColorArray2D[i][n].Unchecked == false || BoundingPixel)
		{
			continue;
		}
		//???????????????
		PickPixelArray.Empty();
		FVector2i SearchIndex = FVector2i(i, n);
		if (!CheckNormalImg(SearchIndex, .5))
		{
			continue;
		}

		FVector2i MaxPixel, MinPixel;
		FVector2i PixelBound = PixelBox(MaxPixel, MinPixel);
		//???????????С???????
		if (PixelBound.X - ErodeNumPixels <= 0 || PixelBound.Y - ErodeNumPixels <= 0)
		{
			//continue;
		}

		//?????????????????????????????. ??????????????????????????.
		//?????????????(??????????????????????????????????????ó????ε????)
		TArray<FVector2i> PickPixelArrayErode = PickPixelArray;

		//?????????????????????????????????.
		if (PickPixelArrayErode.Num() == 0)
		{
			continue;
		}

		for (int32 c = 0; c < PickPixelArrayErode.Num(); c++)
		{
			PickPixelArrayErode.Swap(c, FMath::RandRange(0, PickPixelArrayErode.Num() - 1));
		}


		UE_LOG(LogTemp, Warning, TEXT("ThreadDone"));
		//???5?? ???????????;
		for (int32 c = 0; c < FMath::Min(PickPixelArrayErode.Num(), 5); c++)
		{

			//FVector2i PickPixel = PickPixelArrayErode[FMath::RandRange(0, PickPixelArrayErode.Num() - 1)];
			FVector2i PickPixel = PickPixelArrayErode[c];

			//???????????????????????. 
			//float TargetZ = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].Color.A;
			//????涨?????????1050??500???????????????????
			//if (TargetZ > 1050 || TargetZ < 500)
			//{
			//	continue;
			//}

			//??????????Transform, ??1????????????????Forward
			ObjectTransform = CreateObjectTransform(PickPixel, ObjectTransformType_Up);
			//FixOpenAssetTransform(ObjectTransform);
			//??????????????, ??????峬?????????????false
			Mat ObjectImg;
			if (!RasterizeMesh(CurrenStaticMesh, ObjectTransform, ObjectImg))
			{
				continue;
			}
			Mat PickPixelImg = Array2DToMatBinarization(PickPixelArray);
			TArray<FVector2i> Test = MatBinarizationToVector2D(ObjectImg);

			//?????????debug
			Mat MinImg;
			min(PickPixelImg, ObjectImg, MinImg);
			SceneCaptureContainter->ReferenceImg1 = PickPixelImg;
			SceneCaptureContainter->ReferenceImg2 = MinImg;

			AsyncTask(ENamedThreads::GameThread, [&]()
			{

				TArray<AActor*> ContainerActors;
				UGameplayStatics::GetAllActorsOfClass(GWorld, ASceneCaptureContainter::StaticClass(), ContainerActors);
				if(ContainerActors.Num() > 0)
				{
					ASceneCaptureContainter* SceneCaptureContainter = Cast<ASceneCaptureContainter>(ContainerActors[0]);
					DrawDebugTexture(SceneCaptureContainter->ReferenceImg1, 1);
					DrawDebugTexture(SceneCaptureContainter->ReferenceImg2, 2);
											
				}
			});
			
			//????overlap
			if (OverlapImgCheck(PickPixelImg, ObjectImg, .2))
			{
				continue;
			}

			FMeshData MeshData;
			MeshData.Mesh = CurrenStaticMesh;
			MeshData.Transform = ObjectTransform;
			MeshData.Count = SceneCaptureCount;
			MeshData.Actor = ColorArray2D[PickPixel.X][PickPixel.Y].TraceActor;
			SceneCaptureContainter->MeshData.Add(MeshData);
			//FScenceCaptureCalDelegate.Broadcast();
			//SceneCaptureComponent->ComponentTags.Add(FName(TEXT("Destory")));

			//SceneCaptureContainter->ReferenceImg1 = OutReferenceImg;
			
			//???????????????
			AsyncTask(ENamedThreads::GameThread, [&]()
				{
					SpawnStaticMesh();
				});
			break;
		}
	}
	return true;
}
//????????????????. ????????lamdba???????????????????. 
void ScreenProcess::SpawnStaticMesh()
{
	TArray<AActor*> ContainerActors;
	UGameplayStatics::GetAllActorsOfClass(GWorld, ASceneCaptureContainter::StaticClass(), ContainerActors);
	ASceneCaptureContainter* SceneCaptureContainterWorld = nullptr;
	if (ContainerActors.Num() > 0)
	{
		SceneCaptureContainterWorld = Cast<ASceneCaptureContainter>(ContainerActors[0]);
		TArray<FMeshData> MeshDatas = SceneCaptureContainterWorld->MeshData;
		for (FMeshData MeshData : MeshDatas)
		{
			if (MeshData.Count > SceneCaptureCount)
			{
				continue;
			}
			//bool success = UKismetArrayLibrary::Array_RemoveItem(SceneCaptureContainter->MeshData,1);
			SceneCaptureContainterWorld->MeshData.Remove(MeshData);
			FActorSpawnParameters Params;
			//AStaticMeshActor* StaticActor = GWorld->SpawnActor<AStaticMeshActor>(ObjectTransform.GetLocation(), ObjectTransform.GetRotation().Rotator(), Params);
			AStaticMeshActor* StaticActor = GWorld->SpawnActor<AStaticMeshActor>(MeshData.Transform.GetLocation(), MeshData.Transform.GetRotation().Rotator(), Params);
			StaticActor->SetActorScale3D(MeshData.Transform.GetScale3D());

			StaticActor->GetStaticMeshComponent()->SetStaticMesh(MeshData.Mesh);
			StaticActor->SetMobility(EComponentMobility::Movable);
			//??????????????????.?????bug.???????attach
			StaticActor->AttachToActor(MeshData.Actor, FAttachmentTransformRules(EAttachmentRule::KeepWorld, false));
			//OutActors.Add(StaticActor);


			//CheckCollision
			TArray<FName> Tags;
			Tags.Add(FName(TEXT("SAuto")));
			FString SLocation = CurrentTransform.GetLocation().ToString();			
			FString SRotation = CurrentTransform.GetRotation().Rotator().ToString();
			
			Tags.Add(FName(SLocation));
			Tags.Add(FName(SRotation));
			Tags.Add(FName(TEXT("%d"), CamIndex));
			StaticActor->Tags = Tags;

			CheckCollision(StaticActor);

			TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes{ UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldDynamic) };
			TArray<AActor*> ActorsToIgnore;
			TArray<AActor*> OverlapOutActors;
			if (UKismetSystemLibrary::ComponentOverlapActors(StaticActor->GetStaticMeshComponent(), StaticActor->GetStaticMeshComponent()->GetRelativeTransform(), ObjectTypes, nullptr, ActorsToIgnore, OverlapOutActors))
			{
				for (AActor* OverlapOutActor : OverlapOutActors)
				{
					if (Cast<ASceneCaptureContainter>(OverlapOutActor))
					{
						StaticActor->Destroy();
					}
				}
			}
		}
		//TArray<USceneComponent*> CaptureComponents;
		//SceneCaptureContainterWorld->Root->GetChildrenComponents(false, CaptureComponents);
		//for (USceneComponent* CaptureComponent : CaptureComponents)
		//{
		//	if (CaptureComponent->ComponentTags.Find(FName(TEXT("Destory"))))
		//	{
		//		CaptureComponent->DestroyComponent();
		//	}
		//}
	}
}

//fld????? ??????????????
void ScreenProcess::LineDetection()
{
	Mat TestRefernceImg, color_edge;
	TestRefernceImg = Array2DToMatBinarization(PickPixelArray);
	Mat Test(Height, Width, CV_8UC1, Scalar::all(0));
	Mat CannyImg;
	TArray<FVector2i> TempCheck = MatBinarizationToVector2D(TestRefernceImg);

	Mat image = TestRefernceImg;
	int    length_threshold = 10;
	float  distance_threshold = 1.41421356f;
	double canny_th1 = 50.0;
	double canny_th2 = 50.0;
	int    canny_aperture_size = 3;
	bool   do_merge = false;
	Ptr<UE_FLD> fld = createUE_FLD(
		length_threshold,
		distance_threshold,
		canny_th1,
		canny_th2,
		canny_aperture_size,
		do_merge);
	TArray<Vec4f> lines_fld;
	fld->detect(TestRefernceImg, lines_fld);
	for (size_t i = 0; i < lines_fld.Num(); i++)
	{
		FVector2D ScreenPosStart = FVector2D(FMath::Clamp(lines_fld[i][1], float(0.), float(Width)), FMath::Clamp(lines_fld[i][0], float(0.), float(Height)));
		FVector2D ScreenPosEnd = FVector2D(FMath::Clamp(lines_fld[i][3], float(0.), float(Width)), FMath::Clamp(lines_fld[i][2], float(0.), float(Height)));

		FVector WorldPosStart, WorldDirStart, WorldPosEnd;
		WorldPosStart = ColorArray2D[ScreenPosStart.X][ScreenPosStart.Y].WorldPos;
		WorldPosEnd = ColorArray2D[ScreenPosEnd.X][ScreenPosEnd.Y].WorldPos;
		FVector LineObjectDir = -(WorldDirStart + WorldPosEnd - 2 * SceneCapture->GetActorLocation()).GetSafeNormal();
		FVector2D PixelDistFloat = ScreenPosStart - ScreenPosEnd;
		FVector2i PixelDistInt = FVector2i(PixelDistFloat.X, PixelDistFloat.Y);
		float PixelDist = (FVector2D(PixelDistInt.X, PixelDistInt.Y)).Size();
		float WorldSize = PixelDist * PixelSize;
		FBoxSphereBounds Bounds = CurrenStaticMesh->GetBounds();
		int32 PlaceNum = FMath::Max(int32(WorldSize / Bounds.BoxExtent.Z), 1);
		if (WorldSize < Bounds.BoxExtent.Z/.7)
		{
			continue;
		}
		if (WorldPosStart.Z < WorldPosEnd.Z)
		{
			FVector Temp = WorldPosStart;
			WorldPosStart = WorldPosEnd;
			WorldPosEnd = Temp;
		}
		
		for (int32 n = 0; n < PlaceNum; n++)
		{
			LineObjectDir = (-Forward + FMath::VRand() * .6).GetSafeNormal();
			FVector LineObjectNormal = (WorldPosStart - WorldPosEnd).GetSafeNormal();
			FRotator LineObjectRot = UKismetMathLibrary::MakeRotFromYZ(LineObjectDir, LineObjectNormal);
			float RandomLerp = FMath::FRandRange(0., 1.);
			FVector WorldPos;
			FVector2D ScreenPos = FMath::Lerp(ScreenPosStart, ScreenPosEnd, RandomLerp);
			int32 PosRow = FMath::RoundToInt(ScreenPos.X);
			int32 PosColumn = FMath::RoundToInt(ScreenPos.Y);
			//?????????????????PickPixelArray??.????????????
			if(!PickPixelArray.Find(FVector2i(PosRow,PosColumn)))
			{
				continue;
			}
			
			AActor* TraceActor = ColorArray2D[PosRow][PosColumn].TraceActor;
			if (TraceActor)
			{
				TArray<FName> Tags = TraceActor->Tags;
				if (Tags.Find(FName(TEXT("SAuto"))) >= 0)
				{
					continue;
				}
			}
			//???????????????????????. ??????????????
			if(PickActors.Find(TraceActor)<0)
			{
				continue;
			}
			WorldPos = ColorArray2D[PosRow][PosColumn].WorldPos;
			//?????????????????????????????????б????????????.
			//?????????????????Χ??????.
			//??????????????????ж????.
				//1.??????????????????????, ????????Щ???. ?????????Щ???????????????????????????????????????????????.
				//?????ж???????????????????????, ???????????ü????????浺??????????????.?????????Щ?仯
				//???????Щ????, ????????????????????????????????. 
				//2.???????????????????????????????????????????С. ?????ж???????????????
				//????????????????仯??. ?????????????????????仯????????. ????????. С????????????.

			//????????????????????????????????. 
			TArray<FVector2i> PosChecks;
			PosChecks.Add(FVector2i(PosRow + 1, PosColumn));
			PosChecks.Add(FVector2i(PosRow - 1, PosColumn));
			PosChecks.Add(FVector2i(PosRow, PosColumn + 1));
			PosChecks.Add(FVector2i(PosRow, PosColumn - 1));
			ColorArray2D[PosRow][PosColumn].Unchecked = false;
			ColorArray2D[PosRow][PosColumn].Picking = true;
			PickPixelArray.Add(FVector2i(PosRow, PosColumn));
			
			bool Generate = true;
			//??????
			for (FVector2i PosCheck : PosChecks)
			{
				if (0 <= PosCheck.X && PosCheck.X < Height && 0 <= PosCheck.Y && PosCheck.Y < Height)
				{
					float DotForward = FVector::DotProduct(ColorArray2D[PosCheck.X][PosCheck.Y].Normal, -Forward);
					//????н??С?ж????????. ??????????????.
					if (DotForward < .15)
					{
						Generate = false;
						continue;
					}
					//???????????????????仯
					float FixedDepthThreshold = PixelSize / DotForward + 20;
					float DistanceDiffer = FMath::Abs((ColorArray2D[PosCheck.X][PosCheck.Y].WorldPos - ColorArray2D[PosRow][PosColumn].WorldPos).Size());
					if (DistanceDiffer > FixedDepthThreshold)
					{
						Generate = false;
						continue;
					}
				}
			}
			if (!Generate)
			{
				continue;
			}
			//?????仯??
			TArray<float> DotForwards;
			for (FVector2i PosCheck : PosChecks)
			{
				if (0 <= PosCheck.X && PosCheck.X < Height && 0 <= PosCheck.Y && PosCheck.Y < Height)
				{
					float DotForward = FVector::DotProduct(ColorArray2D[PosCheck.X][PosCheck.Y].Normal, -Forward);
					DotForwards.Add(DotForward);
					//???????????????????仯
				}
				//????????????. ????????
				else
				{
					Generate = false;
					break;
				}
			}
			if (!Generate)
			{
				continue;
			}
			float DotThreashold = .3;
			if(FMath::Abs(DotForwards[0] - DotForwards[1]) < DotThreashold && FMath::Abs(DotForwards[2] - DotForwards[3]) < DotThreashold)
			{
				continue;
			}

			
			FTransform PlaceTransform = FTransform(LineObjectRot, WorldPos, FVector::OneVector);

			FMeshData MeshData;
			MeshData.Mesh = CurrenStaticMesh;
			MeshData.Transform = PlaceTransform;
			MeshData.Count = SceneCaptureCount;
			MeshData.Actor = ColorArray2D[PosRow][PosColumn].TraceActor;
			SceneCaptureContainter->MeshData.Add(MeshData);
		}

		//????mat???????debug
		float rho = lines_fld[i][0], theta = lines_fld[i][1];
		Point pt1, pt2;
		double a = cos(theta), b = sin(theta);
		double x0 = a * rho, y0 = b * rho;
		pt1.x = cvRound(x0 + 1000 * (-b));
		pt1.y = cvRound(y0 + 1000 * (a));
		pt2.x = cvRound(x0 - 1000 * (-b));
		pt2.y = cvRound(y0 - 1000 * (a));
		Vec4i l = lines_fld[i];
		line(OutReferenceImg, Point(l[0],l[1]), Point(l[2],l[3]), 255, 1, 8);
	}
}

void ScreenProcess::DrawDebugTexture(Mat Img, int32 index)
{
	if (Img.rows > 0)
	{
		FString ContentPath = FPaths::ProjectPluginsDir() + TEXT("TAToolsPlugin/Content/");
		FString TestImgPath = FPaths::Combine(ContentPath, TEXT("Test.png"));

		//string temp(TCHAR_TO_UTF8(*TestImgPath));
		FString ts = ContentPath + "Test" + FString::Printf(TEXT("%d"), index) +  ".png";
		imwrite(TCHAR_TO_UTF8(*ts), Img);

		//namedWindow("Example", WINDOW_AUTOSIZE);
		//imshow("Example", OutReferenceImg);
	}

}

UStaticMesh* ScreenProcess::ProcessMeshs()
{
	//???????StaicMesh????????????????Threashold?????????????????????. ??????????λ??.
	//?ò??????????????????????????. ???????????????. ?????С????????. ?????????????????.

	int32 index = FMath::RandRange(0, Meshs.Num() - 1);
	CurrenStaticMesh = Meshs[index];
	FBoxSphereBounds Bounds = CurrenStaticMesh->GetBounds();
	TSharedPtr<FDynamicMesh3> OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(CurrenStaticMesh->GetMeshDescription(0), *OriginalMesh);
	float MinHeight = Bounds.Origin.Z - Bounds.BoxExtent.Z;
	float Threshold = 50.;
	PickVertices.Empty();
	for (int32 VerticeId : OriginalMesh->VertexIndicesItr())
	{
		FVector3d MeshVerticePos = OriginalMesh->GetVertex(VerticeId);
		FVector VerticePos = FVector(MeshVerticePos.X, MeshVerticePos.Y, MeshVerticePos.Z);
		if (VerticePos.Z < MinHeight + Threshold)
		{
			VerticePos.Z = 0.;
			PickVertices.Add(VerticePos);
		}
	}

	PolyVertIndices.Empty();
	ConvexHull2D::ComputeConvexHullLegacy(PickVertices, PolyVertIndices);
	ConvexCenter = FVector(0, 0, 0);
	for (int32 PolyVertIndice : PolyVertIndices)
	{
		ConvexCenter += PickVertices[PolyVertIndice];
	}
	ConvexCenter /= PickVertices.Num() * 1.;

	return CurrenStaticMesh;
}

//FConsiderMesh::FConsiderMesh(UStaticMesh* StaticMesh)
//	: StaticMesh(StaticMesh)
//{
//	
//	OriginalMesh = MakeShared<FDynamicMesh3>();
//	FMeshDescriptionToDynamicMesh Converter;
//	Converter.Convert(StaticMesh->GetMeshDescription(0), *OriginalMesh);
//}


bool FConsiderMeshZ::ProcessMesh()
{
	FBoxSphereBounds Bounds = StaticMesh->GetBounds();
	float MinHeight = Bounds.Origin.Z - Bounds.BoxExtent.Z;
	float Threshold = 50.;
	TArray<FVector> PickVertices;
	for (int32 VerticeId : OriginalMesh->VertexIndicesItr())
	{
		FVector3d MeshVerticePos = OriginalMesh->GetVertex(VerticeId);
		FVector VerticePos = FVector(MeshVerticePos.X, MeshVerticePos.Y, MeshVerticePos.Z);
		if (VerticePos.Z < MinHeight + Threshold)
		{
			VerticePos.Z = 0.;
			PickVertices.Add(VerticePos);
		}
	}

	TArray<int32> PolyVertIndices;
	ConvexHull2D::ComputeConvexHullLegacy(PickVertices, PolyVertIndices);
	ConvexCenter = FVector(0, 0, 0);
	for (int32 PolyVertIndice : PolyVertIndices)
	{
		ConvexCenter += PickVertices[PolyVertIndice];
	}
	ConvexCenter /= PickVertices.Num() * 1.;

	ErodeBoundMax = FVector2D(-999999, -99999);
	ErodeBoundMin = FVector2D(999999, 999999);
	float MaxLength = 0.;
	for (int32 PolyVertIndice : PolyVertIndices)
	{
		if ((PickVertices[PolyVertIndice] - ConvexCenter).Size() > MaxLength)
		{
			MaxLength = PickVertices[PolyVertIndice].Size();
		}
		if (PickVertices[PolyVertIndice].X > ErodeBoundMax.X)
		{
			ErodeBoundMax.X = PickVertices[PolyVertIndice].X;
		}
		if (PickVertices[PolyVertIndice].Y > ErodeBoundMax.Y)
		{
			ErodeBoundMax.Y = PickVertices[PolyVertIndice].Y;
		}
		if (PickVertices[PolyVertIndice].X < ErodeBoundMin.X)
		{
			ErodeBoundMin.X = PickVertices[PolyVertIndice].X;
		}
		if (PickVertices[PolyVertIndice].Y < ErodeBoundMin.Y)
		{
			ErodeBoundMin.Y = PickVertices[PolyVertIndice].Y;
		}
	}
	//??????????????????????????????????. ?????????????????????????
	//int32 ErodeNumPixels = floor(MaxLength / PixelSize);
	//ErodeBoundPixels = (ErodeBoundMax - ErodeBoundMin) / PixelSize / 2 * RandomScale;
	//ErodeBoundPixels = FVector2i(floor(ErodeBoundPixels.X), floor(ErodeBoundPixels.Y));
	//ErodeNumPixels = floor(FMath::Min(ErodeBoundPixels.X, ErodeBoundPixels.Y));

	//if (ErodeNumPixels == 0)
	//{
	//	return false;
	//}
	return true;
}

bool FConsiderMeshY::ProcessMesh()
{
	TArray<FVector> PickVertices;
	for (int32 VerticeId : OriginalMesh->VertexIndicesItr())
	{
		FVector3d MeshVerticePos = OriginalMesh->GetVertex(VerticeId);
		FVector VerticePos = FVector(MeshVerticePos.X, MeshVerticePos.Y, MeshVerticePos.Z);
		VerticePos.Y = 0.;
		PickVertices.Add(VerticePos);
	}

	TArray<int32> PolyVertIndices;
	ConvexHull2D::ComputeConvexHullLegacy(PickVertices, PolyVertIndices);
	ConvexCenter = FVector(0, 0, 0);
	for (int32 PolyVertIndice : PolyVertIndices)
	{
		ConvexCenter += PickVertices[PolyVertIndice];
	}
	ConvexCenter /= PickVertices.Num() * 1.;

	ErodeBoundMax = FVector2D(-999999, -99999);
	ErodeBoundMin = FVector2D(999999, 999999);
	float MaxLength = 0.;
	for (int32 PolyVertIndice : PolyVertIndices)
	{
		if ((PickVertices[PolyVertIndice] - ConvexCenter).Size() > MaxLength)
		{
			MaxLength = PickVertices[PolyVertIndice].Size();
		}
		if (PickVertices[PolyVertIndice].X > ErodeBoundMax.X)
		{
			ErodeBoundMax.X = PickVertices[PolyVertIndice].X;
		}
		if (PickVertices[PolyVertIndice].Z > ErodeBoundMax.Y)
		{
			ErodeBoundMax.Y = PickVertices[PolyVertIndice].Z;
		}
		if (PickVertices[PolyVertIndice].X < ErodeBoundMin.X)
		{
			ErodeBoundMin.X = PickVertices[PolyVertIndice].X;
		}
		if (PickVertices[PolyVertIndice].Z < ErodeBoundMin.Y)
		{
			ErodeBoundMin.Y = PickVertices[PolyVertIndice].Z;
		}
	}
	return true;
}

void ASceneCaptureContainter::GenerateTransforms()
{
	//??????????pickactors??bound???????????????????, ???????????. ?о?????????box???????????????. ????Щ????, ???????????????????????, ??????????pickactors??bound??????????????Χ.
	FVector Center = SourceCenter;
	FVector BoxExtent = SourceExtent;
	//UGameplayStatics::GetActorArrayBounds(PickActors, true, Center, BoxExtent);
	FVector OrigPos = Center - BoxExtent * ExtentMult;
	FVector Size = BoxExtent * ExtentMult;
	//???????????????
	TArray<FVector> Dirs;
	TSet<FVector> DirSet;
	FVector DirTest = FVector::ZeroVector;//DirTest?????????????????????????????0, ???????0 ?????????????????????????
	float DirDivide = 2;
	
	for (int32 i = 0; i < DirDivide * 2 + 1; i++)
	{
		for (int32 n = 0; n < DirDivide * 2 + 1; n++)
		{
			for (int32 c = 0; c < DirDivide * 2 + 1; c++)
			{
				//???????????????
				if (c / DirDivide - 1 > .2)
				{
					continue;
				}
				float RandomAngleRangeMult = FMath::FRandRange(0.1, 0.2);
				FVector RandomVector = FMath::VRandCone(FVector(0, 0, 1), 360, 360);
				//??????.????????
				FVector Dir = (FVector(i / DirDivide - 1, n / DirDivide - 1, 0) + RandomVector * RandomAngleRangeMult).GetSafeNormal();
				DirSet.Add(Dir);
				DirTest += Dir;
			}
		}
	}
	Dirs = DirSet.Array();
	DirTest = DirTest.GetSafeNormal();
	//????????transform
	if (CaptureTransforms.Num() == 0)
	{
		UObject* loadObj = StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("StaticMesh'/TAToolsPlugin/Plane.Plane'"));
		UStaticMesh* PlaneStatic = nullptr;
		if (loadObj != nullptr)
		{
			PlaneStatic = Cast<UStaticMesh>(loadObj);
		}
		FActorSpawnParameters Params;
		AStaticMeshActor* PlaneStaticActor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		PlaneStaticActor->GetStaticMeshComponent()->SetStaticMesh(PlaneStatic);
		PlaneStaticActor->SetMobility(EComponentMobility::Movable);
		PlaneStaticActor->SetActorHiddenInGame(true);
		FVector PlaneScale = FVector(DivdeSize, DivdeSize, DivdeSize) * 2;
		for (int32 i = 0; i < Size.X / DivdeSize + 2; i++)
		{
			for (int32 n = 0; n < Size.Y / DivdeSize + 2; n++)
			{
				for (int32 c = 0; c < Size.Z / DivdeSize + 2; c++)
				{
					if(OrigPos.Z + c * DivdeSize < Center.Z - BoxExtent.Z * 1.1)
					{
						continue;
					}
					for (FVector Dir : Dirs)
					{

						float RandomDist = FMath::FRandRange(0, DivdeSize * 0.2);
						FVector RandomVector = FMath::VRandCone(FVector(0, 0, 1), 360, 360);
						FVector RandomDir = FMath::VRandCone(FVector(0, 0, 1), 360, 360);
						FVector Pos = FVector(i, n, c) * DivdeSize + OrigPos + RandomVector * RandomDist;
						FRotator Rot = UKismetMathLibrary::MakeRotFromX((Dir + RandomDir * .3).GetSafeNormal());
						FTransform PlaneTransform = UKismetMathLibrary::MakeTransform(Pos, Rot, PlaneScale);
						PlaneStaticActor->SetActorTransform(PlaneTransform);
						TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes{ UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldDynamic), UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic)};
						TArray<AActor*> ActorsToIgnore;
						TArray<AActor*> OverlapOutActors;
						bool OverlapPickActor = false;
						FHitResult OutHit;
						//?????????????????. ?????????????????????. ????е???????????????????.
						//???boxtrace???????????????planecomponent????.
						// if (UKismetSystemLibrary::BoxTraceSingleForObjects(
						// 	GetWorld(), Pos, Pos, FVector(0, DivdeSize, DivdeSize), Rot, ObjectTypes, true,
						// 	ActorsToIgnore, EDrawDebugTrace::None, OutHit, true))
						// {
						// 	continue;
						// }
						if(UKismetSystemLibrary::ComponentOverlapActors(PlaneStaticActor->GetStaticMeshComponent(), PlaneStaticActor->GetStaticMeshComponent()->GetRelativeTransform(), ObjectTypes, nullptr, ActorsToIgnore, OverlapOutActors))
						{
							for (AActor* CheckActor : OverlapOutActors)
							{
								if (PickActors.Find(CheckActor))
								{
									OverlapPickActor = true;
									break;
								}
							}
							if(OverlapPickActor)
							{
								continue;
							}
						}
						
						//????????????????????????, ????е??????transform??????????????
						FVector Start = Pos;
						FVector End = Start + Dir * ScreenDepthMax;
						if (UKismetSystemLibrary::LineTraceSingle(GetWorld(), Start, End, ETraceTypeQuery::TraceTypeQuery1, true, ActorsToIgnore, EDrawDebugTrace::None, OutHit, true))
						{
							if (PickActors.Find(OutHit.GetActor()) >= 0 && OutHit.Distance > 200 && OutHit.Distance > 1300)
							{
								//?z????????????????
								End = Start + FVector(0,0,111111);
								if(UKismetSystemLibrary::LineTraceSingle(GetWorld(), Start, End, ETraceTypeQuery::TraceTypeQuery1, true, ActorsToIgnore, EDrawDebugTrace::None, OutHit, true))
								{
									if (Cast<ALandscape>(OutHit.GetActor()))
									{
										continue;
									}
								}
								CaptureTransforms.Add(FTransform(Rot, Pos, FVector::OneVector));
							}
						}
					}
				}
			}
		}
		//?????????????????λ??????. ????Ч??????.????
		//for (int32 i = 0; i < CaptureTransforms.Num(); i++)
		//{
		//	CaptureTransforms.Swap(i, FMath::RandRange(0, CaptureTransforms.Num() - 1));
		//}
	}
}

void ASceneCaptureContainter::GenerateBlockBox()
{
	FVector Center, BoxExtent;
	UGameplayStatics::GetActorArrayBounds(PickActors, true, Center, BoxExtent);
	
	FVector OrigPos = Center - BoxExtent * ExtentMult;
	FVector Size = BoxExtent * ExtentMult;
	//????BlockInstance???????????.
	TArray<FVector> PosRandoms;
	for (int32 i = 0; i < Size.X / DivdeSize + 1; i++)
	{
		for (int32 n = 0; n < Size.Y / DivdeSize + 1; n++)
		{
			for (int32 c = 0; c < Size.Z / DivdeSize + 1; c++)
			{
				FVector Pos = FVector(i, n, c) * DivdeSize + OrigPos;
				PosRandoms.Add(Pos);
			}
		}
	}
	for (int32 i = 0; i < PosRandoms.Num(); i++)
	{
		PosRandoms.Swap(i, FMath::RandRange(0, PosRandoms.Num() - 1));
	}
	for (int32 i = 0; i < PosRandoms.Num() * Block; i++)
	{
		FTransform AddTransform = FTransform(FRotator::ZeroRotator, PosRandoms[i], FVector::OneVector * DivdeSize / 100);
		InstanceBlockMesh->AddInstanceWorldSpace(AddTransform);
	}
}


bool ScreenProcess::CalculateErodePixelNum()
{
	ErodeBoundMax = FVector2D(-999999, -99999);
	ErodeBoundMin = FVector2D(999999, 999999);
	float MaxLength = 0.;
	for (int32 PolyVertIndice : PolyVertIndices)
	{
		if ((PickVertices[PolyVertIndice] - ConvexCenter).Size() > MaxLength)
		{
			MaxLength = PickVertices[PolyVertIndice].Size();
		}
		if (PickVertices[PolyVertIndice].X > ErodeBoundMax.X)
		{
			ErodeBoundMax.X = PickVertices[PolyVertIndice].X;
		}
		if (PickVertices[PolyVertIndice].Y > ErodeBoundMax.Y)
		{
			ErodeBoundMax.Y = PickVertices[PolyVertIndice].Y;
		}
		if (PickVertices[PolyVertIndice].X < ErodeBoundMin.X)
		{
			ErodeBoundMin.X = PickVertices[PolyVertIndice].X;
		}
		if (PickVertices[PolyVertIndice].Y < ErodeBoundMin.Y)
		{
			ErodeBoundMin.Y = PickVertices[PolyVertIndice].Y;
		}
	}
	//??????????????????????????????????. ?????????????????????????
	//int32 ErodeNumPixels = floor(MaxLength / PixelSize);
	ErodeBoundPixels = (ErodeBoundMax - ErodeBoundMin) / PixelSize / 2 * RandomScale;
	ErodeBoundPixels = FVector2D(floor(ErodeBoundPixels.X), floor(ErodeBoundPixels.Y));
	ErodeNumPixels = floor(FMath::Min(ErodeBoundPixels.X, ErodeBoundPixels.Y));

	if (ErodeNumPixels == 0)
	{
		return false;
	}
	return true;
}



bool ScreenProcess::CheckNormalImg(FVector2i SearchIndex, float NormalThreshold)
{
	int32 PosRow = SearchIndex.X;
	int32 PosColumn = SearchIndex.Y;
	TArray<FVector2i> SearchIndexs;
	SearchIndexs.Add(SearchIndex);
	FVector InputNormal = ColorArray2D[PosRow][PosColumn].Normal;
	FVector AvgNormal = FVector::ZeroVector;
	FVector AvgLocation = FVector::ZeroVector;
	int32 PickCount = 0;
	float DepthThreshold = 10;


	while (SearchIndexs.Num())
	{
		SearchIndex = SearchIndexs.Pop();
		PosRow = SearchIndex.X;
		PosColumn = SearchIndex.Y;
		if (//FMath::Abs(ColorArray2D[PosRow][PosColumn].Color.A - InputColor.A) < DepthThreshold && 
			ColorArray2D[PosRow][PosColumn].Unchecked == false)
		{
			continue;
		}
		//?????б??????????????????normal???????.
		AvgNormal += ColorArray2D[PosRow][PosColumn].Normal;
		AvgLocation += ColorArray2D[PosRow][PosColumn].WorldPos;
		TArray<FVector2i> PosChecks;
		PosChecks.Add(FVector2i(PosRow + 1, PosColumn));
		PosChecks.Add(FVector2i(PosRow - 1, PosColumn));
		PosChecks.Add(FVector2i(PosRow, PosColumn + 1));
		PosChecks.Add(FVector2i(PosRow, PosColumn - 1));
		ColorArray2D[PosRow][PosColumn].Unchecked = false;
		ColorArray2D[PosRow][PosColumn].Picking = true;
		PickPixelArray.Add(FVector2i(PosRow, PosColumn));
		PickCount ++;
		
		for (FVector2i PosCheck : PosChecks)
		{
			if (0 <= PosCheck.X && PosCheck.X < Height && 0 <= PosCheck.Y && PosCheck.Y < Height)
			{
				FVector Normal = ColorArray2D[PosCheck.X][PosCheck.Y].Normal;
				float Dot = FVector::DotProduct(Normal, InputNormal);
				float DotForward = FVector::DotProduct(ColorArray2D[PosCheck.X][PosCheck.Y].Normal, -Forward);

				//???????????????????仯
				float FixedDepthThreshold = PixelSize / DotForward + DepthThreshold;
				
				if (Dot > NormalThreshold && ColorArray2D[PosCheck.X][PosCheck.Y].Unchecked == true)
				{
					if (FMath::Abs((ColorArray2D[PosCheck.X][PosCheck.Y].WorldPos - ColorArray2D[PosRow][PosColumn].WorldPos).Size()) < FixedDepthThreshold)
					{
						SearchIndexs.Push(FVector2i(PosCheck.X, PosCheck.Y));
					}
				}
			}
		}
	}

	if (PickCount > 0)
	{
		AvgNormal /= float(PickCount);
		AvgNormal = AvgNormal.GetSafeNormal();
		AvgLocation /= float(PickCount);
		float Dot = FVector::DotProduct(AvgNormal, -Forward);
		if (Dot < NormalThreshold )
		{
			OtherSide OtherSideData;
			OtherSideData.OtherSidePixels= PickPixelArray;
			OtherSideData.Normal = AvgNormal;
			OtherSides.Add(OtherSideData);
			// FVector Dir = AvgNormal;
			// Dir.Z = 0;
			// Dir = UKismetMathLibrary::Normal(Dir);
			// Dir = UKismetMathLibrary::Cross_VectorVector(FVector(0, 0, 1), Dir);
			// FVector CaptureLocation = AvgLocation - AvgNormal * 1000;
			//CaptureTransforms.Add(CaptureTransform);
			return false;
		}
		return true;
	}
	else 
	{
		return false;
	}
}

void ScreenProcess::CheckCollision(AStaticMeshActor* StaticActor)
{
	FTransform MeshTransform = StaticActor->GetActorTransform();
	for (int32 i = 0; i < 5; i++)
	{
		FVector LineCheckPosStart = FVector((ErodeBoundMax.X - ErodeBoundMin.X) / 4, ErodeBoundMin.Y, 25);
		FVector LineCheckPosEnd = FVector((ErodeBoundMax.X - ErodeBoundMin.X) / 4, ErodeBoundMax.Y, 25);
		MeshTransform.TransformPosition(LineCheckPosStart);
		MeshTransform.TransformPosition(LineCheckPosEnd);
		TArray<TEnumAsByte<EObjectTypeQuery> > ObjectTypes{ UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic) };
		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(StaticActor);
		FHitResult OutHit;
		bool Hit = UKismetSystemLibrary::LineTraceSingle(GWorld, LineCheckPosStart, LineCheckPosEnd, ETraceTypeQuery::TraceTypeQuery1, true, ActorsToIgnore, EDrawDebugTrace::None, OutHit, true);
		if (Hit)
		{
			TArray<FName> Tags;
			Tags.Add(FName(TEXT("Collision")));
			StaticActor->Tags = Tags;
			break;
		}
	}
}

Mat ScreenProcess::Array2DToMatBinarization(TArray<FVector2i> ConvertTArray)
{
	Mat Img(Height, Width, CV_8UC1, Scalar::all(0));
	uchar* ptmp = NULL;

	for (FVector2i PickPixel : ConvertTArray)
	{
		
		int32 PickRow = PickPixel.X;
		int32 PickCol = PickPixel.Y;
		if (PickRow >= Height || PickCol >= Width)
		{
			continue;
		}
		ptmp = Img.ptr<uchar>(PickRow);
		ptmp[PickCol] = 255;
	}

	return Img;
}

Mat ScreenProcess::SceneDepthToMatBinarization()
{
	Mat Img(Height, Width, CV_32FC1, Scalar::all(0));

	for (int32 i = 0; i < ColorArray2D.Num(); i++)
	{
		for (int32 n = 0; n < ColorArray2D[0].Num(); n++)
		{
			Img.at<uchar>(i, n) = ColorArray2D[i][n].Color.A;
			//ptmp = Img.ptr<uchar>(i);
			//ptmp[n] = 255;
		}
	}

	return Img;
}

TArray<FVector2i> ScreenProcess::MatBinarizationToVector2D(Mat Img)
{

	TArray<FVector2i> TempArray;
	for (int i = 0; i < Img.rows; ++i)
	{
		for (int j = 0; j < Img.cols; ++j)
		{
			if (Img.at<uchar>(i, j) > 0)
			{
				TempArray.Add(FVector2i(i, j));
			}
		}
	}
	return TempArray;
}

FVector2i ScreenProcess::PixelBox(FVector2i& Max, FVector2i& Min)
{
	Max = FVector2i(-9999, -9999);
	Min = FVector2i(99999, 9999);
	for (FVector2i Pixel : PickPixelArray)
	{
		if (Pixel.X > Max.X)
		{
			Max.X = Pixel.X;
		}
		if (Pixel.Y > Max.Y)
		{
			Max.Y = Pixel.Y;
		}
		if (Pixel.X < Min.X)
		{
			Min.X = Pixel.X;
		}
		if (Pixel.Y < Min.Y)
		{
			Min.Y = Pixel.Y;
		}
	}
	return Max - Min;
}


FTransform ScreenProcess::CreateObjectTransform(FVector2i PickPixel, int32 Type)
{

	//??????(??????????????actor??????????. ????????????.
	//????????????????????????????????????. ????????????????????????. ??????????????????, ???????????????.
	TArray<FVector2i> Dirs;
	Dirs.Add(FVector2i(0, 1));
	Dirs.Add(FVector2i(1, 0));
	Dirs.Add(FVector2i(0, -1));
	Dirs.Add(FVector2i(-1, 0));
	//?ó????2d????б????????????????????
	FVector2i TarDir = FVector2i(0,0);
	for (int32 d = 1; d < FMath::Max(ColorArray2D.Num(), ColorArray2D[0].Num()); d++)
	{
		for (FVector2i Dir : Dirs)
		{
			FVector2i Pos = PickPixel + Dir * d;
			if ((0 <= Pos.X && Pos.X < Height && 0 <= Pos.Y && Pos.Y < Height))
			{
				if (!ColorArray2D[Pos.X][Pos.Y].Picking)
				{
					TarDir = Dir;
					break;
				}
			}
			else
			{
				TarDir = Dir;
				break;
			}
		}
		if (FVector2D(TarDir.X,TarDir.Y).Size() > 0)
		{
			break;
		}
	}
	float RandomAngleRange = 20;
	switch (Type)
	{
	default:
	case ObjectTransformType_Forward://?????x????????????????
		{
			FVector ForwordDir = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].Normal;
			//?????????????Z?????淨?????. ????????????泯??.
			FVector UpDir = FVector::CrossProduct(FVector::CrossProduct(FMath::VRandCone(FVector(0, 0, 1), RandomAngleRange, RandomAngleRange), ForwordDir), ForwordDir);
			FRotator Rot = UKismetMathLibrary::MakeRotFromXZ(ForwordDir, UpDir);
			FVector PlaceLocation = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].WorldPos;
			return FTransform(Rot, PlaceLocation, ObjectScale);
		}
	case ObjectTransformType_ForwardFoliage_OverTarget://???????????Z????0 Z?????????(0,0,1) ????????淨???Forward
		{
			FVector RandomUp = FVector::CrossProduct(UKismetMathLibrary::RandomUnitVectorInEllipticalConeInDegrees(FVector(0, 0, 1), 360, 360), FVector(0,0,1));
			FVector UpDir = (UKismetMathLibrary::VLerp(FVector(0, 0, 1), RandomUp, 1)).GetSafeNormal();

			FVector ForwordDir = ColorArray2D[PickPixel.X][PickPixel.Y].Normal;
			UpDir = FVector::CrossProduct(FVector::CrossProduct(UpDir, ForwordDir), ForwordDir);
			FRotator Rot = UKismetMathLibrary::MakeRotFromYZ(ForwordDir, UpDir);
			FVector PlaceLocation = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].WorldPos;
			return FTransform(Rot, PlaceLocation,FVector::OneVector);
		}
	case ObjectTransformType_Up://Z????????????????, ??????????????
		{
			FVector UpDir = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].Normal;
			//?????????????Z?????淨?????. ????????????泯??.
			FVector ForwordDir = FVector::CrossProduct(UKismetMathLibrary::RandomUnitVectorInEllipticalConeInDegrees(FVector(0, 0, 1), RandomAngleRange, RandomAngleRange), UpDir);
			FRotator Rot = UKismetMathLibrary::MakeRotFromXZ(ForwordDir, UpDir);
			FVector PlaceLocation = ColorArray2D[int32(PickPixel.X)][int32(PickPixel.Y)].WorldPos;
			return FTransform(Rot, PlaceLocation, ObjectScale);
		}
	}
}

void ProcessOpenAsset::FixOpenAssetTransform(FTransform& InTransform)
{
	TSharedPtr<OpenAssetProcess, ESPMode::ThreadSafe> OpenAsset = MakeShareable(new OpenAssetProcess());
	OpenAsset->StaticMesh = CurrenStaticMesh;
	OpenAsset->Vertices = *MeshOpenVertexMap.Find(CurrenStaticMesh);;
	OpenAsset->NumberOfCheck = 5;
	OpenAsset->FixedTransform = InTransform;
	OpenAsset->ObjectTypes =  { UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldDynamic) };
	OpenAsset->CalculateOpenVerticesDir();
	//OpenAsset->CalculateOpenAssetTransform();
	OpenAsset->CalculateSafeTransform();
	InTransform = OpenAsset->FixedTransform;
}

TArray<FVector2i> ScreenProcess::ScreenErode()
{
	Mat Img = Array2DToMatBinarization(PickPixelArray);
	Mat ImgErode;
	int32 ErodeSize = ErodeNumPixels * 2 + 1;
	Mat Element = getStructuringElement(MORPH_RECT, Size(ErodeSize, ErodeSize));
	erode(Img, ImgErode, Element);
	
	return MatBinarizationToVector2D(ImgErode);

}

bool ScreenProcess::OverlapImgCheck(Mat UnLayoutImg, Mat ObjectImg, float OverlapThreashold)
{
	//????????С????????. ???????????..
	Mat MinImg;
	min(UnLayoutImg, ObjectImg, MinImg);
	float CountOverlap = countNonZero(MinImg);
	float CountSource = countNonZero(ObjectImg);
	float OverlapRatio = CountOverlap / CountSource;
	if (OverlapRatio < OverlapThreashold)
	{
		return true;
	}

	// for (int i = 0; i < UnLayoutImg.rows; ++i)
	// {
	// 	for (int j = 0; j < UnLayoutImg.cols; ++j)
	// 	{
	// 		if (UnLayoutImg.at<uchar>(i, j) > 0 && ObjectImg.at<uchar>(i, j) > 0)
	// 		{
	// 			return true;
	// 		}
	// 	}
	// }
	return false;
}

bool ScreenProcess::OverlapCheck(Mat Img, FVector2i PickPixel)
{
	FVector2i MinPos = PickPixel - FVector2i(int32(ErodeBoundPixels.X), int32(ErodeBoundPixels.Y));
	FVector2i MaxPos = PickPixel + FVector2i(int32(ErodeBoundPixels.X), int32(ErodeBoundPixels.Y));
	if (MinPos.X < 0 || MinPos.Y < 0 || MaxPos.X > Width || MaxPos.Y > Height)
	{
		//??????屻???????????
		return true;
	}

	uchar* ptmp = NULL;
	for (int32 i = 0; i < ErodeBoundPixels.X * 2; i++)
	{
		ptmp = Img.ptr<uchar>(int32(MinPos.X) + i);
		for (int32 n = 0; n < ErodeBoundPixels.Y * 2; n++)
		{
			//FVector2i CheckPixel = MinPos + FVector2i(i, n);
			
			if (ptmp[int32(MinPos.Y) + n] == 0)
			{
				//???chart??????????????????Χ
				return true;
			}
		}
	}
	double MaxVal = 0;
	double MinVal = 0;
	minMaxLoc(Img, &MinVal, &MaxVal);

	return false;
}

FRotator ScreenProcess::RandomRotatorFromUpAxis(FVector Up)
{
	Up = Up.GetSafeNormal();
	FVector Tangent = FVector::CrossProduct(UKismetMathLibrary::RandomUnitVectorInEllipticalConeInDegrees(FVector(0, 0, 1), 360, 360), Up);
	return UKismetMathLibrary::MakeRotFromXZ(Tangent, Up);

}

bool ScreenProcess::RasterizeMesh(UStaticMesh* InMesh, FTransform& Transform, Mat& OutImg)
{
	//TSharedPtr<FDynamicMesh3> OriginalMesh = MakeShared<FDynamicMesh3>();
	//FMeshDescriptionToDynamicMesh Converter;
	//Converter.Convert(InMesh->GetMeshDescription(0), *OriginalMesh);
	TSharedPtr<FDynamicMesh3> OriginalMesh = *DynamicMeshData.Find(InMesh);
	FVector3d TriVs[3];
	vector<vector<Point>> Points;
	TMap<FVector2i, float> ScreenDepthMap;
	for (int TID : OriginalMesh->TriangleIndicesItr())
	{
		OriginalMesh->GetTriVertices(TID, TriVs[0], TriVs[1], TriVs[2]);
		vector<Point> TriPoints;
		
		//TArray<float> 
		//float Pair = ScreenDepthMap[0];
		for (FVector3d TriV : TriVs)
		{
			FVector Pos = FVector(TriV);
			FVector2D ScreenPos;
			float ScreenDepth;
			Pos = Transform.TransformPosition(Pos);
			if (UScreenGenerate::ProjectWorldToScreen_General(SceneCapture, Width, Height, Pos, ScreenPos, ScreenDepth))
			{
				if (ScreenPos.X < 0 || ScreenPos.X > Width || ScreenPos.Y < 0 || ScreenPos.Y > Height)
				{
					return false;
				}
				TriPoints.push_back(Point(FMath::Max(ScreenPos.X,float(0.)), FMath::Max(ScreenPos.Y, float(0.))));
				float PixelDepth = (ColorArray2D[ScreenPos.X][ScreenPos.Y].WorldPos - SceneCapture->GetActorLocation()).Size();
				float DepthDifference = ScreenDepth - PixelDepth;
				float* Depth = ScreenDepthMap.Find(FVector2i(ScreenPos.X, ScreenPos.Y));
				//????嵽??????????????????????????.
				if (Depth)
				{
					if (*Depth < DepthDifference)
					{
						ScreenDepthMap.Add(FVector2i(ScreenPos.X, ScreenPos.Y),DepthDifference);
					}
				}
				else
				{
					ScreenDepthMap.Add(FVector2i(ScreenPos.X, ScreenPos.Y), DepthDifference);
				}
			}
			
		}
		if (TriPoints.size())
		{
			Points.push_back(TriPoints);
		}
	}
	ScreenDepthMap.ValueSort([](float A, float B) { return A > B; });
	TArray<float> DepthfArray;
	for (TTuple<FVector2i, float>& Pair : ScreenDepthMap)
	{
		DepthfArray.Add(Pair.Value);
	}
	if (DepthfArray.Num() == 0)
	{
		return false;
	}
	//?????????嵽???????????С???????????????????. ?????????????????
	//?????????????????????????????????????????.????????????.
	float FixedDepth = DepthfArray[int32(DepthArray.Num() * 0.5)];
	if (FixedDepth > 0)
	{
		//Transform.SetLocation(Transform.GetLocation() - Forward * FixedDepth);
	}


	Mat PolyImg(Height, Width, CV_8UC1, Scalar::all(0));
	fillPoly(PolyImg, Points, Scalar(255));
	OutImg = PolyImg;
	TArray<FVector2i> ObjectPixels = MatBinarizationToVector2D(PolyImg);
	// for (FVector2i ObjectPixel : ObjectPixels)
	// {
	// 	//float PixelDpath = (ColorArray2D[ObjectPixel.X][ObjectPixel.Y].WorldPos - SceneCapture->GetActorLocation()).Size();
	//
	// }

	return true;
}

//
//
//void UScreenGenerate::AsyncTest(FVector WorldTransform)
//{
//	//TSharedPtr<AsyncClass, ESPMode::ThreadSafe> AsyncInst = MakeShared<AsyncClass>();
//	TSharedPtr<AsyncClass, ESPMode::ThreadSafe> AsyncInst = MakeShareable(new AsyncClass);
//	//if (ColorData->Calculate())
//	//{
//	//	OutHDRValues = ColorData->OutHDRValues;
//	//	OutActors = ColorData->OutActors;
//	//	OutRT = ColorData->OutRT;
//	//	return true;
//	//}
//	//else
//	//{
//	//	return false;
//	//}
//	AsyncInst->IntTest.Empty();
//	AsyncInst->FCalDelegate.AddThreadSafeSP(AsyncInst.ToSharedRef(), &AsyncClass::AsyncTestFun);
//	//AsyncInst->AsyncBind();
//	//auto AsyncTest = new FAutoDeleteAsyncTask<FSimpleAsyncTasks>(AsyncInst);
//	//AsyncTest->StartBackgroundTask();
//	//AsyncInst->FCalDelegate.BindUObject(AsyncInst, &AsyncClass::AsyncTestFun);
//	
//	
//	//AsyncInst->FCalDelegate.AddUObject
//
//	//FPlatformProcess::Sleep(5);
//	//AsyncInst->IntTest.Empty();
//
//	//FPlatformProcess::Sleep(5);
//	//AsyncInst->IntTest.Empty();
//	//FCalDelegate.Broadcast();
//}
//
//void AsyncClass::AsyncTestFun()
//{
//	//FPlatformProcess::Sleep(3);
//	for (int32 i = 0; i < IntTest.Num()*10; i++)
//	{
//		FActorSpawnParameters Params;
//		GWorld->SpawnActor<AActor>(FVector(0, 0, 0), FRotator(0, 0, 0), Params);
//	}
//}
//
//void AsyncClass::AsyncBind()
//{
//	//FCalDelegate.AddRaw(this, &AsyncClass::AsyncTestFun);
//	FPlatformProcess::Sleep(5);
//}
//
//
bool UScreenGenerate::ProjectWorldToScreen_General(ASceneCapture2D* InSceneCapture, int32 Width, int32 Height, FVector WorldPos, FVector2D& ScreenPos, float& ScreenDepth)
{
	USceneCaptureComponent2D* CaptureComponent = InSceneCapture->GetCaptureComponent2D();
	//Capture2D->UpdateSceneCaptureContents(GWorld->Scene);
	//check(CaptureComponent);
	//UTextureRenderTarget2D* TextureRenderTarget = CaptureComponent->TextureTarget;
	//int32 Width = TextureRenderTarget->SizeX;
	//int32 Height = TextureRenderTarget->SizeX;

	FTransform Transform = CaptureComponent->GetComponentToWorld();
	FVector ViewLocation = Transform.GetTranslation();

	// Remove the translation from Transform because we only need rotation.
	Transform.SetTranslation(FVector::ZeroVector);
	Transform.SetScale3D(FVector::OneVector);
	FMatrix ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

	// swap axis st. x=z,y=x,z=y (unreal coord space) so that z is up
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	const float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
	float CaptureOrthoWidth = CaptureComponent->OrthoWidth;
	FIntPoint RenderTargetSize(Width, Height);

	FMatrix ProjectionMatrix;
	if (CaptureComponent->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
	}
	else
	{
		const float ClippingPlane = (CaptureComponent->bOverride_CustomNearClippingPlane) ? CaptureComponent->CustomNearClippingPlane : GNearClippingPlane;
		TEnumAsByte<ECameraProjectionMode::Type> ProjectionType = CaptureComponent->ProjectionType;

		//GWorld->Scene->BuildProjectionMatrix(RenderTargetSize, CaptureComponent->ProjectionType, FOV, CaptureComponent->OrthoWidth, ClippingPlane, ProjectionMatrix);
		float const XAxisMultiplier = 1.0f;
		float const YAxisMultiplier = RenderTargetSize.X / (float)RenderTargetSize.Y;

		if (ProjectionType == ECameraProjectionMode::Orthographic)
		{
			check((int32)ERHIZBuffer::IsInverted);
			const float OrthoWidth = CaptureOrthoWidth / 2.0f;
			const float OrthoHeight = CaptureOrthoWidth / 2.0f * XAxisMultiplier / YAxisMultiplier;

			const float NearPlane = 0;
			const float FarPlane = WORLD_MAX / 8.0f;

			const float ZScale = 1.0f / (FarPlane - NearPlane);
			const float ZOffset = -NearPlane;

			ProjectionMatrix = FReversedZOrthoMatrix(
				OrthoWidth,
				OrthoHeight,
				ZScale,
				ZOffset
			);
		}
		else
		{
			if ((int32)ERHIZBuffer::IsInverted)
			{
				ProjectionMatrix = FReversedZPerspectiveMatrix(
					FOV,
					FOV,
					XAxisMultiplier,
					YAxisMultiplier,
					ClippingPlane,
					ClippingPlane
				);
			}
			else
			{
				ProjectionMatrix = FPerspectiveMatrix(
					FOV,
					FOV,
					XAxisMultiplier,
					YAxisMultiplier,
					ClippingPlane,
					ClippingPlane
				);
			}
		}
	}

	FMatrix const ViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
	FIntRect ViewRect = FIntRect(0, 0, Width, Height);

	FPlane Result = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPos, 1.f));
	if (Result.W > 0.0f)
	{
		// the result of this will be x and y coords in -1..1 projection space
		const float RHW = 1.0f / Result.W;
		FPlane PosInScreenSpace = FPlane(Result.X * RHW, Result.Y * RHW, Result.Z * RHW, Result.W);

		// Move from projection space to normalized 0..1 UI space
		const float NormalizedX = (PosInScreenSpace.X / 2.f) + 0.5f;
		const float NormalizedY = 1.f - (PosInScreenSpace.Y / 2.f) - 0.5f;

		FVector2D RayStartViewRectSpace(
			(NormalizedX * (float)ViewRect.Width()),
			(NormalizedY * (float)ViewRect.Height())
		);

		ScreenPos = RayStartViewRectSpace + FVector2D(static_cast<float>(ViewRect.Min.X), static_cast<float>(ViewRect.Min.Y));
		ScreenDepth = (InSceneCapture->GetActorLocation() - WorldPos).Size();
		return true;
	}
	return false;
}

bool UScreenGenerate::DeProjectScreenToWorld_General(ASceneCapture2D* InSceneCapture, int32 Width, int32 Height, FVector2D ScreenPos, float ScreenDepth, FVector& WorldPos, FVector& WorldDir)
{
	USceneCaptureComponent2D* CaptureComponent = InSceneCapture->GetCaptureComponent2D();
	//Capture2D->UpdateSceneCaptureContents(GWorld->Scene);
	//check(CaptureComponent);
	//UTextureRenderTarget2D* TextureRenderTarget = CaptureComponent->TextureTarget;
	//int32 Width = TextureRenderTarget->SizeX;
	//int32 Height = TextureRenderTarget->SizeX;

	FTransform Transform = CaptureComponent->GetComponentToWorld();
	FVector ViewLocation = Transform.GetTranslation();

	// Remove the translation from Transform because we only need rotation.
	Transform.SetTranslation(FVector::ZeroVector);
	Transform.SetScale3D(FVector::OneVector);
	FMatrix ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

	// swap axis st. x=z,y=x,z=y (unreal coord space) so that z is up
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	const float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
	float CaptureOrthoWidth = CaptureComponent->OrthoWidth;
	FIntPoint RenderTargetSize(Width, Height);

	FMatrix ProjectionMatrix;
	if (CaptureComponent->bUseCustomProjectionMatrix)
	{
		ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
	}
	else
	{
		const float ClippingPlane = (CaptureComponent->bOverride_CustomNearClippingPlane) ? CaptureComponent->CustomNearClippingPlane : GNearClippingPlane;
		TEnumAsByte<ECameraProjectionMode::Type> ProjectionType = CaptureComponent->ProjectionType;

		//GWorld->Scene->BuildProjectionMatrix(RenderTargetSize, CaptureComponent->ProjectionType, FOV, CaptureComponent->OrthoWidth, ClippingPlane, ProjectionMatrix);
		float const XAxisMultiplier = 1.0f;
		float const YAxisMultiplier = RenderTargetSize.X / (float)RenderTargetSize.Y;

		if (ProjectionType == ECameraProjectionMode::Orthographic)
		{
			check((int32)ERHIZBuffer::IsInverted);
			const float OrthoWidth = CaptureOrthoWidth / 2.0f;
			const float OrthoHeight = CaptureOrthoWidth / 2.0f * XAxisMultiplier / YAxisMultiplier;

			const float NearPlane = 0;
			const float FarPlane = WORLD_MAX / 8.0f;

			const float ZScale = 1.0f / (FarPlane - NearPlane);
			const float ZOffset = -NearPlane;

			ProjectionMatrix = FReversedZOrthoMatrix(
				OrthoWidth,
				OrthoHeight,
				ZScale,
				ZOffset
			);
		}
		else
		{
			if ((int32)ERHIZBuffer::IsInverted)
			{
				ProjectionMatrix = FReversedZPerspectiveMatrix(
					FOV,
					FOV,
					XAxisMultiplier,
					YAxisMultiplier,
					ClippingPlane,
					ClippingPlane
				);
			}
			else
			{
				ProjectionMatrix = FPerspectiveMatrix(
					FOV,
					FOV,
					XAxisMultiplier,
					YAxisMultiplier,
					ClippingPlane,
					ClippingPlane
				);
			}
		}
	}
	FIntRect ViewRect = FIntRect(0, 0, Width, Height);
	FMatrix const ViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
	FMatrix const InvViewProjMatrix = ViewProjectionMatrix.InverseFast();


	float PixelX = FMath::TruncToFloat(ScreenPos.X);
	float PixelY = FMath::TruncToFloat(ScreenPos.Y);

	// Get the eye position and direction of the mouse cursor in two stages (inverse transform projection, then inverse transform view).
	// This avoids the numerical instability that occurs when a view matrix with large translation is composed with a projection matrix

	// Get the pixel coordinates into 0..1 normalized coordinates within the constrained view rectangle
	const float NormalizedX = (PixelX - ViewRect.Min.X) / ((float)ViewRect.Width());
	const float NormalizedY = (PixelY - ViewRect.Min.Y) / ((float)ViewRect.Height());

	// Get the pixel coordinates into -1..1 projection space
	const float ScreenSpaceX = (NormalizedX - 0.5f) * 2.0f;
	const float ScreenSpaceY = ((1.0f - NormalizedY) - 0.5f) * 2.0f;

	// The start of the ray trace is defined to be at mousex,mousey,1 in projection space (z=1 is near, z=0 is far - this gives us better precision)
	// To get the direction of the ray trace we need to use any z between the near and the far plane, so let's use (mousex, mousey, 0.5)
	const FVector4 RayStartProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 1.0f, 1.0f);
	const FVector4 RayEndProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 0.5f, 1.0f);
	const FVector4 ScreenPosWithDepth = FVector4(ScreenSpaceX, ScreenSpaceY, ScreenDepth, 1.0f);

	// Projection (changing the W coordinate) is not handled by the FMatrix transforms that work with vectors, so multiplications
	// by the projection matrix should use homogeneous coordinates (i.e. FPlane).
	const FVector4 HGRayStartWorldSpace = InvViewProjMatrix.TransformFVector4(RayStartProjectionSpace);
	const FVector4 HGRayEndWorldSpace = InvViewProjMatrix.TransformFVector4(RayEndProjectionSpace);
	const FVector4 ScreenPosWithDepthWorldSpace = InvViewProjMatrix.TransformFVector4(ScreenPosWithDepth);
	FVector RayStartWorldSpace(HGRayStartWorldSpace.X, HGRayStartWorldSpace.Y, HGRayStartWorldSpace.Z);
	FVector RayEndWorldSpace(HGRayEndWorldSpace.X, HGRayEndWorldSpace.Y, HGRayEndWorldSpace.Z);
	FVector ScreenWorldSpace(ScreenPosWithDepthWorldSpace.X, ScreenPosWithDepthWorldSpace.Y, ScreenPosWithDepthWorldSpace.Z);
	// divide vectors by W to undo any projection and get the 3-space coordinate
	if (HGRayStartWorldSpace.W != 0.0f)
	{
		RayStartWorldSpace /= HGRayStartWorldSpace.W;
	}
	if (HGRayEndWorldSpace.W != 0.0f)
	{
		RayEndWorldSpace /= HGRayEndWorldSpace.W;
	}
	if (HGRayEndWorldSpace.W != 0.0f)
	{
		ScreenWorldSpace /= ScreenPosWithDepthWorldSpace.W;
	}
	const FVector RayDirWorldSpace = (RayEndWorldSpace - RayStartWorldSpace).GetSafeNormal();

	// Finally, store the results in the outputs
	WorldPos = ScreenWorldSpace;
	WorldDir = RayDirWorldSpace;

	return true;

}



bool UScreenGenerate::TestFunction()
{
	//TArray<FTransform> Transf;
	//Transf.Add(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector::OneVector));
	//int32 Findid = 0;
	//Transf.Find(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector::OneVector), Findid);
	return true;
}
