#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Box -FallbackName=Box
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Box2D -FallbackName=Box2D
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Color -FallbackName=Color
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=DateTime -FallbackName=DateTime
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=IntVector -FallbackName=IntVector
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=LinearColor -FallbackName=LinearColor
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Plane -FallbackName=Plane
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=RandomStream -FallbackName=RandomStream
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Timespan -FallbackName=Timespan
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Transform -FallbackName=Transform
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
#include "BlueprintFunctionLibrary.h"
#include "EEasingFunc.h"
#include "ELerpInterpolationMode.h"
#include "FloatSpringState.h"
#include "VectorSpringState.h"
#include "KismetMathLibrary.generated.h"

class UObject;

UCLASS(Blueprintable)
class ENGINE_API UKismetMathLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UKismetMathLibrary();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Xor_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float VSizeSquared(FVector A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float VSize2DSquared(FVector2D A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float VSize2D(FVector2D A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float VSize(FVector A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector VLerp(FVector A, FVector B, float Alpha);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector VInterpTo_Constant(FVector Current, FVector Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector VInterpTo(FVector Current, FVector Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable)
    static FVector VectorSpringInterp(FVector Current, FVector Target, UPARAM(Ref) FVectorSpringState& SpringState, float Stiffness, float CriticalDampingFactor, float DeltaTime, float Mass);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Vector2DInterpTo_Constant(FVector2D Current, FVector2D Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Vector2DInterpTo(FVector2D Current, FVector2D Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector VEase(FVector A, FVector B, float Alpha, TEnumAsByte<EEasingFunc::Type> EasingFunc, float BlendExp, int32 Steps);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime UtcNow();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector TransformLocation(const FTransform& T, FVector Location);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector TransformDirection(const FTransform& T, FVector Direction);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime Today();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform TLerp(const FTransform& A, const FTransform& B, float Alpha, TEnumAsByte<ELerpInterpolationMode::Type> InterpMode);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform TInterpTo(const FTransform& Current, const FTransform& Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan TimespanZeroValue();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float TimespanRatio(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan TimespanMinValue();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan TimespanMaxValue();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool TimespanFromString(const FString& TimespanString, FTimespan& Result);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform TEase(const FTransform& A, const FTransform& B, float Alpha, TEnumAsByte<EEasingFunc::Type> EasingFunc, float BlendExp, int32 Steps);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Tan(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Subtract_VectorVector(FVector A, FVector B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Subtract_VectorInt(FVector A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Subtract_VectorFloat(FVector A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Subtract_Vector2DVector2D(FVector2D A, FVector2D B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Subtract_Vector2DFloat(FVector2D A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan Subtract_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Subtract_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Subtract_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime Subtract_DateTimeTimespan(FDateTime A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan Subtract_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Subtract_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Square(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Sqrt(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Sin(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 SignOfInteger(int32 A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float SignOfFloat(float A);
    
    UFUNCTION(BlueprintCallable)
    static void SetRandomStreamSeed(UPARAM(Ref) FRandomStream& Stream, int32 NewSeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector SelectVector(FVector A, FVector B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform SelectTransform(const FTransform& A, const FTransform& B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FString SelectString(const FString& A, const FString& B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator SelectRotator(FRotator A, FRotator B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static UObject* SelectObject(UObject* A, UObject* B, bool bSelectA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 SelectInt(int32 A, int32 B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float SelectFloat(float A, float B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor SelectColor(FLinearColor A, FLinearColor B, bool bPickA);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static UClass* SelectClass(UClass* A, UClass* B, bool bSelectA);
    
    UFUNCTION(BlueprintCallable)
    static void SeedRandomStream(UPARAM(Ref) FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Round(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator RotatorFromAxisAndAngle(FVector Axis, float angle);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector RotateAngleAxis(FVector InVect, float AngleDeg, FVector Axis);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator RLerp(FRotator A, FRotator B, float Alpha, bool bShortestPath);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator RInterpTo_Constant(FRotator Current, FRotator Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator RInterpTo(FRotator Current, FRotator Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void RGBToHSV_Vector(const FLinearColor RGB, FLinearColor& HSV);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void RGBToHSV(const FLinearColor InColor, float& H, float& S, float& V, float& A);
    
    UFUNCTION(BlueprintCallable)
    static void ResetVectorSpringState(UPARAM(Ref) FVectorSpringState& SpringState);
    
    UFUNCTION(BlueprintCallable)
    static void ResetRandomStream(const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable)
    static void ResetFloatSpringState(UPARAM(Ref) FFloatSpringState& SpringState);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator REase(FRotator A, FRotator B, float Alpha, bool bShortestPath, TEnumAsByte<EEasingFunc::Type> EasingFunc, float BlendExp, int32 Steps);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector RandomUnitVectorInConeWithYawAndPitch(FVector ConeDir, float MaxYawInDegrees, float MaxPitchInDegrees);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector RandomUnitVectorInCone(FVector ConeDir, float ConeHalfAngle);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector RandomUnitVectorFromStream(const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector RandomUnitVector();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator RandomRotatorFromStream(bool bRoll, const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator RandomRotator(bool bRoll);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector RandomPointInBoundingBox(const FVector& Origin, const FVector& BoxExtent);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 RandomIntegerInRangeFromStream(int32 NewMin, int32 NewMax, const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 RandomIntegerInRange(int32 NewMin, int32 NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 RandomIntegerFromStream(int32 NewMax, const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 RandomInteger(int32 NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float RandomFloatInRangeFromStream(float NewMin, float NewMax, const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float RandomFloatInRange(float NewMin, float NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float RandomFloatFromStream(const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float RandomFloat();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool RandomBoolWithWeightFromStream(float Weight, const FRandomStream& RandomStream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool RandomBoolWithWeight(float Weight);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool RandomBoolFromStream(const FRandomStream& Stream);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool RandomBool();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float RadiansToDegrees(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector ProjectVectorOnToVector(FVector V, FVector Target);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector ProjectVectorOnToPlane(FVector V, FVector PlaneNormal);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector ProjectPointOnToPlane(FVector Point, FVector PlaneBase, FVector PlaneNormal);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool PointsAreCoplanar(const TArray<FVector>& Points, float Tolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Percent_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Percent_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Percent_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Or_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime Now();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_VectorVector(FVector A, FVector B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_Vector2DVector2D(FVector2D A, FVector2D B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_RotatorRotator(FRotator A, FRotator B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_ObjectObject(UObject* A, UObject* B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_NameName(FName A, FName B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_ClassClass(UClass* A, UClass* B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NotEqual_BoolBool(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Not_PreBool(bool A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Not_Int(int32 A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float NormalizeToRange(float Value, float RangeMin, float RangeMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator NormalizedDeltaRotator(FRotator A, FRotator B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float NormalizeAxis(float angle);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Normal2D(FVector2D A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Normal(FVector A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector NegateVector(FVector A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator NegateRotator(FRotator A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NearlyEqual_TransformTransform(const FTransform& A, const FTransform& B, float LocationTolerance, float RotationTolerance, float Scale3DTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool NearlyEqual_FloatFloat(float A, float B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float MultiplyMultiply_FloatFloat(float Base, float NewExp);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float MultiplyByPi(float Value);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Multiply_VectorVector(FVector A, FVector B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Multiply_VectorInt(FVector A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Multiply_VectorFloat(FVector A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Multiply_Vector2DVector2D(FVector2D A, FVector2D B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Multiply_Vector2DFloat(FVector2D A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan Multiply_TimespanFloat(FTimespan A, float Scalar);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator Multiply_RotatorInt(FRotator A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator Multiply_RotatorFloat(FRotator A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor Multiply_LinearColorLinearColor(FLinearColor A, FLinearColor B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor Multiply_LinearColorFloat(FLinearColor A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Multiply_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Multiply_IntFloat(int32 A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Multiply_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Multiply_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector MirrorVectorByNormal(FVector InVect, FVector InNormal);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void MinOfIntArray(const TArray<int32>& IntArray, int32& IndexOfMinValue, int32& MinValue);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void MinOfFloatArray(const TArray<float>& FloatArray, int32& IndexOfMinValue, float& MinValue);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void MinOfByteArray(const TArray<uint8>& ByteArray, int32& IndexOfMinValue, uint8& MinValue);
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void MinimumAreaRectangle(UObject* WorldContextObject, const TArray<FVector>& InVerts, const FVector& SampleSurfaceNormal, FVector& OutRectCenter, FRotator& OutRectRotation, float& OutSideLengthX, float& OutSideLengthY, bool bDebugDraw);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Min(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void MaxOfIntArray(const TArray<int32>& IntArray, int32& IndexOfMaxValue, int32& MaxValue);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void MaxOfFloatArray(const TArray<float>& FloatArray, int32& IndexOfMaxValue, float& MaxValue);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void MaxOfByteArray(const TArray<uint8>& ByteArray, int32& IndexOfMaxValue, uint8& MaxValue);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Max(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float MapRangeUnclamped(float Value, float InRangeA, float InRangeB, float OutRangeA, float OutRangeB);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float MapRangeClamped(float Value, float InRangeA, float InRangeB, float OutRangeA, float OutRangeB);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D MakeVector2D(float X, float Y);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector MakeVector(float X, float Y, float Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform MakeTransform(FVector Location, FRotator Rotation, FVector Scale);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan MakeTimespan(int32 Days, int32 Hours, int32 Minutes, int32 Seconds, int32 Milliseconds);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromZY(const FVector& Z, const FVector& Y);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromZX(const FVector& Z, const FVector& X);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromZ(const FVector& Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromYZ(const FVector& Y, const FVector& Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromYX(const FVector& Y, const FVector& X);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromY(const FVector& Y);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromXZ(const FVector& X, const FVector& Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromXY(const FVector& X, const FVector& Y);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotFromX(const FVector& X);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotator(float Roll, float Pitch, float Yaw);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator MakeRotationFromAxes(FVector Forward, FVector Right, FVector Up);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRandomStream MakeRandomStream(int32 InitialSeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float MakePulsatingValue(float InCurrentTime, float InPulsesPerSecond, float InPhase);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FPlane MakePlaneFromPointAndNormal(FVector Point, FVector NewNormal);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime MakeDateTime(int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute, int32 Second, int32 Millisecond);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor MakeColor(float R, float G, float B, float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FBox2D MakeBox2D(FVector2D NewMin, FVector2D NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FBox MakeBox(FVector NewMin, FVector NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Loge(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Log(float A, float Base);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LinePlaneIntersection_OriginNormal(const FVector& LineStart, const FVector& LineEnd, FVector PlaneOrigin, FVector PlaneNormal, float& T, FVector& Intersection);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LinePlaneIntersection(const FVector& LineStart, const FVector& LineEnd, const FPlane& APlane, float& T, FVector& Intersection);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor LinearColorLerpUsingHSV(FLinearColor A, FLinearColor B, float Alpha);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor LinearColorLerp(FLinearColor A, FLinearColor B, float Alpha);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector LessLess_VectorRotator(FVector A, FRotator B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LessEqual_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LessEqual_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LessEqual_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LessEqual_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool LessEqual_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Less_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Less_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Less_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Less_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Less_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Lerp(float A, float B, float Alpha);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsPointInBoxWithTransform(FVector Point, const FTransform& BoxWorldTransform, FVector BoxExtent);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsPointInBox(FVector Point, FVector BoxOrigin, FVector BoxExtent);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsMorning(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsLeapYear(int32 Year);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsAfternoon(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform InvertTransform(const FTransform& T);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector InverseTransformLocation(const FTransform& T, FVector Location);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector InverseTransformDirection(const FTransform& T, FVector Direction);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float InverseLerp(float A, float B, float Value);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool InRange_FloatFloat(float Value, float NewMin, float NewMax, bool InclusiveMin, bool InclusiveMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Hypotenuse(float Width, float Height);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void HSVToRGB_Vector(const FLinearColor HSV, FLinearColor& RGB);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor HSVToRGB(float H, float S, float V, float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GridSnap_Float(float Location, float GridSize);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GreaterGreater_VectorRotator(FVector A, FRotator B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GreaterEqual_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GreaterEqual_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GreaterEqual_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GreaterEqual_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GreaterEqual_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Greater_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Greater_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Greater_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Greater_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Greater_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetYear(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetYawPitchFromVector(FVector InVec, float& Yaw, float& Pitch);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GetVectorArrayAverage(const TArray<FVector>& Vectors);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GetUpVector(FRotator InRot);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetTotalSeconds(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetTotalMinutes(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetTotalMilliseconds(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetTotalHours(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetTotalDays(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan GetTimeOfDay(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetTAU();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetSeconds(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetSecond(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GetRightVector(FRotator InRot);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GetReflectionVector(FVector Direction, FVector SurfaceNormal);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetPointDistanceToSegment(FVector Point, FVector SegmentStart, FVector SegmentEnd);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetPointDistanceToLine(FVector Point, FVector LineOrigin, FVector LineDirection);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetPI();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetMonth(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetMinutes(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetMinute(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetMinElement(FVector A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetMilliseconds(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetMillisecond(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetMaxElement(FVector A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetHours(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetHour12(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetHour(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GetForwardVector(FRotator InRot);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan GetDuration(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector GetDirectionUnitVector(FVector From, FVector To);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetDays(FTimespan A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetDayOfYear(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetDay(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime GetDate(FDateTime A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetAxes(FRotator A, FVector& X, FVector& Y, FVector& Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FIntVector FTruncVector(const FVector& InVector);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 FTrunc(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan FromSeconds(float Seconds);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan FromMinutes(float Minutes);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan FromMilliseconds(float Milliseconds);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan FromHours(float Hours);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan FromDays(float Days);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Fraction(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 FMod(float Dividend, float Divisor, float& Remainder);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FMin(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FMax(float A, float B);
    
    UFUNCTION(BlueprintCallable)
    static float FloatSpringInterp(float Current, float Target, UPARAM(Ref) FFloatSpringState& SpringState, float Stiffness, float CriticalDampingFactor, float DeltaTime, float Mass);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FixedTurn(float InCurrent, float InDesired, float InDeltaRate);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FInterpTo_Constant(float Current, float Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FInterpTo(float Current, float Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FInterpEaseInOut(float A, float B, float Alpha, float Exponent);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void FindNearestPointsOnLineSegments(FVector Segment1Start, FVector Segment1End, FVector Segment2Start, FVector Segment2End, FVector& Segment1Point, FVector& Segment2Point);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator FindLookAtRotation(const FVector& Start, const FVector& Target);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector FindClosestPointOnSegment(FVector Point, FVector SegmentStart, FVector SegmentEnd);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector FindClosestPointOnLine(FVector Point, FVector LineOrigin, FVector LineDirection);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 FFloor(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float FClamp(float Value, float NewMin, float NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 FCeil(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Exp(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_VectorVector(FVector A, FVector B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_Vector2DVector2D(FVector2D A, FVector2D B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_TransformTransform(const FTransform& A, const FTransform& B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_RotatorRotator(FRotator A, FRotator B, float ErrorTolerance);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_ObjectObject(UObject* A, UObject* B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_NameName(FName A, FName B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_DateTimeDateTime(FDateTime A, FDateTime B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_ClassClass(UClass* A, UClass* B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool EqualEqual_BoolBool(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Ease(float A, float B, float Alpha, TEnumAsByte<EEasingFunc::Type> EasingFunc, float BlendExp, int32 Steps);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DotProduct2D(FVector2D A, FVector2D B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Dot_VectorVector(FVector A, FVector B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Divide_VectorVector(FVector A, FVector B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Divide_VectorInt(FVector A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Divide_VectorFloat(FVector A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Divide_Vector2DVector2D(FVector2D A, FVector2D B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Divide_Vector2DFloat(FVector2D A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Divide_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Divide_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Divide_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegTan(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegSin(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegreesToRadians(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegCos(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegAtan2(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegAtan(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegAsin(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float DegAcos(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 DaysInYear(int32 Year);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 DaysInMonth(int32 Year, int32 Month);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime DateTimeMinValue();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime DateTimeMaxValue();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool DateTimeFromString(const FString& DateTimeString, FDateTime& Result);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool DateTimeFromIsoString(const FString& IsoString, FDateTime& Result);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float CrossProduct2D(FVector2D A, FVector2D B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Cross_VectorVector(FVector A, FVector B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector CreateVectorFromYawPitch(float Yaw, float Pitch, float Length);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Cos(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform ConvertTransformToRelative(const FTransform& Transform, const FTransform& ParentTransform);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Conv_VectorToVector2D(FVector InVector);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform Conv_VectorToTransform(FVector InLocation);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator Conv_VectorToRotator(FVector InVec);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor Conv_VectorToLinearColor(FVector InVec);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Conv_Vector2DToVector(FVector2D InVector2D, float Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Conv_RotatorToVector(FRotator InRot);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Conv_LinearColorToVector(FLinearColor InLinearColor);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FColor Conv_LinearColorToColor(FLinearColor InLinearColor);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Conv_IntVectorToVector(const FIntVector& InIntVector);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FIntVector Conv_IntToIntVector(int32 inInt);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Conv_IntToFloat(int32 inInt);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Conv_IntToByte(int32 inInt);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool Conv_IntToBool(int32 inInt);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Conv_FloatToVector(float inFloat);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor Conv_FloatToLinearColor(float inFloat);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor Conv_ColorToLinearColor(FColor InColor);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Conv_ByteToInt(uint8 InByte);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Conv_ByteToFloat(uint8 InByte);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Conv_BoolToInt(bool inBool);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Conv_BoolToFloat(bool inBool);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Conv_BoolToByte(bool inBool);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTransform ComposeTransforms(const FTransform& A, const FTransform& B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FRotator ComposeRotators(FRotator A, FRotator B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool ClassIsChildOf(UClass* TestClass, UClass* ParentClass);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector ClampVectorSize(FVector A, float NewMin, float NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float ClampAxis(float angle);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float ClampAngle(float AngleDegrees, float MinAngleDegrees, float MaxAngleDegrees);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Clamp(int32 Value, int32 NewMin, int32 NewMax);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLinearColor CInterpTo(FLinearColor Current, FLinearColor Target, float DeltaTime, float InterpSpeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakVector2D(FVector2D InVec, float& X, float& Y);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakVector(FVector InVec, float& X, float& Y, float& Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakTransform(const FTransform& InTransform, FVector& Location, FRotator& Rotation, FVector& Scale);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakTimespan(FTimespan InTimespan, int32& Days, int32& Hours, int32& Minutes, int32& Seconds, int32& Milliseconds);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakRotIntoAxes(const FRotator& InRot, FVector& X, FVector& Y, FVector& Z);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakRotator(FRotator InRot, float& Roll, float& Pitch, float& Yaw);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakRandomStream(const FRandomStream& InRandomStream, int32& InitialSeed);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakDateTime(FDateTime InDateTime, int32& Year, int32& Month, int32& Day, int32& Hour, int32& Minute, int32& Second, int32& Millisecond);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void BreakColor(const FLinearColor InColor, float& R, float& G, float& B, float& A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool BooleanXOR(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool BooleanOR(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool BooleanNOR(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool BooleanNAND(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool BooleanAND(bool A, bool B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 BMin(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 BMax(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Atan2(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Atan(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Asin(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 And_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Add_VectorVector(FVector A, FVector B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Add_VectorInt(FVector A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector Add_VectorFloat(FVector A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Add_Vector2DVector2D(FVector2D A, FVector2D B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FVector2D Add_Vector2DFloat(FVector2D A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FTimespan Add_TimespanTimespan(FTimespan A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Add_IntInt(int32 A, int32 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Add_FloatFloat(float A, float B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FDateTime Add_DateTimeTimespan(FDateTime A, FTimespan B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static uint8 Add_ByteByte(uint8 A, uint8 B);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Acos(float A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 Abs_Int(int32 A);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float Abs(float A);
    
};

